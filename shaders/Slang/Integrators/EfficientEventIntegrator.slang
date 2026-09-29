#include "MotionIntegrator.slang"
import "../Sampler/Sampler";

[[vk::binding(1, 0)]]
RWTexture3D<uint4> resultImage;

[[vk::binding(11, 0)]]
StructuredBuffer<uint2> indexMapping;
[[vk::binding(12, 0)]]
RWTexture2D<uint8_t> streamCompactionFlagImage;

struct EarlySampleCache
{
    float luminance;
    float squareLuminance;
    uint sampleCount;
    uint reserved;
}

[[vk::binding(13, 0)]]
RWStructuredBuffer<EarlySampleCache> earlySampleCache;

[[vk::binding(14, 0)]]
RWStructuredBuffer<uint4> eventSearchStats;

struct PushConstant
{
    uint32_t2 imageSize;  // Offset 0
    uint32_t eventID;     // Offset 8
    uint32_t timeSeed;    // Offset 12
    uint32_t flags;       // Offset 16
    float thresPos;       // Offset 20
    float thresNeg;       // Offset 24
    float thresSigma;     // Offset 28
    uint32_t candidateOffset;
    uint32_t reserved;
}

[[push_constant]]
PushConstant pushConstants;

[ForceInline]
inline uint flatten(const uint2 id, const uint width)
{
    return id.y * width + id.x;
}

[ForceInline]
inline uint2 restore(const uint id, const uint width)
{
    return uint2(id % width, id / width);
}

[shader("raygeneration")]
void rayGenShader()
{
    const static bool kMIS = true;
    //const static bool kSampleLog        = true;
    const static bool kRandomThres      = true;
    const static uint kDefaultSearchSpp = 256;

    // flags
    let indirect         = (pushConstants.flags & 0x00000001) != 0;
    let initialSample    = (pushConstants.flags & 0x00000002) != 0;
    let streamCompaction = (pushConstants.flags & 0x00000004) != 0;
    let testing          = (pushConstants.flags & 0x00000008) != 0;
    let collectStats     = (pushConstants.flags & 0x00000010) != 0;

    let threadIdx   = DispatchRaysIndex().xy;
    let imageSize   = pushConstants.imageSize;
    let width       = imageSize.x;
    let height      = imageSize.y;
    let eventID     = pushConstants.eventID;
    let thresParams = ThresholdParams(pushConstants.thresPos, pushConstants.thresNeg, pushConstants.thresSigma);

    if (initialSample)
    {
        if (threadIdx.x >= DispatchRaysDimensions().x)
            return;

        if (threadIdx.y >= DispatchRaysDimensions().y)
            return;

        var sampler    = IndependentSampler(threadIdx.x, threadIdx.y);
        let thresholds = select(kRandomThres, Thresholds(sampler, thresParams), Thresholds(thresParams));

        float L    = 0.0;
        float sqL  = 0.0;
        let invSpp = 1.0 / float(sceneParams.spp);

        for (int sampleID = 0; sampleID < sceneParams.spp; ++sampleID)
        {
            let tmp = sampleEventLuminance<kMIS>(threadIdx, imageSize, uint(sampleID), 0.0);
            L += invSpp * tmp;
            sqL += invSpp * tmp * tmp;
        }

        let v = sampleVariance(L, sqL, sceneParams.spp);

        poolImage[threadIdx.xy] = float4(L, v, 0.0, 0.0);
        [ForceUnroll]
        for (int i = 0; i < kEventDepthChannel; ++i)
        {
            resultImage[uint3(threadIdx.xy, i)] = uint4(0);
        }

        return;
    }

    // first sampling for stream compaction
    if (!indirect)
    {
        if (streamCompaction && testing)
        {
            if (threadIdx.x >= DispatchRaysDimensions().x)
                return;

            if (threadIdx.y >= DispatchRaysDimensions().y)
                return;

            var sampler    = IndependentSampler(threadIdx.x, threadIdx.y);
            let thresholds = select(kRandomThres, Thresholds(sampler, thresParams), Thresholds(thresParams));

            let pool    = poolImage[threadIdx.xy].x;
            let poolVar = poolImage[threadIdx.xy].y;

            let earlyResult = checkEventEarly<kMIS, kDefaultSearchSpp>(threadIdx, imageSize, pool, poolVar, thresholds);

            EarlySampleCache cache;
            cache.luminance       = earlyResult._1;
            cache.squareLuminance = earlyResult._2;
            cache.sampleCount     = min(kDefaultSearchSpp, sceneParams.spp);
            cache.reserved        = 0;

            earlySampleCache[flatten(threadIdx, width)] = cache;

            streamCompactionFlagImage[threadIdx.xy] = select(earlyResult._0, uint8_t(1), uint8_t(0));
        }
        else
        {
            if (threadIdx.x >= DispatchRaysDimensions().x)
                return;

            if (threadIdx.y >= DispatchRaysDimensions().y)
                return;

            streamCompactionFlagImage[threadIdx.xy] = uint8_t(1);
        }

        return;
    }

    // indirect (event detection)
    if (threadIdx.x >= DispatchRaysDimensions().x)
    {
        return;
    }

    let candidateIdx = pushConstants.candidateOffset + threadIdx.x;

    let sampleIdx = select(streamCompaction, indexMapping[candidateIdx].xy, restore(candidateIdx, width));

    var sampler    = IndependentSampler(sampleIdx.x, sampleIdx.y);
    let thresholds = select(kRandomThres, Thresholds(sampler, thresParams), Thresholds(thresParams));

    if (candidateIdx != 0 && all(sampleIdx == 0))
    {
        return;
    }

    let pool    = poolImage[sampleIdx.xy].x;
    let poolVar = poolImage[sampleIdx.xy].y;
    let nextT0  = poolImage[sampleIdx.xy].z;

    if (!streamCompaction)
    {
        if (eventID > 0 && nextT0 == 0.0)
        {
            return;
        }
    }

    let reuseEarlySamples = streamCompaction && testing && eventID == 0;

    EarlySampleCache cache = {};
    if (reuseEarlySamples)
    {
        cache = earlySampleCache[flatten(sampleIdx, width)];
    }

    uint4 searchStats;
    let result = detectEventEfficient<kMIS, kDefaultSearchSpp>(sampleIdx, imageSize, pool, poolVar, thresholds, nextT0,
                                                               testing, reuseEarlySamples, cache.luminance,
                                                               cache.squareLuminance, cache.sampleCount, collectStats,
                                                               searchStats);

    if (collectStats)
    {
        // Each candidate owns one original-pixel slot; no atomics are needed.
        eventSearchStats[flatten(sampleIdx, width)] = searchStats;
    }

    {
        let endFlag                             = eventID == (kMaxEventNum - 1) || !result._0.hasValue;
        streamCompactionFlagImage[sampleIdx.xy] = uint8_t(result._0.hasValue);
        poolImage[sampleIdx.xy]                 = float4(result._1, result._2, select(endFlag, 0., result._3), 0);
    }

    if (let event = result._0)
    {
        let texDepth       = eventID / kEachEventNum;
        let mod            = eventID % kEachEventNum;
        const uint3 index  = uint3(sampleIdx.xy, texDepth);
        resultImage[index] = append(resultImage[index], event, mod);
    }
}
