/*****************************************************************/ /**
 * \file   Buffer.hpp
 * \brief  header file of Buffer class
 * 
 * \author ichi-raven
 * \date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_BUFFER_HPP_
#define VK2S_INCLUDE_BUFFER_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "Macro.hpp"

#include <functional>

namespace vk2s
{
    //! forward declaration
    class Device;

    /**
     * @brief  Buffer class
     */
    class Buffer
    {
    public:  // methods
        /**
         * @brief  constructor
         */
        Buffer(Device& device, const vk::BufferCreateInfo& bi, vk::MemoryPropertyFlags pbs);

        /**
         * @brief  destructor
         */
        ~Buffer();

        NONCOPYABLE(Buffer);
        NONMOVABLE(Buffer);

        /**
         * Synchronous initial update through a staging buffer.
         * Requires eTransferDst and no pending accesses to this buffer.
         * Subsequent consumers must use the same graphics queue.
         */
        void upload(const void* pSrc, const size_t size);

        /**
         * @brief  writes data to buffer
         * 
         * @param pSrc pointer to copy source memory area
         * @param size size of copy source memory area
         * @param offset offset of copy source memory area
         */
        void write(const void* pSrc, const size_t size, const size_t offset = 0);

        /**
         * @brief  maps buffer information to CPU for reading
         * @detail TODO: std::function is slow
         * 
         * @param readFunc function object to process from mapped memory
         * @param size size of memory area to be mapped
         * @param offset offset of memory area to be mapped
         */
        void read(const std::function<void(const void*)>& readFunc, const size_t size, const size_t offset = 0);

        /**
         * @brief  get vulkan internal handle
         */
        const vk::UniqueBuffer& getVkBuffer();

        /**
         * @brief  get vulkan device memory
         */
        const vk::UniqueDeviceMemory& getVkDeviceMemory();

        /**
         * @brief  get size of vulkan device memory
         */
        const vk::DeviceSize getSize() const;

        /**
         * @brief  get offset of vulkan device memory
         */
        const vk::DeviceSize getOffset() const;

        /**
         * @brief  get vulkan device address
         */
        const vk::DeviceAddress getVkDeviceAddress() const;

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan buffer handle
        vk::UniqueBuffer mBuffer;
        //! vulkan device memory
        vk::UniqueDeviceMemory mMemory;
        //! size of vulkan device memory
        vk::DeviceSize mSize;
        //! offset of vulkan device memory
        vk::DeviceSize mOffset;
    };
}  // namespace vk2s

#endif
