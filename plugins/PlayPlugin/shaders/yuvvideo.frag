#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 params;
} ubuf;

layout(binding = 0) uniform sampler2D texY;
layout(binding = 1) uniform sampler2D texU;
layout(binding = 2) uniform sampler2D texV;

// ✅ 修正：用 .r
float samplePlane(sampler2D plane, vec2 uv)
{
    return texture(plane, uv).r;
}

vec3 yuvToRgb(float y, float u, float v, bool fullRange, bool bt709)
{
    if (fullRange) {
        if (bt709) {
            return vec3(
                y + 1.5748 * v,
                y - 0.1873 * u - 0.4681 * v,
                y + 1.8556 * u
            );
        }
        return vec3(
            y + 1.4020 * v,
            y - 0.3441 * u - 0.7141 * v,
            y + 1.7720 * u
        );
    }

    float yy = max(0.0, y - 0.0625) * 1.16438356;

    if (bt709) {
        return vec3(
            yy + 1.7927 * v,
            yy - 0.2132 * u - 0.5329 * v,
            yy + 2.1124 * u
        );
    }

    return vec3(
        yy + 1.5960 * v,
        yy - 0.3918 * u - 0.8130 * v,
        yy + 2.0172 * u
    );
}

void main()
{
    float y = samplePlane(texY, vTexCoord);
    float u = samplePlane(texU, vTexCoord);
    float v = samplePlane(texV, vTexCoord);

    // 10bit 数据在 R16 纹理里被归一化到 [0, 1]（除以 65535）
    // 实际有效范围是 [0, 1023]，需要重新映射到 [0, 1]
    if (ubuf.params.w > 0.5) {
        // 65535 / 1023 ≈ 64.06，反向缩放回正确范围
        y = y * 64.06;
        u = u * 64.06;
        v = v * 64.06;
    }

    u -= 0.5;
    v -= 0.5;

    vec3 rgb = yuvToRgb(y, u, v,
                        ubuf.params.y > 0.5,
                        ubuf.params.z > 0.5);

    fragColor = vec4(clamp(rgb, 0.0, 1.0), ubuf.params.x);
}