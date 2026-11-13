#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <meshoptimizer.h>


struct InstanceData {
    glm::mat4x4 transform;
    glm::vec3 color;
    uint32_t modelId;
};

struct Vertex {
    // use 16bit floats for position and normal representation.
    // X = position.X & position.Y
    // Y = position.Z & normal.X
    // Z = normal.Z & normal.Z
    // W = unused
    glm::vec4 packedPositionNorma; 
};

struct ModelData {
    // use bounding sphere for simplicity and better code reuse with meshlets. 
    // AABB would've probably been a better choice for culling, but I'll omit it for this task
    glm::vec4 boundingSphere; 
    
    uint32_t indexStart;
    uint32_t indicesCount;

    uint32_t meshletStart;
    uint32_t meshletCount;
};

struct Meshlet {
    uint32_t indexStart;
    uint32_t indicesCount;
};

class Scene {
    std::vector<uint32_t> m_indices;
    std::vector<Vertex> m_vertices;
    std::vector<ModelData> m_models;
    std::vector<InstanceData> m_instances;
    std::vector<Meshlet> m_meshlets;

public:
    Scene() {}


};