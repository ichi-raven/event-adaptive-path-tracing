implementing Material;

import "../Utility/Constants";
import PBR;

__include Lambert;
__include Conductor;
__include Dielectric;

public struct Principle : IMaterial
{
    internal struct LobeInfo
    {
        __init()
        {
            prob   = 0.0;
            weight = 0.0;
        }

        __init(float _prob, float _weight)
        {
            prob   = _prob;
            weight = _weight;
        }

        float prob;
        float weight;
    };

    public struct BSDF : IBSDF
    {
        static MaterialParams initClearcoatParams(const MaterialParams params)
        {
            var ret = params;

            ret.metallic          = 1.0;
            ret.roughness         = lerp(0.1, 0.001, params.clearcoatGloss);
            ret.metalnessTexIndex = k::invalidTexIndex;
            ret.albedo            = k::white;

            return ret;
        }

        static Optional<BSDFSample> processContinuousSample(MaterialParams params, BSDFContext ctx,
                                                            SurfaceInteraction si, BSDFSample ret)
        {
            ret.f   = eval(params, ctx, si, ret.wo);
            ret.pdf = pdf(params, ctx, si, ret.wo);
            if (ret.pdf == 0.0)
            {
                return none;
            }
            return ret;
        }

        [ForceInline]
        static void calcLobeInfo(const MaterialParams params, out LobeInfo pSpecular, out LobeInfo pDiffuse,
                                 out LobeInfo pClearcoat, out LobeInfo pSpecTrans)
        {
            let specTrans          = params.specTrans * (1.0 - params.metallic);
            let specularWeight     = (1.0 - specTrans);
            let transmissionWeight = specTrans;
            let diffuseWeight      = (1.0 - params.metallic) * (1.0 - params.specTrans);
            let clearcoatWeight    = saturate(params.clearcoat);

            let norm = 1.0 / (specularWeight + transmissionWeight + diffuseWeight + clearcoatWeight);

            pSpecular  = { specularWeight * norm, specularWeight };
            pSpecTrans = { transmissionWeight * norm, transmissionWeight };
            pDiffuse   = { diffuseWeight * norm, diffuseWeight };
            pClearcoat = { clearcoatWeight * norm, clearcoatWeight };
        }

        public static Optional<BSDFSample> sample<S : ISampler>(MaterialParams params, BSDFContext ctx,
                                                                SurfaceInteraction si, inout S sampler)
        {
            let lobeSample = sampler.next1D();

            var pSpecular     = LobeInfo();
            var pDiffuse      = LobeInfo();
            var pClearcoat    = LobeInfo();
            var pTransmission = LobeInfo();
            calcLobeInfo(params, pSpecular, pDiffuse, pClearcoat, pTransmission);

            if (lobeSample < pSpecular.prob)  // specular
            {
                if (let res = Conductor.BSDF.sample(params, ctx, si, sampler))
                {
                    if (!res.isSpecular())
                    {
                        return processContinuousSample(params, ctx, si, res);
                    }

                    var ret = res;
                    ret.pdf *= pSpecular.prob;
                    ret.f *= pSpecular.weight;
                    return ret;
                }
            }
            else if (lobeSample < (pSpecular.prob + pClearcoat.prob))  // clearcoat
            {
                let clearcoatParams = initClearcoatParams(params);
                if (let res = Conductor.BSDF.sample(clearcoatParams, ctx, si, sampler))
                {
                    if (!res.isSpecular())
                    {
                        return processContinuousSample(params, ctx, si, res);
                    }

                    var ret = res;
                    ret.pdf *= pClearcoat.prob;
                    ret.f *= pClearcoat.weight;
                    return ret;
                }
            }
            else if (lobeSample < (pSpecular.prob + pClearcoat.prob + pDiffuse.prob))  // diffuse
            {
                if (let res = Lambert.BSDF.sample(params, ctx, si, sampler))
                {
                    return processContinuousSample(params, ctx, si, res);
                }
            }
            else if (pTransmission.prob > 0.0)  // transmission
            {
                if (let res = Dielectric.BSDF.sample(params, ctx, si, sampler))
                {
                    if (!res.isSpecular())
                    {
                        return processContinuousSample(params, ctx, si, res);
                    }

                    var ret = res;
                    ret.pdf *= pTransmission.prob;
                    ret.f *= pTransmission.weight;
                    return ret;
                }
            }

            // invalid sample
            return none;
        }

        public static float pdf(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let dotNV          = pbr::cosTheta(si.wi);
            let dotNL          = pbr::cosTheta(wo);
            let sameHemisphere = dotNL * dotNV > 0.0;

            var pBRDF      = LobeInfo();
            var pDiffuse   = LobeInfo();
            var pClearcoat = LobeInfo();
            var pSpecTrans = LobeInfo();
            calcLobeInfo(params, pBRDF, pDiffuse, pClearcoat, pSpecTrans);

            let diffuseWeight      = (1.0 - params.metallic) * (1.0 - params.specTrans);
            let transmissionWeight = (1.0 - params.metallic) * params.specTrans;

            // pdf
            float wholePDF = 0.0;

            // clearcoat
            if (sameHemisphere)
            {
                let clearcoatParams = initClearcoatParams(params);
                wholePDF += pClearcoat.prob * Conductor.BSDF.pdf(clearcoatParams, ctx, si, wo);
            }

            // diffuse
            if (diffuseWeight > 0.0)
            {
                wholePDF += pDiffuse.prob * Lambert.BSDF.pdf(params, ctx, si, wo);
            }

            // transmission
            if (transmissionWeight > 0.0)
            {
                wholePDF += pSpecTrans.prob * Dielectric.BSDF.pdf(params, ctx, si, wo);
            }

            // specular
            if (sameHemisphere)
            {
                wholePDF += pBRDF.prob * Conductor.BSDF.pdf(params, ctx, si, wo);
            }

            return wholePDF;
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let dotNV          = pbr::cosTheta(si.wi);
            let dotNL          = pbr::cosTheta(wo);
            let sameHemisphere = dotNL * dotNV > 0.0;

            // whole reflectance
            float3 reflectance = float3(0.0);

            let specTrans          = params.specTrans * (1.0 - params.metallic);
            let specularWeight     = (1.0 - specTrans);
            let transmissionWeight = specTrans;
            let diffuseWeight      = (1.0 - params.metallic) * (1.0 - params.specTrans);
            let clearcoatWeight    = saturate(params.clearcoat);

            // clearcoat
            if (sameHemisphere)
            {
                let clearcoatParams = initClearcoatParams(params);
                let clearcoat       = Conductor.BSDF.eval(clearcoatParams, ctx, si, wo);
                reflectance += clearcoatWeight * clearcoat;
            }

            // diffuse
            if (diffuseWeight > 0.0)
            {
                let diffuse = Lambert.BSDF.eval(params, ctx, si, wo);
                reflectance += diffuseWeight * diffuse;
            }

            // transmission
            if (transmissionWeight > 0.0)
            {
                let transmission = Dielectric.BSDF.eval(params, ctx, si, wo);
                reflectance += transmissionWeight * transmission;
            }

            // specular
            if (sameHemisphere)
            {
                let specular = Conductor.BSDF.eval(params, ctx, si, wo);
                reflectance += specularWeight * specular;
            }

            return reflectance;
        }
    }
}
