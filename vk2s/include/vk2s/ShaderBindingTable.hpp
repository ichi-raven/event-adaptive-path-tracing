/*****************************************************************/ /**
 * @file   ShaderBindingTable.hpp
 * @brief  header file of ShaderBindingTable class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_SHADERBINDINGTABLE_HPP_
#define VK2S_INCLUDE_SHADERBINDINGTABLE_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "Macro.hpp"
#include "SlotMap.hpp"

#include <functional>
#include <optional>

namespace vk2s
{
    //! forward declaration
    class Device;
    class Buffer;
    class Pipeline;

    /**
     * @brief  class of shader binding table for ray tracing
     */
    class ShaderBindingTable
    {
    public:  // types
        /**
         * @brief  device address for each shader stage used
         */
        struct VkShaderBindingTableInfo
        {
            vk::StridedDeviceAddressRegionKHR rgen     = {};
            vk::StridedDeviceAddressRegionKHR miss     = {};
            vk::StridedDeviceAddressRegionKHR hit      = {};
            vk::StridedDeviceAddressRegionKHR callable = {};
        };

        /**
         * @brief information on the area for each shader to be allocated on the ShaderBindingTable 
         */
        struct RegionInfo
        {
            //! number of shaders to register
            uint32_t shaderTypeNum;
            //! number of shader entries
            uint32_t entryNum;
            //! size required in addition to normal shader entries
            std::size_t additionalEntrySize;
            //! function to write additional entry information to the table (dst is a pointer to the destination)
            std::function<void(std::byte* pDst, std::byte* pHandleStart, const uint32_t handleSize,
                               const uint32_t alignedHandleSize)>
                entryWriter;
        };

    public:  // methods
        /**
         * @brief  constructor, to simply register each shader in turn
         */
        ShaderBindingTable(Device& device, Pipeline& rayTracePipeline, const uint32_t raygenNum, const uint32_t missNum,
                           const uint32_t hitNum, const uint32_t callableNum,
                           const vk::ArrayProxyNoTemporaries<vk::RayTracingShaderGroupCreateInfoKHR>& shaderGroups);

        /**
         * @brief  constructor, to manually write a handle to each shader entry in detail
         */
        ShaderBindingTable(Device& device, Pipeline& rayTracePipeline, const RegionInfo& raygenShaderInfo,
                           const RegionInfo& missShaderInfo, const RegionInfo& hitShaderInfo,
                           const RegionInfo& callableShaderInfo,
                           const vk::ArrayProxyNoTemporaries<vk::RayTracingShaderGroupCreateInfoKHR>& shaderGroups);

        /**
         * @brief  destructor
         */
        ~ShaderBindingTable();

        NONCOPYABLE(ShaderBindingTable);
        NONMOVABLE(ShaderBindingTable);

        /**
         * @brief  get vulkan shader binding table information
         */
        const VkShaderBindingTableInfo& getVkSBTInfo() const;

    private:  // methods
        /**
         * @brief  get vulkan ray tracing pipeline properties
         */
        inline vk::PhysicalDeviceRayTracingPipelinePropertiesKHR getRayTracingPipelineProperties() const;

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan shader binding table information
        VkShaderBindingTableInfo mSBTInfo;
        //! vulkan shader binding table handle
        Handle<Buffer> mShaderBindingTable;
    };
}  // namespace vk2s

#endif
