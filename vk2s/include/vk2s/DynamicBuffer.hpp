/*****************************************************************/ /**
 * \file   DynamicBuffer.hpp
 * \brief  header file of DynamicBuffer class
 * 
 * \author ichi-raven
 * \date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_DYNAMICBUFFER_HPP_
#define VK2S_INCLUDE_DYNAMICBUFFER_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "Macro.hpp"

namespace vk2s
{
    class Device;

    class DynamicBuffer
    {
    public:  // methods
        DynamicBuffer(Device& device, const vk::BufferCreateInfo& bi, vk::MemoryPropertyFlags pbs, const uint32_t blockNum);

        ~DynamicBuffer();

        NONCOPYABLE(DynamicBuffer);
        NONMOVABLE(DynamicBuffer);

        void write(const void* pSrc, const size_t size, const size_t offset = 0);

        const vk::UniqueBuffer& getVkBuffer();

        const vk::DeviceSize getSize() const;

        const vk::DeviceSize getBlockSize() const;

        const vk::DeviceSize getOffset() const;

        const vk::DeviceAddress getVkDeviceAddress() const;

    private:  // member variables
        Device& mDevice;

        vk::UniqueBuffer mBuffer;
        vk::UniqueDeviceMemory mMemory;
        vk::DeviceSize mSize;
        vk::DeviceSize mOffset;
        vk::DeviceSize mBlockSize;
    };
}  // namespace vk2s

#endif