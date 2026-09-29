#include "../include/vk2s/BindLayout.hpp"

#include "../include/vk2s/Device.hpp"

namespace vk2s
{
    BindLayout::BindLayout(Device& device, const vk::ArrayProxy<vk::DescriptorSetLayoutBinding>& bindings)
        : mDevice(device)
        , mInfo(0)
    {
        vk::DescriptorSetLayoutCreateInfo descLayoutci;

        descLayoutci.bindingCount = static_cast<uint32_t>(bindings.size());
        descLayoutci.pBindings    = bindings.data();

        initAllocationInfo(bindings);

        mDescriptorSetLayout = mDevice.getVkDevice()->createDescriptorSetLayoutUnique(descLayoutci);
    }

    BindLayout::~BindLayout()
    {
    }

    inline void BindLayout::initAllocationInfo(const vk::ArrayProxy<const vk::DescriptorSetLayoutBinding>& bindings)
    {
        for (const auto& b : bindings)
        {
            const auto descCount = b.descriptorCount;
            switch (b.descriptorType)
            {
            case vk::DescriptorType::eAccelerationStructureKHR:
                mInfo.accelerationStructureNum += descCount;
                break;
            case vk::DescriptorType::eCombinedImageSampler:
                mInfo.combinedImageSamplerNum += descCount;
                break;
            case vk::DescriptorType::eSampledImage:
                mInfo.sampledImageNum += descCount;
                break;
            case vk::DescriptorType::eSampler:
                mInfo.samplerNum += descCount;
                break;
            case vk::DescriptorType::eStorageBuffer:
                mInfo.storageBufferNum += descCount;
                break;
            case vk::DescriptorType::eStorageImage:
                mInfo.storageImageNum += descCount;
                break;
            case vk::DescriptorType::eUniformBuffer:
                mInfo.uniformBufferNum += descCount;
                break;
            case vk::DescriptorType::eUniformBufferDynamic:
                mInfo.uniformBufferDynamicNum += descCount;
                break;
            default:
                throw vk2s::VkException("invalid (or unsupported) descriptor type!");
            }
        }
    }

    const vk::UniqueDescriptorSetLayout& BindLayout::getVkDescriptorSetLayout()
    {
        return mDescriptorSetLayout;
    }

    const DescriptorPoolAllocationInfo& BindLayout::getDescriptorPoolAllocationInfo()
    {
        return mInfo;
    }

}  // namespace vk2s
