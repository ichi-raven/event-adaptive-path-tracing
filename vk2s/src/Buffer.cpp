#include "../include/vk2s/Buffer.hpp"
#include "../include/vk2s/Device.hpp"

#include <cstring>
#include <stdexcept>

namespace vk2s
{
    Buffer::Buffer(Device& device, const vk::BufferCreateInfo& bi, vk::MemoryPropertyFlags pbs)
        : mDevice(device)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        mBuffer = vkDevice->createBufferUnique(bi);

        // for ray tracing
        vk::MemoryAllocateFlagsInfo allocateFlagsInfo(vk::MemoryAllocateFlagBits::eDeviceAddress);

        vk::MemoryRequirements reqs = vkDevice->getBufferMemoryRequirements(mBuffer.get());
        vk::MemoryAllocateInfo ai(reqs.size, mDevice.getVkMemoryTypeIndex(reqs.memoryTypeBits, pbs));
        if (bi.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)
        {
            ai.pNext = &allocateFlagsInfo;
        }

        mMemory = vkDevice->allocateMemoryUnique(ai);
        mSize   = bi.size;
        mOffset = 0;

        vkDevice->bindBufferMemory(mBuffer.get(), mMemory.get(), mOffset);
    }

    Buffer::~Buffer()
    {
    }

    void Buffer::upload(const void* pSrc, const size_t size)
    {
        if (size == 0)
        {
            return;
        }

        if (pSrc == nullptr || size > mSize)
        {
            throw std::invalid_argument("invalid buffer upload");
        }

        const auto copySize = static_cast<size_t>(size);

        const auto ci = vk::BufferCreateInfo({}, copySize, vk::BufferUsageFlagBits::eTransferSrc);

        auto staging = mDevice.createUnique<Buffer>(
            ci, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        staging->write(pSrc, size);

        auto fence   = mDevice.createUnique<vk2s::Fence>(false);
        auto command = mDevice.createUnique<vk2s::Command>();

        command->begin(true);
        command->copyBuffer(staging.get(), *this, vk::BufferCopy(0, 0, copySize));

        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask       = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask       = vk::AccessFlagBits::eMemoryRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = mBuffer.get();
        barrier.offset              = 0;
        barrier.size                = copySize;

        command->bufferPipelineBarrier(barrier, vk::PipelineStageFlagBits::eTransfer,
                                       vk::PipelineStageFlagBits::eAllCommands);
        command->end();
        command->execute(fence);

        if (!fence->wait())
        {
            throw std::runtime_error("buffer upload wait failed");
        }
    }

    void Buffer::write(const void* pSrc, const size_t size, const size_t offset)
    {
        if (offset > mSize || size > mSize - offset)
        {
            throw std::out_of_range("buffer write out of range");
        }

        if (size == 0)
        {
            return;
        }

        if (pSrc == nullptr)
        {
            throw std::invalid_argument("null buffer write source");
        }

        const auto& vkDevice = mDevice.getVkDevice();

        void* p = vkDevice->mapMemory(mMemory.get(), offset, size);
        std::memcpy(p, pSrc, size);
        vkDevice->unmapMemory(mMemory.get());
    }

    void Buffer::read(const std::function<void(const void*)>& readFunc, const size_t size, const size_t offset)
    {
        if (offset > mSize || size > mSize - offset)
        {
            throw std::out_of_range("buffer read out of range");
        }

        if (size == 0)
        {
            return;
        }

        if (!readFunc)
        {
            throw std::invalid_argument("empty buffer read callback");
        }

        const auto& vkDevice = mDevice.getVkDevice();

        void* p = vkDevice->mapMemory(mMemory.get(), offset, size);

        try
        {
            readFunc(p);
        }
        catch (...)
        {
            vkDevice->unmapMemory(mMemory.get());
            throw;
        }

        vkDevice->unmapMemory(mMemory.get());
    }

    const vk::UniqueBuffer& Buffer::getVkBuffer()
    {
        return mBuffer;
    }

    const vk::UniqueDeviceMemory& Buffer::getVkDeviceMemory()
    {
        return mMemory;
    }

    const vk::DeviceSize Buffer::getSize() const
    {
        return mSize;
    }

    const vk::DeviceSize Buffer::getOffset() const
    {
        return mOffset;
    }

    const vk::DeviceAddress Buffer::getVkDeviceAddress() const
    {
        return mDevice.getVkDevice()->getBufferAddress(vk::BufferDeviceAddressInfo(mBuffer.get()));
    }

}  // namespace vk2s
