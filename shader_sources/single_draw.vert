#version 450
#extension GL_GOOGLE_include_directive : require

#include "common/draw_common.glsl"

#define SCENE_SET 0
#include "common/scene.glsl"

#define SINGLEDRAW_ACCESS readonly
#define SINGLEDRAW_BUFFERS_SET 1
#include "common/singledraw_buffers.glsl"

#include "common/utils.glsl"

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec3 outPosition;

void main() {
    PerMeshletWorkload workload = PerMeshletWorkloads[gl_InstanceIndex];
    Meshlet meshlet = Meshlets[workload.meshletId];
    uint corner = uint(gl_VertexIndex);
    if (corner >= meshlet.indexCount) {
        // The index template pads shorter meshlets with degenerate triangles.
        outNormal = vec3(0.0);
        outColor = vec3(0.0);
        outPosition = vec3(0.0);
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    Instance instance = Instances[workload.instanceId];
    uvec4 packedVertex = Vertices[Indices[meshlet.globalIndexStart + corner]];
    vec2 xy = unpackHalf2x16(packedVertex.x);
    vec2 znx = unpackHalf2x16(packedVertex.y);
    vec2 nyz = unpackHalf2x16(packedVertex.z);
    vec4 position = instance.transform * vec4(xy.x, xy.y, znx.x, 1);
    vec3 normal = mat3(instance.transform) * vec3(znx.y, nyz.x, nyz.y);

    outNormal = normal;
    outColor = instance.color;
    outPosition = position.xyz;
    gl_Position = Env.cameraViewProjection * position;
}
