/*****************************************************************/ /**
 * \file   BindLayout.hpp
 * \brief  header file of BindLayout class
 * 
 * \author ichi-raven
 * \date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_BINDLAYOUT_HPP_
#define VK2S_INCLUDE_BINDLAYOUT_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "Macro.hpp"
#include "SlotMap.hpp"
#include "Compiler.hpp"

namespace vk2s
{
    //! forward decleration
    class Device;

    class Shader;

    /**
     * @brief  allocation information for a descriptor set from the descriptor pool according to the descriptor layout
     */
    struct DescriptorPoolAllocationInfo
    {
        /**
         * @brief  constructor
         */
        DescriptorPoolAllocationInfo()
            : DescriptorPoolAllocationInfo(0)
        {
        }

        /**
         * @brief  constructor (fixed size specification)
         * 
         * @param init fixed size
         */
        DescriptorPoolAllocationInfo(const uint32_t init)
            : accelerationStructureNum(init)
            , combinedImageSamplerNum(init)
            , sampledImageNum(init)
            , samplerNum(init)
            , storageBufferNum(init)
            , storageImageNum(init)
            , uniformBufferNum(init)
            , uniformBufferDynamicNum(init)
        {
        }

        /**
         * @brief  get total size
         */
        uint32_t sum() const
        {
            return accelerationStructureNum + combinedImageSamplerNum + sampledImageNum + samplerNum +
                   storageBufferNum + storageImageNum + uniformBufferNum + uniformBufferDynamicNum;
        }

        //! each elements allocation num
        uint32_t accelerationStructureNum = 0;
        uint32_t combinedImageSamplerNum  = 0;
        uint32_t sampledImageNum          = 0;
        uint32_t samplerNum               = 0;
        uint32_t storageBufferNum         = 0;
        uint32_t storageImageNum          = 0;
        uint32_t uniformBufferNum         = 0;
        uint32_t uniformBufferDynamicNum  = 0;
    };

    /**
     * @brief  BindLayout class (in Vulkan, it is equivalent to descriptor set layout)
     */
    class BindLayout
    {
    public:  // methods
        /**
         * @brief  constructor
         */
        BindLayout(Device& device, const vk::ArrayProxy<vk::DescriptorSetLayoutBinding>& bindings);

        /**
         * @brief  destructor
         */
        ~BindLayout();

        NONCOPYABLE(BindLayout);
        NONMOVABLE(BindLayout);

        /**
         * @brief  get vulkan internal handle
         */
        const vk::UniqueDescriptorSetLayout& getVkDescriptorSetLayout();

        /**
         * @brief  get descriptor allocation information calculated from this BindLayout
         */
        const DescriptorPoolAllocationInfo& getDescriptorPoolAllocationInfo();

    private:  // methods
        /**
         * @brief  calculate DescriptorPoolAllocationInfo from this BindLayout
         */
        inline void initAllocationInfo(const vk::ArrayProxy<const vk::DescriptorSetLayoutBinding>& bindings);

    private:  // member variables
        //! reference to device
        Device& mDevice;
        //! capacity when descriptor set is allocated (see Device implementation)
        DescriptorPoolAllocationInfo mInfo;
        //! vulkan descriptor set layout handle
        vk::UniqueDescriptorSetLayout mDescriptorSetLayout;
    };
}  // namespace vk2s

#endif
