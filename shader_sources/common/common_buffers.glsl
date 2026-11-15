#ifndef COMMON_BUFFERS_GLSL_
#define COMMON_BUFFERS_GLSL_

#ifndef COMMON_BUFFERS_SET
#define COMMON_BUFFERS_SET 2
#endif

#include "common/constants.glsl"

layout(set = COMMON_BUFFERS_SET, binding = 0, std430) buffer MeshletsWorkloadBinding {
    MeshletsWorkload MeshletsWorkloads[];
};

layout(set = COMMON_BUFFERS_SET, binding = 1, std430) buffer DispatchCommandsBinding {
    VkDispatchIndirectCommand dispatchPerInstanceCommand;
    VkDispatchIndirectCommand dispatchPerMeshletCommand;
    VkDrawIndexedIndirectCommand singleDrawIndexedCommand;
    uint drawCommandCounter;
    uint vertexCounter;
    uint indexCounter;
} Commands;

layout(push_constant) uniform PushConstants {
    mat4 cameraViewProjection;
    uint maxDrawCommands;
    uint maxWorkloadPerInstanceCount;
    uint maxWorkloadPerMeshletCount;
    uint maxVertexCount;
    uint maxIndexCount;
} Setup;

#endif