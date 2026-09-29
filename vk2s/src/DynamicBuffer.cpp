#include "../include/vk2s/DynamicBuffer.hpp"

#include "../include/vk2s/Device.hpp"

namespace vk2s
{

    DynamicBuffer::DynamicBuffer(Device& device, const vk::BufferCreateInfo& bi, vk::MemoryPropertyFlags pbs,
                                 const uint32_t blockNum)
        : mDevice(device)
    {
        assert(bi.size % blockNum == 0 || !"invalid size! (size must be divisible by blockNum)");

        const auto& vkDevice = mDevice.getVkDevice();

        vk::BufferCreateInfo nbi = bi;

        const auto physDevProps    = mDevice.getVkPhysicalDevice().getProperties();
        const auto bufferAlignment = physDevProps.limits.minUniformBufferOffsetAlignment;
        const auto singleSize      = bi.size / blockNum;
        mBlockSize                 = mDevice.align(singleSize, static_cast<uint32_t>(bufferAlignment));
        nbi.size                   = mBlockSize * blockNum;

        mBuffer = vkDevice->createBufferUnique(nbi);

        // for ray tracing
        vk::MemoryAllocateFlagsInfo allocateFlagsInfo(vk::MemoryAllocateFlagBits::eDeviceAddress);

        vk::MemoryRequirements reqs = vkDevice->getBufferMemoryRequirements(mBuffer.get());
        vk::MemoryAllocateInfo ai(reqs.size, mDevice.getVkMemoryTypeIndex(reqs.memoryTypeBits, pbs));
        if (bi.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)
        {
            ai.pNext = &allocateFlagsInfo;
        }

        mMemory = vkDevice->allocateMemoryUnique(ai);
        mSize   = nbi.size;
        mOffset = 0;

        vkDevice->bindBufferMemory(mBuffer.get(), mMemory.get(), mOffset);
    }

    DynamicBuffer::~DynamicBuffer()
    {
    }

    void DynamicBuffer::write(const void* pSrc, const size_t size, const size_t offset)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        void* p = vkDevice->mapMemory(mMemory.get(), offset, size);
        assert(p || !"failed to map memory!");
        memcpy(p, pSrc, size);
        vkDevice->unmapMemory(mMemory.get());
    }

    const vk::UniqueBuffer& DynamicBuffer::getVkBuffer()
    {
        return mBuffer;
    }

    const vk::DeviceSize DynamicBuffer::getSize() const
    {
        return mSize;
    }

    const vk::DeviceSize DynamicBuffer::getOffset() const
    {
        return mOffset;
    }

    const vk::DeviceSize DynamicBuffer::getBlockSize() const
    {
        return mBlockSize;
    }

    const vk::DeviceAddress DynamicBuffer::getVkDeviceAddress() const
    {
        return mDevice.getVkDevice()->getBufferAddress(vk::BufferDeviceAddressInfo(mBuffer.get()));
    }

}  // namespace vk2s
