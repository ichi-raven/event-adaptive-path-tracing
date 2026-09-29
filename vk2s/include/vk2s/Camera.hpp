/*****************************************************************/ /**
 * @file   Camera.hpp
 * @brief  header file of Camera utility class
 * 
 * @author ichi-raven
 * @date   October 2023
 *********************************************************************/
#ifndef VK2S_CAMERA_HPP_
#define VK2S_CAMERA_HPP_

#include <glm/glm.hpp>

#include <GLFW/glfw3.h>

namespace vk2s
{
    /**
     * @brief  utility class that provide basic camera operation
     */
    class Camera
    {
    public:
        /**
         * @brief  constructor
         */
        Camera(const double fov = 60., const double aspect = 16. / 9., const double near = 0.1, const double far = 1e4);

        /**
         * @brief  update camera from mouse and key inputs
         */
        void update(GLFWwindow* pWindow, const double moveSpeed, const double mouseSpeed, const bool reset = false);

        /**
         * @brief update camera without key input
         */
        void update(bool reset = false);

        /**
         * @brief setter and getter of camera's up vector
         */
        void setUpVector(const glm::vec3& pos);
        const glm::vec3& getUpVector() const;

        /**
         * @brief setter and getter of camera's polar angles
         */
        void setPhi(const double phi);
        double getPhi() const;

        void setTheta(const double theta);
        double getTheta() const;

        /**
         * @brief setter and getter of camera's point
         */
        void setPos(const glm::vec3& pos);
        const glm::vec3& getPos() const;

        /**
         * @brief  setter and getter of camera's lookat point
         */
        void setLookAt(const glm::vec3& lookAt);
        const glm::vec3 getLookAt() const;

        /**
         * @brief  setter and getter of camera's field of view
         */
        void setFOV(const double FOV);
        double getFOV() const;

        /**
         * @brief  setter and getter of camera's aspect ratio
         */
        void setAspect(const double aspect);
        double getAspect() const;

        /**
         * @brief  setter and getter of distance to camera's near plane
         */
        void setNear(const double near);
        double getNear() const;

        /**
         * @brief  setter and getter of distance to camera's far plane
         */
        void setFar(const double far);
        double getFar() const;

        /**
         * @brief  whether the camera has moved since the last update
         */
        bool moved() const;

        /**
         * @brief  compute and obtain the view matrix for this camera
         */
        const glm::mat4& getViewMatrix() const;

        /**
         * @brief  compute and obtain the projection matrix for this camera
         */
        const glm::mat4& getProjectionMatrix() const;

    private:
        /**
         * @brief  compute the view matrix for this camera
         */
        void updateViewMat();

        /**
         * @brief  compute the projection matrix for this camera
         */
        void updateProjMat();

        //! field of view
        double mFOV;
        //! aspect ratio
        double mAspect;
        //! distance to the near plane
        double mNear;
        //! distance to the far plane
        double mFar;
        //! looking elevation angle
        double mTheta;
        //! looking azimuth angle
        double mPhi;
        //! position
        glm::vec3 mPos;
        //! up vector
        glm::vec3 mUp;
        //! view matrix
        glm::mat4 mViewMat;
        //! projection matrix
        glm::mat4 mProjectionMat;

        //! flag whether the camera has moved
        bool mMoved;
    };
}  // namespace vk2s
#endif
