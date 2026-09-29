/*****************************************************************
 * @file   EventIntegrator.cpp
 * @brief  source file of EventIntegrator class
 *
 * @author ichi-raven
 * @date   October 2024
 *****************************************************************/

#include "Integrators/EventIntegrator.hpp"

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
    EventIntegrator::EventIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output)
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

        // create pool image
        buildPoolImage(vk::Format::eR32G32B32A32Sfloat);

        // TLAS building
        // rebuildTLAS(0, 0);

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
            device.createUnique<vk2s::Shader>(getShaderPath("EventIntegrator_rayGenShader.spv"), "rayGenShader");
        const auto missShader =
            device.createUnique<vk2s::Shader>(getShaderPath("EventIntegrator_missShader.spv"), "missShader");
        const auto shadowShader = device.createUnique<vk2s::Shader>(
            getShaderPath("EventIntegrator_shadowMissShader.spv"), "shadowMissShader");
        const auto chitShader = device.createUnique<vk2s::Shader>(getShaderPath("EventIntegrator_closestHitShader.spv"),
                                                                  "closestHitShader");

        // create bind layout
        const auto meshNum  = meshes.size();
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
        };

        mBindLayout = device.createUnique<vk2s::BindLayout>(bindings);

        // push constant for tile offset
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
        }
    }

    EventIntegrator::~EventIntegrator()
    {
        mDevice.waitIdle();
    }

    void EventIntegrator::showConfigImGui()
    {
        ImGui::InputInt("spp", &mParams.spp);
        mParams.spp = std::max(1, mParams.spp);
    }

    void EventIntegrator::update(const double t0, const double t1, const uint32_t frameIndex)
    {
        updateInstanceBuffer(t0, t1);
        mTLAS = mScene.getTLAS(frameIndex);
        updateShaderResources();
    }

    void EventIntegrator::updateShaderResources()
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

#ifndef NDEBUG
        // DEBUG showing
        std::cout << "dump:----------------------\n";
        {
            std::cout << "scene params:---\n";
            const auto size = mSceneBuffer->getSize();
            mSceneBuffer->read(
                [&](const void* pData)
                {
                    const auto p = reinterpret_cast<const SceneParams*>(pData);
                    std::cout << "view:\n" << glm::to_string(p->view) << "\n";
                    std::cout << "proj:\n" << glm::to_string(p->proj) << "\n";
                    std::cout << "viewInv:\n" << glm::to_string(p->viewInv) << "\n";
                    std::cout << "projInv:\n" << glm::to_string(p->projInv) << "\n";
                    std::cout << "camPos: " << glm::to_string(p->camPos) << "\n";
                    std::cout << "sppPerFrame: " << p->sppPerFrame << "\n";
                    std::cout << "allEmitterNum: " << p->allEmitterNum << "\n";
                },
                size);
            std::cout << "\n";
        }

        {
            std::cout << "vertices:---\n";
            for (auto& vb : mVertexBuffers)
            {
                const auto size = vb->getSize();
                vb->read(
                    [&](const void* pData)
                    {
                        const auto p     = reinterpret_cast<const Mesh::Vertex*>(pData);
                        const auto count = size / sizeof(Mesh::Vertex);
                        for (size_t i = 0; i < count; ++i)
                        {
                            std::cout << "v[" << i << "]: pos " << glm::to_string(p[i].pos) << ", normal "
                                      << glm::to_string(p[i].normal) << ", uv (" << p[i].u << ", " << p[i].v << ")\n";
                        }
                    },
                    size);
            }
            std::cout << "\n";
        }

        {
            std::cout << "indices:---\n";
            for (auto& ib : mIndexBuffers)
            {
                const auto size = ib->getSize();
                ib->read(
                    [&](const void* pData)
                    {
                        const auto p     = reinterpret_cast<const uint32_t*>(pData);
                        const auto count = size / sizeof(uint32_t);
                        for (size_t i = 0; i < count; ++i)
                        {
                            std::cout << "i[" << i << "]: " << p[i] << "\n";
                        }
                    },
                    size);
            }
            std::cout << "\n";
        }

        {
            std::cout << "motion instance params:---\n";
            const auto size = mInstanceBuffer->getSize();
            mInstanceBuffer->read(
                [&](const void* pData)
                {
                    const auto p     = reinterpret_cast<const MotionInstanceParams*>(pData);
                    const auto count = size / sizeof(MotionInstanceParams);
                    for (size_t i = 0; i < count; ++i)
                    {
                        std::cout << "instance[" << i << "]: \n";
                        std::cout << "  worldT0:\n";
                        std::cout << "    - scale: " << glm::to_string(p[i].transformT0.scale) << "\n";
                        std::cout << "    - shear: " << glm::to_string(p[i].transformT0.shear) << "\n";
                        std::cout << "    -   rot: " << glm::to_string(p[i].transformT0.rotation) << "\n";
                        std::cout << "    - trans: " << glm::to_string(p[i].transformT0.translation) << "\n";
                        std::cout << " worldT1:\n";
                        std::cout << "    - scale: " << glm::to_string(p[i].transformT1.scale) << "\n";
                        std::cout << "    - shear: " << glm::to_string(p[i].transformT1.shear) << "\n";
                        std::cout << "    -   rot: " << glm::to_string(p[i].transformT1.rotation) << "\n";
                        std::cout << "    - trans: " << glm::to_string(p[i].transformT1.translation) << "\n";
                    }
                },
                size);
            std::cout << "\n";
        }

        {
            std::cout << "materials:---\n";
            const auto size = mMaterialBuffer->getSize();
            mMaterialBuffer->read(
                [&](const void* pData)
                {
                    const auto p     = reinterpret_cast<const Material::Params*>(pData);
                    const auto count = size / sizeof(Material::Params);
                    for (size_t i = 0; i < count; ++i)
                    {
                        std::cout << "material[" << i << "]: \n";
                        std::cout << " albedo: " << glm::to_string(p[i].albedo) << "\n";
                        std::cout << " roughness: " << p[i].roughness << "\n";
                        std::cout << " metallic: " << p[i].metallic << "\n";
                        std::cout << " specTrans: " << p[i].specTrans << "\n";
                        std::cout << " diffTrans: " << p[i].diffTrans << "\n";
                        std::cout << " flatness: " << p[i].flatness << "\n";
                        std::cout << " specular tint: " << p[i].specularTint << "\n";
                        std::cout << " sheen tint: " << glm::to_string(p[i].sheenTint) << "\n";
                        std::cout << "sheen: " << p[i].sheen << "\n";
                        std::cout << "anisotropic: " << p[i].anisotropic << "\n";
                        std::cout << "clearcoat: " << p[i].clearcoat << "\n";
                        std::cout << "clearcoat gloss: " << p[i].clearcoatGloss << "\n";
                        std::cout << "IOR: " << p[i].IOR << "\n";
                        std::cout << "albedoTexIndex: " << p[i].albedoTexIndex << "\n";
                        std::cout << "roughnessTexIndex: " << p[i].roughnessTexIndex << "\n";
                        std::cout << "metalnessTexIndex: " << p[i].metalnessTexIndex << "\n";
                        std::cout << "normalMapTexIndex: " << p[i].normalMapTexIndex << "\n";
                        std::cout << "emissive: " << glm::to_string(p[i].emissive) << "\n";
                        std::cout << "material type: " << p[i].materialType << "\n";
                    }
                },
                size);
            std::cout << "\n";
        }

        {
            std::cout << "emitters:---\n";
            const auto size = mEmittersBuffer->getSize();
            mEmittersBuffer->read(
                [&](const void* pData)
                {
                    const auto p     = reinterpret_cast<const Emitter::Params*>(pData);
                    const auto count = size / sizeof(Emitter::Params);
                    for (size_t i = 0; i < count; ++i)
                    {
                        std::cout << "emitter[" << i << "]: \n";
                        std::cout << " pos: " << glm::to_string(p[i].pos) << "\n";
                        std::cout << " faceNum: " << p[i].faceNum << "\n";
                        std::cout << " meshIndex: " << p[i].meshIndex << "\n";
                        std::cout << " primitiveIndex: " << p[i].primitiveIndex << "\n";
                        std::cout << " emissive: " << glm::to_string(p[i].emissive) << "\n";
                        std::cout << " type: " << p[i].type << "\n";
                        std::cout << " texIndex: " << p[i].texIndex << "\n";
                        std::cout << "\n";
                    }
                },
                size);
            std::cout << "\n";
        }
#endif
    }

    void EventIntegrator::sample(Handle<vk2s::Command> command)
    {
        const auto extent = mOutputImage->getVkExtent();

        const uint32_t discreteBit      = static_cast<uint32_t>(mParams.discrete);
        const uint32_t initialSampleBit = static_cast<uint32_t>(mParams.sampleInitialLuminances) << 1;

        PushConstants pushConstants{
            .thresholdPos   = mParams.thresholds.positive,
            .thresholdNeg   = mParams.thresholds.negative,
            .thresholdSigma = mParams.thresholds.sigma,
            .padding        = 0,
            .flags          = (discreteBit | initialSampleBit),
            .eventID        = 0,  // target event ID
            .padding2       = { 0 },
        };

        if (mParams.sampleInitialLuminances)
        {
            // initial luminance sampling
            pushConstants.eventID = 0;  // initial luminance sampling
            command->setPipeline(mRaytracePipeline);
            command->setBindGroup(0, mBindGroup.get());
            command->setPushConstant(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &pushConstants);
            command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
            return;  // early return
        }

        if (mParams.discrete)
        {
            // discrete event sampling
            pushConstants.eventID = 0;  // target event ID
            command->setPipeline(mRaytracePipeline);
            command->setBindGroup(0, mBindGroup.get());
            command->setPushConstant(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &pushConstants);
            command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
            return;  // early return
        }

        constexpr auto kShaderReadWriteBit = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        vk::ImageMemoryBarrier imgBarrier(kShaderReadWriteBit, kShaderReadWriteBit, vk::ImageLayout::eGeneral,
                                          vk::ImageLayout::eGeneral);
        imgBarrier.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

        command->setPipeline(mRaytracePipeline);
        command->setBindGroup(0, mBindGroup.get());

        // vk::MemoryBarrier barrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderWrite);

        // trace ray
        // for (uint32_t eventID = 0; eventID < 1; ++eventID) // DEBUG!!!
        for (uint32_t eventID = 0; eventID < mOutputImage->getVkExtent().depth * 8; ++eventID)
        {
            pushConstants.eventID = eventID;

            command->setPushConstant(vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &pushConstants);
            command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);

            // command->globalPipelineBarrier(barrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR, vk::PipelineStageFlagBits::eRayTracingShaderKHR);

            imgBarrier.setImage(mOutputImage->getVkImage().get());
            command->imagePipelineBarrier(imgBarrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                          vk::PipelineStageFlagBits::eRayTracingShaderKHR);
            imgBarrier.setImage(mPoolImage->getVkImage().get());
            command->imagePipelineBarrier(imgBarrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                          vk::PipelineStageFlagBits::eRayTracingShaderKHR);
        }
    }

}  // namespace evr
