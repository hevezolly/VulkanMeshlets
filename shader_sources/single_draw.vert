#version 450
#extension GL_GOOGLE_include_directive : require

#define ENVIRONMENT_SET 0
#include "common/environment.glsl"

#define SCENE_SET 1
#include "common/scene.glsl"

#define SINGLEDRAW_ACCESS readonly
#define SINGLEDRAW_BUFFERS_SET 2
#include "common/singledraw_buffers.glsl"

#include "common/utils.glsl"

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;

void main() {
    
    uvec4 packedVertex = CulledVertices[gl_VertexIndex];
    Instance instance = Instances[packedVertex.w];

    vec2 xy = unpackHalf2x16(packedVertex.x);
    vec2 znx = unpackHalf2x16(packedVertex.y);
    vec2 nyz = unpackHalf2x16(packedVertex.z);

    vec4 position = instance.transform * vec4(xy.x, xy.y, znx.x, 1);
    vec3 normal = mat3(instance.transform) * vec3(znx.y, nyz.x, nyz.y);

    outNormal = normal;
    outColor = instance.color;
    gl_Position = Env.cameraViewProjection * position;
}