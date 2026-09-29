/*****************************************************************/ /**
 * @file   EventIntegrator.hpp
 * @brief  header file of event integrator class
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_EVENTINTEGRATOR_HPP_
#define PALM_INCLUDE_EVENTINTEGRATOR_HPP_

#include "Thresholds.hpp"

#include <Integrators/MotionIntegrator.hpp>

#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>

namespace evr
{
    class EventIntegrator : public MotionIntegrator
    {
    public:
        EventIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output);

        virtual ~EventIntegrator() override;

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
            float thresholdPos;
            float thresholdNeg;
            float thresholdSigma;
            float padding;
            // bit 1: discrete or search, bit2: whether initial pool image sampling or not
            uint32_t flags;
            uint32_t eventID;      // target event ID
            uint32_t padding2[2];  // padding to 16 bytes
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
