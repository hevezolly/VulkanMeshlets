#ifndef CONSTANTS_GLSL_
#define CONSTANTS_GLSL_

// must match DefaultGroupSize of draw.h
#define DEFAULT_GROUP_SIZE 64

// must match MeshletsProcessedPerGroup of draw.h
#define MESHLETS_PROCESSED_PER_GROUP 256

// must match MaxWorkPerMeshlet from main.cpp
#define MAX_PRIMITIVES_PER_MESHLET 384

struct PerMeshletWorkload {
    uint instanceId;
    uint meshletId;
};

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

#endif