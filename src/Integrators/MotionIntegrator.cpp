/*****************************************************************/ /**
 * @file   MotionIntegrator.cpp
 * @brief  source file of MotionIntegrator (interface) class
 * 
 * @author ichi-raven
 * @date   April 2025
 *********************************************************************/

#include "Integrators/MotionIntegrator.hpp"

#include "Mesh.hpp"
#include "Material.hpp"
#include "Transform.hpp"
#include "Emitter.hpp"
#include "AnimatedTransform.hpp"

#include <iostream>

#include <glm/gtx/string_cast.hpp>

namespace evr
{
    MotionIntegrator::MotionIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output)
        : Integrator(device, scene, output)
        , mEmitterNum(0)
    {
    }

    MotionIntegrator::~MotionIntegrator()
    {
    }

    void MotionIntegrator::buildDummyImage()
    {
        // dummy texture
        constexpr uint8_t kDummyColor[] = { 255, 0, 255, 255 };  // Magenta
        const auto format               = vk::Format::eR8G8B8A8Srgb;
        const uint32_t size             = vk2s::Compiler::getSizeOfFormat(format);  // 1 * 1

        vk::ImageCreateInfo ci;
        ci.arrayLayers   = 1;
        ci.extent        = vk::Extent3D(1, 1, 1);  // 1 * 1
        ci.format        = format;
        ci.imageType     = vk::ImageType::e2D;
        ci.mipLevels     = 1;
        ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        ci.initialLayout = vk::ImageLayout::eUndefined;

        // change format to pooling
        mDummyTexture = mDevice.createUnique<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size,
                                                          vk::ImageAspectFlagBits::eColor);
        mDummyTexture->write(kDummyColor, size);
    }

    void MotionIntegrator::buildSceneBuffer()
    {
        // create scene buffer
        const auto size = sizeof(SceneParams);
        mSceneBuffer    = mDevice.create<vk2s::Buffer>(
            vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer),
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    }

    void MotionIntegrator::buildInstanceBuffer(const double t0, const double t1)
    {
        const auto size = sizeof(MotionInstanceParams) * mScene.getAnimatedTransforms().size();

        const auto bci  = vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer);
        mInstanceBuffer = mDevice.create<vk2s::Buffer>(
            bci, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        updateInstanceBuffer(t0, t1);
    }

    void MotionIntegrator::updateInstanceBuffer(const double t0, const double t1)
    {
        const auto toParams = [](const Transform& transform)
        {
            return SRTParams{
                .scale       = glm::vec4(transform.scale, 0.0f),
                .shear       = glm::vec4(transform.shear, 0.0f),
                .rotation    = glm::vec4(transform.rot.x, transform.rot.y, transform.rot.z, transform.rot.w),
                .translation = glm::vec4(transform.pos, 0.0f),
            };
        };

        const auto& animTransforms = mScene.getAnimatedTransforms();

        std::vector<MotionInstanceParams> params;
        params.reserve(animTransforms.size());

        for (auto& animTrans : animTransforms)
        {
            auto delta = animTrans.getWorld(t0, t1);

            params.emplace_back(MotionInstanceParams{
                .transformT0 = toParams(delta.t0),
                .transformT1 = toParams(delta.t1),
            });
        }

        const auto size = sizeof(MotionInstanceParams) * params.size();
        mInstanceBuffer->write(params.data(), size);
    }

    void MotionIntegrator::buildMaterialBufferAndTextures()
    {
        const auto& materials = mScene.getMaterials();

        // TODO: inefficient
        std::vector<Material::Params> params;
        params.resize(materials.size());
        for (size_t i = 0; i < params.size(); ++i)
        {
            params[i] = materials[i].params;
        }

        const auto size = sizeof(Material::Params) * params.size();
        const auto bci  = vk::BufferCreateInfo(
            {}, size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
        mMaterialBuffer = mDevice.create<vk2s::Buffer>(bci, vk::MemoryPropertyFlagBits::eDeviceLocal);
        mMaterialBuffer->upload(params.data(), size);
    }

    void MotionIntegrator::buildSampler()
    {
        mSampler = mDevice.create<vk2s::Sampler>(vk::SamplerCreateInfo({}, vk::Filter::eLinear, vk::Filter::eLinear));
    }

    void MotionIntegrator::buildPoolImage(const vk::Format format)
    {
        const auto extent   = mOutputImage->getVkExtent();
        const uint32_t size = extent.width * extent.height * vk2s::Compiler::getSizeOfFormat(format);

        vk::ImageCreateInfo ci;
        ci.arrayLayers   = 1;
        ci.extent        = vk::Extent3D(extent.width, extent.height, 1);
        ci.format        = format;
        ci.imageType     = vk::ImageType::e2D;
        ci.mipLevels     = 1;
        ci.usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;
        ci.initialLayout = vk::ImageLayout::eUndefined;

        // change format to pooling
        mPoolImage = mDevice.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size,
                                                 vk::ImageAspectFlagBits::eColor);

        UniqueHandle<vk2s::Command> cmd = mDevice.createUnique<vk2s::Command>();
        cmd->begin(true);
        cmd->transitionImageLayout(mPoolImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
        cmd->end();
        cmd->executeAndWait();
    }

    void MotionIntegrator::buildEmitterBuffer(const double t0, const double t1)
    {
        const auto& emitters = mScene.getEmitters();
        mEmitterNum          = static_cast<uint32_t>(emitters.size());

        std::vector<Emitter::Params> params;
        params.reserve(std::max<size_t>(1, emitters.size()));
        for (const auto& emitter : emitters)
        {
            params.push_back(emitter.params);
        }

        if (params.empty())
        {
            params.emplace_back();  // dummy emitter
        }

        const auto size = sizeof(Emitter::Params) * params.size();
        const auto bci  = vk::BufferCreateInfo(
            {}, size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
        mEmittersBuffer = mDevice.create<vk2s::Buffer>(bci, vk::MemoryPropertyFlagBits::eDeviceLocal);
        mEmittersBuffer->upload(params.data(), size);
    }

    MotionIntegrator::Params& MotionIntegrator::getParamsRef()
    {
        return mParams;
    }

}  // namespace evr
