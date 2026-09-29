/*****************************************************************/ /**
 * @file   Camera.cpp
 * @brief  source file of Camera class
 * 
 * @author ichi-raven
 * @date   November 2024
 *********************************************************************/
#include "../include/vk2s/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

constexpr double kPI = 3.14159265358979;

inline const double sgn(double x)
{
    return x >= 0 ? 1. : -1.;
}

namespace vk2s
{
    Camera::Camera(const double fov, const double aspect, const double near, const double far)
        : mFOV(fov)
        , mAspect(aspect)
        , mNear(near)
        , mFar(far)
        , mPos(0., 0., 0.)
        , mUp(0., 1., 0.)
        , mMoved(false)
    {
        setLookAt(glm::vec3(0.f, 0.f, -1.f));

        updateViewMat();
        updateProjMat();
    }

    void Camera::update(GLFWwindow* pWindow, const double moveSpeed, const double mouseSpeed, const bool reset)
    {
        mMoved = false;

        if (glfwGetMouseButton(pWindow, GLFW_MOUSE_BUTTON_RIGHT))
        {
            double mx = 0, my = 0;
            int width = 0, height = 0;
            glfwGetCursorPos(pWindow, &mx, &my);
            glfwGetWindowSize(pWindow, &width, &height);
            glfwSetCursorPos(pWindow, width / 2, height / 2);
            mPhi += glm::radians(mouseSpeed * static_cast<double>(1. * width / 2. - mx));
            mTheta += glm::radians(-mouseSpeed * static_cast<double>(my - 1. * height / 2.));

            if (mTheta > glm::radians(179.))
            {
                mTheta = glm::radians(179.);
            }
            if (mTheta < glm::radians(1.))
            {
                mTheta = glm::radians(1.);
            }

            mMoved = true;
        }

        glm::vec3 direction(sin(mTheta) * sin(mPhi), cos(mTheta), sin(mTheta) * cos(mPhi));
        glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.f, 1.f, 0.f), direction));  //(sin(mPhi - kPI / 2.), 0, cos(mPhi - kPI / 2.));
        mUp             = glm::normalize(glm::cross(right, direction));

        if (reset)
        {
            mTheta = 0.;
            mPhi   = 0.;
            mMoved = true;
        }

        double moveSpeedMod = moveSpeed;

        if (glfwGetKey(pWindow, GLFW_KEY_LEFT_CONTROL))
        {
            moveSpeedMod *= 10.;
        }

        if (glfwGetKey(pWindow, GLFW_KEY_W))
        {
            mPos += static_cast<float>(moveSpeedMod) * direction;
            mMoved = true;
        }
        if (glfwGetKey(pWindow, GLFW_KEY_S))
        {
            mPos -= static_cast<float>(moveSpeedMod) * direction;
            mMoved = true;
        }
        if (glfwGetKey(pWindow, GLFW_KEY_A))
        {
            mPos -= static_cast<float>(moveSpeedMod) * right;
            mMoved = true;
        }
        if (glfwGetKey(pWindow, GLFW_KEY_D))
        {
            mPos += static_cast<float>(moveSpeedMod) * right;
            mMoved = true;
        }
        if (glfwGetKey(pWindow, GLFW_KEY_UP))
        {
            mPos -= static_cast<float>(moveSpeedMod) * mUp;
            mMoved = true;
        }
        if (glfwGetKey(pWindow, GLFW_KEY_DOWN))
        {
            mPos += static_cast<float>(moveSpeedMod) * mUp;
            mMoved = true;
        }

        updateViewMat();
        updateProjMat();
    }

    void Camera::setUpVector(const glm::vec3& up)
    {
        mUp = up;
        updateViewMat();
        mMoved = true;
    }

    const glm::vec3& Camera::getUpVector() const
    {
        return mUp;
    }

    void Camera::setPos(const glm::vec3& pos)
    {
        mPos = pos;
        updateViewMat();
        mMoved = true;
    }

    const glm::vec3& Camera::getPos() const
    {
        return mPos;
    }

    void Camera::setPhi(const double phi)
    {
        mPhi = phi;
        updateViewMat();
        mMoved = true;
    }

    double Camera::getPhi() const
    {
        return mPhi;
    }

    void Camera::setTheta(const double theta)
    {
        mTheta = theta;
        updateViewMat();
        mMoved = true;
    }

    double Camera::getTheta() const
    {
        return mTheta;
    }

    void Camera::setLookAt(const glm::vec3& lookAt)
    {
        const auto diff = glm::normalize(lookAt - mPos);

        mPhi   = atan2(diff.x, diff.z);
        mTheta = acos(diff.y);

        glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.f, 1.f, 0.f), diff));  //(sin(mPhi - kPI / 2.), 0, cos(mPhi - kPI / 2.));
        mUp             = glm::normalize(glm::cross(right, diff));

        updateViewMat();
    }

    const glm::vec3 Camera::getLookAt() const
    {
        return mPos + glm::vec3(sin(mTheta) * sin(mPhi), cos(mTheta), sin(mTheta) * cos(mPhi));
    }

    void Camera::setFOV(const double fov)
    {
        mFOV = fov;
        updateProjMat();
    }

    double Camera::getFOV() const
    {
        return mFOV;
    }

    void Camera::setAspect(const double aspect)
    {
        mAspect = aspect;
        updateProjMat();
    }

    double Camera::getAspect() const
    {
        return mAspect;
    }

    void Camera::setNear(const double near)
    {
        mNear = near;
        updateProjMat();
    }

    double Camera::getNear() const
    {
        return mNear;
    }

    void Camera::setFar(const double far)
    {
        mFar = far;
        updateProjMat();
    }

    double Camera::getFar() const
    {
        return mFar;
    }

    bool Camera::moved() const
    {
        return mMoved;
    }

    void Camera::updateViewMat()
    {
        mViewMat = glm::lookAt(mPos, getLookAt(), mUp);
    }

    void Camera::updateProjMat()
    {
        mProjectionMat = glm::perspective(mFOV, mAspect, mNear, mFar);
    }

    const glm::mat4& Camera::getViewMatrix() const
    {
        return mViewMat;
    }

    const glm::mat4& Camera::getProjectionMatrix() const
    {
        return mProjectionMat;
    }

}  // namespace vk2s