#ifndef EVENTRENDERER_INCLUDE_TRANSFORM_HPP_
#define EVENTRENDERER_INCLUDE_TRANSFORM_HPP_

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vulkan/vulkan.hpp>

#include <vk2s/Device.hpp>

#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace evr
{
    /**
     * @brief  Struct representing instance transforms
     */
    struct Transform
    {
        static Transform fromWorld(const glm::mat4& world)
        {
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(world[col][row]))
                    {
                        throw std::runtime_error("non-finite world transform");
                    }
                }
            }

            if (std::abs(world[0][3]) > 1e-6f || std::abs(world[1][3]) > 1e-6f || std::abs(world[2][3]) > 1e-6f ||
                std::abs(world[3][3] - 1.0f) > 1e-6f)
            {
                throw std::runtime_error("non-affine world transformation");
            }

            // Use double precision for decomposition
            const glm::dvec3 c0(world[0]);
            const glm::dvec3 c1(world[1]);
            const glm::dvec3 c2(world[2]);

            const double magnitude = std::max({ glm::length(c0), glm::length(c1), glm::length(c2) });
            const double tolerance = 1.0e-10 * magnitude;

            const double sx = glm::length(c0);
            if (sx <= tolerance)
            {
                throw std::runtime_error("singular world transformation");
            }
            const glm::dvec3 r0 = c0 / sx;

            const double a            = glm::dot(r0, c1);
            const glm::dvec3 residual = c1 - a * r0;
            const double sy           = glm::length(residual);
            if (sy <= tolerance)
            {
                throw std::runtime_error("singular world transformation");
            }
            const glm::dvec3 r1 = residual / sy;

            const glm::dvec3 r2 = glm::normalize(glm::cross(r0, r1));
            const double b      = glm::dot(r0, c2);
            const double c      = glm::dot(r1, c2);
            const double sz     = glm::dot(r2, c2);
            if (std::abs(sz) <= tolerance)
            {
                throw std::runtime_error("singular world transformation");
            }

            glm::dmat3 rotation(1.0);
            rotation[0] = r0;
            rotation[1] = r1;
            rotation[2] = r2;

            Transform ret;
            ret.pos   = glm::vec3(world[3]);
            ret.rot   = glm::normalize(glm::quat(glm::quat_cast(rotation)));
            ret.scale = glm::vec3(sx, sy, sz);
            ret.shear = glm::vec3(a, b, c);
            ret.params.update(ret.pos, ret.rot, ret.scale, ret.shear);

            return ret;
        }

        /**
         * @brief Material parameters (passed to the GPU, must always be kept in sync with shader side)
         */
        struct Params
        {
            //! World matrix (model matrix)
            glm::mat4 world = glm::identity<glm::mat4>();
            //! The inverse transpose of the world matrix (transpose(inverse(world matrix)))
            glm::mat4 worldInvTranspose = glm::identity<glm::mat4>();
            //! Velocity (difference in position from the previous frame)
            glm::vec3 vel = glm::vec3(0.0);
            //! Slot part of Entity with this Transform
            uint32_t entitySlot = 0;
            //! Padding
            glm::vec3 padding = glm::vec3(0.0);
            //! Index part of Entity with this Transform
            uint32_t entityIndex = 0;

            /** 
             * @brief  Update each parameter/matrix from the TRS vector
             *  
             * @param translate translate vector
             * @param rotation rotation vector
             * @param scaling scaling vector
             * @param shear shear vector
             */
            void update(glm::vec3 translate, const glm::quat& rotation, const glm::vec3& scaling,
                        const glm::vec3& shear = glm::vec3(0.0f))
            {
                vel = translate - glm::vec3(world[0][3], world[1][3], world[2][3]);

                glm::mat4 scaleShear = glm::scale(glm::identity<glm::mat4>(), scaling);
                scaleShear[1][0]     = shear.x;
                scaleShear[2][0]     = shear.y;
                scaleShear[2][1]     = shear.z;

                world = glm::translate(glm::identity<glm::mat4>(), translate) * glm::mat4_cast(rotation) * scaleShear;
                worldInvTranspose = glm::transpose(glm::inverse(world));
            }

            /** 
             * @brief  Convert the current world matrix to Vulkan's matrix representation for AS
             *  
             * @return World matrix (in Vulkan format)
             */
            vk::TransformMatrixKHR convert() const
            {
                vk::TransformMatrixKHR ret;
                const auto mT = glm::transpose(world);
                memcpy(&ret.matrix[0], &mT[0], sizeof(float) * 4);
                memcpy(&ret.matrix[1], &mT[1], sizeof(float) * 4);
                memcpy(&ret.matrix[2], &mT[2], sizeof(float) * 4);

                return ret;
            };
        };

        //! Parameter
        Params params;
        //! Position (translate) vector
        glm::vec3 pos = { 0.f, 0.f, 0.f };
        //! Rotation quaternion
        glm::quat rot = { 1.f, 0.f, 0.f, 0.f };
        //! Scale vector
        glm::vec3 scale = { 1.f, 1.f, 1.f };
        //! Shear vector
        glm::vec3 shear = { 0.f, 0.f, 0.f };
    };
}  // namespace evr

#endif
