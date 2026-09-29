/*****************************************************************/ /**
 * \file   BindGroup.hpp
 * \brief  header file of BindGroup class
 * 
 * \author ichi-raven
 * \date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_BINDGROUP_HPP_
#define VK2S_INCLUDE_BINDGROUP_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "BindLayout.hpp"
#include "Macro.hpp"
#include "SlotMap.hpp"
#include "Compiler.hpp"

#include <variant>
#include <unordered_map>

namespace vk2s
{
    //! forward declaration
    class Device;

    class AccelerationStructure;
    class BindLayout;
    class Buffer;
    class DynamicBuffer;
    class Image;
    class Sampler;

    /**
     * @brief  BindGroup class (in Vulkan, it is equivalent to descriptor set)
     */
    class BindGroup
    {
    public:  // methods
        /**
         * @brief  constructor
         */
        BindGroup(Device& device, BindLayout& layout);

        /**
         * @brief  destructor
         */
        ~BindGroup();

        NONCOPYABLE(BindGroup);
        NONMOVABLE(BindGroup);

        /**
         * @brief  bind the buffer to the specified location
         */
        void bind(const uint8_t binding, const vk::DescriptorType type, Buffer& buffer);
        /**
         * @brief  bind multiple buffers to the specified location
         */
        void bind(const uint8_t binding, const vk::DescriptorType type, const vk::ArrayProxy<Handle<Buffer>>& buffers);
        /**
         * @brief  bind the dynamic buffer to the specified location
         */
        void bind(const uint8_t binding, const vk::DescriptorType type, DynamicBuffer& buffer);
        /**
         * @brief  bind multiple images to the specified location
         */
        void bind(const uint8_t binding, const vk::DescriptorType type, const vk::ArrayProxy<Handle<Image>>& image,
                  const Handle<Sampler>& sampler = Handle<Sampler>());
        /**
         * @brief  bind the sampler to the specified location
         */
        void bind(const uint8_t binding, Sampler& sampler);
        /**
         * @brief  bind the acceleration structure to the specified location
         */
        void bind(const uint8_t binding, AccelerationStructure& as);
        /**
         * @brief  get vulkan internal handle
         */
        const vk::DescriptorSet& getVkDescriptorSet();

    private:  // types
        //! type for caching information about writing to descriptor set
        using DescriptorInfo = std::variant<std::vector<vk::DescriptorBufferInfo>, std::vector<vk::DescriptorImageInfo>,
                                            vk::WriteDescriptorSetAccelerationStructureKHR>;

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan descriptor set handle
        vk::DescriptorSet mDescriptorSet;
        //! Index of allocated pool (see Device implementation)
        size_t mPoolIndex;
        //! capacity when descriptor set is allocated (see Device implementation)
        DescriptorPoolAllocationInfo mAllocationInfo;
        //! cache information written to each binding (reflected collectively)
        std::unordered_map<uint8_t, DescriptorInfo> mInfoCaches;
        //! writing queue to descriptor set
        std::vector<vk::WriteDescriptorSet> mWriteQueue;
    };
}  // namespace vk2s

#endif
