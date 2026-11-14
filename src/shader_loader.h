#pragma once

#include <vulkan/vulkan.h>
#include <string>

struct VulkanExample;

class ShaderLoader {
    VulkanExample* m_baseLoader;
public:
    ShaderLoader(VulkanExample* b): m_baseLoader(b){}
    VkPipelineShaderStageCreateInfo loadShader(const std::string& path, VkShaderStageFlagBits stage) const;
};