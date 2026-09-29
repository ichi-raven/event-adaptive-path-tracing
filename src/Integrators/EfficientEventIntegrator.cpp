/*****************************************************************/ /**
 * @file   EfficientEventIntegrator.cpp
 * @brief  source file of EventIntegrator class
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/

#include "Integrators/EfficientEventIntegrator.hpp"

#include "Mesh.hpp"
#include "Material.hpp"
#include "Transform.hpp"
#include "Emitter.hpp"
#include "AnimatedTransform.hpp"
#include "ShaderPaths.hpp"

#include <iostream>
#include <cstring>

#include <glm/gtx/string_cast.hpp>

namespace evr
{
    EfficientEventIntegrator::EfficientEventIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output)
        : MotionIntegrator(device, scene, output)
    {
        const auto extent = mOutputImage->getVkExtent();

        // GPU timestamp queries
        {
            const auto queueFamilyIndices = device.getVkQueueFamilyIndices();
            const auto graphicsFamily     = queueFamilyIndices.graphicsFamily.value();
            const auto queueProperties    = device.getVkPhysicalDevice().getQueueFamilyProperties();

            mTimestampValidBits = queueProperties.at(graphicsFamily).timestampValidBits;

            if (mTimestampValidBits > 0)
            {
                mTimestampPeriodNs =
                    static_cast<double>(device.getVkPhysicalDevice().getProperties().limits.timestampPeriod);

                const uint32_t eventCount = extent.depth * 8;
                mTimestampQueryCapacity   = 2 + 4 * eventCount;

                const vk::QueryPoolCreateInfo queryPoolInfo({}, vk::QueryType::eTimestamp, mTimestampQueryCapacity);

                mTimestampQueryPool = device.getVkDevice()->createQueryPoolUnique(queryPoolInfo);
            }
        }

        // create dummy image
        buildDummyImage();

        // create scene buffer
        buildSceneBuffer();

        // create instance buffer
        buildInstanceBuffer(0, 0);

        // create material buffer and load texture
        buildMaterialBufferAndTextures();

        // create emitter buffer
        buildEmitterBuffer(0, 0);

        // create sampler
        buildSampler();

        //create pool image
        buildPoolImage(vk::Format::eR32G32B32A32Sfloat);

        // TLAS building
        //rebuildTLAS(0, 0);

        const auto& meshes   = mScene.getMeshes();
        const auto& textures = mScene.getTextures();

        std::vector<Handle<vk2s::Image>> boundTextures;
        if (textures.empty())
        {
            boundTextures.push_back(mDummyTexture);
        }
        else
        {
            boundTextures = textures;
        }

        // load shaders
        const auto raygenShader = device.createUnique<vk2s::Shader>(
            getShaderPath("EfficientEventIntegrator_rayGenShader.spv"), "rayGenShader");
        const auto missShader =
            device.createUnique<vk2s::Shader>(getShaderPath("EfficientEventIntegrator_missShader.spv"), "missShader");
        const auto shadowShader = device.createUnique<vk2s::Shader>(
            getShaderPath("EfficientEventIntegrator_shadowMissShader.spv"), "shadowMissShader");
        const auto chitShader = device.createUnique<vk2s::Shader>(
            getShaderPath("EfficientEventIntegrator_closestHitShader.spv"), "closestHitShader");

        // create bind layout
        const auto meshNum = meshes.size();

        std::array bindings = {
            // 0: TLAS
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eAccelerationStructureKHR, 1,
                                           vk::ShaderStageFlagBits::eAll),
            // 1: result image
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 2: pool image
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 3: scene parameters
            vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 4: vertex buffers
            vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, meshNum,
                                           vk::ShaderStageFlagBits::eAll),
            // 5: index buffers
            vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, meshNum,
                                           vk::ShaderStageFlagBits::eAll),
            // 6: instance buffers
            vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 7: material buffers
            vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 8: emissive buffers
            vk::DescriptorSetLayoutBinding(8, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 9: textures
            vk::DescriptorSetLayoutBinding(9, vk::DescriptorType::eSampledImage, boundTextures.size(),
                                           vk::ShaderStageFlagBits::eAll),
            // 10: sampler
            vk::DescriptorSetLayoutBinding(10, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eAll),
            // 11: indirect index mapping buffer
            vk::DescriptorSetLayoutBinding(11, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 12: flag image for stream compaction input
            vk::DescriptorSetLayoutBinding(12, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 13: cached early sampling statistics and sample count
            vk::DescriptorSetLayoutBinding(13, vk::DescriptorType::eStorageBuffer, 1,
                                           vk::ShaderStageFlagBits::eRaygenKHR),
            // 14: per-pixel event search diagnostics
            vk::DescriptorSetLayoutBinding(14, vk::DescriptorType::eStorageBuffer, 1,
                                           vk::ShaderStageFlagBits::eRaygenKHR),
        };

        mBindLayout = device.createUnique<vk2s::BindLayout>(bindings);

        // push constant for tile offset
        static_assert(sizeof(RaytracingPushConstants) <= 128);
        vk::PushConstantRange pushConstantRanges(vk::ShaderStageFlagBits::eRaygenKHR, 0,
                                                 sizeof(RaytracingPushConstants));

        // create ray tracing pipeline
        vk2s::Pipeline::RayTracingPipelineInfo rpi{
            .raygenShaders      = { raygenShader },
            .missShaders        = { missShader, shadowShader },
            .chitShaders        = { chitShader },
            .bindLayouts        = mBindLayout,
            .shaderGroups       = { vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral,
                                                                           kIndexRaygen, VK_SHADER_UNUSED_KHR,
                                                                           VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                                    vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral,
                                                                           kIndexMiss, VK_SHADER_UNUSED_KHR,
                                                                           VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                                    vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral,
                                                                           kIndexShadow, VK_SHADER_UNUSED_KHR,
                                                                           VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                                    vk::RayTracingShaderGroupCreateInfoKHR(
                                  vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR,
                                  kIndexClosestHit, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR) },
            .pushConstantRanges = pushConstantRanges,
        };

        mRaytracePipeline = device.create<vk2s::Pipeline>(rpi);

        // create shader binding table

        mShaderBindingTable =
            device.create<vk2s::ShaderBindingTable>(mRaytracePipeline.get(), 1, 2, 1, 0, rpi.shaderGroups);

        // for compute--------------------------

        // create stream compaction buffer
        {
            const uint32_t size = extent.width * extent.height * sizeof(uint32_t);
            const auto bi       = vk::BufferCreateInfo(
                {}, size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
#ifndef NDEBUG
            mStreamCompaction.prefixSumBuffer = device.create<vk2s::Buffer>(
                bi, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
#else
            mStreamCompaction.prefixSumBuffer =
                device.create<vk2s::Buffer>(bi, vk::MemoryPropertyFlagBits::eDeviceLocal);
#endif
        }

        // create block sums buffer
        {
            const uint32_t pixelCount = extent.width * extent.height;
            const uint32_t blockCount =
                (pixelCount + StreamCompaction::kThreadGroupSize - 1) / StreamCompaction::kThreadGroupSize;
            const uint32_t size = blockCount * sizeof(uint32_t);
            const auto bi       = vk::BufferCreateInfo(
                {}, size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
#ifndef NDEBUG
            mStreamCompaction.blockSumBuffer = device.create<vk2s::Buffer>(
                bi, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
#else
            mStreamCompaction.blockSumBuffer =
                device.create<vk2s::Buffer>(bi, vk::MemoryPropertyFlagBits::eDeviceLocal);
#endif
        }

        // create result buffer
        {
            const uint32_t size = sizeof(uint32_t) * 2 * extent.width * extent.height;  // pixel coord
            const auto bi       = vk::BufferCreateInfo(
                {}, size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
#ifndef NDEBUG
            mStreamCompaction.resultBuffer = device.create<vk2s::Buffer>(
                bi, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
#else
            mStreamCompaction.resultBuffer = device.create<vk2s::Buffer>(bi, vk::MemoryPropertyFlagBits::eDeviceLocal);
#endif
        }

        // create early sampling cache buffer
        {
            const vk::DeviceSize size = sizeof(EarlySampleCache) * extent.width * extent.height;
            const auto bi             = vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer);
            mStreamCompaction.earlySampleCacheBuffer =
                device.create<vk2s::Buffer>(bi, vk::MemoryPropertyFlagBits::eDeviceLocal);
        }

        // Create per-pixel diagnostics. Shaders only write device-local memory.
        {
            const vk::DeviceSize size = vk::DeviceSize(sizeof(EventSearchStats)) * extent.width * extent.height;

            const auto gpuInfo =
                vk::BufferCreateInfo({}, size,
                                     vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst |
                                         vk::BufferUsageFlagBits::eTransferSrc);
            mEventSearchStatsBuffer =
                device.createUnique<vk2s::Buffer>(gpuInfo, vk::MemoryPropertyFlagBits::eDeviceLocal);

            const auto readbackInfo         = vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferDst);
            mEventSearchStatsReadbackBuffer = device.createUnique<vk2s::Buffer>(
                readbackInfo, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        }

        // create count buffer
        {
            const uint32_t size = sizeof(vk::TraceRaysIndirectCommandKHR);  // only for actual result size readback
            const auto bi =
                vk::BufferCreateInfo({}, size,
                                     vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst |
                                         vk::BufferUsageFlagBits::eShaderDeviceAddress);
            mStreamCompaction.indirectBuffer = device.create<vk2s::Buffer>(
                bi, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

            vk::TraceRaysIndirectCommandKHR defaultVal(extent.width * extent.height, 1, 1);
            mStreamCompaction.indirectBuffer->write(&defaultVal, size);
        }

        // create candidate count buffer
        {
            const uint32_t eventCount = extent.depth * 8;
            const vk::DeviceSize size = sizeof(uint32_t) * eventCount;

            const auto bi = vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer);

            mStreamCompaction.candidateCountBuffer = device.create<vk2s::Buffer>(
                bi, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        }

        // create flag image
        {
            const auto format = vk::Format::eR8Uint;
            const auto extent = vk::Extent3D(mOutputImage->getVkExtent().width, mOutputImage->getVkExtent().height, 1);

            const uint32_t size = extent.width * extent.height * vk2s::Compiler::getSizeOfFormat(format);

            vk::ImageCreateInfo ci;
            ci.arrayLayers = 1;
            ci.extent      = extent;
            ci.format      = format;
            ci.imageType   = vk::ImageType::e2D;
            ci.mipLevels   = 1;
            ci.usage       = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                       vk::ImageUsageFlagBits::eStorage;
            ci.initialLayout = vk::ImageLayout::eUndefined;

            // change format to pooling
            mStreamCompaction.flagImage = mDevice.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                                      size, vk::ImageAspectFlagBits::eColor);

            UniqueHandle<vk2s::Command> cmd = mDevice.createUnique<vk2s::Command>();
            cmd->begin(true);
            cmd->transitionImageLayout(mStreamCompaction.flagImage.get(), vk::ImageLayout::eUndefined,
                                       vk::ImageLayout::eGeneral);
            cmd->end();
            cmd->executeAndWait();
        }

        // setup compute pipeline and bindgroup
        std::array compBindings = {
            // 0 : input image
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
            // 1 : prefix sum buffer
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            // 2: block sum buffer
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            // 3 : result buffer
            vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            // 4: count buffer
            vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            // 5: candidate count buffer
            vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
        };

        mStreamCompaction.bindLayout = device.createUnique<vk2s::BindLayout>(compBindings);

        // create each pipelines for stream compaction

        const auto prefixSum1Shader = device.createUnique<vk2s::Shader>(
            getShaderPath("EfficientEventIntegratorCompute_prefixSumPass1.spv"), "prefixSumPass1");
        const auto blockSumScanShader = device.createUnique<vk2s::Shader>(
            getShaderPath("EfficientEventIntegratorCompute_scanBlockSums.spv"), "scanBlockSums");
        const auto prefixSum2Shader = device.createUnique<vk2s::Shader>(
            getShaderPath("EfficientEventIntegratorCompute_prefixSumPass2.spv"), "prefixSumPass2");
        const auto compactionShader = device.createUnique<vk2s::Shader>(
            getShaderPath("EfficientEventIntegratorCompute_compaction.spv"), "compaction");

        static_assert(sizeof(ComputePushConstants) <= 128);
        vk::PushConstantRange computePushConstantRanges(vk::ShaderStageFlagBits::eCompute, 0,
                                                        sizeof(ComputePushConstants));

        vk2s::Pipeline::ComputePipelineInfo cpi{
            .cs                 = prefixSum1Shader,
            .bindLayouts        = mStreamCompaction.bindLayout,
            .pushConstantRanges = computePushConstantRanges,
        };

        mStreamCompaction.prefixSum1Pipeline = device.create<vk2s::Pipeline>(cpi);

        cpi.cs                                 = blockSumScanShader;
        mStreamCompaction.blockSumScanPipeline = device.create<vk2s::Pipeline>(cpi);

        cpi.cs                               = prefixSum2Shader;
        mStreamCompaction.prefixSum2Pipeline = device.create<vk2s::Pipeline>(cpi);

        cpi.cs                               = compactionShader;
        mStreamCompaction.compactionPipeline = device.create<vk2s::Pipeline>(cpi);

        mStreamCompaction.bindGroup = device.create<vk2s::BindGroup>(mStreamCompaction.bindLayout.get());
        mStreamCompaction.bindGroup->bind(0, vk::DescriptorType::eStorageImage, mStreamCompaction.flagImage);
        mStreamCompaction.bindGroup->bind(1, vk::DescriptorType::eStorageBuffer, mStreamCompaction.prefixSumBuffer);
        mStreamCompaction.bindGroup->bind(2, vk::DescriptorType::eStorageBuffer, mStreamCompaction.blockSumBuffer);
        mStreamCompaction.bindGroup->bind(3, vk::DescriptorType::eStorageBuffer, mStreamCompaction.resultBuffer);
        mStreamCompaction.bindGroup->bind(4, vk::DescriptorType::eStorageBuffer, mStreamCompaction.indirectBuffer);
        mStreamCompaction.bindGroup->bind(5, vk::DescriptorType::eStorageBuffer,
                                          mStreamCompaction.candidateCountBuffer);

        // create bindgroup
        {
            mVertexBuffers.reserve(meshNum);
            mIndexBuffers.reserve(meshNum);

            for (const auto& mesh : meshes)
            {
                mVertexBuffers.push_back(mesh.vertexBuffer);
                mIndexBuffers.push_back(mesh.indexBuffer);
            }

            mBindGroup = device.create<vk2s::BindGroup>(mBindLayout.get());
            mBindGroup->bind(0, mScene.getTLAS(0).get());
            mBindGroup->bind(1, vk::DescriptorType::eStorageImage, mOutputImage);
            mBindGroup->bind(2, vk::DescriptorType::eStorageImage, mPoolImage);
            mBindGroup->bind(3, vk::DescriptorType::eUniformBuffer, mSceneBuffer.get());
            mBindGroup->bind(4, vk::DescriptorType::eStorageBuffer, mVertexBuffers);
            mBindGroup->bind(5, vk::DescriptorType::eStorageBuffer, mIndexBuffers);
            mBindGroup->bind(6, vk::DescriptorType::eStorageBuffer, mInstanceBuffer.get());
            mBindGroup->bind(7, vk::DescriptorType::eStorageBuffer, mMaterialBuffer.get());
            mBindGroup->bind(8, vk::DescriptorType::eStorageBuffer, mEmittersBuffer.get());
            mBindGroup->bind(9, vk::DescriptorType::eSampledImage, boundTextures);
            mBindGroup->bind(10, mSampler.get());
            mBindGroup->bind(11, vk::DescriptorType::eStorageBuffer, mStreamCompaction.resultBuffer.get());
            mBindGroup->bind(12, vk::DescriptorType::eStorageImage, mStreamCompaction.flagImage);
            mBindGroup->bind(13, vk::DescriptorType::eStorageBuffer, mStreamCompaction.earlySampleCacheBuffer.get());
            mBindGroup->bind(14, vk::DescriptorType::eStorageBuffer, mEventSearchStatsBuffer.get());
        }
    }

    EfficientEventIntegrator::~EfficientEventIntegrator()
    {
        mDevice.waitIdle();
    }

    void EfficientEventIntegrator::showConfigImGui()
    {
        ImGui::InputInt("spp", &mParams.spp);
        mParams.spp = std::max(1, mParams.spp);
    }

    void EfficientEventIntegrator::update(const double t0, const double t1, const uint32_t frameIndex)
    {
        updateInstanceBuffer(t0, t1);
        mTLAS            = mScene.getTLAS(frameIndex);
        mParams.timeSeed = frameIndex;
        updateShaderResources();
    }

    void EfficientEventIntegrator::updateShaderResources()
    {
        mParams.spp = std::max(1, mParams.spp);

        const auto& camera = mScene.getCamera();

        SceneParams params{
            .view          = camera.getViewMatrix(),
            .proj          = camera.getProjectionMatrix(),
            .viewInv       = glm::inverse(camera.getViewMatrix()),
            .projInv       = glm::inverse(camera.getProjectionMatrix()),
            .camPos        = glm::vec4(camera.getPos(), 1.0f),
            .sppPerFrame   = static_cast<uint32_t>(mParams.spp),
            .allEmitterNum = mEmitterNum,
            .padding       = { 0.f },
        };

        mSceneBuffer->write(&params, sizeof(SceneParams));

        if (mBindGroup)
        {
            mBindGroup->bind(0, mTLAS.get());
        }
    }

    void EfficientEventIntegrator::sample(Handle<vk2s::Command> command)
    {
        sampleImpl(command, 0);
    }

    void EfficientEventIntegrator::sampleBatched(Handle<vk2s::Command> command, uint32_t batchSize)
    {
        sampleImpl(command, batchSize);
    }

    void EfficientEventIntegrator::sampleImpl(Handle<vk2s::Command> command, uint32_t batchSize)
    {
        const bool isBatched = batchSize > 0 && !mParams.sampleInitialLuminances;

        if (isBatched && !mParams.streamCompaction)
        {
            throw std::runtime_error("batched event tracing currently requires stream compaction");
        }

        const auto extent = mOutputImage->getVkExtent();

        constexpr auto kShaderReadWriteBit = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        constexpr auto kMemoryReadWriteBit = vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eMemoryRead;

        const uint32_t streamCompactionBit = mParams.streamCompaction ? 0x4 : 0x0;
        const uint32_t testBit             = mParams.test ? 0x8 : 0x0;
        const uint32_t eventCount          = extent.depth * 8;
        const uint32_t eventSeedBase       = mParams.timeSeed * eventCount + 1;

        const bool gpuTimingEnabled = !isBatched && static_cast<bool>(mTimestampQueryPool);

        mRecordedReference        = mParams.sampleInitialLuminances;
        mRecordedStreamCompaction = mParams.streamCompaction;
        mRecordedEventCount       = mRecordedReference ? 0 : eventCount;
        mRecordedTimestampCount   = gpuTimingEnabled ? (mRecordedReference ? 2u : 2u + 4u * eventCount) : 0u;

        mSearchStatsRecorded = mSearchStatsEnabled && !mRecordedReference;
        if (mSearchStatsRecorded)
        {
            // Clear every slot, including pixels excluded by compaction, before
            // the first timestamp so old-frame statistics cannot survive.
            vk::BufferMemoryBarrier barrier(vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite |
                                                vk::AccessFlagBits::eShaderWrite,
                                            vk::AccessFlagBits::eTransferWrite, VK_QUEUE_FAMILY_IGNORED,
                                            VK_QUEUE_FAMILY_IGNORED, mEventSearchStatsBuffer->getVkBuffer().get(), 0,
                                            VK_WHOLE_SIZE);
            command->bufferPipelineBarrier(barrier, vk::PipelineStageFlagBits::eAllCommands,
                                           vk::PipelineStageFlagBits::eTransfer);

            command->fillBuffer(mEventSearchStatsBuffer.get(), 0, VK_WHOLE_SIZE, 0);

            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            command->bufferPipelineBarrier(barrier, vk::PipelineStageFlagBits::eTransfer,
                                           vk::PipelineStageFlagBits::eRayTracingShaderKHR);
        }

        const auto writeTimestamp = [&](const uint32_t query)
        {
            if (gpuTimingEnabled)
            {
                command->writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, mTimestampQueryPool.get(), query);
            }
        };

        if (gpuTimingEnabled)
        {
            command->resetQueryPool(mTimestampQueryPool.get(), 0, mRecordedTimestampCount);
            writeTimestamp(0);
        }

        if (mParams.sampleInitialLuminances)
        {
            constexpr uint32_t indirectBit      = 0x0;
            constexpr uint32_t initialSampleBit = 0x2;

            RaytracingPushConstants pushConstants{
                .imageSize       = glm::uvec2(extent.width, extent.height),
                .eventID         = 0,  // initial luminance sampling
                .timeSeed        = 0,
                .flags           = (testBit | streamCompactionBit | indirectBit | initialSampleBit),
                .thresholdPos    = mParams.thresholds.positive,
                .thresholdNeg    = mParams.thresholds.negative,
                .thresholdSigma  = mParams.thresholds.sigma,
                .candidateOffset = 0,
                .reserved        = 0,
            };

            command->setPipeline(mRaytracePipeline);
            command->setBindGroup(0, mBindGroup.get());
            command->setPushConstant(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(RaytracingPushConstants),
                                     &pushConstants);
            command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);

            writeTimestamp(1);

            return;  // early return
        }

        // trace ray (primary pass)
        {
            const uint32_t indirectBit = 0x0;

            RaytracingPushConstants pushConstants{
                .imageSize       = glm::uvec2(extent.width, extent.height),
                .eventID         = 0,
                .timeSeed        = eventSeedBase,
                .flags           = (testBit | streamCompactionBit | indirectBit),
                .thresholdPos    = mParams.thresholds.positive,
                .thresholdNeg    = mParams.thresholds.negative,
                .thresholdSigma  = mParams.thresholds.sigma,
                .candidateOffset = 0,
                .reserved        = 0,
            };

            command->setPipeline(mRaytracePipeline);
            command->setBindGroup(0, mBindGroup.get());
            command->setPushConstant(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(RaytracingPushConstants),
                                     &pushConstants);
            command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
        }

        const auto submitAndResume = [&]()
        {
            command->end();
            command->executeAndWait();

            command->reset();
            command->begin();

            const vk::MemoryBarrier barrier(vk::AccessFlagBits::eMemoryWrite,
                                            vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite);

            command->getVkCommandBuffer()->pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, barrier, {}, {});
        };

        writeTimestamp(1);

        if (isBatched)
        {
            submitAndResume();
        }

        for (uint32_t eventID = 0; eventID < mOutputImage->getVkExtent().depth * 8; ++eventID)
        {
            const uint32_t queryBase = 2 + 4 * eventID;

            writeTimestamp(queryBase + 0);

            if (mParams.streamCompaction)
            {
                // barrier for outputimage
                {
                    vk::ImageMemoryBarrier imgBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                                                      vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral);
                    imgBarrier.setImage(mStreamCompaction.flagImage->getVkImage().get())
                        .setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                    command->imagePipelineBarrier(imgBarrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                                  vk::PipelineStageFlagBits::eComputeShader);
                }

                // clear stream compaction buffers
                {
                    vk::BufferMemoryBarrier bufBarrier(kShaderReadWriteBit, vk::AccessFlagBits::eTransferWrite, 0, 0,
                                                       mStreamCompaction.prefixSumBuffer->getVkBuffer().get(), 0,
                                                       VK_WHOLE_SIZE);
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eTransfer);
                    bufBarrier.setBuffer(mStreamCompaction.blockSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eTransfer);
                    bufBarrier.setBuffer(mStreamCompaction.resultBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eTransfer);

                    command->fillBuffer(mStreamCompaction.prefixSumBuffer.get(), 0, VK_WHOLE_SIZE, 0);
                    command->fillBuffer(mStreamCompaction.blockSumBuffer.get(), 0, VK_WHOLE_SIZE, 0);
                    command->fillBuffer(mStreamCompaction.resultBuffer.get(), 0, VK_WHOLE_SIZE, 0);

                    std::swap(bufBarrier.srcAccessMask, bufBarrier.dstAccessMask);
                    bufBarrier.setBuffer(mStreamCompaction.prefixSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eTransfer,
                                                   vk::PipelineStageFlagBits::eComputeShader);
                    bufBarrier.setBuffer(mStreamCompaction.blockSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eTransfer,
                                                   vk::PipelineStageFlagBits::eComputeShader);
                    bufBarrier.setBuffer(mStreamCompaction.resultBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eTransfer,
                                                   vk::PipelineStageFlagBits::eComputeShader);
                }

                // compute
                {
                    ComputePushConstants pushConstants{
                        .imageSize = glm::uvec2(extent.width, extent.height),
                        .eventID   = eventID,
                        .padding   = 0,
                    };

                    vk::BufferMemoryBarrier bufBarrier(kShaderReadWriteBit, kShaderReadWriteBit, 0, 0,
                                                       mStreamCompaction.prefixSumBuffer->getVkBuffer().get(), 0,
                                                       VK_WHOLE_SIZE);

                    // prefix sum 1
                    const uint32_t pixelCount = extent.width * extent.height;
                    const uint32_t blockCount =
                        (pixelCount + StreamCompaction::kThreadGroupSize - 1) / StreamCompaction::kThreadGroupSize;

                    command->setPipeline(mStreamCompaction.prefixSum1Pipeline);
                    command->setBindGroup(0, mStreamCompaction.bindGroup.get());
                    command->setPushConstant(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ComputePushConstants),
                                             &pushConstants);
                    command->dispatch(blockCount, 1, 1);

                    // Make pass 1 results available to subsequent compute passes
                    bufBarrier.setBuffer(mStreamCompaction.prefixSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eComputeShader);
                    bufBarrier.setBuffer(mStreamCompaction.blockSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eComputeShader);

                    // Convert block totals into exclusive block offsets
                    command->setPipeline(mStreamCompaction.blockSumScanPipeline);
                    command->setBindGroup(0, mStreamCompaction.bindGroup.get());
                    command->setPushConstant(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ComputePushConstants),
                                             &pushConstants);
                    command->dispatch(1, 1, 1);

                    bufBarrier.setBuffer(mStreamCompaction.blockSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eComputeShader);

                    // prefix sum 2
                    command->setPipeline(mStreamCompaction.prefixSum2Pipeline);
                    command->setBindGroup(0, mStreamCompaction.bindGroup.get());
                    command->setPushConstant(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ComputePushConstants),
                                             &pushConstants);
                    command->dispatch(blockCount, 1, 1);

                    // barrier
                    bufBarrier.setBuffer(mStreamCompaction.prefixSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eComputeShader);
                    bufBarrier.setBuffer(mStreamCompaction.blockSumBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eComputeShader);
                    bufBarrier.setBuffer(mStreamCompaction.resultBuffer->getVkBuffer().get());
                    command->bufferPipelineBarrier(bufBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eComputeShader);

                    // compaction
                    command->setPipeline(mStreamCompaction.compactionPipeline);
                    command->setBindGroup(0, mStreamCompaction.bindGroup.get());
                    command->setPushConstant(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ComputePushConstants),
                                             &pushConstants);
                    command->dispatch((extent.width + 15) / 16, (extent.height + 15) / 16, 1);
                }

                // clear flag image
                {
                    vk::ImageMemoryBarrier flagClearBarrier(vk::AccessFlagBits::eShaderWrite,
                                                            vk::AccessFlagBits::eTransferWrite,
                                                            vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral);
                    flagClearBarrier.setImage(mStreamCompaction.flagImage->getVkImage().get())
                        .setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                    command->imagePipelineBarrier(flagClearBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                  vk::PipelineStageFlagBits::eTransfer);

                    constexpr auto colorClearValue = vk::ClearValue(std::array{ 0, 0, 0, 0 });
                    const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                    command->clearImage(mStreamCompaction.flagImage.get(), vk::ImageLayout::eGeneral, colorClearValue,
                                        range);

                    std::swap(flagClearBarrier.srcAccessMask, flagClearBarrier.dstAccessMask);
                    command->imagePipelineBarrier(flagClearBarrier, vk::PipelineStageFlagBits::eTransfer,
                                                  vk::PipelineStageFlagBits::eRayTracingShaderKHR);
                }

                // buffer barrier
                {
                    vk::BufferMemoryBarrier indirectBarrier(
                        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eIndirectCommandRead, 0, 0,
                        mStreamCompaction.indirectBuffer->getVkBuffer().get(), 0, VK_WHOLE_SIZE);
                    command->bufferPipelineBarrier(indirectBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eDrawIndirect);

                    vk::BufferMemoryBarrier resultBarrier(
                        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, 0, 0,
                        mStreamCompaction.resultBuffer->getVkBuffer().get(), 0, VK_WHOLE_SIZE);
                    command->bufferPipelineBarrier(resultBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                                   vk::PipelineStageFlagBits::eRayTracingShaderKHR);

                    if (eventID == 0)
                    {
                        vk::BufferMemoryBarrier cacheBarrier(
                            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, 0, 0,
                            mStreamCompaction.earlySampleCacheBuffer->getVkBuffer().get(), 0, VK_WHOLE_SIZE);
                        command->bufferPipelineBarrier(cacheBarrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                                       vk::PipelineStageFlagBits::eRayTracingShaderKHR);
                    }
                }
            }

            writeTimestamp(queryBase + 1);

            uint32_t candidateCount = 0;

            if (isBatched)
            {
                vk::BufferMemoryBarrier hostBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead,
                                                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                                                    mStreamCompaction.candidateCountBuffer->getVkBuffer().get(), 0,
                                                    VK_WHOLE_SIZE);

                command->bufferPipelineBarrier(hostBarrier, vk::PipelineStageFlagBits::eComputeShader,
                                               vk::PipelineStageFlagBits::eHost);

                submitAndResume();

                mStreamCompaction.candidateCountBuffer->read(
                    [&](const void* data)
                    {
                        const auto* counts = static_cast<const uint32_t*>(data);
                        candidateCount     = counts[eventID];
                    },
                    sizeof(uint32_t) * (eventID + 1));

                const uint64_t pixelCount = uint64_t(extent.width) * extent.height;

                if (candidateCount > pixelCount)
                {
                    throw std::runtime_error("event candidate count exceeds pixel count");
                }
            }

            writeTimestamp(queryBase + 2);

            // Every event reads the previous pool state and appends to the output image.
            // Compaction barriers only cover the flag image and compaction buffers.
            {
                vk::ImageMemoryBarrier imgBarrier(kShaderReadWriteBit, kShaderReadWriteBit, vk::ImageLayout::eGeneral,
                                                  vk::ImageLayout::eGeneral);
                imgBarrier.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                imgBarrier.setImage(mOutputImage->getVkImage().get());
                command->imagePipelineBarrier(imgBarrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                              vk::PipelineStageFlagBits::eRayTracingShaderKHR);
                imgBarrier.setImage(mPoolImage->getVkImage().get());
                command->imagePipelineBarrier(imgBarrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                              vk::PipelineStageFlagBits::eRayTracingShaderKHR);
            }

            // true event detection
            {
                const uint32_t indirectBit    = 0x1;
                const uint32_t searchStatsBit = (mSearchStatsRecorded && eventID == 0) ? 0x10u : 0u;

                RaytracingPushConstants pushConstants{
                    .imageSize       = glm::uvec2(extent.width, extent.height),
                    .eventID         = eventID,  // target event
                    .timeSeed        = eventSeedBase + eventID,
                    .flags           = (testBit | streamCompactionBit | indirectBit | searchStatsBit),
                    .thresholdPos    = mParams.thresholds.positive,
                    .thresholdNeg    = mParams.thresholds.negative,
                    .thresholdSigma  = mParams.thresholds.sigma,
                    .candidateOffset = 0,
                    .reserved        = 0,
                };

                const auto bindTracing = [&]()
                {
                    command->setPipeline(mRaytracePipeline);
                    command->setBindGroup(0, mBindGroup.get());
                    command->setPushConstant(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(RaytracingPushConstants),
                                             &pushConstants);
                };

                if (!isBatched)
                {
                    bindTracing();
                    command->traceRaysIndirect(mShaderBindingTable.get(),
                                               mStreamCompaction.indirectBuffer->getVkDeviceAddress());
                }
                else
                {
                    uint32_t offset = 0;

                    while (offset < candidateCount)
                    {
                        const uint32_t count = std::min(batchSize, candidateCount - offset);

                        pushConstants.candidateOffset = offset;

                        bindTracing();

                        command->traceRays(mShaderBindingTable.get(), count, 1, 1);

                        submitAndResume();

                        offset += count;
                    }
                }
            }

            writeTimestamp(queryBase + 3);
        }

        if (mSearchStatsRecorded)
        {
            // Untouched slots still contain transfer-written zeros; make both
            // those and shader-written slots visible to the copy.
            vk::BufferMemoryBarrier barrier(vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite,
                                            vk::AccessFlagBits::eTransferRead, VK_QUEUE_FAMILY_IGNORED,
                                            VK_QUEUE_FAMILY_IGNORED, mEventSearchStatsBuffer->getVkBuffer().get(), 0,
                                            VK_WHOLE_SIZE);
            command->bufferPipelineBarrier(
                barrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR | vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eTransfer);

            command->copyBuffer(mEventSearchStatsBuffer.get(), mEventSearchStatsReadbackBuffer.get(),
                                vk::BufferCopy(0, 0, mEventSearchStatsBuffer->getSize()));

            barrier.buffer        = mEventSearchStatsReadbackBuffer->getVkBuffer().get();
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
            command->bufferPipelineBarrier(barrier, vk::PipelineStageFlagBits::eTransfer,
                                           vk::PipelineStageFlagBits::eHost);
        }
    }

    std::optional<MotionIntegrator::GpuTiming> EfficientEventIntegrator::consumeGpuTiming()
    {
        if (!mTimestampQueryPool || mRecordedTimestampCount == 0)
        {
            return std::nullopt;
        }

        std::vector<uint64_t> timestamps(mRecordedTimestampCount);

        const VkResult result = vkGetQueryPoolResults(static_cast<VkDevice>(mDevice.getVkDevice().get()),
                                                      static_cast<VkQueryPool>(mTimestampQueryPool.get()), 0,
                                                      mRecordedTimestampCount, sizeof(uint64_t) * timestamps.size(),
                                                      timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

        if (result != VK_SUCCESS)
        {
            throw std::runtime_error(
                "failed to read GPU timestamp queries (VkResult=" + std::to_string(static_cast<int>(result)) + ")");
        }

        const auto durationMs = [&](const uint64_t begin, const uint64_t end)
        {
            uint64_t ticks = 0;

            if (mTimestampValidBits >= 64)
            {
                ticks = end - begin;
            }
            else
            {
                const uint64_t mask = (uint64_t(1) << mTimestampValidBits) - 1;
                ticks               = (end - begin) & mask;
            }

            return static_cast<double>(ticks) * mTimestampPeriodNs * 1e-6;
        };

        GpuTiming timing;
        timing.referenceInitialization = mRecordedReference;
        timing.primaryRayTracingMs     = durationMs(timestamps[0], timestamps[1]);
        timing.totalMs                 = timing.primaryRayTracingMs;

        if (!mRecordedReference)
        {
            timing.compactionPerEventMs.reserve(mRecordedEventCount);
            timing.rayTracingPerEventMs.reserve(mRecordedEventCount);
            timing.candidatePixelsPerEvent.resize(mRecordedEventCount);

            if (mRecordedStreamCompaction)
            {
                const size_t size = sizeof(uint32_t) * mRecordedEventCount;

                mStreamCompaction.candidateCountBuffer->read(
                    [&](const void* data)
                    {
                        const auto* counts = static_cast<const uint32_t*>(data);

                        for (uint32_t eventID = 0; eventID < mRecordedEventCount; ++eventID)
                        {
                            timing.candidatePixelsPerEvent[eventID] = counts[eventID];
                        }
                    },
                    size);
            }
            else
            {
                const auto extent               = mOutputImage->getVkExtent();
                const uint32_t dispatchedPixels = extent.width * extent.height;

                for (uint32_t eventID = 0; eventID < mRecordedEventCount; ++eventID)
                {
                    timing.candidatePixelsPerEvent[eventID] = dispatchedPixels;
                }
            }

            for (uint32_t eventID = 0; eventID < mRecordedEventCount; ++eventID)
            {
                const uint32_t queryBase = 2 + 4 * eventID;

                const double compactionMs = durationMs(timestamps[queryBase + 0], timestamps[queryBase + 1]);
                const double rayTracingMs = durationMs(timestamps[queryBase + 2], timestamps[queryBase + 3]);

                timing.compactionPerEventMs.push_back(compactionMs);
                timing.rayTracingPerEventMs.push_back(rayTracingMs);

                timing.streamCompactionMs += compactionMs;
                timing.eventRayTracingMs += rayTracingMs;
            }

            timing.totalMs += timing.streamCompactionMs + timing.eventRayTracingMs;
        }

        mRecordedTimestampCount   = 0;
        mRecordedEventCount       = 0;
        mRecordedStreamCompaction = false;

        return timing;
    }

    std::vector<EfficientEventIntegrator::EventSearchStats> EfficientEventIntegrator::consumeEventSearchStats()
    {
        if (!mSearchStatsRecorded)
        {
            return {};
        }

        const auto extent       = mOutputImage->getVkExtent();
        const size_t pixelCount = size_t(extent.width) * extent.height;
        std::vector<EventSearchStats> stats(pixelCount);

        mEventSearchStatsReadbackBuffer->read(
            [&](const void* data) { std::memcpy(stats.data(), data, stats.size() * sizeof(EventSearchStats)); },
            stats.size() * sizeof(EventSearchStats));

        mSearchStatsRecorded = false;
        return stats;
    }

}  // namespace evr
