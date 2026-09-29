
#include "MotionIntegrator.slang"
import "../Sampler/Sampler";

[[vk::binding(1, 0)]]
RWTexture2D resultImage;
[[vk::binding(11, 0)]]
RWTexture2D<float4> albedoImage;
[[vk::binding(12, 0)]]
RWTexture2D<float4> normalImage;

struct PushConstant
{
    uint32_t timeSeed;
    uint32_t renderMode;
}

[[vk::push_constant]]
PushConstant pushConstants;

[shader("raygeneration")]
void rayGenShader()
{
    const static bool kEnableMIS = true;

    let threadIdx = DispatchRaysIndex().xy;
    let imageSize = DispatchRaysDimensions().xy;
    if (threadIdx.x >= DispatchRaysDimensions().x)
        return;
    if (threadIdx.y >= DispatchRaysDimensions().y)
        return;

    // flags
    let linLogL         = (pushConstants.renderMode & 0x00000001) != 0;
    let toneMapping     = (pushConstants.renderMode & 0x00000002) != 0;
    let motionblur      = (pushConstants.renderMode & 0x00000004) != 0;
    let generateGBuffer = (pushConstants.renderMode & 0x00000008) != 0;

    float L       = 0.0;
    float sqL     = 0.0;
    float3 rgb    = float3(0.0);
    float3 albedo = float3(0.0);
    float3 normal = float3(0.0);

    let invSpp    = 1.0 / float(sceneParams.spp);
    let pixelSeed = threadIdx.y * DispatchRaysDimensions().x + threadIdx.x;
    var sampler   = IndependentSampler(pixelSeed, pushConstants.timeSeed);

    for (int sampleID = 0; sampleID < sceneParams.spp; ++sampleID)
    {
        float3 tmpAlbedo = float3(0.0);
        float3 tmpNormal = float3(0.0);
        let time         = select(motionblur, sampler.next1D(), 0.0);
        let tmp =
            sample<float3, kEnableMIS>(sampler, threadIdx, imageSize, time, tmpAlbedo, tmpNormal, generateGBuffer);

        rgb += invSpp * tmp;

        if (generateGBuffer)
        {
            albedo += invSpp * tmpAlbedo;
            normal += invSpp * tmpNormal;
        }

        let lum = toGray(tmp);
        L += invSpp * lum;
        sqL += invSpp * (lum * lum);
    }

    if (generateGBuffer)
    {
        if (length(normal) > k::eps)
        {
            normal = normalize(normal);
        }

        albedoImage[threadIdx.xy] = float4(albedo, 1.0);
        normalImage[threadIdx.xy] = float4(normal, 1.0);
    }

    // lin-log
    if (linLogL)
    {
        rgb = linLog(rgb);
    }

    if (toneMapping)
    {
        let gamma        = 2.2;  // HACK: gamma correction
        float3 corrected = pow(rgb, 1. / gamma);

        resultImage[threadIdx.xy] = float4(corrected, 1.0);
        return;
    }

    // linear
    let variance              = sampleVariance(L, sqL, sceneParams.spp);
    resultImage[threadIdx.xy] = float4(rgb, variance);
    return;
}
