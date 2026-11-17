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
#include <numeric>
#include "capacity_controller.h"
#include "delayed_destroy_queue.h"
#include "barriers.h"
#include "camera_view_control.h"

constexpr uint32_t DefaultGroupSize = 64;

constexpr uint32_t MeshletsProcessedPerGroup = 256;
constexpr uint32_t MinIndirectCommands = 16;
constexpr uint32_t MinMeshletWorkloads = 8;

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
};

struct Commands {
    VkDispatchIndirectCommand dispatchPerInstanceCommand;
    VkDrawIndexedIndirectCommand singleDrawIndexedCommand;
    uint32_t drawCommandCounter;
};

class DrawSystem {
    
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
        vks::Buffer meshletInstances;
        std::vector<vks::Buffer> readbackBuffers;
    } m_buffers;

    struct {
        BufferStateHandle indirectDraw;
        BufferStateHandle commands;
        BufferStateHandle perInstance;
        BufferStateHandle meshletInstance;
    } m_bufferState;

    struct {

        CapacityController indirectDrawBufferController;
        CapacityController indirectDrawCountController;

        CapacityController perInstanceWorkloadController;

        CapacityController instancesBufferController;

    } m_capacityTracking;

    struct {
        ComputePass clearCommands;
        ComputePass cullInstances;
        ComputePass cullMeshletsMultidraw;

        ComputePass cullMeshletsSingledraw;

        GraphicsPass multiDraw;
        GraphicsPass singleDraw;
    } m_pipelines;

    struct {
        float fov = 45.0f;
        
        glm::mat4 viewProjection{1.0f};
        glm::mat4 cullViewProjection{1.0f};
        
        glm::vec3 cameraPosition{0.5f, 0.5f, 0.5f};
        glm::vec3 cullCameraPosition{0.5f, 0.5f, 0.5f};
        bool freezeFrustumCull = false;
    } m_cameraData;

    EnvironmentData m_environment  {
        .lightDirIntensity = glm::vec4(glm::vec3(0, 1, 0.5), 1.),
        .lightColor = glm::vec4(1., 1., 1., 1.),
        .envColorIntensity = glm::vec4(0.53, 0.80, 0.92, 0.1)
    };
    
    Commands m_readbackCommands;

    uint32_t m_maxGroupCount;
    uint32_t m_maxIndirectCommands;

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
        m_bufferState.perInstance = BufferStateHandle{m_buffers.perInstanceWorkload.buffer, {}};
    }

    void allocIndirectDrawCommands(uint32_t count) {
        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.indirectDrawCommandsBuffer, 
            sizeof(VkDrawIndexedIndirectCommand) * count
        ));
        m_bufferState.indirectDraw = BufferStateHandle{m_buffers.indirectDrawCommandsBuffer.buffer, {}};
    }

    void allocMeshletInstances(uint32_t count) {
        VK_CHECK_RESULT(m_vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &m_buffers.meshletInstances, 
            sizeof(PerMeshletWorkload) * count
        ));
        m_bufferState.meshletInstance = BufferStateHandle{m_buffers.meshletInstances.buffer, {}};
    }

    void allocateBuffers(Scene& scene) {
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
        m_bufferState.commands = BufferStateHandle(m_buffers.commandBuffer.buffer, {});

        allocPerInstanceWorkload(MinMeshletWorkloads);

        if (m_multidraw) {
            allocIndirectDrawCommands(MinIndirectCommands);

        } else {
            allocMeshletInstances(MinMeshletWorkloads);
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
            m_buffers.meshletInstances.descriptor,
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
            gpuScene.indexBuffer.descriptor,
            gpuScene.sceneStats.descriptor
        });
    }

    void preparePipelines(const ShaderLoader& shaderLoader, VkFormat dsFormat, VkFormat colorFormat) 
    {
        m_pipelines.clearCommands = ComputePass(m_device,
            shaderLoader.loadShader("compute/clear_indirect_dispatch.comp",  VK_SHADER_STAGE_COMPUTE_BIT),
            {
                m_layouts.commonBuffers,
                m_layouts.scene
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
        constexpr uint32_t defaultShrinkDelay = 520;
        constexpr float defaultExcessFactor = 1.5;
        // ammortizing capacities of both command buffers and number of multi-draw commands.

        m_capacityTracking.perInstanceWorkloadController = CapacityController {
            MinMeshletWorkloads, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor, m_maxGroupCount
        };

        if (m_multidraw) {

            m_capacityTracking.indirectDrawBufferController = CapacityController {
                MinIndirectCommands, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor, m_maxIndirectCommands
            };

            // it costs nothing to change the number of commands, so make it a lot more responsive
            m_capacityTracking.indirectDrawCountController = CapacityController {
                MinIndirectCommands, 0.85, 0.75, 240, 1.25, m_maxIndirectCommands
            };
        } else {
            
            m_capacityTracking.instancesBufferController = CapacityController {
                MinMeshletWorkloads, 0.85, 0.35, defaultShrinkDelay, defaultExcessFactor
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
        m_buffers.meshletInstances.destroy();

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
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
        });

        if (!m_multidraw) {
            m_layouts.singleDraw = DescriptorSetLayout(m_device,{
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
        VkFormat colorFormat,
        VkPhysicalDeviceLimits limits
    ) {
        m_vulkanDevice = vulkanDevice;
        m_maxGroupCount = limits.maxComputeWorkGroupCount[0];
        m_maxIndirectCommands = limits.maxDrawIndirectCount;

        allocateBuffers(scene);
        allocateDescriptors(descriptors, gpuScene);
        preparePipelines(shaderLoader, dsFormat, colorFormat);
        setUpCapacityTracking();
    }

    void updateUI(vks::UIOverlay& overlay) {

        if (overlay.header("draw stats")) {
            if (m_multidraw) {
                overlay.text("multi-draw enabled");
                overlay.text("per-instance workload allocated: %zu", m_capacityTracking.perInstanceWorkloadController.capacity());
                overlay.text("per-instance workload executed: %zu", m_readbackCommands.dispatchPerInstanceCommand.x);
                overlay.text("draw commands: %zu", m_readbackCommands.drawCommandCounter);
                overlay.text("max draw commands: %zu", m_capacityTracking.indirectDrawCountController.capacity());
                overlay.text("draw commands allocated: %zu", m_capacityTracking.indirectDrawBufferController.capacity());
            } else {
                overlay.text("multi-draw disabled");
                overlay.text("per-instance workload allocated: %zu", m_capacityTracking.perInstanceWorkloadController.capacity());
                overlay.text("per-instance workload executed: %zu", m_readbackCommands.dispatchPerInstanceCommand.x);
                overlay.text("meshlet instances allocated: %zu", m_capacityTracking.instancesBufferController.capacity());
                overlay.text("meshlet instances rendered: %zu", m_readbackCommands.singleDrawIndexedCommand.instanceCount);
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

    void applyCamereaParameters(View view, VkExtent2D extent) 
    {
        m_cameraData.cameraPosition = view.position;
        glm::mat4 projection = glm::perspective(glm::radians(m_cameraData.fov), extent.width / (float) extent.height, 0.1f, 25.0f);
        projection[1][1] *= -1;
        m_cameraData.viewProjection = projection * view.view;

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
        m_capacityTracking.instancesBufferController.update(commands.drawCommandCounter);
        
        if (m_buffers.meshletInstances.size != m_capacityTracking.instancesBufferController.capacity() * sizeof(PerMeshletWorkload)) 
        {
            m_destroyQueue.enqueue(m_buffers.meshletInstances);

            allocMeshletInstances(m_capacityTracking.instancesBufferController.capacity());

            m_singledrawBuffersDirty |= (1 << m_framesInFlight) - 1;
        }

        if (m_singledrawBuffersDirty & (1 << frameId)) {
            writeSingledrawDescriptors(frameId);
            m_singledrawBuffersDirty &= ~(1 << frameId);
        }
    }

    void updateCapacities(uint32_t frameId) 
    {
        memcpy(&m_readbackCommands, m_buffers.readbackBuffers[frameId].mapped, sizeof(Commands));

        m_capacityTracking.perInstanceWorkloadController.update(m_readbackCommands.dispatchPerInstanceCommand.x);

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
            updateCapacitiesMultidraw(m_readbackCommands, frameId);
        } else {
            updateCapacitiesSingledraw(m_readbackCommands, frameId);
        }
    }

private:
    void readBackCommands(VkCommandBuffer cmd, uint32_t frameId) {

        m_barriers
            .enqueue(m_bufferState.commands, {
                .stage = VK_PIPELINE_STAGE_2_COPY_BIT,
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

        BufferStateHandle readback{
            m_buffers.readbackBuffers[frameId].buffer,
            {
                .stage = VK_PIPELINE_STAGE_2_COPY_BIT,
                .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            },
        };

        m_barriers.enqueue(readback, {
            .stage = VK_PIPELINE_STAGE_2_HOST_BIT,
            .access = VK_ACCESS_2_HOST_READ_BIT,
        }).barrier(cmd);
    }

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
            m_barriers
                .enqueue(m_bufferState.indirectDraw, {
                    .stage = VK_PIPELINE_STAGE_2_CLEAR_BIT,
                    .access = VK_ACCESS_2_TRANSFER_WRITE_BIT
                })
                .barrier(cmd);

            // clear indirect command buffer. Required, since drawInstancedIndirect is used, not drawInstancedIndirectCount
            vkCmdFillBuffer(
                cmd, m_buffers.indirectDrawCommandsBuffer.buffer, 0, VK_WHOLE_SIZE, 0
            );
        }

        {

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

        readBackCommands(cmd, frameId);

        {
            m_barriers
                .enqueue(m_bufferState.indirectDraw, {
                    .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                })
                .barrier(cmd);


            uint32_t drawCount = std::min(
                m_capacityTracking.indirectDrawCountController.capacity(),
                m_capacityTracking.indirectDrawBufferController.capacity() 
            );

            // multi-draw inderect. Each command contains one or more meshlets, up to MeshletsProcessedPerGroup in the best case.
            // empty commands are possible, but their number is minimized by using CapacityController's
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

        {
            m_barriers
                .enqueue(m_bufferState.commands, {
                    .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                })
                .enqueue(m_bufferState.perInstance, {
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                })
                .enqueue(m_bufferState.meshletInstance, {
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                })
                .barrier(cmd);

            // compute work analogus to the multi-draw branch. Culls meshlets and
            // produces intermediate buffer with meshletId and instanceId correspondance.

            m_pipelines.cullMeshletsSingledraw.bind(cmd, m_sets.singleDraw[frameId], 2);
            m_pipelines.cullMeshletsSingledraw.dispatchIndirect(cmd, m_buffers.commandBuffer.buffer);
        }

        readBackCommands(cmd, frameId);

        {
            m_barriers
                .enqueue(m_bufferState.meshletInstance, {
                    .stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                })
                .barrier(cmd);

            // Indirect draw with a single command. Uses degenerate index buffer looking like [0, 1, 2, ..., n] where n
            // is the maximum possible index count in a single meshlet. This draw is instanced for the number of visible meshlets.
            // Vertex shader reads meshlet and instance ids and produces correct vertex based on gl_VertexIndex.
            // Indices out of range result in invisible triangles. 
    
            m_pipelines.singleDraw.beginPass(cmd, frameBuffer, extent, glm::vec3(m_environment.envColorIntensity));
            m_pipelines.singleDraw.bind(cmd, m_sets.scene, 0);
            m_pipelines.singleDraw.bind(cmd, m_sets.singleDraw[frameId], 1);
            m_pipelines.singleDraw.pushConstant(cmd, m_environment);
            m_pipelines.singleDraw.drawIndexedIndirect(
                cmd, gpuScene.meshletIndexBuffer.buffer,
                m_buffers.commandBuffer.buffer, 1,
                offsetof(Commands, singleDrawIndexedCommand)
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
        GpuScene& gpuScene,
        View view
    ) {
        m_destroyQueue.pump(frameId);
        applyCamereaParameters(view, extent);

        // skip first cycle of frames in flight as at that time there is nothing to read back
        if (frameId != frameIndex) {
            updateCapacities(frameId);
        }

        CullingSetup cullingSetup {
            .cameraViewProjection = m_cameraData.cullViewProjection,
            .cameraPosition = glm::vec4(m_cameraData.cullCameraPosition, 0),
            .maxDrawCommands = m_capacityTracking.indirectDrawBufferController.capacity(),
            .maxWorkloadPerInstanceCount = m_capacityTracking.perInstanceWorkloadController.capacity(),
            .maxWorkloadPerMeshletCount = m_capacityTracking.instancesBufferController.capacity(),
        };
        
        {
            m_barriers
                .enqueue(m_bufferState.commands, {
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                    .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                })
                .barrier(cmd);

            // clear commands and counters
            m_pipelines.clearCommands.pushConstant(cmd, cullingSetup);
            m_pipelines.clearCommands.bind(cmd, m_sets.commonBuffers[frameId], 0);
            m_pipelines.clearCommands.bind(cmd, m_sets.scene, 1);
            m_pipelines.clearCommands.dispatch(cmd, 1);
        }
        

        {
            // compute work over all instances:
            // frustum-cull an entire instance, and write a number of PerInstanceWorkload for surviving meshlets, 
            // tying meshelt and instance ids together. 
            //
            // a simpler algorithm is possible, but avoided by design:
            // It is possible to use the fact that we have only one model type in the scene, and cull meshlets straight-away,
            // calculating instance id by dispatchId % meshletCount.
            // However this is only the case for this toy example and doesn't stand in real world, where many models are possible.
            // For this reason a more complex algorithm was selected, that can support multiple models.
    
            m_barriers
                .enqueue(m_bufferState.commands, ResourceState {
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                    .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                })
                .enqueue(m_bufferState.perInstance, ResourceState {
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                    .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                })
                .barrier(cmd);

            // don't rebind - pipelines are compatible
            m_pipelines.cullInstances.dispatch(cmd, (scene.instances().size() + DefaultGroupSize - 1) / DefaultGroupSize);
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
