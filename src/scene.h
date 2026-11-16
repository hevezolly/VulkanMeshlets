#pragma once
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

    // Half-float pair: index count in the low half, unique vertex count in the high half.
    uint32_t packedIndexVertexCount;

    // First index of the meshlet's contiguous range in the global index buffer.
    uint32_t globalIndexStart;

    // First of vertexCount entries in m_meshletUniqueVertices, each a global vertex index.
    uint32_t uniqueVerticesStart;

    // Region from localIndicesStart with size of indexCount in m_meshletLocalIndices contains
    // local indices of meshlets. The indirection is the following:
    // local index -> unique vertex index (offset from uniqueVerticesStart) -> vertex in global buffer
    uint32_t localIndicesStart;

    // Some info for backface culling. Getting it almost for free from meshoptimizer lib.
    glm::vec4 coneAxisAngle;
    glm::vec4 coneApex;

    Meshlet(uint32_t vertexCount, uint32_t indexCount):
        boundingSphere{}, globalIndexStart{}, uniqueVerticesStart{}, localIndicesStart{}
    {
        packedIndexVertexCount = (indexCount & 0xFFFF) | ((vertexCount & 0xFFFF) << 16);
    }
};

struct SceneStats 
{
    uint32_t countModels;
    uint32_t countInstances;
    uint32_t countMeshlets;
    uint32_t _padding;
};

struct GpuScene {
    vks::Buffer indexBuffer;
    vks::Buffer vertexBuffer;
    vks::Buffer modelsBuffer;
    vks::Buffer meshletsBuffer;
    vks::Buffer instancesBuffer;
    vks::Buffer meshletUniqueVerticesBuffer;
    vks::Buffer meshletLocalIndicesBuffer;
    vks::Buffer sceneStats;

    void destroy() {
        indexBuffer.destroy();
        vertexBuffer.destroy();
        modelsBuffer.destroy();
        meshletsBuffer.destroy();
        instancesBuffer.destroy();
        meshletUniqueVerticesBuffer.destroy();
        meshletLocalIndicesBuffer.destroy();
        sceneStats.destroy();
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
    std::vector<uint32_t> m_meshletUniqueVertices{};
    std::vector<uint32_t> m_meshletLocalIndices{};

public:

    Scene(uint32_t maxVerticesPerMeshlet = 64, uint32_t maxTrianglesPerMeshlet = 126): 
        m_maxVerticesPerMeshlet(maxVerticesPerMeshlet), m_maxTrianglesPerMeshlet(maxTrianglesPerMeshlet) {}

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
    std::vector<uint32_t>& meshletUniqueVertices() { return m_meshletUniqueVertices; }
    std::vector<uint32_t>& meshletLocalIndices() { return m_meshletLocalIndices; }
};
