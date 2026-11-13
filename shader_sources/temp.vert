#version 450

layout(push_constant, std430) uniform pc {
    mat4 viewProj;
};

layout(set = 0, binding = 0, std430) readonly buffer VertexBuffer {
    uvec4 vertices[];
};

layout(location = 0) out vec3 outNormal;

void main() {
    
    uvec4 packedVertex = vertices[gl_VertexIndex];

    vec2 xy = unpackHalf2x16(packedVertex.x);
    vec2 znx = unpackHalf2x16(packedVertex.y);
    vec2 nyz = unpackHalf2x16(packedVertex.z);

    vec3 position = vec3(xy.x, xy.y, znx.x);
    vec3 normal = vec3(znx.y, nyz.x, nyz.y);

    outNormal = normal;
    gl_Position = viewProj * vec4(position, 1);
}