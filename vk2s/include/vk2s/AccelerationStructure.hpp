/*****************************************************************/ /**
 * \file   AccelerationStructure.hpp
 * \brief  header file of AccelerationStructure class
 * 
 * \author ichi-raven
 * \date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_ACCELERATIONSTRUCTURE_HPP_
#define VK2S_INCLUDE_ACCELERATIONSTRUCTURE_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "Macro.hpp"
#include "SlotMap.hpp"

namespace vk2s
{
    //! forward declarations
    class Device;
    class Buffer;
    class Command;

    /**
     * @brief  Acceleration Structure class
     */
    class AccelerationStructure
    {
    public:  // types
        struct MotionInstancePadNV : vk::AccelerationStructureMotionInstanceNV
        {
            uint64_t _pad{ 0 };
        };

        //AccelerationStructureSRTMotionInstanceNV must have a stride of 160 bytes
        static_assert(sizeof(MotionInstancePadNV) == 160);

    public:  // methods
        /**
         * @brief  create as BLAS
         */
        AccelerationStructure(Device& device, const uint32_t vertexNum, const uint32_t vertexStride,
                              Buffer& vertexBuffer, const uint32_t faceNum, Buffer& indexBuffer,
                              const bool motion = false, const Handle<Command>& buildCommand = Handle<Command>());

        /**
         * @brief  create as TLAS
         */
        AccelerationStructure(Device& device, const vk::ArrayProxy<vk::AccelerationStructureInstanceKHR>& instances,
                              const Handle<Command>& buildCommand = Handle<Command>());

        /**
         * @brief  create as TLAS (for NV_Motion_Blur)
         * @warn  note that this function cannot be used if the NV-motion_blur extension is not enabled
         */
        AccelerationStructure(Device& device, const vk::ArrayProxy<MotionInstancePadNV>& motionInstances,
                              const Handle<Command>& buildCommand = Handle<Command>());

        /**
         * @brief  destructor
         */
        ~AccelerationStructure();

        /**
         * @brief  build this AS as BLAS
         */
        void build(const uint32_t vertexNum, const uint32_t vertexStride, Buffer& vertexBuffer, const uint32_t faceNum,
                   Buffer& indexBuffer, const bool motion = false,
                   const Handle<Command>& buildCommand = Handle<Command>());

        /**
         * @brief  build this AS as TLAS
         */
        void build(const vk::ArrayProxy<vk::AccelerationStructureInstanceKHR>& instances,
                   const Handle<Command>& buildCommand = Handle<Command>());

        /**
         * @brief  build this AS as TLAS with motion (NV_Motion_Blur)
         * @warn  note that this function cannot be used if the NV-motion_blur extension is not enabled
         * @warn  AccelerationStructureSRTMotionInstanceNV must have a stride of 160 bytes
         */
        void build(const vk::ArrayProxy<MotionInstancePadNV>& instances,
                   const Handle<Command>& buildCommand = Handle<Command>());

        //! noncopyable, nonmovable
        NONCOPYABLE(AccelerationStructure);
        NONMOVABLE(AccelerationStructure);

        /**
         * @brief  get vulkan internal handle
         */
        const vk::UniqueAccelerationStructureKHR& getVkAccelerationStructure();

        /**
         * @brief  get vulkan device address
         */
        const vk::DeviceAddress getVkDeviceAddress() const;

    private:  // methods
        /**
         * @brief  internal function for building AS
         */
        void buildInternal(
            const vk::AccelerationStructureTypeKHR type,
            const vk::ArrayProxyNoTemporaries<vk::AccelerationStructureGeometryKHR>& asGeometry,
            const vk::ArrayProxyNoTemporaries<vk::AccelerationStructureBuildRangeInfoKHR>& asBuildRangeInfo,
            vk::BuildAccelerationStructureFlagsKHR flags, const bool motion,
            const Handle<Command>& buildCommand = Handle<Command>());

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan acceleration structure
        vk::UniqueAccelerationStructureKHR mAccelerationStructure;
        //! vulkan device address
        vk::DeviceAddress mDeviceAddress;
        //! vulkan device memory size
        vk::DeviceSize mSize;

        //! buffer to allocating AS
        Handle<Buffer> mBuffer;
        //! buffer to create AS
        Handle<Buffer> mScratchBuffer;
        //! buffer to update AS
        Handle<Buffer> mUpdateBuffer;
        //! buffer to store instances for TLAS
        Handle<Buffer> mInstanceBuffer;
    };
}  // namespace vk2s

#endif
