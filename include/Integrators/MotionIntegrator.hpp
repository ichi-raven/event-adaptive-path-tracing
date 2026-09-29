/*****************************************************************/ /**
 * @file   MotionIntegrator.hpp
 * @brief  Interface class for motion integrators
 * 
 * @author ichi-raven
 * @date   April 2025
 *********************************************************************/
#ifndef EVENTRENDERER_INCLUDE_INTEGRATORS_MOTIONINTEGRATOR_HPP_
#define EVENTRENDERER_INCLUDE_INTEGRATORS_MOTIONINTEGRATOR_HPP_

#include "Integrator.hpp"
#include "Thresholds.hpp"

#include <vector>
#include <optional>

namespace evr
{
    class MotionIntegrator : public Integrator
    {
    public:
        // can be modified from outside
        struct Params
        {
            int spp                      = 1;
            int accumulatedSpp           = 0;
            bool discrete                = false;
            bool useEXR                  = false;
            bool uselinLogL              = false;
            bool useMotionBlur           = false;
            bool sampleInitialLuminances = false;
            bool streamCompaction        = true;  // for ablation study
            bool test                    = true;  // for ablation study
            uint32_t timeSeed            = 0;
            Thresholds thresholds;
            bool generateGBuffer = false;  // needed for denoising
        };

        struct GpuTiming
        {
            bool referenceInitialization = false;

            double primaryRayTracingMs = 0.0;
            double streamCompactionMs  = 0.0;
            double eventRayTracingMs   = 0.0;
            double totalMs             = 0.0;

            std::vector<double> compactionPerEventMs;
            std::vector<double> rayTracingPerEventMs;
            std::vector<uint32_t> candidatePixelsPerEvent;
        };

    public:
        MotionIntegrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> output);

        virtual ~MotionIntegrator() override;

        // virtual void showConfigImGui() = 0;

        virtual void update(const double t0, const double t1, const uint32_t frameIndex) = 0;

        // virtual void updateShaderResources() = 0;

        // virtual void sample(Handle<vk2s::Command> command) = 0;

        Params& getParamsRef();

        virtual std::optional<GpuTiming> consumeGpuTiming()
        {
            return std::nullopt;
        }

    protected:
        virtual void buildDummyImage();

        virtual void buildSceneBuffer();

        virtual void buildInstanceBuffer(const double t0, const double t1);

        virtual void buildMaterialBufferAndTextures();

        virtual void buildSampler();

        virtual void buildPoolImage(const vk::Format format);

        virtual void buildEmitterBuffer(const double t0, const double t1);

        virtual void updateInstanceBuffer(const double t0, const double t1);

        //virtual void rebuildTLAS(const double t0, const double t1);

    protected:
        struct SceneParams  // std140
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            glm::vec4 camPos;

            uint32_t sppPerFrame;
            uint32_t allEmitterNum;
            float padding[2];
        };

        struct InstanceParams
        {
            glm::mat4 world;
            glm::mat4 worldInvTrans;
        };

        struct alignas(16) SRTParams
        {
            glm::vec4 scale;
            glm::vec4 shear;
            glm::vec4 rotation;
            glm::vec4 translation;
        };

        struct alignas(16) MotionInstanceParams
        {
            SRTParams transformT0;
            SRTParams transformT1;
        };

        static_assert(sizeof(SRTParams) == 64);
        static_assert(sizeof(MotionInstanceParams) == 128);

        uint32_t mEmitterNum;

        // TLAS
        Handle<vk2s::AccelerationStructure> mTLAS;

        // WARN: VB, IB and textures have no ownership
        std::vector<Handle<vk2s::Buffer>> mVertexBuffers;
        std::vector<Handle<vk2s::Buffer>> mIndexBuffers;
        //std::vector<Handle<vk2s::Image>> mTextures;

        // shader resources
        UniqueHandle<vk2s::Buffer> mSceneBuffer;
        UniqueHandle<vk2s::Buffer> mInstanceBuffer;
        UniqueHandle<vk2s::Buffer> mMaterialBuffer;
        UniqueHandle<vk2s::Buffer> mSampleBuffer;
        UniqueHandle<vk2s::Buffer> mEmittersBuffer;
        UniqueHandle<vk2s::Image> mPoolImage;
        UniqueHandle<vk2s::Sampler> mSampler;
        UniqueHandle<vk2s::Image> mDummyTexture;

        Params mParams;
    };
}  // namespace evr

#endif
