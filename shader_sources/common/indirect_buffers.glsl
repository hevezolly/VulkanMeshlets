#ifndef INDIRECT_BUFFERS_GLSL_
#define INDIRECT_BUFFERS_GLSL_

#ifndef INDIRECT_BUFFERS_SET
#define INDIRECT_BUFFERS_SET 2
#endif

struct MeshletsWorkload {
    uint instanceId;
    uint startMeshlet;
    uint meshletsCount;
    uint _padding;
};

struct VkDrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

struct VkDispatchIndirectCommand {
    uint x;
    uint y;
    uint z;
};

layout(set = INDIRECT_BUFFERS_SET, binding = 0, std430) buffer DrawCommandsBinding {
    VkDrawIndexedIndirectCommand DrawIndirectCommands[];
};

layout(set = INDIRECT_BUFFERS_SET, binding = 1, std430) buffer MeshletsWorkloadBinding {
    MeshletsWorkload MeshletsWorkloads[];
};

layout(set = INDIRECT_BUFFERS_SET, binding = 2, std430) buffer DispatchCommandsBinding {
    VkDispatchIndirectCommand dispatchCommand;
    uint drawCommandCounter;
} Commands;



#endif