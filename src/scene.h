#pragma once
#include <cstddef>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <meshoptimizer.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp> 
#include <cstdint>
#include <string>
#include <vector>
#include <cassert> 
#include <VulkanBuffer.h>
#include "descriptors.h"


inline uint32_t packHalfPair(float first, float second)
{
    return uint32_t{meshopt_quantizeHalf(first)} | (uint32_t{meshopt_quantizeHalf(second)} << 16);
}

struct InstanceData {
    glm::mat4x4 transform;
    glm::vec3 color;
    uint32_t modelId;
};

struct Vertex {
    // use 16bit floats for position and normal representation.
    // X = position.X & position.Y
    // Y = position.Z & normal.X
    // Z = normal.Y & normal.Z
    // W = unused
    glm::uvec4 packedPositionNormal;

    Vertex(): packedPositionNormal(){}
    Vertex(glm::vec3 position, glm::vec3 normal) {
        packedPositionNormal = glm::uvec4(
            packHalfPair(position[0], position[1]),
            packHalfPair(position[2], normal[0]),
            packHalfPair(normal[1], normal[2]),
        0);
    }
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
    glm::vec4 boundingSphere;
    glm::vec4 coneAxisAngle;
    glm::vec4 coneApex;
    uint32_t indexCount;
    // First index of the meshlet's contiguous range in the global index buffer.
    uint32_t globalIndexStart;

    // pad to cleanly fit into a cache line
    uint32_t _pad0;
    uint32_t _pad1;
};

struct SceneStats 
{
    uint32_t countModels;
    uint32_t countInstances;
    uint32_t countMeshlets;
    uint32_t maxMeshletIndices;
};

struct GpuScene {
    vks::Buffer indexBuffer;
    vks::Buffer vertexBuffer;
    vks::Buffer modelsBuffer;
    vks::Buffer meshletsBuffer;
    vks::Buffer instancesBuffer;
    vks::Buffer sceneStats;

    // degenerate buffer consisting on [0, 1, 2, 3, ..., m_maxUsedIndicesPerMeshlet-1].
    // used for instanced meshlets rendering
    vks::Buffer meshletIndexBuffer; 

    void destroy() {
        indexBuffer.destroy();
        vertexBuffer.destroy();
        modelsBuffer.destroy();
        meshletsBuffer.destroy();
        instancesBuffer.destroy();
        sceneStats.destroy();
        meshletIndexBuffer.destroy();
    }
};

class Scene {
    uint32_t m_maxVerticesPerMeshlet;
    uint32_t m_maxTrianglesPerMeshlet;
    std::vector<uint32_t> m_indices{};
    std::vector<Vertex> m_vertices{};
    std::vector<ModelData> m_models{};
    std::vector<Meshlet> m_meshlets{};
    std::vector<InstanceData> m_instances{};
    uint32_t m_maxUsedIndicesPerMeshlet;

public:

    Scene(uint32_t maxVerticesPerMeshlet = 64, uint32_t maxTrianglesPerMeshlet = 126): 
        m_maxVerticesPerMeshlet(maxVerticesPerMeshlet), m_maxTrianglesPerMeshlet(maxTrianglesPerMeshlet), m_maxUsedIndicesPerMeshlet(0) {}

    uint32_t addModel(const std::string& filePath);

    // support only uniform scale for bounding spheres to be easier to handle
    void instantiate(uint32_t model, glm::vec3 position, glm::quat rotation, float scale, glm::vec3 color) {
        assert(model < m_models.size());
        glm::mat4x4 transform = glm::translate(glm::mat4(1.f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.f), glm::vec3(scale));

        m_instances.push_back(InstanceData {
            .transform = transform,
            .color = color,
            .modelId = model
        });
    }

    std::vector<uint32_t>& indices() { return m_indices; }
    std::vector<Vertex>& vertices() { return m_vertices; }
    std::vector<ModelData>& models() { return m_models; }
    std::vector<InstanceData>& instances() { return m_instances; }
    std::vector<Meshlet>& meshlets() { return m_meshlets; }
    uint32_t maxIndexPerMeshlet() {return m_maxUsedIndicesPerMeshlet; }
};
