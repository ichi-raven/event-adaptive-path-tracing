/*****************************************************************//**
 * @file   Sampler.hpp
 * @brief  header file of Sampler class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_SAMPLER_HPP_
#define VK2S_INCLUDE_SAMPLER_HPP_

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
     * @brief  class that determines how to sample textures on the shader
     */
    class Sampler
    {
    public:  // methods
        /**
         * @brief  constructor
         */
        Sampler(Device& device, const vk::SamplerCreateInfo& ci);

        /**
         * @brief  destructor
         */
        ~Sampler();

        NONCOPYABLE(Sampler);
        NONMOVABLE(Sampler);

        /**
         * @brief  get vulkan handle
         */
        const vk::UniqueSampler& getVkSampler();

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan handle
        vk::UniqueSampler mSampler;
    };
}

#endif