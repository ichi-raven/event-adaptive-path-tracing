#include "MotionIntegrator.slang"

[[vk::binding(1, 0)]]
RWTexture3D<uint32_t4> resultImage;

struct PushConstant
{
    float thresPos;
    float thresNeg;
    float thresSigma;
    float padding;

    uint32_t flags;
    uint32_t eventID;
    uint32_t timeSeed;
    uint32_t padding2;
}

[[vk::push_constant]]
PushConstant pushConstants;

[shader("raygeneration")]
void rayGenShader()
{
    const static bool kMIS = true;
    //const static bool kSampleLog   = true;
    const static bool kRandomThres = true;

    // flags
    let discrete      = (pushConstants.flags & 0x00000001) != 0;
    let initialSample = (pushConstants.flags & 0x00000002) != 0;

    let threadIdx   = DispatchRaysIndex().xy;
    let imageSize   = DispatchRaysDimensions().xy;
    let thresParams = ThresholdParams(pushConstants.thresPos, pushConstants.thresNeg, pushConstants.thresSigma);

    if (threadIdx.x >= DispatchRaysDimensions().x)
        return;
    if (threadIdx.y >= DispatchRaysDimensions().y)
        return;

    var sampler = IndependentSampler(threadIdx.x, threadIdx.y);
    // thresholds are specific to each pixel
    let thresholds = select(kRandomThres, Thresholds(sampler, thresParams), Thresholds(thresParams));
    let eventID    = pushConstants.eventID;
    // scramble with time seed
    //sampler.scramble(pushConstants.timeSeed);

    // pool initialize
    if (initialSample)
    {
        let invSpp = 1.0 / float(sceneParams.spp);
        float L    = 0.0;
        for (int sampleID = 0; sampleID < sceneParams.spp; ++sampleID)
        {
            L += invSpp * sampleEventLuminance<kMIS>(threadIdx, imageSize, uint(sampleID), 0.0);
        }

        poolImage[threadIdx.xy] = float4(L, 0.0, 0.0, 0.0);

        return;
    }

    let pool   = poolImage[threadIdx.xy].x;
    let nextT0 = poolImage[threadIdx.xy].y;

    // discrete event detection (baseline)
    if (discrete)
    {
        // high-fidelity event detection
        let event = detectEventDiscrete<kMIS>(threadIdx, imageSize, pool, thresholds);

        [ForceUnroll]
        for (int i = 0; i < kEventDepthChannel; ++i)
        {
            resultImage[uint32_t3(threadIdx.xy, i)] = event._0[i];
        }

        poolImage[threadIdx.xy] = float4(event._1, nextT0, 0., 0.);
        return;
    }

    // continuous event detection (baseline)
    if (eventID > 0 && nextT0 == 0.)
    {
        return;
    }

    let result = detectEventBaseline<kMIS>(threadIdx, imageSize, pool, thresholds, nextT0);
    {
        let endFlag             = eventID == (kMaxEventNum - 1) || !result._0.hasValue;
        poolImage[threadIdx.xy] = float4(result._1, select(endFlag, 0., result._2), 0, 0);
    }

    if (let event = result._0)
    {
        let texDepth          = eventID / kEachEventNum;
        let mod               = eventID % kEachEventNum;
        const uint32_t3 index = uint32_t3(threadIdx.xy, texDepth);
        resultImage[index]    = append(resultImage[index], event, mod);
    }
}
