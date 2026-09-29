module Emitter;

import "../Utility/Constants";
import "../Utility/Warp";
import "../Utility/SurfaceInteraction";
import "../Sampler/Sampler";

public struct EmitterSample
{
    public float pdf       = 1.0;
    public float3 emissive = k::black;
    public float3 to       = k::zeros.xyz;
    public float distance  = 0.0;
    public float3 normal   = float3(0., 0., 1.);
    public bool isInfinite = false;
    public bool isDelta    = false;
}

// **always synchronize with CPU side**
public enum EmitterType
{
    Point    = 0,
    Area     = 1,
    Infinite = 2,
}

// all parameters bound from CPU, **always synchronize with CPU side**
public struct EmitterParams
{
    public float3 pos     = k::zeros.xyz;
    public float padding0 = 0.0;  // for 16-byte alignment

    public int32_t faceNum        = 0;
    public int32_t meshIndex      = -1;
    public int32_t primitiveIndex = -1;  // for area emitter, the index of the triangle in the mesh
    public int32_t padding        = 0;

    public float3 emissive = k::zeros.xyz;
    public float padding1  = 0.0;  // for 16-byte alignment

    public int32_t type      = EmitterType::Point;
    public int32_t texIndex  = -1;
    public int32_t2 padding2 = int32_t2(0, 0);
}

public interface IVertex
{
    public property float3 pos { get; set; }
    public property float3 normal { get; set; }
}

public interface IMotionInstance
{
    public float4x4 world(const float time);
}

public class EmitterSampler
{
    // for motion blur
    struct MotionFace<V : IVertex, I : IMotionInstance>
    {
        __init(V v0_, V v1_, V v2_, I motionInstance, const float time)
        {
            let worldMat = motionInstance.world(time);

            v0 = mul(worldMat, float4(v0_.pos, 1.)).xyz;
            v1 = mul(worldMat, float4(v1_.pos, 1.)).xyz;
            v2 = mul(worldMat, float4(v2_.pos, 1.)).xyz;

            let outer = cross(v1 - v0, v2 - v0);
            let len   = length(outer);
            area      = 0.5 * len;
            normal    = len > k::eps ? outer / len : float3(0.0);
        }

        float3 sample(float2 sample2)
        {
            if (sample2.x + sample2.y > 1.0)
            {
                sample2.x = (1.0 - sample2.x);
                sample2.y = (1.0 - sample2.y);
            }

            let w = float3(sample2.x, sample2.y, 1.0 - sample2.x - sample2.y);

            return v0 * w.x + v1 * w.y + v2 * w.z;
        }

        float3 v0;
        float3 v1;
        float3 v2;
        float area;
        float3 normal;
    }

    public static Optional<EmitterSample> sample<V : IVertex, I : IMotionInstance, S : ISampler>(
        StructuredBuffer<EmitterParams> params, StructuredBuffer<V> vertices[], StructuredBuffer<uint32_t> indices[],
        StructuredBuffer<I> instances, Texture2D<float4> textures[], SamplerState texSampler,
        const SurfaceInteraction si, const float time, inout S sampler)
    {
        let sample1 = sampler.next1D();
        let sample2 = sampler.next2D();

        // TODO: should be originally sampled based on distance and area
        uint emitterCount  = 0;
        uint emitterStride = 0;
        params.GetDimensions(emitterCount, emitterStride);

        if (emitterCount == 0)
        {
            return none;
        }

        let emitterIndex = min(uint(emitterCount * sample1), emitterCount - 1);
        let sampled      = params[emitterIndex];
        let selectPdf    = 1.0 / float(emitterCount);

        EmitterSample ret = EmitterSample();

        switch (sampled.type)
        {
        case EmitterType::Point:
        {
            ret.distance = distance(si.pos, sampled.pos);
            if (ret.distance <= k::eps)
            {
                return none;
            }

            ret.pdf        = selectPdf;
            ret.emissive   = sampled.emissive;
            ret.to         = (sampled.pos - si.pos) / ret.distance;  // normalize
            ret.normal     = -ret.to;
            ret.isInfinite = false;
            ret.isDelta    = true;
            break;
        }

        case EmitterType::Area:
        {
            // TODO: more effective sampling
            let meshIndex      = sampled.meshIndex;
            let primitiveIndex = sampled.primitiveIndex;
            let index = uint3(indices[meshIndex][primitiveIndex * 3 + 0], indices[meshIndex][primitiveIndex * 3 + 1],
                              indices[meshIndex][primitiveIndex * 3 + 2]);
            let face  = MotionFace<V, I>(vertices[meshIndex][index.x], vertices[meshIndex][index.y],
                                        vertices[meshIndex][index.z], instances[meshIndex], time);
            if (face.area <= k::eps)
            {
                return none;
            }

            let pos      = face.sample(sample2);
            ret.distance = distance(si.pos, pos);
            if (ret.distance <= k::eps)
            {
                return none;
            }

            ret.pdf        = selectPdf / face.area;
            ret.emissive   = sampled.emissive;
            ret.to         = (pos - si.pos) / ret.distance;
            ret.normal     = face.normal;
            ret.isInfinite = false;
            ret.isDelta    = false;
            break;
        }

        case EmitterType::Infinite:

            // TODO: uniform
            ret.pdf        = selectPdf * k::invFourPi;  // WARN: directional pdf
            ret.distance   = k::infty;
            ret.to         = si.frame.toWorld(Warp::toUniformSphere(sample2));
            ret.normal     = -ret.to;
            ret.isInfinite = true;
            ret.isDelta    = false;

            if (sampled.texIndex == -1)  // constant emissive
            {
                ret.emissive = sampled.emissive;
            }
            else  // envmap
            {
                // sample the direction of ret.to
                let dir = normalize(ret.to);
                if (abs(dir.x) <= 1e-6 && abs(dir.z) <= 1e-6)
                {
                    ret.emissive = float3(0.);
                    return ret;
                }

                let phi   = k::invTwoPi * atan2(dir.z, dir.x);
                let theta = k::invPi * acos(clamp(dir.y, -1.0, 1.0));
                let angle = float2(phi, theta);

                ret.emissive =
                    textures[NonUniformResourceIndex(sampled.texIndex)].SampleLevel(texSampler, angle, 0.0).xyz;
            }
            break;

        default:  // invalid
            return none;
        }

        return ret;
    }

    // directional pdf
    public static float pdf(const SurfaceInteraction si, const Optional<SurfaceInteraction> bsdfSi,
                            const uint32_t emitterNum)
    {
        if (emitterNum == 0)
        {
            return 0.0;
        }

        if (let bsi = bsdfSi)
        {
            let to       = bsi.pos - si.pos;
            let distSq   = dot(to, to);
            let lightCos = abs(dot(normalize(-to), bsi.ng));
            if (distSq <= k::eps || lightCos <= k::eps || bsi.area <= k::eps)
            {
                return 0.0;
            }

            let invJacobian = distSq / lightCos;
            return invJacobian / bsi.area / float(emitterNum);
        }

        return k::invFourPi / float(emitterNum);
    }
}
