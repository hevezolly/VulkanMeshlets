#pragma once
#include "scene.h"
#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include "descriptors.h"
#include "compute_pass.h"
#include "graphics_pass.h"
#include "shader_loader.h"
#include "VulkanUIOverlay.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "capacity_controller.h"
#include "delayed_destroy_queue.h"
#include "barriers.h"

constexpr uint32_t DefaultGroupSize = 64;

constexpr uint32_t MeshletsProcessedPerGroup = 256;
constexpr uint32_t MinIndirectCommands = 16;
constexpr uint32_t MinMeshletWorkloads = 8;
constexpr uint32_t MinVertexIndexBuffers = 256;

struct PerInstanceWorkload {
    uint32_t instanceId;
    uint32_t startMeshlet;
    uint32_t meshletsCount;
    uint32_t _padding;
};

struct PerMeshletWorkload {
    uint32_t instanceId;
    uint32_t meshletId;
};

struct EnvironmentData {
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;

    glm::vec4 lightDirIntensity;
    glm::vec4 lightColor;
    glm::vec4 envColorIntensity;
};

struct CullingSetup {
    glm::mat4 cameraViewProjection;
    glm::vec4 cameraPosition;
    uint32_t maxDrawCommands;
    uint32_t maxWorkloadPerInstanceCount;
    uint32_t maxWorkloadPerMeshletCount;
    uint32_t maxVertexCount;
    uint32_t maxIndexCount;
};

struct Commands {
    VkDispatchIndirectCommand dispatchPerInstanceCommand;
    VkDispatchIndirectCommand dispatchPerMeshletCommand;
    VkDrawIndexedIndirectCommand singleDrawIndexedCommand;
    uint32_t drawCommandCounter;
    uint32_t vertexCounter;
    uint32_t indexCounter;
};

class DrawSystem {
public:
    enum class CameraKey { Forward, Backward, Left, Right, Up, Down, Count };

    DrawSystem(uint32_t maxWorkPerMeshlet): m_maxWorkPerMeshlet(maxWorkPerMeshlet){}

private:
    static inline const glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};
    uint32_t m_maxWorkPerMeshlet;
    
    
    struct {
        DescriptorSetLayout commonBuffers;
        DescriptorSetLayout scene;
        DescriptorSetLayout multiDraw;
        DescriptorSetLayout singleDraw;
    } m_layouts;

    struct {
        std::vector<DescriptorSet> commonBuffers;
        DescriptorSet scene;
        std::vector<DescriptorSet> multiDraw;
        std::vector<DescriptorSet> singleDraw;
    } m_sets;

    struct {
        vks::Buffer indirectDrawCommandsBuffer;
        vks::Buffer commandBuffer;
        vks::Buffer perInstanceWorkload;
        vks::Buffer perMeshletWorkload;
        vks::Buffer culledVertexBuffer;
        vks::Buffer culledIndexBuffer;
        std::vector<vks::Buffer> readbackBuffers;
    } m_buffers;

    struct {
        BufferStateHandle indirectDraw;
        BufferStateHandle commands;
        BufferStateHandle perInstance;
        BufferStateHandle perMeshlet;
        BufferStateHandle culledVertices;
        BufferStateHandle culledIndices;
    } m_bufferState;

    struct {

        CapacityController indirectDrawBufferController;
        CapacityController indirectDrawCountController;

        CapacityController perInstanceWorkloadController;

        CapacityController perMeshletWorkloadController;
        CapacityController culledVertexCapacityController;
        CapacityController culledIndexCapacityController;

    } m_capacityTracking;

    struct {
        ComputePass clearCommands;
        ComputePass cullInstances;
        ComputePass cullMeshletsMultidraw;

        ComputePass cullMeshletsSingledraw;
        ComputePass writeDrawBuffers;

        GraphicsPass multiDraw;
        GraphicsPass singleDraw;
    } m_pipelines;

    struct {
        glm::vec3 cameraPosition{0.5f, 0.5f, 0.5f};
        glm::vec3 cameraForward = glm::normalize(glm::vec3{0., 0.3, 0.} - cameraPosition);
        float fov = 45.0f;

        glm::mat4 viewProjection{1.0f};

        glm::mat4 cullViewProjection{1.0f};
        glm::vec3 cullCameraPosition{0.5f, 0.5f, 0.5f};
        bool freezeFrustumCull = false;
    } m_cameraData;

    EnvironmentData m_environment  {
        .lightDirIntensity = glm::vec4(glm::vec3(0, 1, 0.5), 1.),
        .lightColor = glm::vec4(1., 1., 1., 1.),
        .envColorIntensity = glm::vec4(0.53, 0.80, 0.92, 0.1)
    };

    std::array<bool, static_cast<size_t>(CameraKey::Count)> m_cameraKeys{};

    uint32_t m_framesInFlight;
    VkDevice m_device;
    vks::VulkanDevice* m_vulkanDevice;
    DelayedDestroyQueue m_destroyQueue;
    BarrierManager m_barriers;
    uint32_t m_commonBuffersDirty{0};
    uint32_t m_multidrawBuffersDirty{0};
    uint32_t m_singledrawBuffersDirty{0};
    uint32_t m_multidrawSupported;
    uint32_t m_multidraw;

    void allocPerInstanceWorkload(uint32_t count) {
        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.perInstanceWorkload, 
            sizeof(PerInstanceWorkload) * count
        ));
    }

    void allocIndirectDrawCommands(uint32_t count) {
        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.indirectDrawCommandsBuffer, 
            sizeof(VkDrawIndexedIndirectCommand) * count
        ));
    }

    void allocPerMeshletWorkload(uint32_t count) {
        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.perMeshletWorkload, 
            sizeof(PerMeshletWorkload) * count
        ));
    }

    void allocCulledVertexBuffer(uint32_t count) {
        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.culledVertexBuffer, 
            sizeof(Vertex) * count
        ));
    }

    void allocCulledIndexBuffer(uint32_t count) {
        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.culledIndexBuffer, 
            sizeof(uint32_t) * count
        ));
    }

    void allocateBuffers() {
        m_buffers.readbackBuffers.resize(m_framesInFlight);

        for (uint32_t i = 0; i < m_framesInFlight; i++) {

            VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &m_buffers.readbackBuffers[i], sizeof(Commands)
            ));
            VK_CHECK_RESULT(m_buffers.readbackBuffers[i].map());
        }

        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.commandBuffer, 
            sizeof(Commands)
        ));

        allocPerInstanceWorkload(MinMeshletWorkloads);

        if (m_multidraw) {
            allocIndirectDrawCommands(MinIndirectCommands);

        } else {

            allocPerMeshletWorkload(MinMeshletWorkloads);

            allocCulledVertexBuffer(MinVertexIndexBuffers);

            allocCulledIndexBuffer(MinVertexIndexBuffers);
        }

        
    }

    void writeCommonDescriptors(uint32_t frameId) {
        m_sets.commonBuffers[frameId].write({
            m_buffers.perInstanceWorkload.descriptor,
            m_buffers.commandBuffer.descriptor
        });
    }

    void writeMultidrawDescriptors(uint32_t frameId) {
        m_sets.multiDraw[frameId].write({
            m_buffers.indirectDrawCommandsBuffer.descriptor,
        });
    }

    void writeSingledrawDescriptors(uint32_t frameId) {
        m_sets.singleDraw[frameId].write({
            m_buffers.perMeshletWorkload.descriptor,
            m_buffers.culledVertexBuffer.descriptor,
            m_buffers.culledIndexBuffer.descriptor
        });
    }

    void allocateDescriptors(DescriptorPool& descriptors, GpuScene& gpuScene) 
    {
        m_sets.commonBuffers.reserve(m_framesInFlight);

        for (uint32_t i = 0; i < m_framesInFlight; i++) {

            m_sets.commonBuffers.push_back(descriptors.allocate(m_layouts.commonBuffers));
            writeCommonDescriptors(i);

            if (!m_multidraw) {
                m_sets.singleDraw.push_back(descriptors.allocate(m_layouts.singleDraw));
                writeSingledrawDescriptors(i);
            } else {
                m_sets.multiDraw.push_back(descriptors.allocate(m_layouts.multiDraw));
                writeMultidrawDescriptors(i);
            }
        }

        m_sets.scene = descriptors.allocate(m_layouts.scene);
        m_sets.scene.write({
            gpuScene.vertexBuffer.descriptor,
            gpuScene.modelsBuffer.descriptor,
            gpuScene.meshletsBuffer.descriptor,
            gpuScene.instancesBuffer.descriptor,
            gpuScene.meshletUniqueVerticesBuffer.descriptor,
            gpuScene.meshletLocalIndicesBuffer.descriptor,
            gpuScene.sceneStats.descriptor
        });
    }

    void preparePipelines(const ShaderLoader& shaderLoader, VkFormat dsFormat, VkFormat colorFormat) 
    {
        m_pipelines.clearCommands = ComputePass(m_device,
            shaderLoader.loadShader("compute/clear_indirect_dispatch.comp",  VK_SHADER_STAGE_COMPUTE_BIT),
            {
                m_layouts.commonBuffers,
            },
            sizeof(CullingSetup)
        );

        m_pipelines.cullInstances = ComputePass(m_device, 
            shaderLoader.loadShader("compute/cull_instances.comp", VK_SHADER_STAGE_COMPUTE_BIT),
            {
                m_layouts.commonBuffers,
                m_layouts.scene 
            },
            sizeof(CullingSetup)
        );

        if (m_multidraw) {
            m_pipelines.cullMeshletsMultidraw = ComputePass(m_device, 
                shaderLoader.loadShader("compute/cull_meshlets_multidraw.comp", VK_SHADER_STAGE_COMPUTE_BIT),
                {
                    m_layouts.commonBuffers,
                    m_layouts.scene,
                    m_layouts.multiDraw
                },
                sizeof(CullingSetup)
            );
    
            m_pipelines.multiDraw = GraphicsPass(m_device,
                {
                    shaderLoader.loadShader("multi_draw.vert", VK_SHADER_STAGE_VERTEX_BIT),
                    shaderLoader.loadShader("draw.frag", VK_SHADER_STAGE_FRAGMENT_BIT),
                }, dsFormat, colorFormat,
                {
                    m_layouts.scene
                },
                sizeof(EnvironmentData)
            );
        }
        else {
            m_pipelines.cullMeshletsSingledraw = ComputePass(m_device, 
                shaderLoader.loadShader("compute/cull_meshlets_singledraw.comp", VK_SHADER_STAGE_COMPUTE_BIT),
                {
                    m_layouts.commonBuffers,
                    m_layouts.scene,
                    m_layouts.singleDraw
                },
                sizeof(CullingSetup)
            );

            m_pipelines.writeDrawBuffers = ComputePass(m_device, 
                shaderLoader.loadShader("compute/write_draw_buffers.comp", VK_SHADER_STAGE_COMPUTE_BIT),
                {
                    m_layouts.commonBuffers,
                    m_layouts.scene,
                    m_layouts.singleDraw
                },
                sizeof(CullingSetup)
            );

            m_pipelines.singleDraw = GraphicsPass(m_device,
                {
                    shaderLoader.loadShader("single_draw.vert", VK_SHADER_STAGE_VERTEX_BIT),
                    shaderLoader.loadShader("draw.frag", VK_SHADER_STAGE_FRAGMENT_BIT),
                }, dsFormat, colorFormat,
                {
                    m_layouts.scene,
                    m_layouts.singleDraw
                },
                sizeof(EnvironmentData)
            );
        }

    }

    void setUpCapacityTracking() 
    {
        constexpr uint32_t defaultShrinkDelay = 360;
        constexpr float defaultExcessFactor = 1.5;
        // ammortizing capacities of both command buffers and number of multi-draw commands.

        m_capacityTracking.perInstanceWorkloadController = CapacityController {
            MinMeshletWorkloads, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor
        };

        if (m_multidraw) {

            m_capacityTracking.indirectDrawBufferController = CapacityController {
                MinIndirectCommands, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor
            };

            // it costs nothing to change the number of commands, so make it a lot more responsive
            m_capacityTracking.indirectDrawCountController = CapacityController {
                MinIndirectCommands, 0.85, 0.75, 10, 1.1
            };
        } else {
            
            m_capacityTracking.perMeshletWorkloadController = CapacityController {
                MinMeshletWorkloads, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor
            };

            m_capacityTracking.culledIndexCapacityController = CapacityController {
                MinVertexIndexBuffers, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor
            };

            m_capacityTracking.culledVertexCapacityController = CapacityController {
                MinVertexIndexBuffers, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor
            };

        }

    }
 
public:

    void destroy() {

        for (auto& readback : m_buffers.readbackBuffers) {
            readback.unmap();
            readback.destroy();
        }

        m_buffers.indirectDrawCommandsBuffer.destroy();
        m_buffers.commandBuffer.destroy();
        m_buffers.perInstanceWorkload.destroy();
        m_buffers.perMeshletWorkload.destroy();
        m_buffers.culledVertexBuffer.destroy();
        m_buffers.culledIndexBuffer.destroy();

        m_destroyQueue.flush();
    }

    void registerLayouts(
        VkDevice device, 
        uint32_t framesInFlight, 
        DescriptorPool& pool,
        bool multidraw
    )
    {
        m_multidraw = multidraw;
        m_framesInFlight = framesInFlight;    
        m_device = device;

        m_layouts.commonBuffers = DescriptorSetLayout(device, {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        });

        m_layouts.scene = DescriptorSetLayout(m_device,
        {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
        });

        if (!m_multidraw) {
            m_layouts.singleDraw = DescriptorSetLayout(m_device,{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
            });

            pool.registerLayout(m_layouts.singleDraw, framesInFlight);
        } else {
            m_layouts.multiDraw = DescriptorSetLayout(m_device, {
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            });
            pool.registerLayout(m_layouts.multiDraw, framesInFlight);
        }

        pool.registerLayout(m_layouts.scene);
        pool.registerLayout(m_layouts.commonBuffers, framesInFlight);
    }

    void prepare(
        vks::VulkanDevice* vulkanDevice, 
        DescriptorPool& descriptors, 
        GpuScene& gpuScene, 
        Scene& scene,
        const ShaderLoader& shaderLoader,
        VkFormat dsFormat,
        VkFormat colorFormat
    ) {
        m_vulkanDevice = vulkanDevice;

        allocateBuffers();
        allocateDescriptors(descriptors, gpuScene);
        preparePipelines(shaderLoader, dsFormat, colorFormat);
        setUpCapacityTracking();
    }

    void updateUI(vks::UIOverlay& overlay) {

        if (overlay.header("draw stats")) {
            if (m_multidraw) {
                overlay.text("multi-draw enabled");
                overlay.text("per-instance workload allocated: %zu", m_capacityTracking.perInstanceWorkloadController.capacity());
                overlay.text("max draw commands: %zu", m_capacityTracking.indirectDrawCountController.capacity());
                overlay.text("draw commands allocated: %zu", m_capacityTracking.indirectDrawBufferController.capacity());
            } else {
                overlay.text("multi-draw disabled");
                overlay.text("per-instance workload allocated: %zu", m_capacityTracking.perInstanceWorkloadController.capacity());
                overlay.text("per-meshlet workload allocated: %zu", m_capacityTracking.perMeshletWorkloadController.capacity());
                overlay.text("indices allocated: %zu", m_capacityTracking.culledIndexCapacityController.capacity());
                overlay.text("vertices allocated: %zu", m_capacityTracking.culledVertexCapacityController.capacity());
            }
        }


        if (overlay.header("camera and environment")) {
            overlay.checkBox("freeze frustum cull", &m_cameraData.freezeFrustumCull);
            overlay.sliderFloat("field of view", &m_cameraData.fov, 10., 120.);

            overlay.colorPicker("direct light color", &m_environment.lightColor[0]);
            overlay.inputFloat("direct light intensity", &m_environment.lightDirIntensity[3], 0.01, 2);

            overlay.colorPicker("environment color", &m_environment.envColorIntensity[0]);
            overlay.inputFloat("environment intensity", &m_environment.envColorIntensity[3], 0.01, 2);

            overlay.inputFloat("light direction X", &m_environment.lightDirIntensity[0], 0.01, 2);
            overlay.inputFloat("light direction Y", &m_environment.lightDirIntensity[1], 0.01, 2);
            overlay.inputFloat("light direction Z", &m_environment.lightDirIntensity[2], 0.01, 2);
        }
    }

    void setCameraKey(CameraKey key, bool pressed)
    {
        m_cameraKeys[static_cast<size_t>(key)] = pressed;
    }

    void clearCameraKeys()
    {
        m_cameraKeys.fill(false);
    }

    void rotateCamera(float dx, float dy)
    {
        constexpr float sensitivity = 0.003f;
        const float pitchLimit = glm::radians(89.0f);
        const auto& forward = m_cameraData.cameraForward;
        const float yaw = std::atan2(forward.z, forward.x) + dx * sensitivity;
        const float pitch = std::clamp(std::asin(std::clamp(forward.y, -1.0f, 1.0f)) - dy * sensitivity,
            -pitchLimit, pitchLimit);
        m_cameraData.cameraForward = glm::vec3(
            std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
    }

    void updateCamera(float deltaTime)
    {
        const auto axis = [this](CameraKey positive, CameraKey negative) {
            return float(m_cameraKeys[static_cast<size_t>(positive)]) -
                float(m_cameraKeys[static_cast<size_t>(negative)]);
        };
        const glm::vec3 forward = m_cameraData.cameraForward;
        const glm::vec3 right = glm::normalize(glm::cross(forward, WorldUp));
        const glm::vec3 movement = forward * axis(CameraKey::Forward, CameraKey::Backward) +
            right * axis(CameraKey::Right, CameraKey::Left) + WorldUp * axis(CameraKey::Up, CameraKey::Down);
        if (glm::dot(movement, movement) > 0.0f) {
            constexpr float moveSpeed = 0.5f;
            m_cameraData.cameraPosition += glm::normalize(movement) * moveSpeed * std::min(deltaTime, 0.1f);
        }
    }

    void applyCamereaParameters(uint32_t frameId, VkExtent2D extent) 
    {
        // just hardcode in some parameters
        const glm::vec3 position = m_cameraData.cameraPosition;
        const glm::mat4 view = glm::lookAt(position, position + m_cameraData.cameraForward, WorldUp);
        glm::mat4 projection = glm::perspective(glm::radians(m_cameraData.fov), extent.width / (float) extent.height, 0.1f, 25.0f);
        projection[1][1] *= -1;
        m_cameraData.viewProjection = projection * view;

        if (!m_cameraData.freezeFrustumCull) {
            m_cameraData.cullViewProjection = m_cameraData.viewProjection;
            m_cameraData.cullCameraPosition = m_cameraData.cameraPosition;
        }

        m_environment.viewProjection = m_cameraData.viewProjection;
        m_environment.cameraPosition = glm::vec4(m_cameraData.cameraPosition, 1);
    }

    void updateCapacitiesMultidraw(const Commands& commands, uint32_t frameId) 
    {
        m_capacityTracking.indirectDrawBufferController.update(commands.drawCommandCounter);
        m_capacityTracking.indirectDrawCountController.update(commands.drawCommandCounter);

        if (m_buffers.indirectDrawCommandsBuffer.size != m_capacityTracking.indirectDrawBufferController.capacity() * 
            sizeof(VkDrawIndexedIndirectCommand)) 
        {
            m_destroyQueue.enqueue(m_buffers.indirectDrawCommandsBuffer);
            allocIndirectDrawCommands(m_capacityTracking.indirectDrawBufferController.capacity());
            m_multidrawBuffersDirty |= (1 << m_framesInFlight) - 1;
        }

        if (m_multidrawBuffersDirty & (1 << frameId)) {
            writeMultidrawDescriptors(frameId);
            m_multidrawBuffersDirty &= ~(1 << frameId);
        }
    }

    void updateCapacitiesSingledraw(const Commands& commands, uint32_t frameId) 
    {
        m_capacityTracking.perMeshletWorkloadController.update(commands.dispatchPerMeshletCommand.x);
        m_capacityTracking.culledIndexCapacityController.update(commands.indexCounter);
        m_capacityTracking.culledVertexCapacityController.update(commands.vertexCounter);

        if (m_buffers.culledIndexBuffer.size != m_capacityTracking.culledIndexCapacityController.capacity() * sizeof(uint32_t)) 
        {
            m_destroyQueue.enqueue(m_buffers.culledIndexBuffer);

            allocCulledIndexBuffer(m_capacityTracking.culledIndexCapacityController.capacity());

            m_singledrawBuffersDirty |= (1 << m_framesInFlight) - 1;
        }

        if (m_buffers.culledVertexBuffer.size != m_capacityTracking.culledVertexCapacityController.capacity() * sizeof(Vertex)) 
        {
            m_destroyQueue.enqueue(m_buffers.culledVertexBuffer);

            allocCulledVertexBuffer(m_capacityTracking.culledVertexCapacityController.capacity());

            m_singledrawBuffersDirty |= (1 << m_framesInFlight) - 1;
        }
        
        if (m_buffers.perMeshletWorkload.size != m_capacityTracking.perMeshletWorkloadController.capacity() * sizeof(PerMeshletWorkload)) 
        {
            m_destroyQueue.enqueue(m_buffers.perMeshletWorkload);

            allocPerMeshletWorkload(m_capacityTracking.perMeshletWorkloadController.capacity());

            m_singledrawBuffersDirty |= (1 << m_framesInFlight) - 1;
        }

        if (m_singledrawBuffersDirty & (1 << frameId)) {
            writeSingledrawDescriptors(frameId);
            m_singledrawBuffersDirty &= ~(1 << frameId);
        }
    }

    void updateCapacities(uint32_t frameId) 
    {
        Commands commands;
        memcpy(&commands, m_buffers.readbackBuffers[frameId].mapped, sizeof(Commands));

        m_capacityTracking.perInstanceWorkloadController.update(commands.dispatchPerInstanceCommand.x);

        if (m_buffers.perInstanceWorkload.size != m_capacityTracking.perInstanceWorkloadController.capacity() * 
            sizeof(PerInstanceWorkload)) 
        {
            m_destroyQueue.enqueue(m_buffers.perInstanceWorkload);

            allocPerInstanceWorkload(m_capacityTracking.perInstanceWorkloadController.capacity());

            m_commonBuffersDirty |= (1 << m_framesInFlight) - 1;
        }

        if (m_commonBuffersDirty & (1 << frameId)) {
            writeCommonDescriptors(frameId);
            m_commonBuffersDirty &= ~(1 << frameId);
        }
        
        if (m_multidraw) {
            updateCapacitiesMultidraw(commands, frameId);
        } else {
            updateCapacitiesSingledraw(commands, frameId);
        }
    }

private:
    void continueMultiDraw(
        VkCommandBuffer cmd, 
        uint32_t frameId, 
        uint64_t frameIndex,
        VkFramebuffer frameBuffer, 
        VkExtent2D extent,
        Scene& scene,
        GpuScene& gpuScene
    ) {
        {
            // clear indirect command buffer. Required, since I'm using drawInstancedIndirect, not drawInstancedIndirectCount
            vkCmdFillBuffer(
                cmd, m_buffers.indirectDrawCommandsBuffer.buffer, 0, VK_WHOLE_SIZE, 0
            );

            m_bufferState.indirectDraw = {m_buffers.indirectDrawCommandsBuffer.buffer, ResourceState {
                .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .access = VK_ACCESS_2_TRANSFER_WRITE_BIT
            }};
        }

        m_barriers
            .enqueue(m_bufferState.commands, {
                .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            })
            .enqueue(m_bufferState.perInstance, {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            })
            .enqueue(m_bufferState.indirectDraw, {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            })
            .barrier(cmd);

        {
            // compute work with group count equal to the number of items in perInstanceWorkload,
            // with group size equal to MeshletsProcessedPerGroup. 
            // PerInstanceWorkload.meshletsCount can never surpass MeshletsProcessedPerGroup, so every meshlet is processed.
            // MeshletsProcessedPerGroup must be selected carefully based on expected number of meshlets in a typical model, 
            // so the work is balanced.
            //
            // m_pipelines.cullMeshletsMultidraw culls meshlets of visible instances and generates indirect draw commands. 
            // Thanks to particular vertex and index buffer ordering, it is possible to combine adjesent meshlets into a single draw command.
            
            m_pipelines.cullMeshletsMultidraw.bind(cmd, m_sets.multiDraw[frameId], 2);
            m_pipelines.cullMeshletsMultidraw.dispatchIndirect(cmd, m_buffers.commandBuffer.buffer);
        }

        m_barriers
            .enqueue(m_bufferState.indirectDraw, {
                .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
            })
            .enqueue(m_bufferState.commands, {
                .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .access = VK_ACCESS_2_TRANSFER_READ_BIT
            })
            .barrier(cmd);

        VkBufferCopy region {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = sizeof(Commands)
        };
        
        vkCmdCopyBuffer(
            cmd,
            m_buffers.commandBuffer.buffer,
            m_buffers.readbackBuffers[frameId].buffer,
            1,
            &region
        );

        {
            uint32_t drawCount = std::min(
                m_capacityTracking.indirectDrawCountController.capacity(),
                m_capacityTracking.indirectDrawBufferController.capacity() 
            );
            // multi-draw inderect
            m_pipelines.multiDraw.beginPass(cmd, frameBuffer, extent, glm::vec3(m_environment.envColorIntensity));
            m_pipelines.multiDraw.bind(cmd, m_sets.scene, 0);
            m_pipelines.multiDraw.pushConstant(cmd, m_environment);
            m_pipelines.multiDraw.drawIndexedIndirect(
                cmd, gpuScene.indexBuffer.buffer, 
                m_buffers.indirectDrawCommandsBuffer.buffer,
                drawCount
            );
        }
    }

    void continueSingleDraw(
        VkCommandBuffer cmd, 
        uint32_t frameId, 
        uint64_t frameIndex,
        VkFramebuffer frameBuffer, 
        VkExtent2D extent,
        Scene& scene,
        GpuScene& gpuScene
    ) {

        m_barriers
            .enqueue(m_bufferState.commands, {
                .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            })
            .enqueue(m_bufferState.perInstance, {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            })
            .barrier(cmd);

        {
            // compute work analogus to the multi-draw branch. Culls meshlets and
            // produces intermediate buffer with meshletId and instanceId correspondance.

            m_pipelines.cullMeshletsSingledraw.bind(cmd, m_sets.singleDraw[frameId], 2);
            m_pipelines.cullMeshletsSingledraw.dispatchIndirect(cmd, m_buffers.commandBuffer.buffer);

            m_bufferState.perMeshlet = BufferStateHandle{m_buffers.perMeshletWorkload.buffer, ResourceState {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            }};
        }

        m_barriers
            .enqueue(m_bufferState.perMeshlet, {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            })
            .enqueue(m_bufferState.commands, {
                .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            })
            .barrier(cmd);

        {
            // compute for every visible meshlet in every visible instance. Produces new vertex and index buffer of 
            // primitives that survived culling. There is additional info stored per meshlet, that allows to reuse
            // vertices effectively within every meshlet. Instance id is stored in the unused W component of each vertex.

            m_pipelines.writeDrawBuffers.dispatchIndirect(cmd, 
                m_buffers.commandBuffer.buffer, sizeof(VkDispatchIndirectCommand));

            m_bufferState.culledIndices = BufferStateHandle{m_buffers.culledIndexBuffer.buffer, ResourceState {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            }};

            m_bufferState.culledVertices = BufferStateHandle{m_buffers.culledVertexBuffer.buffer, ResourceState {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            }};
        }

        m_barriers
            .enqueue(m_bufferState.culledIndices, {
                .stage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                .access = VK_ACCESS_2_INDEX_READ_BIT
            })
            .enqueue(m_bufferState.culledVertices, {
                .stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT_KHR
            })
            .enqueue(m_bufferState.commands, {
                .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT
            })
            .barrier(cmd);

        VkBufferCopy region {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = sizeof(Commands)
        };
        
        vkCmdCopyBuffer(
            cmd,
            m_buffers.commandBuffer.buffer,
            m_buffers.readbackBuffers[frameId].buffer,
            1,
            &region
        );

        {
            m_pipelines.singleDraw.beginPass(cmd, frameBuffer, extent, glm::vec3(m_environment.envColorIntensity));
            m_pipelines.singleDraw.bind(cmd, m_sets.scene, 0);
            m_pipelines.singleDraw.bind(cmd, m_sets.singleDraw[frameId], 1);
            m_pipelines.multiDraw.pushConstant(cmd, m_environment);
            m_pipelines.singleDraw.drawIndexedIndirect(
                cmd, m_buffers.culledIndexBuffer.buffer, 
                m_buffers.commandBuffer.buffer, 1,
                sizeof(VkDispatchIndirectCommand) * 2
            );
        }
    }

public:
    void beginDraw(
        VkCommandBuffer cmd, 
        uint32_t frameId, 
        uint64_t frameIndex,
        VkFramebuffer frameBuffer, 
        VkExtent2D extent,
        Scene& scene,
        GpuScene& gpuScene
    ) {
        m_destroyQueue.pump(frameId);
        applyCamereaParameters(frameId, extent);

        // skip first cycle of frames in flight as at that time there is nothing to read back
        if (frameId != frameIndex) {
            updateCapacities(frameId);
        }

        CullingSetup cullingSetup {
            .cameraViewProjection = m_cameraData.cullViewProjection,
            .cameraPosition = glm::vec4(m_cameraData.cullCameraPosition, 0),
            .maxDrawCommands = m_capacityTracking.indirectDrawBufferController.capacity(),
            .maxWorkloadPerInstanceCount = m_capacityTracking.perInstanceWorkloadController.capacity(),
            .maxWorkloadPerMeshletCount = m_capacityTracking.perMeshletWorkloadController.capacity(),
            .maxVertexCount = m_capacityTracking.culledVertexCapacityController.capacity(),
            .maxIndexCount = m_capacityTracking.culledIndexCapacityController.capacity()
        };
        
        {
            // clear the commandBuffer
            m_pipelines.clearCommands.pushConstant(cmd, cullingSetup);
            m_pipelines.clearCommands.bind(cmd, m_sets.commonBuffers[frameId], 0);
            m_pipelines.clearCommands.dispatch(cmd, 1);

            m_bufferState.commands = {m_buffers.commandBuffer.buffer, ResourceState {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            }};
        }
        

        m_barriers.enqueue(m_bufferState.commands, ResourceState {
            .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
            .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
        }).barrier(cmd);

        {
            // compute work over all instances:
            // frustum-cull an entire instance, and write a number of PerInstanceWorkload for surviving meshlets, 
            // tying meshelt and instance ids together. 
            //
            // a simpler algorithm is possible, but avoided by design:
            // It is possible to use the fact that we have only one model type in the scene, and cull meshlets straight-away,
            // calculating instance id by dispatchId % meshletCount.
            // However this is only the case for this toy example and doesn't stand in real world, where many models are present.
            // For this reason I've intentionally went for a more complex algorithm, that can support multiple models
    
            // don't rebind - pipelines are compatible
            m_pipelines.cullInstances.bind(cmd, m_sets.scene, 1);
            m_pipelines.cullInstances.dispatch(cmd, (scene.instances().size() + DefaultGroupSize - 1) / DefaultGroupSize);

            m_bufferState.perInstance = {m_buffers.perInstanceWorkload.buffer, ResourceState {
                .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            }};
        }

        if (m_multidraw) {
            continueMultiDraw(cmd, frameId, frameIndex, frameBuffer, extent, scene, gpuScene);
        } else {
            continueSingleDraw(cmd, frameId, frameIndex, frameBuffer, extent, scene, gpuScene);
        }
    }

    // separate call to be able to draw ui inside the draw pass
    void endDraw(VkCommandBuffer cmd) {

        if (m_multidraw) {
            m_pipelines.multiDraw.endPass(cmd);
        } else {
            m_pipelines.singleDraw.endPass(cmd);
        }

    }
};
