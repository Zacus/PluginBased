#include "render/VideoMaterial.h"

// Implements the QSG material shader and QRhi texture upload path.
// FFmpegSurface and VideoNode use this as an internal rendering detail, not as a QML-facing API.

#include <QByteArray>
#include <QDebug>
#include <QSGTexture>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
}

class RhiTextureWrapper : public QSGTexture
{
public:
    void setRhiTexture(QRhiTexture* texture) { m_texture = texture; }

    qint64 comparisonKey() const override
    {
        return reinterpret_cast<qint64>(m_texture);
    }

    QSize textureSize() const override
    {
        if (!m_texture)
            return {};
        const auto size = m_texture->pixelSize();
        return { size.width(), size.height() };
    }

    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps() const override { return false; }
    QRhiTexture* rhiTexture() const override { return m_texture; }
    void commitTextureOperations(QRhi*, QRhiResourceUpdateBatch*) override {}

private:
    QRhiTexture* m_texture = nullptr;
};

class VideoShader : public QSGMaterialShader
{
public:
    VideoShader()
    {
        setShaderFileName(VertexStage,
                          QLatin1String(":/PlayPlugin/shaders/yuvvideo.vert.qsb"));
        setShaderFileName(FragmentStage,
                          QLatin1String(":/PlayPlugin/shaders/yuvvideo.frag.qsb"));
    }

    bool updateUniformData(RenderState& state,
                           QSGMaterial* newMaterial,
                           QSGMaterial*) override
    {
        auto* material = static_cast<VideoMaterial*>(newMaterial);
        QByteArray* buffer = state.uniformData();

        if (buffer->size() < 80) {
            qWarning("FFmpegSurface: uniform buffer too small (%lld bytes), "
                     "expected >= 80. Check UBO in shader.",
                     static_cast<long long>(buffer->size()));
            return false;
        }

        bool changed = false;

        if (state.isMatrixDirty()) {
            memcpy(buffer->data(), state.combinedMatrix().constData(), 64);
            changed = true;
        }

        if (material->paramsDirty) {
            float params[4] = {
                material->opacity,
                material->fullRange ? 1.0f : 0.0f,
                material->bt709 ? 1.0f : 0.0f,
                material->fmtInfo.formatMode,
            };
            memcpy(buffer->data() + 64, params, sizeof(params));
            material->paramsDirty = false;
            changed = true;
        }

        return changed;
    }

    void updateSampledImage(RenderState& state,
                            int binding,
                            QSGTexture** texture,
                            QSGMaterial* newMaterial,
                            QSGMaterial*) override
    {
        auto* material = static_cast<VideoMaterial*>(newMaterial);

        if (material->pendingBlackFrame) {
            const int width = material->size.width();
            const int height = material->size.height();
            const int chromaWidth = material->fmtInfo.chromaWidth(width);
            const int chromaHeight = material->fmtInfo.chromaHeight(height);

            const int bytesPerSample = material->fmtInfo.is10bit ? 2 : 1;
            auto makePlane = [&](int samples, quint16 value) {
                QByteArray data;
                data.resize(samples * bytesPerSample);
                if (bytesPerSample == 1) {
                    memset(data.data(), static_cast<int>(value & 0xff), data.size());
                    return data;
                }

                auto* destination = reinterpret_cast<quint16*>(data.data());
                for (int i = 0; i < samples; ++i)
                    destination[i] = value;
                return data;
            };
            auto makeInterleavedChromaPlane = [&](int samples, quint16 value) {
                QByteArray data;
                data.resize(samples * bytesPerSample * 2);
                if (bytesPerSample == 1) {
                    auto* destination = reinterpret_cast<quint8*>(data.data());
                    for (int i = 0; i < samples; ++i) {
                        destination[i * 2] = static_cast<quint8>(value & 0xff);
                        destination[i * 2 + 1] = static_cast<quint8>(value & 0xff);
                    }
                    return data;
                }

                auto* destination = reinterpret_cast<quint16*>(data.data());
                for (int i = 0; i < samples; ++i) {
                    destination[i * 2] = value;
                    destination[i * 2 + 1] = value;
                }
                return data;
            };

            const QByteArray blackY =
                makePlane(width * height,
                          material->fmtInfo.needs10BitExpansion ? 64 :
                          (material->fmtInfo.is10bit ? 4096 : 16));
            const quint16 neutralChroma = material->fmtInfo.needs10BitExpansion ? 512 :
                                          (material->fmtInfo.is10bit ? 32768 : 128);

            auto* batch = state.resourceUpdateBatch();
            auto upload = [&](QRhiTexture* rhiTexture, const QByteArray& data, QSize size) {
                QRhiTextureSubresourceUploadDescription description(data.constData(), data.size());
                description.setSourceSize(size);
                batch->uploadTexture(
                    rhiTexture,
                    QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));
            };
            upload(material->tex_y, blackY, { width, height });
            if (material->fmtInfo.isSemiplanar()) {
                const QByteArray blackUv =
                    makeInterleavedChromaPlane(chromaWidth * chromaHeight, neutralChroma);
                const QByteArray blackV = makePlane(1, neutralChroma);
                upload(material->tex_u, blackUv, { chromaWidth, chromaHeight });
                upload(material->tex_v, blackV, { 1, 1 });
            } else {
                const QByteArray blackUv = makePlane(chromaWidth * chromaHeight, neutralChroma);
                upload(material->tex_u, blackUv, { chromaWidth, chromaHeight });
                upload(material->tex_v, blackUv, { chromaWidth, chromaHeight });
            }

            material->pendingBlackFrame = false;
        }

        if (material->pending.valid && material->pending.frameData) {
            const AVFrame* frame = material->pending.frameData->frame.get();
            const PixelFormatInfo& format = material->fmtInfo;
            auto* batch = state.resourceUpdateBatch();

            auto uploadPlane = [&](QRhiTexture* rhiTexture,
                                   const uint8_t* data,
                                   int stride,
                                   QSize textureSize) {
                QRhiTextureSubresourceUploadDescription description(
                    data, stride * textureSize.height());
                description.setSourceSize(textureSize);
                description.setDataStride(stride);
                batch->uploadTexture(
                    rhiTexture,
                    QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));
            };

            const int chromaWidth = format.chromaWidth(frame->width);
            const int chromaHeight = format.chromaHeight(frame->height);

            uploadPlane(material->tex_y, frame->data[0], frame->linesize[0],
                        { frame->width, frame->height });
            uploadPlane(material->tex_u, frame->data[1], frame->linesize[1],
                        { chromaWidth, chromaHeight });
            if (!format.isSemiplanar()) {
                uploadPlane(material->tex_v, frame->data[2], frame->linesize[2],
                            { chromaWidth, chromaHeight });
            }

            material->pending.valid = false;
            material->currentFrame = std::move(material->pending.frameData);
        }

        static thread_local RhiTextureWrapper wrapY;
        static thread_local RhiTextureWrapper wrapU;
        static thread_local RhiTextureWrapper wrapV;
        switch (binding) {
        case 0:
            wrapY.setRhiTexture(material->tex_y);
            *texture = &wrapY;
            break;
        case 1:
            wrapU.setRhiTexture(material->tex_u);
            *texture = &wrapU;
            break;
        case 2:
            wrapV.setRhiTexture(material->tex_v);
            *texture = &wrapV;
            break;
        default:
            *texture = nullptr;
            break;
        }
    }
};

QSGMaterialType* VideoMaterial::type() const
{
    static QSGMaterialType materialType;
    return &materialType;
}

int VideoMaterial::compare(const QSGMaterial* other) const
{
    const auto lhs = reinterpret_cast<quintptr>(this);
    const auto rhs = reinterpret_cast<quintptr>(other);
    return (lhs > rhs) - (lhs < rhs);
}

QSGMaterialShader* VideoMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new VideoShader();
}
