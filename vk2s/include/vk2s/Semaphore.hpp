/*****************************************************************/ /**
 * @file   Semaphore.hpp
 * @brief  header file of Semaphore class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_SEMAPHORE_HPP_
#define VK2S_INCLUDE_SEMAPHORE_HPP_

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
     * @brief  class representing the GPU-GPU synchronization mechanism (Semaphore)
     */
    class Semaphore
    {
    public:  // methods
        /**
         * @brief  constructor
         */
        Semaphore(Device& device);

        /**
         * @brief  destructor
         */
        ~Semaphore();

        NONCOPYABLE(Semaphore);
        NONMOVABLE(Semaphore);

        /**
         * @brief  get vulkan handle 
         */
        const vk::UniqueSemaphore& getVkSemaphore();

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan handle
        vk::UniqueSemaphore mSemaphore;
    };
}  // namespace vk2s

#endif
