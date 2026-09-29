/*****************************************************************/ /**
 * @file   MotionPathIntegrator.hpp
 * @brief  header file of path integrator class
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef EVENTRENDERER_INCLUDE_MOTIONPATHINTEGRATOR_HPP_
#define EVENTRENDERER_INCLUDE_MOTIONPATHINTEGRATOR_HPP_

#include <Integrators/MotionIntegrator.hpp>

#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>

namespace evr
{
    class MotionPathIntegrator : public MotionIntegrator
    {
    public:
        MotionPathIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output, Handle<vk2s::Image> albedo,
                             Handle<vk2s::Image> normal);

        virtual ~MotionPathIntegrator() override;

        virtual void showConfigImGui() override;

        virtual void update(const double t0, const double t1, const uint32_t frameIndex) override;

        virtual void updateShaderResources() override;

        virtual void sample(Handle<vk2s::Command> command) override;

    private:
        // shader groups
        constexpr static int kIndexRaygen     = 0;
        constexpr static int kIndexMiss       = 1;
        constexpr static int kIndexShadow     = 2;
        constexpr static int kIndexClosestHit = 3;

    private:
        struct PushConstants
        {
            uint32_t timeSeed = 0;
            // bit 1: linear or linLog, bit2: whether apply tone-mapping, bit3: whether apply motion-blur
            uint32_t renderMode = 0;
        };

        // binding
        UniqueHandle<vk2s::BindLayout> mBindLayout;
        UniqueHandle<vk2s::BindGroup> mBindGroup;

        // pipeline and SBT
        UniqueHandle<vk2s::Pipeline> mRaytracePipeline;
        UniqueHandle<vk2s::ShaderBindingTable> mShaderBindingTable;
    };
}  // namespace evr

#endif
