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

constexpr uint32_t DefaultGroupSize = 64;

constexpr uint32_t MeshletsProcessedPerGroup = 256;
constexpr uint32_t MinIndirectCommands = 16;
constexpr uint32_t MinMeshletWorkloads = 8;

struct MeshletsWorkload {
    uint32_t instanceId;
    uint32_t startMeshlet;
    uint32_t meshletsCount;
    uint32_t _padding;
};

struct EnvironmentData {
    glm::mat4 viewProjection;

    glm::vec4 lightDirIntensity;
    glm::vec4 lightColor;
    glm::vec4 envColorIntensity;
    
    // align to fit into cache lines nicely
    glm::vec4 _padding;
};

struct CullingSetup {
    glm::mat4 viewProjection;
    uint32_t maxDrawCommands;
    uint32_t maxWorkloadCount;
};

struct Commands {
    VkDispatchIndirectCommand dispatchCommand;
    uint32_t drawCommandsCounter;
};

class DrawSystem {
public:
    enum class CameraKey { Forward, Backward, Left, Right, Up, Down, Count };

private:
    static inline const glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};
    
    struct {
        DescriptorSetLayout environment;
        DescriptorSetLayout indirectBuffers;
        DescriptorSetLayout scene;
    } m_layouts;

    struct {
        std::vector<DescriptorSet> environment;
        DescriptorSet indirectBuffers;
        DescriptorSet scene;
    } m_sets;

    struct {
        std::vector<vks::Buffer> environmentBuffers;
        vks::Buffer indirectDrawCommandsBuffer;
        vks::Buffer commandBuffer;
        vks::Buffer meshletsCullWorkload;
        std::vector<vks::Buffer> readbackBuffers;
    } m_buffers;

    struct {

        CapacityController indirectDrawBufferController;
        CapacityController indirectDrawCountController;
        CapacityController meshletsWorkloadController;

    } m_capacityTracking;

    struct {
        ComputePass clearCommands;
        ComputePass cullInstances;
        ComputePass cullMeshlets;

        GraphicsPass draw;
    } m_pipelines;

    struct {
        glm::vec3 cameraPosition{0.5f, 0.5f, 0.5f};
        glm::vec3 cameraForward = glm::normalize(-cameraPosition);
        float fov = 45.0f;

        glm::mat4 viewProjection{1.0f};

        glm::mat4 cullViewProjection{1.0f};
        bool freezeFrustumCull = false;
    } m_cameraData;

    std::array<bool, static_cast<size_t>(CameraKey::Count)> m_cameraKeys{};

    uint32_t m_framesInFlight;
    VkDevice m_device;
    vks::VulkanDevice* m_vulkanDevice;
    DelayedDestroyQueue m_destroyQueue;

    void barrier(VkCommandBuffer cmd, std::initializer_list<VkBufferMemoryBarrier2> barriers) {
        VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
            .pBufferMemoryBarriers = barriers.begin()
        };

        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    void allocateBuffers() {
        m_buffers.environmentBuffers.resize(m_framesInFlight);
        m_buffers.readbackBuffers.resize(m_framesInFlight);

        for (uint32_t i = 0; i < m_framesInFlight; i++) {
            VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &m_buffers.environmentBuffers[i], sizeof(EnvironmentData)
            ));
            VK_CHECK_RESULT(m_buffers.environmentBuffers[i].map());

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

        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.indirectDrawCommandsBuffer, 
            sizeof(VkDrawIndexedIndirectCommand) * MinIndirectCommands
        ));

        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.meshletsCullWorkload, 
            sizeof(MeshletsWorkload) * MinMeshletWorkloads
        ));
    }

    void allocateDescriptors(DescriptorPool& descriptors, GpuScene& gpuScene) 
    {
        for (uint32_t i = 0; i < m_framesInFlight; i++) {
            m_sets.environment.push_back(descriptors.allocate(m_layouts.environment));
            m_sets.environment[i].write({
                m_buffers.environmentBuffers[i].descriptor
            });
        }

        m_sets.environment.reserve(m_framesInFlight);

        m_sets.indirectBuffers = descriptors.allocate(m_layouts.indirectBuffers);
        m_sets.indirectBuffers.write({
            m_buffers.indirectDrawCommandsBuffer.descriptor,
            m_buffers.meshletsCullWorkload.descriptor,
            m_buffers.commandBuffer.descriptor
        });

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
                m_layouts.indirectBuffers,
            }
        );

        m_pipelines.cullInstances = ComputePass(m_device, 
            shaderLoader.loadShader("compute/cull_instances.comp", VK_SHADER_STAGE_COMPUTE_BIT),
            {
                m_layouts.indirectBuffers,
                m_layouts.scene 
            },
            sizeof(CullingSetup)
        );

        m_pipelines.cullMeshlets = ComputePass(m_device, 
            shaderLoader.loadShader("compute/cull_meshlets.comp", VK_SHADER_STAGE_COMPUTE_BIT),
            {
                m_layouts.indirectBuffers,
                m_layouts.scene
            },
            sizeof(CullingSetup)
        );

        m_pipelines.draw = GraphicsPass(m_device,
            {
                shaderLoader.loadShader("multi_draw.vert", VK_SHADER_STAGE_VERTEX_BIT),
                shaderLoader.loadShader("draw.frag", VK_SHADER_STAGE_FRAGMENT_BIT),
            }, dsFormat, colorFormat,
            {
                m_layouts.environment,
                m_layouts.scene
            }
        );
    }

    void setUpCapacityTracking() 
    {
        // ammortizing capacities of both command buffers and number of multi-draw commands.
        m_capacityTracking.indirectDrawBufferController = CapacityController {
            MinIndirectCommands, 0.85, 0.35, 120, 1.5
        };

        m_capacityTracking.meshletsWorkloadController = CapacityController {
            MinMeshletWorkloads, 0.85, 0.35, 120, 1.5
        };

        // it costs nothing to change the number of commands, so make it a lot more responsive
        m_capacityTracking.indirectDrawCountController = CapacityController {
            MinIndirectCommands, 0.85, 0.75, 10, 1.1
        };
    }
 
public:

    void destroy() {
        for (auto& env : m_buffers.environmentBuffers) {
            env.unmap();
            env.destroy();
        }

        for (auto& readback : m_buffers.readbackBuffers) {
            readback.unmap();
            readback.destroy();
        }

        m_buffers.commandBuffer.destroy();
        m_buffers.indirectDrawCommandsBuffer.destroy();
        m_buffers.meshletsCullWorkload.destroy();

        m_destroyQueue.flush();
    }

    void registerLayouts(VkDevice device, uint32_t framesInFlight, DescriptorPool& pool)
    {
        m_framesInFlight = framesInFlight;    
        m_device = device;
        m_layouts.environment = DescriptorSetLayout(device, {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT}
        });

        m_layouts.indirectBuffers = DescriptorSetLayout(device, {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
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

        pool.registerLayout(m_layouts.scene);
        pool.registerLayout(m_layouts.environment, framesInFlight);
        pool.registerLayout(m_layouts.indirectBuffers);
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

        overlay.text("max draw commands: %zu", m_capacityTracking.indirectDrawCountController.capacity());
        overlay.text("max draw commands allocated: %zu", m_capacityTracking.indirectDrawBufferController.capacity());
        overlay.text("max meshlet workload allocated: %zu", m_capacityTracking.meshletsWorkloadController.capacity());

        if (overlay.header("camera data")) {
            overlay.checkBox("freeze frustum cull", &m_cameraData.freezeFrustumCull);
            overlay.sliderFloat("field of view", &m_cameraData.fov, 10., 120.);
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

    void applyCamereaParameters(uint32_t frameId, VkExtent2D extent) {
        const glm::vec3 position = m_cameraData.cameraPosition;
        const glm::mat4 view = glm::lookAt(position, position + m_cameraData.cameraForward, WorldUp);
        glm::mat4 projection = glm::perspective(glm::radians(m_cameraData.fov), extent.width / (float) extent.height, 0.1f, 10.0f);
        projection[1][1] *= -1;
        m_cameraData.viewProjection = projection * view;

        if (!m_cameraData.freezeFrustumCull) {
            m_cameraData.cullViewProjection = m_cameraData.viewProjection;
        }

        EnvironmentData env {
            .viewProjection = m_cameraData.viewProjection,
            .lightDirIntensity = glm::vec4(glm::normalize(glm::vec3(0, -1, -1)), 2.),
            .lightColor = glm::vec4(1., 1., 1., 1.),
            .envColorIntensity = glm::vec4(0.53, 0.80, 0.92, 1.)
        };
        memcpy(m_buffers.environmentBuffers[frameId].mapped, &env, sizeof(env));
    }

    void updateCapacities(uint32_t frameId) 
    {
        Commands commands;
        memcpy(&commands, m_buffers.readbackBuffers[frameId].mapped, sizeof(Commands));

        m_capacityTracking.indirectDrawBufferController.update(commands.drawCommandsCounter);
        m_capacityTracking.indirectDrawCountController.update(commands.drawCommandsCounter);
        m_capacityTracking.meshletsWorkloadController.update(commands.dispatchCommand.x);

        // nothing fancy - no memory pulling or aleasing, just plain destroy and recreate.
        // it is not the most optimal way, but good enough for the task scope
        if (m_buffers.indirectDrawCommandsBuffer.size != m_capacityTracking.indirectDrawBufferController.capacity() * 
            sizeof(VkDrawIndexedIndirectCommand)) 
        {
            m_destroyQueue.enqueue(m_buffers.indirectDrawCommandsBuffer);

            VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &m_buffers.indirectDrawCommandsBuffer, 
                sizeof(VkDrawIndexedIndirectCommand) * m_capacityTracking.indirectDrawBufferController.capacity()
            ));
        }

        if (m_buffers.meshletsCullWorkload.size != m_capacityTracking.meshletsWorkloadController.capacity() * 
            sizeof(MeshletsWorkload)) 
        {
            m_destroyQueue.enqueue(m_buffers.meshletsCullWorkload);

            VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &m_buffers.meshletsCullWorkload, 
                sizeof(MeshletsWorkload) * m_capacityTracking.meshletsWorkloadController.capacity()
            ));
        }
    }

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
        
        {
            // clear the commandBuffer
            m_pipelines.cullInstances.bind(cmd, m_sets.indirectBuffers, 0);
            m_pipelines.clearCommands.dispatch(cmd, 1);
        }

        {
            // clear draw commands buffer
            vkCmdFillBuffer(
                cmd, m_buffers.indirectDrawCommandsBuffer.buffer, 0, VK_WHOLE_SIZE, 0
            );
        }

        barrier(cmd, {
            VkBufferMemoryBarrier2 { 
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = m_buffers.commandBuffer.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            }
        });

        CullingSetup cullingSetup {
            .viewProjection = m_cameraData.cullViewProjection,
            .maxDrawCommands = m_capacityTracking.indirectDrawBufferController.capacity(),
            .maxWorkloadCount = m_capacityTracking.meshletsWorkloadController.capacity()
        };

        {
            // compute work over all instances:
            // frustum-cull an entire instance, and write a number of MeshletsWorkload for surviving meshlets, 
            // tying meshelt and instance ids together. 
            //
            // a simpler algorithm is possible, but avoided by design:
            // It is possible to use the fact that we have only one model type in the scene, and cull meshlets straight-away,
            // calculating instance id by dispatchId % meshletCount.
            // However this is only the case for this toy example and doesn't stand in real world, where many models are present.
            // For this reason I've intentionally went for a more complex algorithm, that can support multiple models
    
            // don't bind set 0 as it is already bound
            m_pipelines.cullInstances.bind(cmd, m_sets.scene, 1);
            m_pipelines.cullInstances.pushConstant(cmd, cullingSetup);
            m_pipelines.cullInstances.dispatch(cmd, (scene.instances().size() + DefaultGroupSize - 1) / DefaultGroupSize);
        }
        
        barrier(cmd, {
            VkBufferMemoryBarrier2 { 
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = m_buffers.meshletsCullWorkload.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            },
            VkBufferMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = m_buffers.commandBuffer.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            },
            VkBufferMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = m_buffers.indirectDrawCommandsBuffer.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            },
        });

        {
            // compute work with group count equal to the number of items in meshletsCullWorkload,
            // with group size equal to MeshletsProcessedPerGroup. 
            // MeshletsWorkload.meshletsCount can never surpass MeshletsProcessedPerGroup, so every meshlet is processed.
            // MeshletsProcessedPerGroup must be selected carefully based on expected number of meshlets in a typical model, 
            // so the work is balanced.
            //
            // m_pipelines.cullMeshlets culls meshlets of visible instances and generates indirect draw commands. 
            // Thanks to particular vertex and index buffer ordering, it is possible to combine adjesent meshlets into a single draw command.
            // The shader uses subgroup operations (wave intrinsics) to do so efficiently. 
    
            // uses the same descriptor sets, no need to rebind
            m_pipelines.cullMeshlets.dispatchIndirect(cmd, m_buffers.commandBuffer.buffer);
        }
    
        barrier(cmd, {
            VkBufferMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = m_buffers.indirectDrawCommandsBuffer.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            },
            VkBufferMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = m_buffers.commandBuffer.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            }
        });

        // prepare readback
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
            // multi-draw inderect
            m_pipelines.draw.beginPass(cmd, frameBuffer, extent);
            m_pipelines.draw.bind(cmd, m_sets.environment[frameId], 0);
            m_pipelines.draw.bind(cmd, m_sets.scene, 1);
            m_pipelines.draw.drawIndexedIndirect(
                cmd, gpuScene.indexBuffer.buffer, 
                m_buffers.indirectDrawCommandsBuffer.buffer,
                m_capacityTracking.indirectDrawCountController.capacity()
            );
        }
    }

    // separate call to be able to draw ui inside the draw pass
    void endDraw(VkCommandBuffer cmd) {
        m_pipelines.draw.endPass(cmd);
    }
};
