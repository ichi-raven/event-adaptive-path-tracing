implementing Material;

import "../Utility/Constants";
import PBR;
import GGX;

public struct Dielectric : IMaterial
{
    public struct BSDF : IBSDF
    {
        public static Optional<BSDFSample> sample<S : ISampler>(MaterialParams params, BSDFContext ctx,
                                                                SurfaceInteraction si, inout S sampler)
        {
            let sample1 = sampler.next1D();
            let sample2 = sampler.next2D();

            BSDFSample ret;
            ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Reflection;

            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            // specular
            if (params.IOR == 1.0 || pbr::isEffectivelySmooth(ax, ay))  // specular
            {
                // FIXME: this line only works for air vs something else
                let pR = pbr::fresnelDielectric(pbr::cosTheta(si.wi), 1.0, params.IOR);
                let pT = 1.0 - pR;

                if (sample1 < pR / (pR + pT))  // reflection
                {
                    // sample perfect specular BRDF
                    ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Reflection;
                    ret.wo    = float3(-si.wi.x, -si.wi.y, si.wi.z);
                    ret.f     = float3(pR / abs(pbr::cosTheta(ret.wo)));
                    ret.pdf   = pR / (pR + pT);
                }
                else  // refraction (transmission)
                {
                    // sample perfect specular BTDF
                    ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Transmission;
                    float3 wo;
                    float etap;
                    let valid = pbr::refract(si.wi, float3(0., 0., 1.), params.IOR, etap, wo);
                    if (!valid)
                    {
                        return none;
                    }

                    ret.pdf = pT / (pR + pT);
                    ret.f   = float3(pT / abs(pbr::cosTheta(wo)));
                    ret.f /= pbr::sqr(etap);  // radiance transport
                    ret.wo = wo;
                }

                // early return
                // specular f and pdf can't calculate by simple eval
                return ret;
            }

            // glossy
            let wm = ggx::sampleWm(si.wi, sample2, ax, ay);

            // FIXME: this line only works for air vs something else
            let pR = pbr::fresnelDielectric(dot(si.wi, wm), 1.0, params.IOR);
            let pT = 1. - pR;

            if (sample1 < pR / (pR + pT))  // reflected
            {
                ret.flags = BSDFSample::Flags::Glossy | BSDFSample::Flags::Reflection;
                let wo    = reflect(-si.wi, wm);
                if (wo.z * si.wi.z <= 0.)  // not in same hemisphere
                {
                    return none;
                }

                let ct_o = pbr::cosTheta(wo);
                let ct_i = pbr::cosTheta(si.wi);
                ret.pdf  = ggx::pdf(si.wi, wm, ax, ay) / (4. * abs(dot(si.wi, wm))) * pR / (pR + pT);
                ret.f    = float3(ggx::D(wm, ax, ay) * ggx::G(wo, si.wi, ax, ay) * pR / (4. * ct_i * ct_o));
                ret.wo   = wo;
            }
            else  // transmitted
            {
                ret.flags = BSDFSample::Flags::Glossy | BSDFSample::Flags::Transmission;
                float3 wo;
                float etap;
                let tir = !pbr::refract(si.wi, wm, params.IOR, etap, wo);

                if (wo.z * si.wi.z > 0. || wo.z == 0. || tir)  // in same hemisphere or failed to refract
                {
                    return none;
                }

                let ct_o    = pbr::cosTheta(wo);
                let ct_i    = pbr::cosTheta(si.wi);
                let denom   = pbr::sqr(dot(wo, wm) + dot(si.wi, wm) / etap);
                let dwm_dwo = abs(dot(wo, wm)) / denom;
                ret.pdf     = ggx::pdf(si.wi, wm, ax, ay) * dwm_dwo * pT / (pR + pT);
                ret.f       = float3(pT * ggx::D(wm, ax, ay) * ggx::G(wo, si.wi, ax, ay) *
                                     abs(dot(si.wi, wm) * dot(wo, wm) / (ct_i * ct_o * denom)));
                ret.f /= pbr::sqr(etap);  // radiance transport
                ret.wo = wo;
            }

            return ret;
        }

        public static float pdf(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            if (params.IOR == 1.0 || pbr::isEffectivelySmooth(ax, ay))
            {
                return 0.0;
            }

            let cosThetaO = pbr::cosTheta(wo);
            let cosThetaI = pbr::cosTheta(si.wi);
            let isReflect = cosThetaI * cosThetaO > 0.0;

            float etap = 1.0;
            if (!isReflect)
            {
                etap = cosThetaI > 0.0 ? params.IOR : (1. / params.IOR);
            }

            // compute generalized half vector wm
            float3 wm = wo * etap + si.wi;
            if (cosThetaI == 0. || cosThetaO == 0. || dot(wm, wm) == 0.)
            {
                return 0.0;
            }

            // WARN: Slang's facefoward is different from that of PBRT!!
            wm = normalize(wm);
            if (wm.z < 0.0)
            {
                wm = -wm;
            }

            // discard backfacing microfacets
            if (dot(wm, si.wi) * cosThetaI < 0.0 || dot(wm, wo) * cosThetaO < 0.0)
            {
                return 0.0;
            }

            // FIXME: this line only works for air vs something else
            let pR = pbr::fresnelDielectric(dot(si.wi, wm), 1.0, params.IOR);
            let pT = 1. - pR;

            float pdf = 0.0;
            if (isReflect)
            {
                pdf = ggx::pdf(si.wi, wm, ax, ay) / (4. * abs(dot(si.wi, wm))) * pR / (pR + pT);
            }
            else
            {
                let denom   = pbr::sqr(dot(wo, wm) + dot(si.wi, wm) / etap);
                let dwm_dwo = abs(dot(wo, wm)) / denom;
                pdf         = ggx::pdf(si.wi, wm, ax, ay) * dwm_dwo * pT / (pR + pT);
            }

            return pdf;
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            if (params.IOR == 1.0 || pbr::isEffectivelySmooth(ax, ay))
            {
                return k::zeros.xyz;
            }

            let cosThetaO = pbr::cosTheta(wo);
            let cosThetaI = pbr::cosTheta(si.wi);
            let isReflect = cosThetaI * cosThetaO > 0.0;

            float etap = 1.0;
            if (!isReflect)
            {
                etap = cosThetaI > 0.0 ? params.IOR : (1. / params.IOR);
            }

            float3 wm = wo * etap + si.wi;
            if (cosThetaI == 0.0 || cosThetaO == 0.0 || dot(wm, wm) == 0.0)
            {
                return k::zeros.xyz;
            }

            wm = normalize(wm);
            if (wm.z < 0.0)
            {
                wm = -wm;
            }

            // discard backfacing microfacets
            if (dot(wm, si.wi) * cosThetaI < 0.0 || dot(wm, wo) * cosThetaO < 0.0)
            {
                return k::zeros.xyz;
            }

            // FIXME: this line only works for air vs something else
            let F = pbr::fresnelDielectric(dot(si.wi, wm), 1.0, params.IOR);

            if (isReflect)
            {
                return float3(ggx::D(wm, ax, ay) * F * ggx::G(wo, si.wi, ax, ay) / abs(4.0 * cosThetaI * cosThetaO));
            }
            else
            {
                let denom = pbr::sqr(dot(wo, wm) + dot(si.wi, wm) / etap);
                float ft  = (1.0 - F) * ggx::D(wm, ax, ay) * ggx::G(wo, si.wi, ax, ay) *
                           abs(dot(si.wi, wm) * dot(wo, wm) / (cosThetaI * cosThetaO * denom));
                ft /= pbr::sqr(etap);  // radiance transport
                return float3(ft);
            }

            return k::zeros.xyz;
        }
    }
}
