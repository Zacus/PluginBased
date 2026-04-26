#version 440
layout(location = 0) in vec2 aVertex;
layout(location = 1) in vec2 aTexCoord;
layout(location = 0) out vec2 vTexCoord;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 params;
} ubuf;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    vTexCoord   = aTexCoord;
    gl_Position = ubuf.qt_Matrix * vec4(aVertex, 0.0, 1.0);
}