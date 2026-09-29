/*****************************************************************/ /**
 * @file   AccelerationStructure.cpp
 * @brief  source file of AccelerationStructure class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/

#include "../include/vk2s/AccelerationStructure.hpp"
#include "../include/vk2s/Device.hpp"

namespace vk2s
{
    // BLAS
    AccelerationStructure::AccelerationStructure(Device& device, const uint32_t vertexNum, const uint32_t vertexStride,
                                                 Buffer& vertexBuffer, const uint32_t faceNum, Buffer& indexBuffer,
                                                 const bool motion, const Handle<Command>& buildCommand)
        : mDevice(device)
    {
        build(vertexNum, vertexStride, vertexBuffer, faceNum, indexBuffer, motion, buildCommand);
    }

    // TLAS
    AccelerationStructure::AccelerationStructure(Device& device,
                                                 const vk::ArrayProxy<vk::AccelerationStructureInstanceKHR>& instances,
                                                 const Handle<Command>& buildCommand)
        : mDevice(device)
    {
        build(instances, buildCommand);
    }

    // TLAS (NV_Motion_blur)
    AccelerationStructure::AccelerationStructure(Device& device,
                                                 const vk::ArrayProxy<MotionInstancePadNV>& motionInstances,
                                                 const Handle<Command>& buildCommand)
        : mDevice(device)
    {
        build(motionInstances, buildCommand);
    }

    AccelerationStructure::~AccelerationStructure()
    {
        // Destroy KHR's acceleration structure first
        mAccelerationStructure.reset();

        // Then, destroy member buffers
        mDevice.destroy(mInstanceBuffer);
        mDevice.destroy(mUpdateBuffer);
        mDevice.destroy(mScratchBuffer);
        mDevice.destroy(mBuffer);
    }

    const vk::UniqueAccelerationStructureKHR& AccelerationStructure::getVkAccelerationStructure()
    {
        return mAccelerationStructure;
    }

    const vk::DeviceAddress AccelerationStructure::getVkDeviceAddress() const
    {
        return mDeviceAddress;
    }

    void AccelerationStructure::build(const uint32_t vertexNum, const uint32_t vertexStride, Buffer& vertexBuffer,
                                      const uint32_t faceNum, Buffer& indexBuffer, const bool motion,
                                      const Handle<Command>& buildCommand)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        vk::AccelerationStructureGeometryKHR geometryInfo;
        geometryInfo.flags        = vk::GeometryFlagBitsKHR::eOpaque;
        geometryInfo.geometryType = vk::GeometryTypeKHR::eTriangles;

        //vk::AccelerationStructureGeometryMotionTrianglesDataNV motionTriangles;
        //if (motion && mDevice.getVkAvailableExtensions().useNVMotionBlurExt)
        //{
        //    geometryInfo.geometry.triangles.pNext = &motionTriangles;
        //}

        {
            vk::BufferDeviceAddressInfo VBdeviceAddressInfo(vertexBuffer.getVkBuffer().get());
            vk::BufferDeviceAddressInfo IBdeviceAddressInfo(indexBuffer.getVkBuffer().get());

            auto& triangles                    = geometryInfo.geometry.triangles;
            triangles.vertexFormat             = vk::Format::eR32G32B32Sfloat;
            triangles.vertexData.deviceAddress = vkDevice->getBufferAddress(VBdeviceAddressInfo);
            triangles.maxVertex                = vertexNum;
            triangles.vertexStride             = vertexStride;
            triangles.indexType                = vk::IndexType::eUint32;
            triangles.indexData.deviceAddress  = vkDevice->getBufferAddress(IBdeviceAddressInfo);
        }

        vk::AccelerationStructureBuildRangeInfoKHR asBuildRangeInfo(faceNum);

        std::array geometries = { geometryInfo };
        std::array ranges     = { asBuildRangeInfo };

        buildInternal(vk::AccelerationStructureTypeKHR::eBottomLevel, geometries, ranges,
                      vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, motion, buildCommand);
    }

    void AccelerationStructure::build(const vk::ArrayProxy<vk::AccelerationStructureInstanceKHR>& instances,
                                      const Handle<Command>& buildCommand)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        const auto instancesBufferSize   = sizeof(vk::AccelerationStructureInstanceKHR) * instances.size();
        const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                           vk::BufferUsageFlagBits::eShaderDeviceAddress;
        const auto hostMemProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        vk::BufferCreateInfo ci({}, instancesBufferSize, usage);

        mInstanceBuffer = mDevice.create<Buffer>(ci, hostMemProps);
        mInstanceBuffer->write(instances.data(), instancesBufferSize);

        vk::BufferDeviceAddressInfo instanceBufferDeviceAddressInfo(mInstanceBuffer->getVkBuffer().get());
        const auto address = vkDevice->getBufferAddress(instanceBufferDeviceAddressInfo);
        vk::DeviceOrHostAddressConstKHR instanceDataDeviceAddress(address);

        vk::AccelerationStructureGeometryKHR asGeometry;
        asGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
        asGeometry.flags        = vk::GeometryFlagBitsKHR::eOpaque;
        asGeometry.geometry.instances =
            vk::AccelerationStructureGeometryInstancesDataKHR(VK_FALSE, instanceDataDeviceAddress);

        vk::AccelerationStructureBuildRangeInfoKHR asBuildRangeInfo(static_cast<uint32_t>(instances.size()), 0, 0, 0);

        std::array geometries = { asGeometry };
        std::array ranges     = { asBuildRangeInfo };

        buildInternal(vk::AccelerationStructureTypeKHR::eTopLevel, geometries, ranges,
                      vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, false, buildCommand);
    }

    void AccelerationStructure::build(const vk::ArrayProxy<MotionInstancePadNV>& motionInstances,
                                      const Handle<Command>& buildCommand)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        const auto instancesBufferSize   = sizeof(MotionInstancePadNV) * motionInstances.size();
        const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                           vk::BufferUsageFlagBits::eShaderDeviceAddress;
        const auto hostMemProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        vk::BufferCreateInfo ci({}, instancesBufferSize, usage);

        mInstanceBuffer = mDevice.create<Buffer>(ci, hostMemProps);
        mInstanceBuffer->write(motionInstances.data(), instancesBufferSize);

        vk::BufferDeviceAddressInfo instanceBufferDeviceAddressInfo(mInstanceBuffer->getVkBuffer().get());
        const auto address = vkDevice->getBufferAddress(instanceBufferDeviceAddressInfo);
        vk::DeviceOrHostAddressConstKHR instanceDataDeviceAddress(address);

        vk::AccelerationStructureGeometryKHR asGeometry;
        asGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
        asGeometry.flags        = vk::GeometryFlagBitsKHR::eOpaque;
        asGeometry.geometry.instances =
            vk::AccelerationStructureGeometryInstancesDataKHR(VK_FALSE, instanceDataDeviceAddress);

        vk::AccelerationStructureBuildRangeInfoKHR asBuildRangeInfo(static_cast<uint32_t>(motionInstances.size()), 0, 0,
                                                                    0);
        std::array geometries = { asGeometry };
        std::array ranges     = { asBuildRangeInfo };

        buildInternal(vk::AccelerationStructureTypeKHR::eTopLevel, geometries, ranges,
                      vk::BuildAccelerationStructureFlagBitsKHR::eMotionNV |
                          vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
                      true, buildCommand);
    }

    void AccelerationStructure::buildInternal(
        const vk::AccelerationStructureTypeKHR type,
        const vk::ArrayProxyNoTemporaries<vk::AccelerationStructureGeometryKHR>& asGeometry,
        const vk::ArrayProxyNoTemporaries<vk::AccelerationStructureBuildRangeInfoKHR>& asBuildRangeInfo,
        vk::BuildAccelerationStructureFlagsKHR flags, const bool motion, const Handle<Command>& buildCommand)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        vk::AccelerationStructureBuildGeometryInfoKHR asBuildGeometryInfo;
        asBuildGeometryInfo.type  = type;
        asBuildGeometryInfo.mode  = vk::BuildAccelerationStructureModeKHR::eBuild;
        asBuildGeometryInfo.flags = flags;

        asBuildGeometryInfo.geometryCount = 1;
        asBuildGeometryInfo.pGeometries   = asGeometry.data();

        std::vector<uint32_t> numPrimitives;
        numPrimitives.reserve(asBuildRangeInfo.size());
        for (const auto& buildRangeInfo : asBuildRangeInfo)
        {
            numPrimitives.emplace_back(buildRangeInfo.primitiveCount);
        }

        const auto asBuildSizesInfo = vkDevice->getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, asBuildGeometryInfo, numPrimitives);

        // allocate accleration structure
        vk::BufferUsageFlags asUsage =
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        vk::MemoryPropertyFlags memProps = vk::MemoryPropertyFlagBits::eDeviceLocal;
        vk::BufferCreateInfo ci({}, asBuildSizesInfo.accelerationStructureSize, asUsage);

        mBuffer = mDevice.create<Buffer>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal);
        mSize   = asBuildSizesInfo.accelerationStructureSize;

        vk::AccelerationStructureCreateInfoKHR asCreateInfo;
        asCreateInfo.buffer = mBuffer->getVkBuffer().get();
        asCreateInfo.size   = mSize;
        asCreateInfo.type   = asBuildGeometryInfo.type;

        // for NV_Motion_blur
        vk::AccelerationStructureMotionInfoNV motionInfo(asBuildRangeInfo.back().primitiveCount);

        if (motion && mDevice.getVkAvailableExtensions().useNVMotionBlurExt)
        {
            asCreateInfo.createFlags = vk::AccelerationStructureCreateFlagBitsKHR::eMotionNV;
            asCreateInfo.pNext       = &motionInfo;
        }

        // create
        mAccelerationStructure = vkDevice->createAccelerationStructureKHRUnique(asCreateInfo);

        // get the device address of AccelerationStructure
        vk::AccelerationStructureDeviceAddressInfoKHR asDeviceAddressInfo(mAccelerationStructure.get());
        mDeviceAddress = vkDevice->getAccelerationStructureAddressKHR(asDeviceAddressInfo);

        // prepare scratch buffer
        if (asBuildSizesInfo.buildScratchSize > 0)
        {
            vk::BufferCreateInfo scratchci({}, asBuildSizesInfo.buildScratchSize,
                                           asUsage | vk::BufferUsageFlagBits::eStorageBuffer);
            mScratchBuffer = mDevice.create<Buffer>(scratchci, memProps);
        }

        // prepare update buffer
        if (asBuildSizesInfo.updateScratchSize > 0)
        {
            vk::BufferCreateInfo updateci({}, asBuildSizesInfo.updateScratchSize,
                                          asUsage | vk::BufferUsageFlagBits::eStorageBuffer);
            mUpdateBuffer = mDevice.create<Buffer>(updateci, memProps);
        }

        // build acceleration structure
        asBuildGeometryInfo.dstAccelerationStructure = mAccelerationStructure.get();
        vk::BufferDeviceAddressInfo scratchDeviceAddressInfo(mScratchBuffer->getVkBuffer().get());
        asBuildGeometryInfo.scratchData.deviceAddress = vkDevice->getBufferAddress(scratchDeviceAddressInfo);

        {  // exec build
            std::vector<const vk::AccelerationStructureBuildRangeInfoKHR*> buildRangeInfoPtrs;
            buildRangeInfoPtrs.reserve(asBuildRangeInfo.size());
            for (auto& v : asBuildRangeInfo)
            {
                buildRangeInfoPtrs.emplace_back(&v);
            }

            UniqueHandle<vk2s::Command> ownedCommand;
            Handle<vk2s::Command> command = buildCommand;

            if (!command)
            {
                ownedCommand = mDevice.createUnique<vk2s::Command>();
                command      = static_cast<Handle<vk2s::Command>>(ownedCommand);
            }

            UniqueHandle<vk2s::Fence> fence;

            if (!buildCommand)
            {
                fence = mDevice.createUnique<vk2s::Fence>(false);
                command->begin(true);
            }

            command->getVkCommandBuffer()->buildAccelerationStructuresKHR(asBuildGeometryInfo, buildRangeInfoPtrs);

            // need memory barrier
            vk::MemoryBarrier barrier;

            barrier.srcAccessMask =
                vk::AccessFlagBits::eAccelerationStructureReadKHR | vk::AccessFlagBits::eAccelerationStructureWriteKHR;
            barrier.dstAccessMask =
                vk::AccessFlagBits::eAccelerationStructureReadKHR | vk::AccessFlagBits::eAccelerationStructureWriteKHR;

            command->globalPipelineBarrier(barrier, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                           vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR);

            if (!buildCommand)
            {
                command->end();

                command->execute(fence);
                fence->wait();
            }
        }
    }

}  // namespace vk2s
