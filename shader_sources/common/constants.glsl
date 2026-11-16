#ifndef CONSTANTS_GLSL_
#define CONSTANTS_GLSL_

// must match DefaultGroupSize of draw.h
#define DEFAULT_GROUP_SIZE 64

// must match MeshletsProcessedPerGroup of draw.h
#define MESHLETS_PROCESSED_PER_GROUP 256

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

struct Model {
    vec4 boundingSphere; 
    
    uint indexStart;
    uint indicesCount;

    uint meshletStart;
    uint meshletCount;
};

struct Instance {
    mat4 transform;
    vec3 color;
    uint modelId;
};

struct Meshlet {
    vec4 boundingSphere;
    vec4 coneAxisAngle;
    vec4 coneApex;
    uint indexCount;
    uint globalIndexStart;

    uvec2 _pad;
};

#endif
