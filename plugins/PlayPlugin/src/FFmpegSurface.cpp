#include "FFmpegSurface.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QMutexLocker>

#include <rhi/qrhi.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// ══════════════════════════════════════════════════════════════════════════════
// 颜色空间矩阵
//
// 将 R8 纹理采样到的归一化 YCbCr [0,1] 转换为 RGB。
// 矩阵编码为 mat4，shader 里：rgb = (colorMatrix * vec4(y, cb, cr, 1.0)).rgb
//
// 支持四种组合：BT.601 / BT.709  ×  limited / full range。
// ══════════════════════════════════════════════════════════════════════════════

static QMatrix4x4 buildColorMatrix(bool fullRange, bool bt709)
{
    // Luma 偏移（[0,1] 归一化）
    const float y_off = fullRange ? 0.0f           : 16.0f  / 255.0f;
    const float c_off =             128.0f / 255.0f;                    // Cb/Cr 始终以 128 为中心
    const float ky    = fullRange ? 1.0f            : 255.0f / 219.0f;  // limited: ~1.1644

    // 色度系数（来源：ITU-R BT.601 / BT.709）
    float kr_cr, kg_cb, kg_cr, kb_cb;
    if (bt709) {
        kr_cr = fullRange ? 1.5748f : 1.7927f;
        kg_cb = fullRange ? 0.1873f : 0.2132f;
        kg_cr = fullRange ? 0.4681f : 0.5329f;
        kb_cb = fullRange ? 1.8556f : 2.1124f;
    } else {                                    // BT.601
        kr_cr = fullRange ? 1.4020f : 1.5960f;
        kg_cb = fullRange ? 0.3441f : 0.3917f;
        kg_cr = fullRange ? 0.7141f : 0.8129f;
        kb_cb = fullRange ? 1.7720f : 2.0172f;
    }

    // 展开常数项，合并到矩阵第四列（w=1 的乘积即常数）：
    //   R = ky*(y - y_off) +  kr_cr*(cr - c_off)
    //   G = ky*(y - y_off) - kg_cb*(cb - c_off) - kg_cr*(cr - c_off)
    //   B = ky*(y - y_off) + kb_cb*(cb - c_off)
    const float r_c = -ky * y_off - kr_cr * c_off;
    const float g_c = -ky * y_off + kg_cb * c_off + kg_cr * c_off;
    const float b_c = -ky * y_off - kb_cb * c_off;

    // QMatrix4x4 构造函数按行优先（row-major）填充：
    //           Y        Cb        Cr      常数
    return QMatrix4x4(
         ky,    0.0f,   kr_cr,    r_c,   // → R
         ky,  -kg_cb,  -kg_cr,   g_c,   // → G
         ky,   kb_cb,   0.0f,    b_c,   // → B
        0.0f,  0.0f,    0.0f,   1.0f   // → w（保留）
    );
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
    QByteArray data_y, data_u, data_v;
    int stride_y = 0, stride_u = 0, stride_v = 0;
    int width = 0, height = 0;
    bool valid = false;
};

// ══════════════════════════════════════════════════════════════════════════════
// VideoMaterial
// ══════════════════════════════════════════════════════════════════════════════
enum class PixelFormat { YUV420P, YUV420P10 };

// ══════════════════════════════════════════════════════════════════════════════
// PixelFormatInfo
//
// 描述一种 YUV 像素格式的所有属性，集中管理格式判断逻辑。
// 新增格式只需在 fromAVFormat() 里加一行。
// ══════════════════════════════════════════════════════════════════════════════

struct PixelFormatInfo
{
    // 格式是否有效
    bool valid = false;

    // RHI 纹理格式
    QRhiTexture::Format rhiFormat = QRhiTexture::R8;

    // 色度平面相对亮度平面的尺寸比例
    int chromaWidthDivisor  = 2;  // UV 宽 = Y 宽 / divisor
    int chromaHeightDivisor = 2;  // UV 高 = Y 高 / divisor

    // shader 里是否需要 10bit 归一化
    bool is10bit = false;

    // 色度平面实际尺寸
    int chromaWidth (int w) const { return (w + chromaWidthDivisor  - 1) / chromaWidthDivisor;  }
    int chromaHeight(int h) const { return (h + chromaHeightDivisor - 1) / chromaHeightDivisor; }

    // ── 工厂方法：从 AVPixelFormat 构造 ─────────────────────────────────────
    static PixelFormatInfo fromAVFormat(int avFormat)
    {
        switch (avFormat) {
        // 8-bit YUV420
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            return { true, QRhiTexture::R8,  2, 2, false };

        // 10-bit YUV420
        case AV_PIX_FMT_YUV420P10LE:
            return { true, QRhiTexture::R16, 2, 2, true  };

        // 10-bit YUV422（色度与亮度等高）
        case AV_PIX_FMT_YUV422P10LE:
            return { true, QRhiTexture::R16, 2, 1, true  };

        // 10-bit YUV444（色度与亮度等宽等高）
        case AV_PIX_FMT_YUV444P10LE:
            return { true, QRhiTexture::R16, 1, 1, true  };

        default:
            return {};  // valid = false
        }
    }

    bool operator==(const PixelFormatInfo& o) const {
        return rhiFormat           == o.rhiFormat
            && chromaWidthDivisor  == o.chromaWidthDivisor
            && chromaHeightDivisor == o.chromaHeightDivisor
            && is10bit             == o.is10bit;
    }
    bool operator!=(const PixelFormatInfo& o) const { return !(*this == o); }
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

    // 缓存上一帧的标志，避免每帧都 dirty
    bool  cachedFullRange  = false;
    bool  cachedBt709      = false;
    bool  paramsDirty      = true;   // 首帧强制写入
    

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
//   offset 64 : mat4 colorMatrix  (64 bytes) — YCbCr → RGB
//   total     : 128 bytes
//
// 对应 GLSL：
//   layout(std140, binding = 0) uniform buf {
//       mat4 qt_Matrix;
//       mat4 colorMatrix;
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
            mat->fmtInfo.is10bit  ? 1.0f : 0.0f,  // w=is10bit
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

        // ── 纹理数据上传（每帧最多一次）──────────────────────────────────
        if (mat->pending.valid) {
            const auto& p     = mat->pending;
            auto*       batch = state.resourceUpdateBatch();

            // 辅助 lambda：上传单个 YUV 平面，正确处理 FFmpeg linesize 对齐
            auto upload_plane = [&](QRhiTexture*      tex,
                                    const QByteArray& data,
                                    int               stride,
                                    QSize             tex_size)
            {
                QRhiTextureSubresourceUploadDescription desc(
                    data.constData(), data.size());
                // setSourceSize：有效像素区域（不含 stride 末尾 padding）
                desc.setSourceSize(tex_size);
                // setSourceLayout：每行实际字节数（含 FFmpeg 对齐 padding）
                // 若省略，图像出现斜向错位条纹
                desc.setDataStride(stride);
                batch->uploadTexture(tex,
                    QRhiTextureUploadDescription(
                        QRhiTextureUploadEntry(0, 0, desc)));
            };

            // [优化2] 色度平面高度向上取整，兼容奇数高度视频
            const int chroma_h = (p.height + 1) / 2;
            const int chroma_w = (p.width  + 1) / 2;

            upload_plane(mat->tex_y, p.data_y, p.stride_y, { p.width,   p.height  });
            upload_plane(mat->tex_u, p.data_u, p.stride_u, { chroma_w,  chroma_h  });
            upload_plane(mat->tex_v, p.data_v, p.stride_v, { chroma_w,  chroma_h  });

            mat->pending.valid = false;
        }

        // ── 将 QRhiTexture* 包装为 QSGTexture* 并返回 ────────────────────
        // thread_local：渲染线程固定，避免每帧堆分配
        static thread_local RhiTextureWrapper wrap_y, wrap_u, wrap_v;

        switch (binding) {
        case 0: wrap_y.setRhiTexture(mat->tex_y); *texture = &wrap_y; break;
        case 1: wrap_u.setRhiTexture(mat->tex_u); *texture = &wrap_u; break;
        case 2: wrap_v.setRhiTexture(mat->tex_v); *texture = &wrap_v; break;
        default: *texture = nullptr;                                   break;
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

        // 拷贝帧数据；GPU 上传延迟到 VideoShader 回调中执行
    void setFrame(QQuickWindow* window, const VideoFrameData& data)
    {
        if (!window) return;

        const AVFrame* frame = data.frame.get();
        if (!frame) return;

        const PixelFormatInfo fmt = PixelFormatInfo::fromAVFormat(frame->format);
        if (!fmt.valid) {
            qWarning("FFmpegSurface: unsupported pixel format %d (%s), frame dropped.",
                    frame->format,
                    av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));
            return;
        }

        
        ensureTextures(window, frame->width, frame->height, fmt);


        const int chroma_h = fmt.chromaHeight(frame->height);

        // const int chroma_h = (frame->height + 1) / 2;

        auto& p    = m_material_.pending;
        p.width    = frame->width;
        p.height   = frame->height;
        p.stride_y = frame->linesize[0];
        p.stride_u = frame->linesize[1];
        p.stride_v = frame->linesize[2];
     
        // 10bit 每像素 2 字节
        p.data_y = QByteArray(reinterpret_cast<const char*>(frame->data[0]),
                            frame->linesize[0] * frame->height);
        p.data_u = QByteArray(reinterpret_cast<const char*>(frame->data[1]),
                            frame->linesize[1] * chroma_h);
        p.data_v = QByteArray(reinterpret_cast<const char*>(frame->data[2]),
                            frame->linesize[2] * chroma_h);
        p.valid  = true;
         m_hasFrame  = true;
        // 仅在参数变化时标脏，避免每帧写 uniform buffer
        if (m_material_.cachedFullRange != data.fullRange ||
            m_material_.cachedBt709     != data.bt709)
        {
            m_material_.fullRange       = data.fullRange;
            m_material_.bt709           = data.bt709;
            m_material_.cachedFullRange = data.fullRange;
            m_material_.cachedBt709     = data.bt709;
            m_material_.paramsDirty     = true;
        }

        markDirty(QSGNode::DirtyMaterial);
    }

    bool hasFrame() const { return m_hasFrame; }

    // 用于 FFmpegSurface 在无新帧时仍能计算 drawRect
    QSize videoSize() const { return m_material_.size; }

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
        m_material_.tex_y = rhi->newTexture(fmt.rhiFormat, { w,  h  }); m_material_.tex_y->create();
        m_material_.tex_u = rhi->newTexture(fmt.rhiFormat, { cw, ch }); m_material_.tex_u->create();
        m_material_.tex_v = rhi->newTexture(fmt.rhiFormat, { cw, ch }); m_material_.tex_v->create();
        m_material_.size    = { w, h };
        m_material_.fmtInfo = fmt;

        // ✅ 填充占位数据：Y=16, U=128, V=128 → 纯黑（limited range）
        auto& p    = m_material_.pending;
        p.width    = w;
        p.height   = h;
        p.stride_y = w;
        p.stride_u = cw;
        p.stride_v = cw;
        p.data_y   = QByteArray(w * h,        char(16));
        p.data_u   = QByteArray(cw * ch, char(128));
        p.data_v   = QByteArray(cw * ch, char(128));
        p.valid    = true;

        // // 纹理重建后颜色矩阵需重新写入 uniform buffer
        // m_material_.colorMatrixDirty = true;
        // // 纹理重建后强制重新检查色彩空间标志（下一帧重建矩阵）
        // m_material_.colorFlagsInited = false;
        // 纹理重建后强制重新写入 uniform buffer
        m_material_.paramsDirty     = true;
        // 纹理重建后强制重新检查色彩空间标志
        m_material_.cachedFullRange = !m_material_.fullRange;  // 故意与当前值不同，触发下一帧重建
        m_material_.cachedBt709     = !m_material_.bt709;
    }

    void releaseTextures()
    {
        delete m_material_.tex_y; m_material_.tex_y = nullptr;
        delete m_material_.tex_u; m_material_.tex_u = nullptr;
        delete m_material_.tex_v; m_material_.tex_v = nullptr;
        m_material_.size = {};
    }

    VideoMaterial m_material_;
    bool m_hasFrame  = false; 
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

    // ── 取出待上屏的帧（move 转移所有权，避免引用计数抖动）────────────────
    VideoFrameDataPtr frame;
    {
        QMutexLocker lock(&m_mutex);
        if (m_dirty) {
            frame   = std::move(m_pendingFrame);
            m_dirty = false;
        }
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

    if (frame)
        node->setFrame(window(), *frame);

    return node;
}