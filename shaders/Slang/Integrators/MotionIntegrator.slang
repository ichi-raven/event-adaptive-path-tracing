
import "../Sampler/Sampler";
import "../Material/Material";
import "../Emitter/Emitter";
import "../Utility/SurfaceInteraction";
import "../Utility/Frame";
import "../Utility/Constants";
import "../Utility/Warp";

import "../Event/Event";
import "../Utility/Stack";

// event detection
static const int kEventDepthChannel = 2;                                   // WARN: always match the host side
static const int kEachEventNum      = 8;                                   // number of events per thread
static const int kMaxEventNum       = kEventDepthChannel * kEachEventNum;  // maximum number of events per thread
static const int kMaxDepth          = 15;                                  // maximum depth of event detection
static const int16_t kMaxIn16Bit    = int16_t((1 << kMaxDepth) - 1);
static const float kEpsilon         = 1. / 1024.;  // 1 / 1024 ~ 1e-3, 10 search depth

// bindings for motion integrators
[[vk::binding(0, 0)]]
RaytracingAccelerationStructure sceneBVH;
// WARN: result image type is depends on concrete integrator types
[[vk::binding(2, 0)]]
RWTexture2D<float4> poolImage;
[[vk::binding(3, 0)]]
ConstantBuffer<SceneParams> sceneParams;
[[vk::binding(4, 0)]]
StructuredBuffer<Vertex> vertices[];
[[vk::binding(5, 0)]]
StructuredBuffer<uint32_t> indices[];
[[vk::binding(6, 0)]]
StructuredBuffer<MotionInstanceParams> motionInstanceParams;
[[vk::binding(7, 0)]]
StructuredBuffer<MaterialParams> materialParams;
[[vk::binding(8, 0)]]
StructuredBuffer<EmitterParams> emitterParams;
[[vk::binding(9, 0)]]
Texture2D<float4> textures[];
[[vk::binding(10, 0)]]
SamplerState texSampler;

struct SceneParams
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float4 cameraPos;

    uint32_t spp;
    uint32_t allEmitterNum;
    float2 padding;
}

struct SRTParams
{
    float4 scale;
    float4 shear;
    float4 rotation;
    float4 translation;
}

float4x4 quat2rot(const float4 q)
{
    let xx = q.x * q.x;
    let yy = q.y * q.y;
    let zz = q.z * q.z;
    let xy = q.x * q.y;
    let xz = q.x * q.z;
    let yz = q.y * q.z;
    let xw = q.x * q.w;
    let yw = q.y * q.w;
    let zw = q.z * q.w;

    return float4x4(1.0 - 2.0 * (yy + zz), 2.0 * (xy - zw), 2.0 * (xz + yw), 0.0,  //
                    2.0 * (xy + zw), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - xw), 0.0,  //
                    2.0 * (xz - yw), 2.0 * (yz + xw), 1.0 - 2.0 * (xx + yy), 0.0,  //
                    0.0, 0.0, 0.0, 1.0);
}

float4x4 composeSRT(const SRTParams transform)
{
    let sx  = transform.scale.x;
    let sy  = transform.scale.y;
    let sz  = transform.scale.z;
    let shx = transform.shear.x;
    let shy = transform.shear.y;
    let shz = transform.shear.z;

    let scaleShear = float4x4(sx, shx, shy, 0.0,  //
                              0.0, sy, shz, 0.0,  //
                              0.0, 0.0, sz, 0.0,  //
                              0.0, 0.0, 0.0, 1.0);

    let q        = transform.rotation;
    let rotation = quat2rot(q);

    let tx = transform.translation.x;
    let ty = transform.translation.y;
    let tz = transform.translation.z;

    let translation = float4x4(1.0, 0.0, 0.0, tx,  //
                               0.0, 1.0, 0.0, ty,  //
                               0.0, 0.0, 1.0, tz,  //
                               0.0, 0.0, 0.0, 1.0);

    return mul(translation, mul(rotation, scaleShear));
}

struct MotionInstanceParams : IMotionInstance
{
    SRTParams transformT0;
    SRTParams transformT1;

    inline float4x4 world(const float time)
    {
        SRTParams transform;
        transform.scale       = lerp(transformT0.scale, transformT1.scale, time);
        transform.shear       = lerp(transformT0.shear, transformT1.shear, time);
        transform.rotation    = lerp(transformT0.rotation, transformT1.rotation, time);
        transform.translation = lerp(transformT0.translation, transformT1.translation, time);

        return composeSRT(transform);
    }
}

static const uint kTracePayloadHit      = 0x01;
static const uint kTracePayloadEmissive = 0x02;

struct TraceHit
{
    float3 pos;
    float3 ng;
    float2 uv;
    float area;
    uint instanceIndex;
    Frame frame;
}

struct TracePayload
{
    __init(const float time_)
    {
        hit      = {};
        emissive = float3(0.0);
        time     = time_;
        flags    = 0;
    }

    TraceHit hit;
    float3 emissive;
    float time;
    uint flags;
}

[ForceInline]
inline bool hasPayloadHit(const TracePayload payload)
{
    return (payload.flags & kTracePayloadHit) != 0;
}

[ForceInline]
inline bool hasPayloadEmissive(const TracePayload payload)
{
    return (payload.flags & kTracePayloadEmissive) != 0;
}

[ForceInline]
inline void setPayloadEmissive(inout TracePayload payload, const float3 emissive)
{
    payload.emissive = emissive;
    payload.flags |= kTracePayloadEmissive;
}

[ForceInline]
inline SurfaceInteraction makeSurfaceInteraction(const TracePayload payload, const RayDesc ray)
{
    return SurfaceInteraction(payload.hit.pos, -ray.Direction, payload.hit.frame.n, payload.hit.ng, payload.hit.uv,
                              payload.hit.area, payload.hit.instanceIndex, payload.hit.frame);
}

struct PathState
{
    __init(const IndependentSampler sampler_)
    {
        ctx     = BSDFContext();
        sampler = sampler_;
    }

    BSDFContext ctx;
    IndependentSampler sampler;
}

void resetForNextBounce(inout TracePayload payload, out Optional<BSDFSample> bsdfSample,
                        out Optional<EmitterSample> emitterSample)
{
    payload.emissive = float3(0.0);
    payload.flags    = 0;
    bsdfSample       = none;
    emitterSample    = none;
}

bool shouldFinish(const TracePayload payload, const Optional<SurfaceInteraction> si,
                  const Optional<BSDFSample> bsdfSample)
{
    if (!si.hasValue || hasPayloadEmissive(payload) || !bsdfSample.hasValue)
    {
        return true;
    }

    if (bsdfSample.value.pdf == 0.0)
    {
        return true;
    }

    return false;
}

struct Vertex : IVertex  // std140
{
    float3 pos;
    float u;
    float3 normal;
    float v;
    float4 tangent;

    property float2 uv
    {
        get {
            return float2(u, v);
        }
        set {
            u = newValue.x;
            v = newValue.y;
        }
    }

    static Vertex barycentric(const Vertex v1, const Vertex v2, const Vertex v3, const float2 barycentric)
    {
        let w = float3(1. - barycentric.x - barycentric.y, barycentric.x, barycentric.y);

        Vertex ret;
        ret.pos     = v1.pos * w.x + v2.pos * w.y + v3.pos * w.z;
        ret.normal  = v1.normal * w.x + v2.normal * w.y + v3.normal * w.z;
        ret.uv      = v1.uv * w.x + v2.uv * w.y + v3.uv * w.z;
        ret.tangent = v1.tangent * w.x + v2.tangent * w.y + v3.tangent * w.z;

        return ret;
    }
}

RayDesc getCameraRay(const uint2 threadIdx, const uint2 imageSize, const float2 sample2)
{
    // TODO: lens sampling
    let screenPos = (float2(threadIdx.xy) + sample2) / float2(imageSize);
    let d         = screenPos * 2.0 - 1.0;
    let target    = mul(sceneParams.projInv, float4(d.x, d.y, 1, 1));
    let direction = mul(sceneParams.viewInv, float4(target.xyz, 0)).xyz;

    RayDesc ray;
    ray.Origin    = sceneParams.cameraPos.xyz;
    ray.Direction = normalize(direction);
    ray.TMin      = k::eps;
    ray.TMax      = k::infty;
    return ray;
}

bool occluded(const float3 pos, const EmitterSample es, const float time)
{
    if (es.distance <= k::eps)
    {
        return false;
    }

    // trace shadow ray
    RayDesc shadowRay;
    shadowRay.Origin    = pos;
    shadowRay.Direction = es.to;
    shadowRay.TMin      = k::eps;
    shadowRay.TMax      = es.distance - k::eps;  // WARN: adhoc

    // payload, true if hit other object
    const static uint flags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    bool occluded           = true;
    TraceMotionRay(sceneBVH, flags, ~0, 0, 0, 1, shadowRay, time, occluded);

    return occluded;
}

[ForceInline]
inline float russianRoulette<Spectral>(const Spectral c)
    where Spectral : IFloat
{
    if (Spectral is float3)
    {
        let cf3 = (c as float3).value;
        return max(max(cf3.x, cf3.y), cf3.z);
    }
    else if (Spectral is float)
    {
        return (c as float).value;
    }

    return 0.;
}

[ForceInline]
inline Spectral cast<Spectral>(const float3 c)
    where Spectral : IFloat
{
    if (Spectral is float3)
    {
        return (c as Spectral).value;
    }
    else if (Spectral is float)
    {
        return (toGray(c) as Spectral).value;
    }

    return Spectral(0.0);
}

[ForceInline]
inline Spectral cast<Spectral>(const float x)
    where Spectral : IFloat
{
    if (Spectral is float3)
    {
        return (float3(x) as Spectral).value;
    }
    else if (Spectral is float)
    {
        return (x as Spectral).value;
    }

    return Spectral(0.0);
}

[ForceInline]
inline bool validate<Spectral>(const Spectral c)
    where Spectral : IFloat
{
    if (Spectral is float3)
    {
        let cf3 = (c as float3).value;
        return !any(isinf(cf3)) && !any(isnan(cf3));
    }
    else if (Spectral is float)
    {
        let cf1 = (c as float).value;
        return !isinf(cf1) && !isnan(cf1);
    }

    return false;
}

Spectral sample<Spectral, let enableMIS : bool>(inout IndependentSampler sampler, const uint2 threadId,
                                                const uint2 imageSize, const float time)
    where Spectral : IFloat
{
    float3 dummyAlbedo;
    float3 dummyNormal;
    return sample<Spectral, enableMIS>(sampler, threadId, imageSize, time, dummyAlbedo, dummyNormal);
}

float sampleLuminance<let enableMIS : bool>(inout IndependentSampler sampler, const uint2 threadId,
                                            const uint2 imageSize, const float time)
{
    const float3 rgb = sample<float3, enableMIS>(sampler, threadId, imageSize, time);
    return toGray(rgb);
}

float sampleEventLuminance<let enableMIS : bool>(const uint2 threadId, const uint2 imageSize, const uint sampleID,
                                                 const float time)
{
    let pixelID = threadId.y * imageSize.x + threadId.x;

    let seedLo = tea(pixelID, sampleID ^ 0xa511e9b3u);
    let seedHi = tea(pixelID, sampleID ^ 0x63d83595u);

    let seed = (uint64_t(seedHi) << 32) | uint64_t(seedLo);

    var sampler = IndependentSampler(seed, uint64_t(1));
    return sampleLuminance<enableMIS>(sampler, threadId, imageSize, time);
}

void afterTrace(inout TracePayload payload, const RayDesc ray, inout PathState pathState, const bool sampleOnlyEmissive,
                out Optional<SurfaceInteraction> isect, out Optional<BSDFSample> bsdfSample,
                out Optional<EmitterSample> emitterSample, out MaterialParams matParams)
{
    isect         = none;
    bsdfSample    = none;
    emitterSample = none;
    matParams     = MaterialParams();

    if (!hasPayloadHit(payload))
    {
        return;
    }

    let si = makeSurfaceInteraction(payload, ray);
    isect  = si;

    let instanceIndex = si.instanceIndex;

    // sample BSDF
    matParams = MaterialParams::loadWithTextures(materialParams[NonUniformResourceIndex(instanceIndex)], textures,
                                                 texSampler, si.uv);

    if (any(matParams.emissive > 0.0))
    {
        setPayloadEmissive(payload, matParams.emissive);
    }

    // skip emission-only object
    if (sampleOnlyEmissive || hasPayloadEmissive(payload))
    {
        return;
    }

    // normal BSDF sampling
    bsdfSample = DynamicMaterial::BSDF::sample(matParams, pathState.ctx, si.toLocal(), pathState.sampler);

    if (let bs = bsdfSample)
    {
        if (bs.pdf == 0.0)
        {
            bsdfSample = none;
        }
    }

    // NEE
    if (sceneParams.allEmitterNum > 0)
    {
        emitterSample = EmitterSampler::sample(emitterParams, vertices, indices, motionInstanceParams, textures,
                                               texSampler, si, payload.time, pathState.sampler);
    }
    else
    {
        emitterSample = none;
    }
}

Spectral sample<Spectral, let enableMIS : bool>(inout IndependentSampler sampler, const uint2 threadId,
                                                const uint2 imageSize, const float time, out float3 albedo,
                                                out float3 normal, const bool generateGBuffer = true)
    where Spectral : IFloat
{
    albedo = float3(0.0);
    normal = float3(0.0);

    Spectral L          = Spectral(0.0);
    Spectral beta       = Spectral(1.0);
    PathState pathState = PathState(sampler);

    TracePayload payload                  = TracePayload(time);
    Optional<SurfaceInteraction> isect    = none;
    Optional<BSDFSample> bsdfSample       = none;
    Optional<EmitterSample> emitterSample = none;
    MaterialParams resolvedMatParams      = MaterialParams();

    // trace primary ray
    RayDesc ray = getCameraRay(threadId.xy, imageSize, pathState.sampler.next2D());

    TraceMotionRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, time, payload);

    afterTrace(payload, ray, pathState, false, isect, bsdfSample, emitterSample, resolvedMatParams);

    // required for MIS (for primal hit)
    if (enableMIS && hasPayloadEmissive(payload))
    {
        L = L + beta * cast<Spectral>(payload.emissive);
    }

    if (generateGBuffer)
    {
        if (let si = isect)
        {
            albedo = resolvedMatParams.albedo;
            normal = si.ns;
        }
    }

    // trace ray
    for (int depth = 1; depth < k::maxDepth; ++depth)
    {
        if (shouldFinish(payload, isect, bsdfSample))
        {
            break;
        }

        let si = isect.value;
        let bs = bsdfSample.value;

        if (depth > 1)
        {
            let prr : float = clamp(russianRoulette(beta), 0.0, 0.95);
            if (pathState.sampler.next1D() >= prr)
            {
                break;
            }

            if (prr > 0.0)
            {
                beta = beta / Spectral(prr);
            }
        }

        TracePayload cachedNextPayload = TracePayload(time);

        if (enableMIS)
        {
            // NEE (direct light sampling)
            if (let es = emitterSample)
            {
                if (es.pdf > 0.0 && es.distance != 0.0)
                {
                    // evaluate BSDF first
                    let params = resolvedMatParams;
                    let wo     = si.frame.toLocal(es.to);
                    let f3     = DynamicMaterial.BSDF.eval(params, pathState.ctx, si.toLocal(), wo);

                    if (any(f3 > 0.0))
                    {
                        if (!occluded(si.pos, es,
                                      time))  // Slang (HLSL) does not support short circuit and occluded() is heavy
                        {
                            let cosine   = abs(dot(si.ns, es.to));
                            let lightCos = abs(dot(es.normal, -es.to));

                            // if infinite emitter, sampling space is direction (not point) -> jacobian is just 1.0
                            let jacobian = select(es.isInfinite, 1.0, lightCos / (es.distance * es.distance));
                            let G        = cosine * jacobian;

                            float misWeight = 1.0;
                            if (!es.isDelta)
                            {
                                let bsdfPdf =
                                    jacobian * DynamicMaterial::BSDF::pdf(params, pathState.ctx, si.toLocal(), wo);
                                misWeight = Warp::heuristic<k::MISHeuristicBeta>(es.pdf, { es.pdf, bsdfPdf });
                            }

                            // spectral cast
                            let f = cast<Spectral>(f3);
                            {
                                L = L + Spectral(misWeight * G / es.pdf) * f * beta * cast<Spectral>(es.emissive);
                            }
                        }
                    }
                }
            }

            // BSDF sampling
            {
                TracePayload bsdfPayload               = TracePayload(time);
                Optional<SurfaceInteraction> bsdfIsect = none;

                RayDesc bsdfRay;
                bsdfRay.Origin    = si.pos;
                bsdfRay.Direction = si.frame.toWorld(bs.wo);
                bsdfRay.TMin      = k::eps;
                bsdfRay.TMax      = k::infty;
                TraceMotionRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, bsdfRay, time, bsdfPayload);

                MaterialParams probeMatParams = MaterialParams();

                afterTrace(bsdfPayload, bsdfRay, pathState, true, bsdfIsect, bsdfSample, emitterSample, probeMatParams);

                cachedNextPayload = bsdfPayload;

                if (hasPayloadEmissive(bsdfPayload))
                {
                    uint32_t emitterNum = 0, stride = 0;
                    emitterParams.GetDimensions(emitterNum, stride);
                    let emitterPdf = select(bs.isSpecular(), 0.0, EmitterSampler.pdf(si, bsdfIsect, emitterNum));
                    let cosine     = abs(dot(si.ns, si.frame.toWorld(bs.wo)));
                    let MISWeight  = Warp::heuristic<k::MISHeuristicBeta>(bs.pdf, { emitterPdf, bs.pdf });
                    {
                        L = L + Spectral(MISWeight * cosine / bs.pdf) * cast<Spectral>(bs.f) * beta *
                                    cast<Spectral>(bsdfPayload.emissive);
                    }
                }
            }
        }

        if (!validate(L))
        {
            break;
        }

        // update parameters
        let cosine = abs(dot(si.ns, si.frame.toWorld(bs.wo)));
        beta       = beta * cast<Spectral>(bs.f) * Spectral(cosine / bs.pdf);

        ray.Origin    = si.pos;
        ray.Direction = si.frame.toWorld(bs.wo);
        ray.TMin      = k::eps;
        ray.TMax      = k::infty;
        resetForNextBounce(payload, bsdfSample, emitterSample);

        // Reuse the BSDF ray intersection when MIS already trace it
        if (enableMIS)
        {
            payload = cachedNextPayload;
        }
        else
        {
            TraceMotionRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, time, payload);
        }
        afterTrace(payload, ray, pathState, false, isect, bsdfSample, emitterSample, resolvedMatParams);
    }

    // required only if no explicit sampling(PT) is performed
    if (!enableMIS && hasPayloadEmissive(payload))
    {
        L = L + beta * cast<Spectral>(payload.emissive);
    }

    // Reflect sampler's transition
    sampler = pathState.sampler;

    // Reject invalid sample
    return select(validate(L), L, Spectral(0.0));
}

Tuple<uint4[kEventDepthChannel], float> detectEventDiscrete<let MIS : bool>(const uint2 threadId, const uint2 imageSize,
                                                                            const float previousL,
                                                                            const Thresholds thresholds)
{
    let invSpp = 1.0 / float(sceneParams.spp);

    uint16_t events[kMaxEventNum] = {};
    float observedL               = 0.0;
    float storedL                 = previousL;
    for (int sampleID = 0; sampleID < sceneParams.spp; ++sampleID)
    {
        observedL += invSpp * sampleEventLuminance<MIS>(threadId, imageSize, uint(sampleID), 1.0);
    }

    let Ldiff      = linLog(observedL) - linLog(storedL);
    let threshold  = select(Ldiff > 0.0, thresholds.pos, thresholds.neg);
    let eventNum   = int(abs(Ldiff) / threshold);
    float eventGap = 0.0;
    if (eventNum > 0)
    {
        eventGap = 1.0 / float(eventNum);
    }

    [unroll]
    for (int evsID = 0; evsID < min(kMaxEventNum, eventNum); ++evsID)
    {
        events[evsID] = pack(sign(Ldiff), eventGap * (evsID + 1.), kMaxIn16Bit);
    }

    if (eventNum > 0)
    {
        storedL = observedL;
    }

    uint4 rtn[kEventDepthChannel] = {};
    uint16_t tmp[kEachEventNum]   = {};

    [ForceUnroll]
    for (int i = 0; i < kEventDepthChannel; ++i)
    {
        for (int j = i * kEachEventNum; j < (i + 1) * kEachEventNum; ++j)
        {
            let idx  = j - i * kEachEventNum;
            tmp[idx] = events[j];
        }

        rtn[i] = packEvents(tmp);
    }

    return makeTuple(rtn, storedL);
}

Tuple<Optional<uint16_t>, float, float> detectEventBaseline<let MIS : bool>(const uint2 threadId, const uint2 imageSize,
                                                                            const float previousL,
                                                                            const Thresholds thresholds, const float t0)
{
    let invSpp = 1. / float(sceneParams.spp);
    var stack  = Stack<Range, 32>();

    Optional<uint16_t> event = none;
    float eventTime          = 0.;
    float storedL            = previousL;
    float observedL          = 0.;
    float Ldiff              = 0.;

    stack.clear();
    stack.push(Range(t0, 1.0));

    while (!stack.flood() && !stack.empty())
    {
        Range now = stack.pop();

        observedL = 0.0;

        for (int sampleID = 0; sampleID < sceneParams.spp; ++sampleID)
        {
            observedL += invSpp * sampleEventLuminance<MIS>(threadId, imageSize, uint(sampleID), now.t1);
        }

        Ldiff = linLog(observedL) - linLog(storedL);

        // no events occur within this range
        if (abs(Ldiff) < select(Ldiff > 0., thresholds.pos, thresholds.neg))
        {
            continue;
        }

        // events occured!
        let mid = now.mid();

        // reach the exact time of the event
        if (now.diff() <= kEpsilon)
        {
            event     = pack(sign(Ldiff), mid, kMaxIn16Bit);
            storedL   = observedL;
            eventTime = now.t1;
            break;
        }

        // LIFO
        stack.push(Range(mid, now.t1));
        stack.push(Range(now.t0, mid));
    }

    return makeTuple(event, storedL, eventTime);
}

// whether an event occurs, followed by the sampled luminace and square mean
Tuple<bool, float, float> checkEventEarly<let MIS : bool, let defaultSearchSpp : uint>(
    const uint2 threadId, const uint2 imageSize, const float previousL, const float previousVar,
    const Thresholds thresholds, const bool testing = true)
{
    let searchSpp = min(defaultSearchSpp, sceneParams.spp);

    float L          = 0.;
    float sqL        = 0.;
    let invSearchSpp = 1. / float(searchSpp);

    for (int sampleID = 0; sampleID < searchSpp; ++sampleID)
    {
        let tmp = sampleEventLuminance<MIS>(threadId, imageSize, uint(sampleID), 1.0);
        L += invSearchSpp * tmp;
        sqL += invSearchSpp * tmp * tmp;
    }

    //let Ldiff = L - previousL;
    let v         = sampleVariance(L, sqL, searchSpp);
    let threshold = select(L - previousL > 0., thresholds.pos, thresholds.neg);
    if (testing)
    {
        return makeTuple(tTest(L, previousL, v, previousVar, searchSpp, sceneParams.spp, threshold), L, sqL);
    }

    let linLogDiff = linLog(L) - linLog(previousL);
    return makeTuple(abs(linLogDiff) >= threshold, L, sqL);
}

Tuple<Optional<uint16_t>, float, float, float> detectEventEfficient<let MIS : bool, let defaultSearchSpp : uint>(
    const uint2 threadId, const uint2 imageSize, const float previousL, const float previousVar,
    const Thresholds thresholds, const float t0, const bool testing, const bool reuseEarlySamples, const float earlyL,
    const float earlySqL, const uint earlySampleCount, const bool collectStats, out uint4 searchStats)
{
    // x: visited ranges, y: newly traced luminance evaluations,
    // z: path samples (not individual rays), w: termination status.
    searchStats = uint4(0, 0, 0, 0);

    let searchSpp        = min(defaultSearchSpp, sceneParams.spp);
    let additionalSpp    = sceneParams.spp - searchSpp;
    let invSearchSpp     = 1. / float(searchSpp);
    let invAdditionalSpp = select(additionalSpp == 0, 0, 1. / float(additionalSpp));
    let ratio            = float(searchSpp) / float(sceneParams.spp);

    var stack = Stack<Range, 32>();

    float storedL     = previousL;
    float storedVar   = previousVar;
    float observedL   = 0.;
    float observedSqL = 0.;
    float Ldiff       = 0.;
    float linLogDiff  = 0.;
    bool initialRange = true;

    stack.clear();
    stack.push(Range(t0, 1.0));

    while (!stack.flood() && !stack.empty())
    {
        Range now = stack.pop();

        if (collectStats)
        {
            searchStats.x += 1;
        }

        observedL   = 0.0;
        observedSqL = 0.0;

        let useEarlySamples = reuseEarlySamples && initialRange && earlySampleCount == searchSpp;
        initialRange        = false;

        if (now.L1.hasValue && now.sqL1.hasValue)
        {
            observedL   = now.L1.value;
            observedSqL = now.sqL1.value;
        }
        else
        {
            // Full endpoint-cache hits do not trace new paths. An early
            // cache hit only counts if additional samples are needed.
            if (collectStats && (!useEarlySamples || additionalSpp > 0))
            {
                searchStats.y += 1;
            }

            if (useEarlySamples)
            {
                observedL   = earlyL;
                observedSqL = earlySqL;
            }
            else
            {
                if (collectStats)
                {
                    searchStats.z += searchSpp;
                }

                for (int sampleID = 0; sampleID < searchSpp; ++sampleID)
                {
                    let tmp = sampleEventLuminance<MIS>(threadId, imageSize, uint(sampleID), now.t1);
                    observedL += invSearchSpp * tmp;
                    observedSqL += invSearchSpp * tmp * tmp;
                }

                if (testing)
                {
                    let v         = sampleVariance(observedL, observedSqL, searchSpp);
                    let threshold = select(observedL - storedL > 0., thresholds.pos, thresholds.neg);
                    if (!tTest(observedL, storedL, v, storedVar, searchSpp, sceneParams.spp, threshold))
                    {
                        // no events occur within this range
                        continue;
                    }
                }
            }

            // serious exploration because “an event is likely to happen”
            // calc true Ldiff and observedL
            if (sceneParams.spp > searchSpp)
            {
                if (collectStats)
                {
                    searchStats.z += additionalSpp;
                }

                float additionalL   = 0;
                float additionalSqL = 0;
                for (int sampleID = searchSpp; sampleID < sceneParams.spp; ++sampleID)
                {
                    let tmp = sampleEventLuminance<MIS>(threadId, imageSize, uint(sampleID), now.t1);
                    additionalL += invAdditionalSpp * tmp;
                    additionalSqL += invAdditionalSpp * tmp * tmp;
                }

                observedL   = lerp(additionalL, observedL, ratio);
                observedSqL = lerp(additionalSqL, observedSqL, ratio);
            }

            // set cache
            now.L1   = observedL;
            now.sqL1 = observedSqL;
        }

        // fine sampled difference
        linLogDiff = linLog(observedL) - linLog(storedL);

        if (abs(linLogDiff) < select(linLogDiff > 0., thresholds.pos, thresholds.neg))
        {
            // no events occur within this range after all
            continue;
        }

        // events occured!-----
        let mid = now.mid();

        // reach the exact time of the event
        if (now.diff() <= kEpsilon)
        {
            storedL                  = observedL;
            storedVar                = sampleVariance(observedL, observedSqL, sceneParams.spp);
            Optional<uint16_t> event = pack(sign(linLogDiff), now.t0, kMaxIn16Bit);
            if (collectStats)
            {
                searchStats.w = 1;  // Event detected
            }
            return makeTuple(event, storedL, storedVar, now.t0);
        }

        // split the range
        // LIFO
        stack.push(Range(mid, now.t1, now.L1.value, now.sqL1.value));
        stack.push(Range(now.t0, mid));
    }

    if (collectStats)
    {
        searchStats.w = stack.flood() ? 3u : 2u;
    }

    Optional<uint16_t> empty = none;
    return makeTuple(empty, storedL, storedVar, 0.0);
}

// Preserve the non-profiling call signature for other integrators.
Tuple<Optional<uint16_t>, float, float, float> detectEventEfficient<let MIS : bool, let defaultSearchSpp : uint>(
    const uint2 threadId, const uint2 imageSize, const float previousL, const float previousVar,
    const Thresholds thresholds, const float t0, const bool testing, const bool reuseEarlySamples, const float earlyL,
    const float earlySqL, const uint earlySampleCount)
{
    uint4 unusedStats;
    return detectEventEfficient<MIS, defaultSearchSpp>(threadId, imageSize, previousL, previousVar, thresholds, t0,
                                                      testing, reuseEarlySamples, earlyL, earlySqL, earlySampleCount,
                                                      false, unusedStats);
}

[shader("miss")]
void missShader(inout TracePayload payload: SV_RayPayload)
{
    payload.emissive = float3(0.0);
    payload.flags    = 0;

    uint emitterCount  = 0;
    uint emitterStride = 0;
    emitterParams.GetDimensions(emitterCount, emitterStride);

    if (sceneParams.allEmitterNum == 0)
    {
        return;
    }

    // infinite emitter
    // NOTE: here assumes the index for infinite emitter is always 0
    if (emitterParams[0].type == EmitterType::Infinite)
    {
        if (emitterParams[0].texIndex == -1)  // constant emissive
        {
            payload.emissive = emitterParams[0].emissive;
        }
        else  // envmap
        {
            let dir = normalize(WorldRayDirection());

            if (abs(dir.x) <= 1e-6 && abs(dir.z) <= 1e-6)
            {
                payload.emissive = float3(0.);
                return;
            }

            let phi   = k::invTwoPi * atan2(dir.z, dir.x);
            let theta = k::invPi * acos(clamp(dir.y, -1.0, 1.0));
            let angle = float2(phi, theta);

            payload.emissive =
                textures[NonUniformResourceIndex(emitterParams[0].texIndex)].SampleLevel(texSampler, angle, 0.0).xyz;
        }
    }
}

[shader("miss")]
void shadowMissShader(inout bool occluded: SV_RayPayload)
{
    occluded = false;
}

[shader("closesthit")]
void closestHitShader(inout TracePayload payload: SV_RayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
    let worldRayDir = WorldRayDirection();
    let hitLocation = WorldRayOrigin() + worldRayDir * RayTCurrent();

    let instanceIndex  = InstanceIndex();
    let primitiveIndex = PrimitiveIndex();

    let index = uint3(indices[instanceIndex][primitiveIndex * 3 + 0], indices[instanceIndex][primitiveIndex * 3 + 1],
                      indices[instanceIndex][primitiveIndex * 3 + 2]);

    let v0     = vertices[instanceIndex][index.x];
    let v1     = vertices[instanceIndex][index.y];
    let v2     = vertices[instanceIndex][index.z];
    let vertex = Vertex.barycentric(v0, v1, v2, attr.barycentrics);

    let worldPos = hitLocation;

    let baseWorldNs = normalize(mul(vertex.normal, WorldToObject3x4()).xyz);
    let worldMat    = motionInstanceParams[instanceIndex].world(payload.time);

    let p0 = mul(worldMat, float4(v0.pos, 1.0)).xyz;
    let p1 = mul(worldMat, float4(v1.pos, 1.0)).xyz;
    let p2 = mul(worldMat, float4(v2.pos, 1.0)).xyz;

    let area    = getArea(p0, p1, p2);
    let worldNg = normalize(cross(p1 - p0, p2 - p0));

    var worldNs = baseWorldNs;
    var frame   = Frame(baseWorldNs);

    let mat        = materialParams[NonUniformResourceIndex(instanceIndex)];
    let hasTangent = dot(vertex.tangent.xyz, vertex.tangent.xyz) > k::eps;

    if (hasTangent)
    {
        var worldT = mul(worldMat, float4(vertex.tangent.xyz, 0.0)).xyz;
        worldT -= baseWorldNs * dot(baseWorldNs, worldT);

        if (dot(worldT, worldT) > k::eps)
        {
            worldT = normalize(worldT);

            let objHandedness = select(vertex.tangent.w < 0.0, -1.0, 1.0);
            let handedness    = objHandedness * sign(determinant((float3x3)worldMat));
            let worldB        = handedness * normalize(cross(baseWorldNs, worldT));

            if (mat.normalmapTexIndex != k::invalidTexIndex)
            {
                let normalTex = textures[NonUniformResourceIndex(mat.normalmapTexIndex)]
                                    .SampleLevel(texSampler, vertex.uv, 0.0)
                                    .xyz;

                let tangentNs = normalize(normalTex * 2.0 - 1.0);

                worldNs = normalize(tangentNs.x * worldT + tangentNs.y * worldB + tangentNs.z * baseWorldNs);
            }

            var mappedT = worldT - worldNs * dot(worldNs, worldT);
            if (dot(mappedT, mappedT) > k::eps)
            {
                mappedT     = normalize(mappedT);
                let mappedB = handedness * normalize(cross(worldNs, mappedT));

                frame = Frame(worldNs, mappedT, mappedB);
            }
            else
            {
                frame = Frame(worldNs);
            }
        }
    }

    payload.hit.pos           = worldPos;
    payload.hit.ng            = worldNg;
    payload.hit.uv            = vertex.uv;
    payload.hit.area          = area;
    payload.hit.instanceIndex = instanceIndex;
    payload.hit.frame         = frame;

    payload.emissive = float3(0.0);
    payload.flags    = kTracePayloadHit;
}
