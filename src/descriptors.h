#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <initializer_list>
#include "VulkanTools.h"
#include <memory>
#include <stdexcept>

inline void addDescriptorBinding(std::vector<VkDescriptorPoolSize>& sizes, VkDescriptorPoolSize currentSize) {
    bool found = false;
    for (size_t i = 0; i < sizes.size(); i++) {
        if (sizes[i].type == currentSize.type) {
            sizes[i].descriptorCount += currentSize.descriptorCount;
            found = true;
            break;
        }
    }

    if (!found) {
        sizes.push_back(currentSize);
    }
}

struct SingleBinding {
    VkDescriptorType type;
    VkShaderStageFlags stage;
    SingleBinding(VkDescriptorType t, VkShaderStageFlags s): type(t), stage(s){}
};

class DescriptorSetLayout {

    struct Impl {
        VkDevice m_device{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_layout;
        std::vector<VkDescriptorSetLayoutBinding> m_bindings{};

        Impl(): m_device(VK_NULL_HANDLE), m_layout(VK_NULL_HANDLE){}

        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        
        Impl(Impl&& other) noexcept {
            *this = std::move(other);
        }

        Impl& operator=(Impl&& other) noexcept {
            if (this != &other) {
                m_device = other.m_device;
                m_layout = other.m_layout;
                m_bindings = std::move(other.m_bindings);
                other.m_layout = VK_NULL_HANDLE;
                other.m_device = VK_NULL_HANDLE;
            }

            return *this;
        }

        ~Impl() {
            if (m_device != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
                m_device = VK_NULL_HANDLE;
                m_layout = VK_NULL_HANDLE;
            }
        }
    };

    std::shared_ptr<Impl> m_impl; 

    DescriptorSetLayout(std::shared_ptr<Impl> impl): m_impl(impl) {}
public:
    DescriptorSetLayout(): m_impl(){}
    DescriptorSetLayout(VkDevice device, std::initializer_list<SingleBinding> sizes): m_impl(std::make_shared<Impl>()) {

        uint32_t bindingOffset = 0;
        auto& bindings = m_impl->m_bindings;
        bindings.reserve(sizes.size());

        for (SingleBinding binding: sizes) {
            bindings.push_back(VkDescriptorSetLayoutBinding {
                .binding = bindingOffset++,
                .descriptorType = binding.type,
                .descriptorCount = 1,
                .stageFlags = binding.stage
            });
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        };
        
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_impl->m_layout));
        m_impl->m_device = device;
    }

    inline const VkDescriptorSetLayout* layout() const {
        return &m_impl->m_layout;
    }

    friend class DescriptorPool;
    friend class DescriptorSet;
};

class DescriptorSet {
    std::shared_ptr<DescriptorSetLayout::Impl> m_layout;
    VkDescriptorSet m_set;

    DescriptorSet(VkDescriptorSet set, std::shared_ptr<DescriptorSetLayout::Impl> layout): m_set(set), m_layout(layout){}

public:
    DescriptorSet(): m_set(VK_NULL_HANDLE), m_layout(){}

    void write(std::initializer_list<VkDescriptorBufferInfo> buffers) {
        if (!m_layout || m_layout->m_bindings.size() != buffers.size()) {
            throw std::invalid_argument("Expected one buffer per descriptor set layout binding");
        }

        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(buffers.size());
        uint32_t i = 0;
        for (auto& buffer: buffers) {
            const auto& binding = m_layout->m_bindings[i++];
            writes.push_back(VkWriteDescriptorSet {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_set,
                .dstBinding = binding.binding,
                .dstArrayElement = 0,
                .descriptorCount = binding.descriptorCount,
                .descriptorType = binding.descriptorType,
                .pBufferInfo = &buffer
            });
        }

        vkUpdateDescriptorSets(m_layout->m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    } 

    const VkDescriptorSet* set() const {
        return &m_set;
    }

    DescriptorSetLayout layout() const {
        return DescriptorSetLayout(m_layout);
    }

    friend class DescriptorPool;
};

class DescriptorPool {
    std::vector<VkDescriptorPoolSize> m_sizes;
    VkDescriptorPool m_pool;
    uint32_t m_maxSets;
    VkDevice m_device;

public:

    DescriptorPool(): m_sizes(), m_pool(VK_NULL_HANDLE), m_maxSets(0), m_device(VK_NULL_HANDLE){}
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;
    DescriptorPool(DescriptorPool&& other) = delete;
    DescriptorPool& operator=(DescriptorPool&& other) = delete;

    ~DescriptorPool() {
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        }
    }


    void registerLayout(const DescriptorSetLayout& layout, uint32_t count=1) {
        m_maxSets += count;
        for (const auto& binding : layout.m_impl->m_bindings) {
            addDescriptorBinding(m_sizes, {binding.descriptorType, binding.descriptorCount * count});
        }
    }

    void prepare(VkDevice device) {
        m_device = device;
        VkDescriptorPoolCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = m_maxSets,
            .poolSizeCount = static_cast<uint32_t>(m_sizes.size()),
            .pPoolSizes = m_sizes.data(),
        };

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &createInfo, nullptr, &m_pool));        
    }

    DescriptorSet allocate(const DescriptorSetLayout& layout) {
        assert(m_device != VK_NULL_HANDLE);
        VkDescriptorSet set;

        VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = layout.layout()
        };

        VK_CHECK_RESULT(vkAllocateDescriptorSets(m_device, &allocInfo, &set));

        return DescriptorSet(set, layout.m_impl);
    }
};
