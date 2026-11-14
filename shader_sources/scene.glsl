#ifndef SCENE_GLSL_
#define SCENE_GLSL_

#ifndef SCENE_SET
#define SCENE_SET 0
#endif

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
    uint packedIndexVertexCount;
    uint globalIndexStart;
    uint uniqueVerticesStart;
    uint localIndicesStart;
};

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

layout(set = SCENE_SET, binding = 4, std430) readonly buffer MeshletUniqueVerticesBuffer {
    uint MeshletUnuqueVertices[];
};

layout(set = SCENE_SET, binding = 5, std430) readonly buffer MeshletLocalIndicesBuffer {
    uint MeshletLocalIndices[];
};

#endif
