#include "../include/vk2s/BindGroup.hpp"

#include "../include/vk2s/Device.hpp"

namespace vk2s
{
    BindGroup::BindGroup(Device& device, BindLayout& layout)
        : mDevice(device)
        , mAllocationInfo(layout.getDescriptorPoolAllocationInfo())
    {
        mInfoCaches.reserve(mAllocationInfo.sum());

        const auto [descriptorSet, poolIndex] =
            mDevice.allocateVkDescriptorSet(layout.getVkDescriptorSetLayout().get(), mAllocationInfo);

        mDescriptorSet = descriptorSet;
        mPoolIndex     = poolIndex;
    }

    BindGroup::~BindGroup()
    {
        mDevice.deallocateVkDescriptorSet(mDescriptorSet, mPoolIndex, mAllocationInfo);
    }

    void BindGroup::bind(const uint8_t binding, const vk::DescriptorType type, Buffer& buffer)
    {
        std::vector<vk::DescriptorBufferInfo> infos = { vk::DescriptorBufferInfo(buffer.getVkBuffer().get(),
                                                                                 buffer.getOffset(), VK_WHOLE_SIZE) };
        const auto& info = mInfoCaches[binding] = infos;

        mWriteQueue.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, binding, 0, type, {}, std::get<0>(info)));
    }

    void BindGroup::bind(const uint8_t binding, const vk::DescriptorType type,
                         const vk::ArrayProxy<Handle<Buffer>>& buffers)
    {
        std::vector<vk::DescriptorBufferInfo> infos;
        infos.reserve(buffers.size());

        for (const auto& buffer : buffers)
        {
            infos.emplace_back(
                vk::DescriptorBufferInfo(buffer->getVkBuffer().get(), buffer->getOffset(), VK_WHOLE_SIZE));
        }

        const auto& info = mInfoCaches[binding] = infos;

        mWriteQueue.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, binding, 0, type, {}, std::get<0>(info)));
    }

    void BindGroup::bind(const uint8_t binding, const vk::DescriptorType type, DynamicBuffer& buffer)
    {
        std::vector<vk::DescriptorBufferInfo> infos = { vk::DescriptorBufferInfo(
            buffer.getVkBuffer().get(), buffer.getOffset(), buffer.getBlockSize()) };
        const auto& info = mInfoCaches[binding] = infos;

        mWriteQueue.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, binding, 0, type, {}, std::get<0>(info)));
    }

    void BindGroup::bind(const uint8_t binding, const vk::DescriptorType type,
                         const vk::ArrayProxy<Handle<Image>>& images, const Handle<Sampler>& sampler)
    {
        //assert(!images.empty() || !"images must be greater than 0");
        if (images.empty())
        {
            return;
        }

        // HACK;
        const auto imageLayout = type == vk::DescriptorType::eStorageImage ? vk::ImageLayout::eGeneral
                                                                           : vk::ImageLayout::eShaderReadOnlyOptimal;

        std::vector<vk::DescriptorImageInfo> infos;
        infos.reserve(images.size());

        for (const auto& image : images)
        {
            const vk::ImageView vkImageView = image->getVkImageView().get();
            if (sampler)
            {
                infos.emplace_back(vk::DescriptorImageInfo(sampler->getVkSampler().get(), vkImageView, imageLayout));
            }
            else
            {
                infos.emplace_back(vk::DescriptorImageInfo({}, vkImageView, imageLayout));
            }
        }

        const auto& info = mInfoCaches[binding] = infos;

        mWriteQueue.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, binding, 0, type, std::get<1>(info)));
    }

    void BindGroup::bind(const uint8_t binding, Sampler& sampler)
    {
        const auto& info = mInfoCaches[binding] = std::vector{ vk::DescriptorImageInfo(sampler.getVkSampler().get()) };

        mWriteQueue.emplace_back(
            vk::WriteDescriptorSet(mDescriptorSet, binding, 0, vk::DescriptorType::eSampler, std::get<1>(info)));
    }

    void BindGroup::bind(const uint8_t binding, AccelerationStructure& as)
    {
        const auto& info = mInfoCaches[binding] =
            vk::WriteDescriptorSetAccelerationStructureKHR(as.getVkAccelerationStructure().get());

        mWriteQueue.emplace_back(
            vk::WriteDescriptorSet(mDescriptorSet, binding, 0, vk::DescriptorType::eAccelerationStructureKHR, {})
                .setDescriptorCount(1)
                .setPNext(&info));
    }

    const vk::DescriptorSet& BindGroup::getVkDescriptorSet()
    {
        if (!mWriteQueue.empty())
        {
            const auto& vkDevice = mDevice.getVkDevice();
            vkDevice->updateDescriptorSets(mWriteQueue, {});

            mWriteQueue.clear();
            //mInfoCaches.clear();
        }

        return mDescriptorSet;
    }
}  // namespace vk2s
