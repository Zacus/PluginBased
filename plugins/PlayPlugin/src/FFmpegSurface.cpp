#include "FFmpegSurface.h"
#include "render/VideoPixelFormat.h"

#if defined(Q_OS_APPLE)
#include "native/AppleMetalVideoTextureBridge.h"
#endif

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QMutexLocker>

#include <rhi/qrhi.h>

#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// ══════════════════════════════════════════════════════════════════════════════
// RhiTextureWrapper
//
// 将裸 QRhiTexture* 非拥有地包装为 QSGTexture*，
// 供 updateSampledImage 的出参使用。
// ══════════════════════════════════════════════════════════════════════════════

class RhiTextureWrapper : public QSGTexture
{
public:
    RhiTextureWrapper() = default;

    void setRhiTexture(QRhiTexture* tex) { tex_ = tex; }

    qint64 comparisonKey() const override {
        return reinterpret_cast<qint64>(tex_);
    }
    QSize textureSize() const override {
        if (!tex_) return {};
        const auto s = tex_->pixelSize();
        return { s.width(), s.height() };
    }
    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps()      const override { return false; }
    QRhiTexture* rhiTexture() const override { return tex_; }
    void commitTextureOperations(QRhi*, QRhiResourceUpdateBatch*) override {}

private:
    QRhiTexture* tex_ = nullptr;
};

// ══════════════════════════════════════════════════════════════════════════════
// PendingUpload
//
// 在 VideoNode::setFrame（渲染线程外）准备像素数据，
// 在 VideoShader::updateSampledImage（渲染线程内）通过 RHI 上传。
// QByteArray 保证跨线程生命期安全。
// ══════════════════════════════════════════════════════════════════════════════

struct PendingUpload
{
    VideoFrameDataPtr frameData; // ← 直接持有 shared_ptr，不再拷贝像素
    bool valid = false;
};

class VideoMaterial : public QSGMaterial
{
public:
    QRhiTexture* tex_y = nullptr;
    QRhiTexture* tex_u = nullptr;
    QRhiTexture* tex_v = nullptr;
    QSize        size;

    PendingUpload pending;
    PixelFormatInfo fmtInfo;      // 当前纹理对应的格式

    // 对应 shader 的 params：x=opacity, y=fullRange, z=bt709
    float opacity   = 1.0f;
    bool  fullRange = false;
    bool  bt709     = false;
    bool pendingBlackFrame = false; // 纹理刚建好，需要上传一次占位黑帧

    // 缓存上一帧的标志，避免每帧都 dirty
    bool  cachedFullRange  = false;
    bool  cachedBt709      = false;
    bool  paramsDirty      = true;   // 首帧强制写入
    VideoFrameDataPtr currentFrame; // 保证 GPU 读取期间 AVFrame 不被释放
#if defined(Q_OS_APPLE)
    std::unique_ptr<AppleMetalTextureSet> nativeTextures;
#endif
    bool usingNativeTextures = false;

    QSGMaterialType* type() const override {
        static QSGMaterialType k_type;
        return &k_type;
    }
    int compare(const QSGMaterial* other) const override {
        const auto a = reinterpret_cast<quintptr>(this);
        const auto b = reinterpret_cast<quintptr>(other);
        return (a > b) - (a < b);
    }
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
};

// ══════════════════════════════════════════════════════════════════════════════
// VideoShader
//
// Uniform buffer 布局（std140）：
//   offset  0 : mat4 qt_Matrix    (64 bytes) — MVP
//   offset 64 : vec4 params       (16 bytes) — opacity/range/space/bit-depth
//   total     : 80 bytes
//
// 对应 GLSL：
//   layout(std140, binding = 0) uniform buf {
//       mat4 qt_Matrix;
//       vec4 params;
//   };
// ══════════════════════════════════════════════════════════════════════════════

class VideoShader : public QSGMaterialShader
{
public:
    VideoShader() {
        setShaderFileName(VertexStage,
            QLatin1String(":/PlayPlugin/shaders/yuvvideo.vert.qsb"));
        setShaderFileName(FragmentStage,
            QLatin1String(":/PlayPlugin/shaders/yuvvideo.frag.qsb"));
    }

    // ── Uniform 更新 ──────────────────────────────────────────────────────────
bool updateUniformData(RenderState& state,
                       QSGMaterial* new_mat,
                       QSGMaterial*) override
{
    auto*       mat = static_cast<VideoMaterial*>(new_mat);
    QByteArray* buf = state.uniformData();

    if (buf->size() < 80) {
        qWarning("FFmpegSurface: uniform buffer too small (%lld bytes), "
                 "expected >= 80. Check UBO in shader.",
                 (long long)buf->size());
        return false;
    }

    bool changed = false;

    // offset 0：MVP 矩阵（64字节）
    if (state.isMatrixDirty()) {
        memcpy(buf->data(),
               state.combinedMatrix().constData(),
               64);
        changed = true;
    }

    // offset 64：vec4 params（16字节）
    // x = opacity, y = fullRange(0/1), z = bt709(0/1), w = 保留
    if (mat->paramsDirty) {
        float params[4] = {
            mat->opacity,
            mat->fullRange                              ? 1.0f : 0.0f,
            mat->bt709                                  ? 1.0f : 0.0f,
            mat->fmtInfo.formatMode,
        };
        memcpy(buf->data() + 64, params, sizeof(params));
        mat->paramsDirty = false;
        changed = true;
    }

    return changed;
}

    // ── 纹理绑定 & 上传 ───────────────────────────────────────────────────────
    //
    // Qt 对 shader 中每个 sampler 各调用一次，binding 顺序不保证。
    // 纹理上传在首次调用时完成（pending.valid 保证仅上传一次）。
    //
    // 【线程安全说明】
    // updateSampledImage 在渲染线程调用，updatePaintNode 与渲染线程串行化
    // （Qt 场景图保证），因此对 m_material_.pending 的读写不存在竞态。
    //
    void updateSampledImage(RenderState& state,
                            int           binding,
                            QSGTexture**  texture,
                            QSGMaterial*  new_mat,
                            QSGMaterial*) override
    {
        auto* mat = static_cast<VideoMaterial*>(new_mat);

        // 纹理刚创建时上传一次黑帧占位，只执行一次
        if (mat->pendingBlackFrame) {
            const int w  = mat->size.width(),  h  = mat->size.height();
            const int cw = mat->fmtInfo.chromaWidth(w);
            const int ch = mat->fmtInfo.chromaHeight(h);

            const int bytesPerSample = mat->fmtInfo.is10bit ? 2 : 1;
            auto makePlane = [&](int samples, quint16 value) {
                QByteArray data;
                data.resize(samples * bytesPerSample);
                if (bytesPerSample == 1) {
                    memset(data.data(), static_cast<int>(value & 0xff), data.size());
                    return data;
                }

                auto* dst = reinterpret_cast<quint16*>(data.data());
                for (int i = 0; i < samples; ++i)
                    dst[i] = value;
                return data;
            };
            auto makeInterleavedChromaPlane = [&](int samples, quint16 value) {
                QByteArray data;
                data.resize(samples * bytesPerSample * 2);
                if (bytesPerSample == 1) {
                    auto* dst = reinterpret_cast<quint8*>(data.data());
                    for (int i = 0; i < samples; ++i) {
                        dst[i * 2]     = static_cast<quint8>(value & 0xff);
                        dst[i * 2 + 1] = static_cast<quint8>(value & 0xff);
                    }
                    return data;
                }

                auto* dst = reinterpret_cast<quint16*>(data.data());
                for (int i = 0; i < samples; ++i) {
                    dst[i * 2]     = value;
                    dst[i * 2 + 1] = value;
                }
                return data;
            };

            const QByteArray black_y =
                makePlane(w * h, mat->fmtInfo.needs10BitExpansion ? 64 :
                                  (mat->fmtInfo.is10bit ? 4096 : 16));
            const quint16 neutralChroma = mat->fmtInfo.needs10BitExpansion ? 512 :
                                          (mat->fmtInfo.is10bit ? 32768 : 128);

            auto* batch = state.resourceUpdateBatch();
            auto upload = [&](QRhiTexture* tex, const QByteArray& data, QSize sz) {
                QRhiTextureSubresourceUploadDescription desc(data.constData(), data.size());
                desc.setSourceSize(sz);
                batch->uploadTexture(tex,
                    QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, desc)));
            };
            upload(mat->tex_y, black_y,  { w,  h  });
            if (mat->fmtInfo.isSemiplanar()) {
                const QByteArray black_uv = makeInterleavedChromaPlane(cw * ch, neutralChroma);
                const QByteArray black_v = makePlane(1, neutralChroma);
                upload(mat->tex_u, black_uv, { cw, ch });
                upload(mat->tex_v, black_v, { 1, 1 });
            } else {
                const QByteArray black_uv = makePlane(cw * ch, neutralChroma);
                upload(mat->tex_u, black_uv, { cw, ch });
                upload(mat->tex_v, black_uv, { cw, ch });
            }

            mat->pendingBlackFrame = false;
        }

        // ── 直接从 AVFrame 上传，无中间拷贝 ──────────────────────────────────
        if (mat->pending.valid && mat->pending.frameData)
        {
            const AVFrame*        frame = mat->pending.frameData->frame.get();
            const PixelFormatInfo& fmt  = mat->fmtInfo;
            auto* batch = state.resourceUpdateBatch();

            auto upload_plane = [&](QRhiTexture*   tex,
                                    const uint8_t* data,
                                    int            stride,   // linesize（含对齐）
                                    QSize          texSize)  // 有效像素区域
            {
                QRhiTextureSubresourceUploadDescription desc(
                    data, stride * texSize.height());
                desc.setSourceSize(texSize);
                desc.setDataStride(stride);  // 告知 RHI 每行实际字节数
                batch->uploadTexture(tex,
                    QRhiTextureUploadDescription(
                        QRhiTextureUploadEntry(0, 0, desc)));
            };

            const int cw = fmt.chromaWidth (frame->width);
            const int ch = fmt.chromaHeight(frame->height);

            upload_plane(mat->tex_y, frame->data[0], frame->linesize[0],
                        { frame->width, frame->height });
            upload_plane(mat->tex_u, frame->data[1], frame->linesize[1],
                        { cw, ch });
            if (!fmt.isSemiplanar()) {
                upload_plane(mat->tex_v, frame->data[2], frame->linesize[2],
                            { cw, ch });
            }

            mat->pending.valid = false;
            mat->currentFrame   = std::move(mat->pending.frameData); // ← 延迟释放
// currentFrame 会在下一帧的 upload 时被下一帧的 shared_ptr 覆盖替换
// 届时 GPU 必然已经执行完上一帧的命令，安全
        }

        // ── 将 QRhiTexture* 包装为 QSGTexture* ───────────────────────────────
        static thread_local RhiTextureWrapper wrap_y, wrap_u, wrap_v;
        switch (binding) {
        case 0: wrap_y.setRhiTexture(mat->tex_y); *texture = &wrap_y; break;
        case 1: wrap_u.setRhiTexture(mat->tex_u); *texture = &wrap_u; break;
        case 2: wrap_v.setRhiTexture(mat->tex_v); *texture = &wrap_v; break;
        default: *texture = nullptr; break;
        }
    }
};

QSGMaterialShader*
VideoMaterial::createShader(QSGRendererInterface::RenderMode) const {
    return new VideoShader();
}

// ══════════════════════════════════════════════════════════════════════════════
// VideoNode
// ══════════════════════════════════════════════════════════════════════════════

class VideoNode : public QSGGeometryNode
{
public:
    VideoNode()
    {
        setFlag(OwnsGeometry);
        // 【不设 OwnsMaterial】：m_material_ 是成员变量，不能被 Qt delete

        auto* g = new QSGGeometry(
            QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
        g->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        setGeometry(g);
        setMaterial(&m_material_);
    }

    ~VideoNode() {
        releaseTextures();
    }

    // 更新顶点几何（由 FFmpegSurface 根据 aspectRatioMode 计算后传入）
    void setRect(const QRectF& rect)
    {
        QSGGeometry::updateTexturedRectGeometry(
            geometry(), rect, QRectF(0, 0, 1, 1));
        markDirty(QSGNode::DirtyGeometry);
    }

    bool setNativeFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData)
    {
#if defined(Q_OS_APPLE)
        if (!window || !frameData || !frameData->frame ||
            frameData->native.kind != NativeFrameKind::VideoToolbox)
            return false;

        if (!m_nativeBridge)
            m_nativeBridge = std::make_unique<AppleMetalVideoTextureBridge>();
        if (!m_nativeBridge->isAvailable(window->rhi()))
            return false;

        auto textures = m_nativeBridge->createTextureSet(window->rhi(), frameData->frame.get());
        if (!textures)
            return false;

        releaseTextures();
        m_material_.tex_y = textures->yTexture.get();
        m_material_.tex_u = textures->uvTexture.get();
        m_material_.tex_v = textures->vPlaceholderTexture.get();
        m_material_.size = textures->lumaSize;
        m_material_.fmtInfo = PixelFormatInfo::fromAVFormat(textures->native.is10bit
            ? AV_PIX_FMT_P010LE
            : AV_PIX_FMT_NV12);
        m_material_.fullRange = textures->native.fullRange;
        m_material_.bt709 = textures->native.bt709;
        m_material_.cachedFullRange = textures->native.fullRange;
        m_material_.cachedBt709 = textures->native.bt709;
        m_material_.paramsDirty = true;
        m_material_.pendingBlackFrame = false;
        m_material_.pending.valid = false;
        m_material_.pending.frameData.reset();
        m_material_.currentFrame = frameData;
        m_material_.nativeTextures = std::move(textures);
        m_material_.usingNativeTextures = true;
        m_hasFrame = true;
        markDirty(QSGNode::DirtyMaterial);
        return true;
#else
        Q_UNUSED(window);
        Q_UNUSED(frameData);
        return false;
#endif
    }

    // 拷贝帧数据；GPU 上传延迟到 VideoShader 回调中执行
    bool setFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData)
    {
        if (!window || !frameData)
            return false;
        const AVFrame* frame = frameData->frame.get();
        if (!frame)
            return false;

        if (frameData->native.kind == NativeFrameKind::VideoToolbox)
            return setNativeFrame(window, frameData);

        const PixelFormatInfo fmt = PixelFormatInfo::fromAVFormat(frame->format);
        if (!fmt.valid) {
            qWarning("FFmpegSurface: unsupported pixel format %d, frame dropped.", frame->format);
            return false;
        }

        ensureTextures(window, frame->width, frame->height, fmt);

        // ✅ 只存 shared_ptr，零拷贝
        m_material_.pending.frameData = frameData;
        m_material_.pending.valid     = true;
        m_hasFrame = true;

        // 色彩空间参数（仅变化时才标脏）
        if (m_material_.cachedFullRange != frameData->fullRange ||
            m_material_.cachedBt709     != frameData->bt709)
        {
            m_material_.fullRange       = frameData->fullRange;
            m_material_.bt709           = frameData->bt709;
            m_material_.cachedFullRange = frameData->fullRange;
            m_material_.cachedBt709     = frameData->bt709;
            m_material_.paramsDirty     = true;
        }

        markDirty(QSGNode::DirtyMaterial);
        return true;
    }

    bool hasFrame() const { return m_hasFrame; }

    // 用于 FFmpegSurface 在无新帧时仍能计算 drawRect
    QSize videoSize() const { return m_material_.size; }

    void clearToBlack() {
        m_hasFrame = false;
        if (m_material_.usingNativeTextures) {
            releaseTextures();
            markDirty(QSGNode::DirtyMaterial);
            return;
        }
        if (m_material_.tex_y) {  // 纹理存在才填，否则直接透明
            m_material_.pendingBlackFrame = true;
            m_material_.paramsDirty = true;
            markDirty(QSGNode::DirtyMaterial);
        }
    }

private:
    void ensureTextures(QQuickWindow* window, int w, int h,  const PixelFormatInfo& fmt)
    {
        if (m_material_.size == QSize(w, h) &&
            m_material_.tex_y &&
            m_material_.fmtInfo == fmt)
            return;

        releaseTextures();

        const int cw = fmt.chromaWidth (w);
        const int ch = fmt.chromaHeight(h);

        QRhi* rhi = window->rhi();
        m_material_.tex_y = rhi->newTexture(fmt.lumaFormat, { w,  h  });
        m_material_.tex_y->create();
        m_material_.tex_u = rhi->newTexture(fmt.chromaFormat, { cw, ch });
        m_material_.tex_u->create();
        const QSize vSize = fmt.isSemiplanar() ? QSize(1, 1) : QSize(cw, ch);
        const QRhiTexture::Format vFormat = fmt.isSemiplanar() ? QRhiTexture::R8 : fmt.chromaFormat;
        m_material_.tex_v = rhi->newTexture(vFormat, vSize);
        m_material_.tex_v->create();
        m_material_.size    = { w, h };
        m_material_.fmtInfo = fmt;

        // ✅ 填充占位数据：Y=16, U=128, V=128 → 纯黑（limited range）
        m_material_.pendingBlackFrame = true; // ← 只立 flag，不再填 QByteArray
        m_material_.paramsDirty     = true;
        m_material_.cachedFullRange = !m_material_.fullRange;  // 故意与当前值不同，触发下一帧重建
        m_material_.cachedBt709     = !m_material_.bt709;
    }

    void releaseTextures()
    {
        if (m_material_.usingNativeTextures) {
#if defined(Q_OS_APPLE)
            m_material_.nativeTextures.reset();
#endif
        } else {
            delete m_material_.tex_y;
            delete m_material_.tex_u;
            delete m_material_.tex_v;
        }
        m_material_.tex_y = nullptr;
        m_material_.tex_u = nullptr;
        m_material_.tex_v = nullptr;
        m_material_.usingNativeTextures = false;
        m_material_.pending.valid = false;
        m_material_.pending.frameData.reset();
        m_material_.pendingBlackFrame = false;
        m_material_.size = {};
        m_material_.currentFrame.reset(); 
    }

    
    VideoMaterial m_material_;
    bool m_hasFrame  = false;
#if defined(Q_OS_APPLE)
    std::unique_ptr<AppleMetalVideoTextureBridge> m_nativeBridge;
#endif
};

// ══════════════════════════════════════════════════════════════════════════════
// FFmpegSurface
// ══════════════════════════════════════════════════════════════════════════════

FFmpegSurface::FFmpegSurface(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

// FFmpegSurface::~FFmpegSurface() = default;
// m_pendingFrame 是 shared_ptr，析构时自动减引用计数

void FFmpegSurface::setAspectRatioMode(Qt::AspectRatioMode mode)
{
    if (m_aspectRatioMode == mode)
        return;
    m_aspectRatioMode = mode;
    emit aspectRatioModeChanged();
    update();
}

bool FFmpegSurface::supportsNativeVideoToolboxRendering() const
{
#if defined(Q_OS_APPLE)
    return window() &&
           window()->rendererInterface() &&
           window()->rendererInterface()->graphicsApi() == QSGRendererInterface::MetalRhi;
#else
    return false;
#endif
}

// 可从任意线程调用（VideoRenderer::frameReady 可能跨线程发出）
void FFmpegSurface::onFrameReady(const VideoFrameDataPtr& frame)
{
    {
        QMutexLocker lock(&m_mutex);
        m_pendingFrame = frame;   // shared_ptr 赋值，旧帧引用计数 -1，线程安全
        m_dirty        = true;
    }
    // update() 必须在 GUI 线程执行；QueuedConnection 保证正确派发
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

QSGNode* FFmpegSurface::updatePaintNode(QSGNode*         old_node,
                                        UpdatePaintNodeData*)
{
    auto* node = static_cast<VideoNode*>(old_node);

    bool clearPending = false;
    // ── 取出待上屏的帧（move 转移所有权，避免引用计数抖动）────────────────
    VideoFrameDataPtr frame;
    {
        QMutexLocker lock(&m_mutex);
        clearPending   = m_clearPending;
        m_clearPending = false;
        if (m_dirty) {
            frame   = std::move(m_pendingFrame);
            m_dirty = false;
        }
    }

    // 清屏：不删节点，只填黑，GPU 纹理安全
    if (clearPending && node) {
        node->clearToBlack();
    }

    if (!frame && (!node || !node->hasFrame())) {
        delete node;
        return nullptr;
    }

    // ── 计算绘制区域（支持 KeepAspectRatio / KeepAspectRatioByExpanding）──
    QRectF draw_rect = boundingRect();

    const QSize video_size = frame
        ? QSize(frame->frame ? frame->frame->width  : 0,
                frame->frame ? frame->frame->height : 0)
        : (node ? node->videoSize() : QSize{});

    if (video_size.isValid() && m_aspectRatioMode != Qt::IgnoreAspectRatio) {
        const QSizeF scaled =
            QSizeF(video_size).scaled(boundingRect().size(), m_aspectRatioMode);
        draw_rect = QRectF(
            (boundingRect().width()  - scaled.width())  / 2.0,
            (boundingRect().height() - scaled.height()) / 2.0,
            scaled.width(),
            scaled.height());
    }

    // ── 创建或更新节点 ────────────────────────────────────────────────────
    if (!node)
        node = new VideoNode();

    node->setRect(draw_rect);

    if (frame && !node->setFrame(window(), frame) &&
        frame->native.kind == NativeFrameKind::VideoToolbox) {
        if (m_nativeRenderingFailureLogs < 3) {
            qWarning("FFmpegSurface: native VideoToolbox texture creation failed, "
                     "disabling native rendering for future frames.");
            ++m_nativeRenderingFailureLogs;
        }
        emit nativeRenderingFailed();
    }

    return node;
}

void FFmpegSurface::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_clearPending = true; // 标记正在清除，updatePaintNode 里可选地处理（如显示纯黑）
    }
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}
