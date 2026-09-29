/*****************************************************************/ /**
 * @file   AnimatedTransform.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef EVENTRENDERER_INCLUDE_ANIMATEDTRANSFORM_HPP_
#define EVENTRENDERER_INCLUDE_ANIMATEDTRANSFORM_HPP_

#include "Transform.hpp"

#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <vulkan/vulkan.hpp>

#include <vk2s/Device.hpp>

namespace evr
{
    /**
     * @brief  Struct representing instance transforms
     */
    struct AnimatedTransform
    {
        std::map<uint32_t, Transform> animations;
        std::shared_ptr<const AnimatedTransform> parent;

        struct Delta
        {
            Transform t0;
            Transform t1;
        };

        Transform operator()(const double t) const
        {
            return mix(t);
        }

        Transform mix(const double t) const
        {
            if (animations.empty())
            {
                return Transform{};
            }

            const auto first = animations.begin();
            const auto last  = std::prev(animations.end());

            if (t <= static_cast<double>(first->first))
            {
                return first->second;
            }

            if (t >= static_cast<double>(last->first))
            {
                return last->second;
            }

            const auto t1 = animations.lower_bound(static_cast<uint32_t>(std::ceil(t)));
            const auto t0 = std::prev(t1);

            const double rate =
                std::clamp((t - static_cast<double>(t0->first)) / static_cast<double>(t1->first - t0->first), 0.0, 1.0);

            Transform ret{
                .pos   = glm::mix(t0->second.pos, t1->second.pos, rate),
                .rot   = glm::shortMix(t0->second.rot, t1->second.rot, static_cast<float>(rate)),
                .scale = glm::mix(t0->second.scale, t1->second.scale, rate),
                .shear = glm::mix(t0->second.shear, t1->second.shear, rate),
            };
            ret.params.update(ret.pos, ret.rot, ret.scale, ret.shear);

            return ret;
        }

        glm::mat4 world(const double t) const
        {
            const glm::mat4 local = mix(t).params.world;
            if (parent)
            {
                return parent->world(t) * local;
            }
            return local;
        }

        bool isStatic() const
        {
            if (animations.size() > 1)
            {
                return false;
            }

            return !parent || parent->isStatic();
        }

        Delta get(const double t) const
        {
            if (animations.empty())
            {
                return Delta{};
            }

            const auto first = animations.begin();
            const auto last  = std::prev(animations.end());

            if (t <= static_cast<double>(first->first))
            {
                return Delta{ .t0 = first->second, .t1 = first->second };
            }

            if (t >= static_cast<double>(last->first))
            {
                return Delta{ .t0 = last->second, .t1 = last->second };
            }

            const auto t1 = animations.lower_bound(static_cast<uint32_t>(std::ceil(t)));
            const auto t0 = std::prev(t1);

            return Delta{ .t0 = t0->second, .t1 = t1->second };
        }

        Delta get(const double t0, const double t1) const
        {
            return Delta{
                .t0 = mix(t0),
                .t1 = mix(t1),
            };
        }

        Delta getWorld(const double t0, const double t1) const
        {
            Delta ret{
                .t0 = Transform::fromWorld(world(t0)),
                .t1 = Transform::fromWorld(world(t1)),
            };

            if (glm::dot(ret.t0.rot, ret.t1.rot) < 0.f)
            {
                ret.t1.rot = -ret.t1.rot;
            }

            return ret;
        }
    };
}  // namespace evr

#endif
