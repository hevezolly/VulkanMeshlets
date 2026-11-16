#ifndef SCENE_GLSL_
#define SCENE_GLSL_

#ifndef SCENE_SET
#define SCENE_SET 1
#endif

#include "common/constants.glsl"

layout(set = SCENE_SET, binding = 0, std430) readonly buffer VertexBuffer {
    uvec4 Vertices[];
};

layout(set = SCENE_SET, binding = 1, std430) readonly buffer ModelBuffer {
    Model Models[];
};

layout(set = SCENE_SET, binding = 2, std430) readonly buffer MeshletBuffer {
    Meshlet Meshlets[];
};

layout(set = SCENE_SET, binding = 3, std430) readonly buffer InstanceBuffer {
    Instance Instances[];
};

layout(set = SCENE_SET, binding = 4, std430) readonly buffer SceneIndexBuffer {
    uint Indices[];
};

layout(set = SCENE_SET, binding = 5) uniform SceneStatsUBO {
    uint countModels;
    uint countInstances;
    uint countMeshlets;
    uint maxMeshletIndices;
} SceneStats;

#endif
