/*****************************************************************/ /**
 * @file   EfficientEventIntegrator.hpp
 * @brief  header file of efficient event integrator class
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef EVENTRENDERER_INCLUDE_EFFICIENTEVENTINTEGRATOR_HPP_
#define EVENTRENDERER_INCLUDE_EFFICIENTEVENTINTEGRATOR_HPP_

#include "Thresholds.hpp"
#include "Scene.hpp"

#include <Integrators/MotionIntegrator.hpp>

#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>

#include <cstdint>
#include <vector>

namespace evr
{
    class EfficientEventIntegrator : public MotionIntegrator
    {
    public:
        EfficientEventIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output);

        virtual ~EfficientEventIntegrator() override;

        virtual void showConfigImGui() override;

        virtual void update(const double t0, const double t1, const uint32_t frameIndex) override;

        virtual void updateShaderResources() override;

        virtual void sample(Handle<vk2s::Command> command) override;

        void sampleBatched(Handle<vk2s::Command> command, uint32_t batchSize);

        virtual std::optional<GpuTiming> consumeGpuTiming() override;

        struct EventSearchStats
        {
            uint32_t visitedRanges;
            uint32_t luminanceEvaluations;
            uint32_t pathSamples;
            uint32_t status;  // 0: untouched, 1: event, 2: no event, 3: stack full
        };

        static_assert(sizeof(EventSearchStats) == 16);

        void setSearchStatsEnabled(const bool enabled)
        {
            mSearchStatsEnabled = enabled;
        }

        // Call after the frame's GPU work has completed, before the next sample().
        // Slots are indexed by original pixel ID; only eventID == 0 is recorded.
        std::vector<EventSearchStats> consumeEventSearchStats();

    private:
        void sampleImpl(Handle<vk2s::Command> command, uint32_t batchSize);

        // shader groups
        constexpr static int kIndexRaygen     = 0;
        constexpr static int kIndexMiss       = 1;
        constexpr static int kIndexShadow     = 2;
        constexpr static int kIndexClosestHit = 3;

        struct RaytracingPushConstants
        {
            alignas(8) glm::uvec2 imageSize;  // Offset 0, Size 8
            alignas(4) uint32_t eventID;      // Offset 8, Size 4
            alignas(4) uint32_t timeSeed;     // Offset 12, Size 4

            alignas(4) uint32_t flags;        // Offset 16, Size 4
            alignas(4) float thresholdPos;    // Offset 20, Size 4
            alignas(4) float thresholdNeg;    // Offset 24, Size 4
            alignas(4) float thresholdSigma;  // Offset 28, Size 4
            alignas(4) uint32_t candidateOffset;
            alignas(4) uint32_t reserved;
        };

        static_assert(sizeof(RaytracingPushConstants) == 40);

        struct ComputePushConstants
        {
            alignas(8) glm::uvec2 imageSize;
            alignas(4) uint32_t eventID;
            alignas(4) uint32_t padding;
        };

        static_assert(sizeof(ComputePushConstants) == 16);

        struct EarlySampleCache
        {
            float luminance;
            float squareLuminance;
            uint32_t sampleCount;
            uint32_t reserved;
        };

        static_assert(sizeof(EarlySampleCache) == 16);

        struct StreamCompaction
        {
            constexpr static int kThreadGroupSize = 512;

            UniqueHandle<vk2s::BindLayout> bindLayout;
            UniqueHandle<vk2s::BindGroup> bindGroup;

            UniqueHandle<vk2s::Pipeline> prefixSum1Pipeline;
            UniqueHandle<vk2s::Pipeline> blockSumScanPipeline;
            UniqueHandle<vk2s::Pipeline> prefixSum2Pipeline;
            UniqueHandle<vk2s::Pipeline> compactionPipeline;

            UniqueHandle<vk2s::Buffer> prefixSumBuffer;
            UniqueHandle<vk2s::Buffer> blockSumBuffer;
            UniqueHandle<vk2s::Buffer> resultBuffer;
            UniqueHandle<vk2s::Buffer> indirectBuffer;
            UniqueHandle<vk2s::Buffer> candidateCountBuffer;
            UniqueHandle<vk2s::Buffer> earlySampleCacheBuffer;
            UniqueHandle<vk2s::Image> flagImage;
        };

        // binding
        UniqueHandle<vk2s::BindLayout> mBindLayout;
        UniqueHandle<vk2s::BindGroup> mBindGroup;

        // ray tracing pipeline and SBT
        UniqueHandle<vk2s::Pipeline> mRaytracePipeline;
        UniqueHandle<vk2s::ShaderBindingTable> mShaderBindingTable;

        // compute resources
        StreamCompaction mStreamCompaction;

        UniqueHandle<vk2s::Buffer> mEventSearchStatsBuffer;
        UniqueHandle<vk2s::Buffer> mEventSearchStatsReadbackBuffer;

        bool mSearchStatsEnabled  = false;
        bool mSearchStatsRecorded = false;

        // GPU timestamp queries
        vk::UniqueQueryPool mTimestampQueryPool;

        uint32_t mTimestampQueryCapacity = 0;
        uint32_t mRecordedTimestampCount = 0;
        uint32_t mRecordedEventCount     = 0;
        uint32_t mTimestampValidBits     = 0;

        double mTimestampPeriodNs      = 0.0;
        bool mRecordedReference        = false;
        bool mRecordedStreamCompaction = false;
    };
}  // namespace evr

#endif
