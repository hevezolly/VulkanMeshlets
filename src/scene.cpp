#include "scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <span>

struct AccessorData {
    const unsigned char* data;
    size_t stride;
};

AccessorData readAccessor(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t elementSize)
{
    if (accessor.sparse.isSparse || accessor.bufferView < 0 || accessor.count == 0) {
        throw std::runtime_error("Expected a nonempty, non-sparse glTF accessor with a buffer view");
    }

    const auto& view = model.bufferViews.at(accessor.bufferView);
    const auto& bytes = model.buffers.at(view.buffer).data;
    const int stride = accessor.ByteStride(view);
    if (stride < 0 || static_cast<size_t>(stride) < elementSize ||
        view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset ||
        accessor.byteOffset > view.byteLength || elementSize > view.byteLength - accessor.byteOffset ||
        accessor.count - 1 > (view.byteLength - accessor.byteOffset - elementSize) / stride) {
        throw std::runtime_error("glTF accessor exceeds its buffer view or has an invalid stride");
    }
    return {bytes.data() + view.byteOffset + accessor.byteOffset, static_cast<size_t>(stride)};
}

glm::mat4 nodeTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16) {
        return glm::mat4(glm::make_mat4(node.matrix.data()));
    }

    glm::mat4 transform(1.0f);
    if (node.translation.size() == 3) {
        transform = glm::translate(transform, glm::vec3(glm::make_vec3(node.translation.data())));
    }
    if (node.rotation.size() == 4) {
        const auto& rotation = node.rotation;
        transform *= glm::mat4_cast(glm::quat(rotation[3], rotation[0], rotation[1], rotation[2]));
    }
    if (node.scale.size() == 3) {
        transform = glm::scale(transform, glm::vec3(glm::make_vec3(node.scale.data())));
    }
    return transform;
}

void loadMesh(const tinygltf::Model& model, const tinygltf::Mesh& mesh, const glm::mat4x4& transform, std::vector<uint32_t>& indices, std::vector<Vertex>& vertices, std::vector<float>& resultPositions) {

    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));


    for (const auto& primitive : mesh.primitives) {
        const auto baseVertexIndex = static_cast<uint32_t>(vertices.size());
        if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
            throw std::runtime_error("Scene requires triangle-list glTF primitives");
        }
        const auto positionAttribute = primitive.attributes.find("POSITION");
        const auto normalAttribute = primitive.attributes.find("NORMAL");
        if (positionAttribute == primitive.attributes.end() || normalAttribute == primitive.attributes.end()) {
            throw std::runtime_error("glTF primitive is missing POSITION or NORMAL");
        }

        const auto& positions = model.accessors.at(positionAttribute->second);
        const auto& normals = model.accessors.at(normalAttribute->second);
        if (positions.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || positions.type != TINYGLTF_TYPE_VEC3 ||
            normals.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || normals.type != TINYGLTF_TYPE_VEC3 ||
            positions.count != normals.count) {
            throw std::runtime_error("Expected matching float VEC3 positions and normals");
        }

        const auto positionData = readAccessor(model, positions, 3 * sizeof(float));
        const auto normalData = readAccessor(model, normals, 3 * sizeof(float));
        for (size_t vertex = 0; vertex < positions.count; ++vertex) {
            glm::vec3 position;
            glm::vec3 normal;
            std::memcpy(&position[0], positionData.data + vertex * positionData.stride, sizeof(position));
            std::memcpy(&normal[0], normalData.data + vertex * normalData.stride, sizeof(normal));

            position = glm::vec3(transform * glm::vec4(position, 1));
            normal = glm::normalize(normalMatrix * normal);
            
            resultPositions.insert(resultPositions.end(), &position[0], &position[0] + 3);
            vertices.push_back(Vertex(position, normal));
        }

        if (primitive.indices < 0) {
            for (uint32_t index = 0; index < positions.count; ++index) {
                assert(index < positions.count);
                indices.push_back(index + baseVertexIndex);
            }
            continue;
        }

        const auto& modelIndices = model.accessors.at(primitive.indices);
        switch (modelIndices.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            break;
        default:
            throw std::runtime_error("Expected unsigned 8-, 16-, or 32-bit glTF indices");
        }
        const auto indexData = readAccessor(model, modelIndices, tinygltf::GetComponentSizeInBytes(modelIndices.componentType));
        for (size_t element = 0; element < modelIndices.count; ++element) {
            const auto* bytes = indexData.data + element * indexData.stride;
            uint32_t index = 0;
            std::memcpy(&index, bytes, tinygltf::GetComponentSizeInBytes(modelIndices.componentType));
            assert(index < positions.count);
            indices.push_back(index + baseVertexIndex);
        }
    }
}

// flatten every node into the same vertex/index buffers
void loadNode(const tinygltf::Model& model, int nodeId, const glm::mat4& parentTransform, std::vector<uint32_t>& indices, std::vector<Vertex>& vertices, std::vector<float>& resultPositions) {
    const auto& node = model.nodes.at(nodeId);
    const glm::mat4 transform = parentTransform * nodeTransform(node);
    if (node.mesh >= 0) {
        loadMesh(model, model.meshes.at(node.mesh), transform, indices, vertices, resultPositions);
    }
    for (int child : node.children) {
        loadNode(model, child, transform, indices, vertices, resultPositions);
    }
}

struct IntermediateModelRepresentation {
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    std::vector<float> positions;
};

IntermediateModelRepresentation loadModel(const std::string& filename) 
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model gltf;
    std::string error;
    std::string warning;
    IntermediateModelRepresentation modelRepr;
    if (!loader.LoadASCIIFromFile(&gltf, &error, &warning, filename)) {
        throw std::runtime_error("Could not load glTF model " + filename + ": " + error);
    }
    if (gltf.scenes.empty()) {
        throw std::runtime_error("No scenes in glTF model " + filename);
    }

    const auto& scene = gltf.scenes.at(gltf.defaultScene >= 0 ? gltf.defaultScene : 0);
    for (int node : scene.nodes) {
        loadNode(gltf, node, glm::mat4(1.0f), modelRepr.indices, modelRepr.vertices, modelRepr.positions);
    }
    return modelRepr;
}

static std::vector<Meshlet> reorderModelByMeshlets(
    IntermediateModelRepresentation& model,
    std::span<const meshopt_Meshlet> meshlets,
    std::span<const unsigned int> meshletVertices,
    std::span<const unsigned char> meshletTriangles,
    std::vector<uint32_t>& uniqueVertices,
    std::vector<uint32_t>& localIndices)
{
    constexpr size_t maxIndex = std::numeric_limits<uint32_t>::max();
    const size_t vertexCount = model.vertices.size();
    if (vertexCount > maxIndex || model.indices.size() > maxIndex ||
        model.positions.size() % 3 != 0 || model.positions.size() / 3 != vertexCount) {
        throw std::invalid_argument("Invalid vertex streams or index count");
    }

    std::vector<Meshlet> ranges;
    ranges.reserve(meshlets.size());
    IntermediateModelRepresentation reordered;
    reordered.indices.reserve(model.indices.size());
    uniqueVertices.reserve(model.indices.size());
    localIndices.reserve(model.indices.size());

    for (const auto& meshlet : meshlets) {
        const size_t indexCount = size_t{meshlet.triangle_count} * 3;
        if (meshlet.vertex_offset > meshletVertices.size() ||
            meshlet.vertex_count > meshletVertices.size() - meshlet.vertex_offset ||
            meshlet.triangle_offset > meshletTriangles.size() ||
            indexCount > meshletTriangles.size() - meshlet.triangle_offset ||
            indexCount > model.indices.size() - reordered.indices.size() ||
            meshlet.vertex_count > indexCount) {
            throw std::invalid_argument("Meshlet range exceeds its input buffer");
        }

        auto& range = ranges.emplace_back(meshlet.vertex_count, static_cast<uint32_t>(indexCount));
        range.globalIndexStart = static_cast<uint32_t>(reordered.indices.size());
        range.uniqueVerticesStart = static_cast<uint32_t>(uniqueVertices.size());
        range.localIndicesStart = static_cast<uint32_t>(localIndices.size());
        const auto localVertices = meshletVertices.subspan(meshlet.vertex_offset, meshlet.vertex_count);
        uniqueVertices.insert(uniqueVertices.end(), localVertices.begin(), localVertices.end());
        for (unsigned char localIndex : meshletTriangles.subspan(meshlet.triangle_offset, indexCount)) {
            if (localIndex >= localVertices.size() || localVertices[localIndex] >= vertexCount) {
                throw std::invalid_argument("Meshlet references an invalid vertex");
            }
            localIndices.push_back(localIndex);
            reordered.indices.push_back(localVertices[localIndex]);
        }
    }
    if (reordered.indices.size() != model.indices.size()) {
        throw std::invalid_argument("Meshlet indices do not cover the model's index count");
    }
    if (reordered.indices.empty()) {
        return ranges;
    }

    // optimize vertex fetch
    std::vector<unsigned int> remap(vertexCount);
    const size_t usedVertices = meshopt_optimizeVertexFetchRemap(
        remap.data(), reordered.indices.data(), reordered.indices.size(), vertexCount);
    
    // perform remapping of indices, vertices and positions
    reordered.vertices.resize(usedVertices);
    reordered.positions.resize(usedVertices * 3);
    meshopt_remapVertexBuffer(reordered.vertices.data(), model.vertices.data(), vertexCount, sizeof(Vertex), remap.data());
    meshopt_remapVertexBuffer(reordered.positions.data(), model.positions.data(), vertexCount, 3 * sizeof(float), remap.data());
    meshopt_remapIndexBuffer(reordered.indices.data(), reordered.indices.data(), reordered.indices.size(), remap.data());
    for (auto& index : uniqueVertices) {
        if (index >= remap.size() || remap[index] == std::numeric_limits<unsigned int>::max()) {
            throw std::invalid_argument("Meshlet references an unused or invalid unique vertex");
        }
        index = remap[index];
    }

    model = std::move(reordered);
    return ranges;
}

uint32_t Scene::addModel(const std::string& filename)
{
    // load model, producing vertex buffer, index buffer and positions
    auto model = loadModel(filename);

    // build meshlets from loaded model
    const size_t indexCount = model.indices.size();
    const size_t meshletBound = meshopt_buildMeshletsBound(indexCount, m_maxVerticesPerMeshlet, m_maxTrianglesPerMeshlet);
    std::vector<meshopt_Meshlet> meshlets(meshletBound);
    std::vector<unsigned int> meshletVertices(indexCount);
    std::vector<unsigned char> meshletTriangles(indexCount);
    const float coneWeight = 0.25f; // recommended by meshoptimizer lib if cone-culling is used 
    meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
        model.indices.data(), indexCount, model.positions.data(), model.vertices.size(),
        3 * sizeof(float), m_maxVerticesPerMeshlet, m_maxTrianglesPerMeshlet, coneWeight));

    // optimize each individual meshlet
    for (const auto& meshlet : meshlets) {
        meshopt_optimizeMeshlet(meshletVertices.data() + meshlet.vertex_offset,
            meshletTriangles.data() + meshlet.triangle_offset, meshlet.triangle_count, meshlet.vertex_count);
    }

    std::vector<uint32_t> uniqueVertices;
    std::vector<uint32_t> localIndices;
    auto ranges = reorderModelByMeshlets(model, meshlets, meshletVertices, meshletTriangles, uniqueVertices, localIndices);

    // compute per-meshlet bounds
    for (size_t i = 0; i < ranges.size(); ++i) {
        auto& meshlet = ranges[i];
        const auto bounds = meshopt_computeClusterBounds(
            model.indices.data() + meshlet.globalIndexStart,
            size_t{meshlets[i].triangle_count} * 3,
            model.positions.data(),
            model.vertices.size(),
            3 * sizeof(float));

        meshlet.boundingSphere = glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
        meshlet.coneAxisAngle = glm::vec4(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff);
        meshlet.coneApex = glm::vec4(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2], 0);
    }
   
    // compute entire mesh bounding sphere
    const auto bounds = meshopt_computeSphereBounds(model.positions.data(), model.vertices.size(), 3 * sizeof(float), nullptr, 0);

    constexpr size_t maxOffset = std::numeric_limits<uint32_t>::max();
    if (model.vertices.size() > maxOffset - m_vertices.size() ||
        model.indices.size() > maxOffset - m_indices.size() ||
        ranges.size() > maxOffset - m_meshlets.size() ||
        uniqueVertices.size() > maxOffset - m_meshletUniqueVertices.size() ||
        localIndices.size() > maxOffset - m_meshletLocalIndices.size()) {
        throw std::overflow_error("Scene exceeds 32-bit meshlet or geometry ranges");
    }

    const auto vertexStart = static_cast<uint32_t>(m_vertices.size());
    const auto indexStart = static_cast<uint32_t>(m_indices.size());
    const auto meshletStart = static_cast<uint32_t>(m_meshlets.size());
    const auto uniqueVerticesStart = static_cast<uint32_t>(m_meshletUniqueVertices.size());
    const auto localIndicesStart = static_cast<uint32_t>(m_meshletLocalIndices.size());
    for (auto& index : model.indices) {
        index += vertexStart;
    }
    for (auto& meshlet : ranges) {
        meshlet.globalIndexStart += indexStart;
        meshlet.uniqueVerticesStart += uniqueVerticesStart;
        meshlet.localIndicesStart += localIndicesStart;
    }
    for (auto& index : uniqueVertices) {
        index += vertexStart;
    }

    m_vertices.reserve(m_vertices.size() + model.vertices.size());
    m_indices.reserve(m_indices.size() + model.indices.size());
    m_meshlets.reserve(m_meshlets.size() + ranges.size());
    m_meshletUniqueVertices.reserve(m_meshletUniqueVertices.size() + uniqueVertices.size());
    m_meshletLocalIndices.reserve(m_meshletLocalIndices.size() + localIndices.size());
    uint32_t modelId = m_models.size();
    m_models.push_back({
        .boundingSphere = glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius),
        .indexStart = indexStart,
        .indicesCount = static_cast<uint32_t>(model.indices.size()),
        .meshletStart = meshletStart,
        .meshletCount = static_cast<uint32_t>(ranges.size()),
    });
    m_vertices.insert(m_vertices.end(), model.vertices.begin(), model.vertices.end());
    m_indices.insert(m_indices.end(), model.indices.begin(), model.indices.end());
    m_meshlets.insert(m_meshlets.end(), ranges.begin(), ranges.end());
    m_meshletUniqueVertices.insert(m_meshletUniqueVertices.end(), uniqueVertices.begin(), uniqueVertices.end());
    m_meshletLocalIndices.insert(m_meshletLocalIndices.end(), localIndices.begin(), localIndices.end());

    return modelId;
}
