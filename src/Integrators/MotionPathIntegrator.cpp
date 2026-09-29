/*****************************************************************/ /**
 * @file   MotionPathIntegrator.cpp
 * @brief  source file of MotionPathIntegrator class
 * 
 * @author ichi-raven
 * @date   April 2025
 *********************************************************************/

#include "Integrators/MotionPathIntegrator.hpp"

#include "Mesh.hpp"
#include "Material.hpp"
#include "Transform.hpp"
#include "Emitter.hpp"
#include "AnimatedTransform.hpp"
#include "ShaderPaths.hpp"

#include <iostream>

#include <glm/gtx/string_cast.hpp>

namespace evr
{

    MotionPathIntegrator::MotionPathIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output,
                                               Handle<vk2s::Image> albedo, Handle<vk2s::Image> normal)
        : MotionIntegrator(device, scene, output)
    {
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
        const auto raygenShader =
            device.createUnique<vk2s::Shader>(getShaderPath("MotionPathIntegrator_rayGenShader.spv"), "rayGenShader");
        const auto missShader =
            device.createUnique<vk2s::Shader>(getShaderPath("MotionPathIntegrator_missShader.spv"), "missShader");
        const auto shadowShader = device.createUnique<vk2s::Shader>(
            getShaderPath("MotionPathIntegrator_shadowMissShader.spv"), "shadowMissShader");
        const auto chitShader = device.createUnique<vk2s::Shader>(
            getShaderPath("MotionPathIntegrator_closestHitShader.spv"), "closestHitShader");

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
            // 11: G-Buffer : albedo
            vk::DescriptorSetLayoutBinding(11, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 12: G-Buffer : normal
            vk::DescriptorSetLayoutBinding(12, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
        };

        mBindLayout = device.createUnique<vk2s::BindLayout>(bindings);

        // push constant for tonemapping / linlog
        static_assert(sizeof(PushConstants) <= 128);
        vk::PushConstantRange pushConstantRanges(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants));

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
            mBindGroup->bind(11, vk::DescriptorType::eStorageImage, albedo);
            mBindGroup->bind(12, vk::DescriptorType::eStorageImage, normal);
        }
    }

    MotionPathIntegrator::~MotionPathIntegrator()
    {
        mDevice.waitIdle();
    }

    void MotionPathIntegrator::showConfigImGui()
    {
        ImGui::InputInt("spp", &mParams.spp);
        mParams.spp = std::max(1, mParams.spp);
        ImGui::Text("total spp: %d", mParams.accumulatedSpp);
    }

    void MotionPathIntegrator::update(const double t0, const double t1, const uint32_t frameIndex)
    {
        updateInstanceBuffer(t0, t1);
        mTLAS = mScene.getTLAS(frameIndex);
        updateShaderResources();
    }

    void MotionPathIntegrator::updateShaderResources()
    {
        mParams.spp = std::max(1, mParams.spp);

        constexpr int maxSpp = std::numeric_limits<int>::max();
        mParams.accumulatedSpp += std::min(mParams.spp, maxSpp - mParams.accumulatedSpp);

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

    void MotionPathIntegrator::sample(Handle<vk2s::Command> command)
    {
        const auto extent = mOutputImage->getVkExtent();

        const uint32_t gBufferBit    = static_cast<uint32_t>(mParams.generateGBuffer) << 3;
        const uint32_t motionBlurBit = static_cast<uint32_t>(mParams.useMotionBlur) << 2;
        const uint32_t toneMapBit    = static_cast<uint32_t>(!mParams.useEXR) << 1;
        const uint32_t linLogBit     = static_cast<uint32_t>(mParams.uselinLogL);

        PushConstants pushConstants{
            .timeSeed   = mParams.timeSeed,
            .renderMode = (gBufferBit | motionBlurBit | toneMapBit | linLogBit),
        };

        // trace ray
        command->setPipeline(mRaytracePipeline);
        command->setBindGroup(0, mBindGroup.get());
        command->setPushConstant(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &pushConstants);
        command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
    }

}  // namespace evr
