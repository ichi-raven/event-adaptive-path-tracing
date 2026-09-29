#ifndef EVENTRENDERER_INCLUDE_EMITTER_HPP_
#define EVENTRENDERER_INCLUDE_EMITTER_HPP_

#include <vk2s/Device.hpp>
#include <glm/glm.hpp>

#include <utility>

namespace evr
{
    /**
     * @brief  Struct representing emitter (light source)
     */
    struct Emitter
    {
        /**
         * @brief  Emitter type (must always be kept in sync with shader side)
         */
        enum class Type : int32_t
        {
            ePoint    = 0,
            eArea     = 1,
            eInfinite = 2,
        };

        /**
         * @brief Emitter parameters (passed to the GPU, must always be kept in sync with shader side)
         */
        struct Params  // std430
        {
            //! Position
            glm::vec3 pos = glm::vec3(0.);
            float pad0;

            //! Index of the Entity's mesh with this Emitter (only for area emitter)
            int32_t faceNum        = 0;
            int32_t meshIndex      = -1;
            int32_t primitiveIndex = -1;
            int32_t padding        = 0;

            //! The luminous component of this Emitter
            glm::vec3 emissive = glm::vec3(0.);
            float pad1;

            //! Emitter type
            int32_t type = static_cast<std::underlying_type_t<Type>>(Type::ePoint);
            //! Index to emissiveTex
            int32_t texIndex    = -1;
            int32_t padding2[2] = {};
        };

        //! GPU Parameters
        Params params;
    };
}  // namespace evr

#endif
