/*****************************************************************/ /**
 * @file   Fence.hpp
 * @brief  header file of Fence class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_FENCE_HPP_
#define VK2S_INCLUDE_FENCE_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "Macro.hpp"

namespace vk2s
{
    //! forward declaration
    class Device;

    /**
     * @brief  class representing the GPU-CPU synchronization mechanism (Fence)
     */
    class Fence
    {
    public:  // methods
        /**
         * @brief  construction
         */
        Fence(Device& device, const bool signaled = true);

        /**
         * @brief  destruction
         */
        ~Fence();

        NONCOPYABLE(Fence);
        NONMOVABLE(Fence);

        /**
         * @brief  wait until this fence is signaled by GPU
         */
        bool wait();

        /**
         * @brief  reset this fences state
         */
        void reset();

        /**
         * @brief  check this fence is signaled
         */
        bool signaled();

        /**
         * @brief  get vulkan handle
         */
        const vk::UniqueFence& getVkFence();

    private:  // member variables
        //! reference to device
        Device& mDevice;

        // vulkan fence handle
        vk::UniqueFence mFence;
    };
}  // namespace vk2s

#endif
