#pragma once
#include "scene.h"
#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include "descriptors.h"
#include "compute_pass.h"
#include "graphics_pass.h"
#include "shader_loader.h"
#include "VulkanUIOverlay.h"

const uint32_t DefaultGroupSize = 64;

const uint32_t MeshletsProcessedPerGroup = 256;

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

struct Commands {
    VkDispatchIndirectCommand dispatchCommand;
    uint32_t drawCommandsCounter;
};

class DrawSystem {
    
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
    } m_buffers;

    struct {
        ComputePass clearCommands;
        ComputePass cullInstances;
        ComputePass cullMeshlets;

        GraphicsPass draw;
    } m_pipelines;

    struct {
        glm::vec3 cameraPosition;
        glm::vec3 cameraForward;
        float fov;

        glm::mat4 viewProjection;

        glm::mat4 frozenViewProjection;
        bool freezeFrustumCull;
    } m_cameraData;

    uint32_t m_framesInFlight;
    VkDevice m_device;
    vks::VulkanDevice* m_vulkanDevice;

    void barrier(VkCommandBuffer cmd, std::initializer_list<VkBufferMemoryBarrier2> barriers) {
        VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
            .pBufferMemoryBarriers = barriers.begin()
        };

        vkCmdPipelineBarrier2(cmd, &dependency);
    }
 
public:

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
        m_sets.environment.reserve(m_framesInFlight);
        m_buffers.environmentBuffers.resize(m_framesInFlight);
        m_vulkanDevice = vulkanDevice;

        for (uint32_t i = 0; i < m_framesInFlight; i++) {
            VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &m_buffers.environmentBuffers[i], sizeof(EnvironmentData)
            ));
            VK_CHECK_RESULT(m_buffers.environmentBuffers[i].map());
            m_sets.environment.push_back(descriptors.allocate(m_layouts.environment));
            m_sets.environment[i].write({
                m_buffers.environmentBuffers[i].descriptor
            });
        }

        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.commandBuffer, 
            sizeof(Commands)
        ));

        uint32_t totalMeshlets = scene.meshlets().size() * scene.instances().size();

        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.indirectDrawCommandsBuffer, 
            sizeof(VkDrawIndexedIndirectCommand) * totalMeshlets
        ));

        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.meshletsCullWorkload, 
            sizeof(MeshletsWorkload) * ((scene.meshlets().size() + MeshletsProcessedPerGroup - 1) / MeshletsProcessedPerGroup) * scene.instances().size()
        ));

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
            sizeof(glm::mat4)
        );

        m_pipelines.cullMeshlets = ComputePass(m_device, 
            shaderLoader.loadShader("compute/cull_meshlets.comp", VK_SHADER_STAGE_COMPUTE_BIT),
            {
                m_layouts.indirectBuffers,
                m_layouts.scene
            },
            sizeof(glm::mat4)
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

    void updateUI(vks::UIOverlay& overlay) {

    }

    void beginDraw(
        VkCommandBuffer cmd, 
        uint32_t frameId, 
        VkFramebuffer frameBuffer, 
        VkExtent2D extent,
        Scene& scene,
        GpuScene& gpuScene
    ) {
        glm::mat4 view = glm::lookAt(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), extent.width / (float) extent.height, 0.1f, 10.0f);
        projection[1][1] *= -1;
        glm::mat4 viewProjection = projection * view;

        EnvironmentData env {
            .viewProjection = projection * view,
            .lightDirIntensity = glm::vec4(glm::normalize(glm::vec3(0, -1, -1)), 2.),
            .lightColor = glm::vec4(1., 1., 1., 1.),
            .envColorIntensity = glm::vec4(0.53, 0.80, 0.92, 1.)
        };
        memcpy(m_buffers.environmentBuffers[frameId].mapped, &env, sizeof(env));
        
        // clear the commandBuffer
        m_pipelines.cullInstances.bind(cmd, m_sets.indirectBuffers, 0);
        m_pipelines.clearCommands.dispatch(cmd, 1);

        // clear draw commands buffer
        vkCmdFillBuffer(
            cmd, m_buffers.indirectDrawCommandsBuffer.buffer, 0, VK_WHOLE_SIZE, 0
        );

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
        m_pipelines.cullInstances.pushConstant(cmd, viewProjection);
        m_pipelines.cullInstances.dispatch(cmd, (scene.instances().size() + DefaultGroupSize - 1) / DefaultGroupSize);
        
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
                .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
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
            }
        });

        m_pipelines.draw.beginPass(cmd, frameBuffer, extent);
        m_pipelines.draw.bind(cmd, m_sets.environment[frameId], 0);
        m_pipelines.draw.bind(cmd, m_sets.scene, 1);
        m_pipelines.draw.drawIndexedIndirect(
            cmd, gpuScene.indexBuffer.buffer, 
            m_buffers.indirectDrawCommandsBuffer.buffer,
            scene.meshlets().size() * scene.instances().size()
        );
    }

    // separate call to be able to draw ui in between
    void endDraw(VkCommandBuffer cmd) {
        m_pipelines.draw.endPass(cmd);
    }
};