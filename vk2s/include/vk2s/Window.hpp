/*****************************************************************/ /**
 * @file   Window.hpp
 * @brief  header file of window class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_WINDOW_HPP_
#define VK2S_INCLUDE_WINDOW_HPP_

#include "Macro.hpp"

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>

namespace vk2s
{
    //! forward declaration
    class Device;
    class Semaphore;

    /**
     * @brief  class representing a window and its surface, swapchain
     */
    class Window
    {
    public:
        using ResizeFlag       = bool;
        using GLFWKeyCode      = int;
        using GLFWMouseKeyCode = int;

    public:
        /**
         * @brief  constructor
         */
        Window(Device& device, const uint32_t width, const uint32_t height, const uint32_t frameNum,
               std::string_view windowName, bool fullScreen = false);

        /**
         * @brief  destructor
         */
        ~Window();

        NONCOPYABLE(Window);
        NONMOVABLE(Window);

        /**
         * @brief  update the window state (must be called each time in the main loop)
         */
        bool update();

        /**
         * @brief  get the input status of the specified key
         * 
         * @param key key to detect (GLFW_KEY_***)
         * @return GLFW_PRESS or GLFW_RELEASE
         */
        int getKey(const GLFWKeyCode key) const;

        /**
         * @brief  get the input status of the specified mouse button
         * 
         * @param mouseKey mouse button to detect (GLFW_MOUSE_BUTTON_***)
         * @return GLFW_PRESS or GLFW_RELEASE
         */
        int getMouseKey(const GLFWMouseKeyCode mouseKey) const;

        /**
         * @brief  get mouse pointer coordinates (absolute coordinates from top left of window)
         * 
         * @returns [x, y] of mouse pointer coordinates
         */
        std::pair<double, double> getMousePos() const;

        /**
         * @brief  get the index of the framebuffer that is now available from the swap chain
         * 
         * @params signalSem semaphore that enters the signal state when its frame buffer becomes available for acquisition
         * @returns [index of frame buffer, whether resizing is necessary]
         */
        std::pair<uint32_t, ResizeFlag> acquireNextImage(Semaphore& signalSem);

        /**
         * @brief  get window size 
         * @returns [width, height] of this window
         */
        std::pair<uint32_t, uint32_t> getWindowSize() const;

        /**
         * @brief  get window frame buffer count
         */
        uint32_t getFrameCount() const;

        /**
         * @brief  display framebuffer at specified index
         * 
         * @params frameBufferIndex index of frame buffer to display
         * @params waitSem semaphore waiting for display
         */
        ResizeFlag present(const uint32_t frameBufferIndex, Semaphore& waitSem);

        /**
         * @brief  go to full screen mode
         */
        void setFullScreen();

        /**
         * @brief  go to windowed mode
         */
        void setWindowed();

        /**
         * @brief  reflect window resizing
         */
        void resize();

        // internal-------------

        /**
         * @brief  get GLFW window pointer
         */
        GLFWwindow* getpGLFWWindow();

        /**
         * @brief  get vulkan swapchain images
         */
        const std::vector<vk::Image>& getVkImages();

        /**
         * @brief  get vulkan swapchain image views
         */
        const std::vector<vk::UniqueImageView>& getVkImageViews();

        /**
         * @brief  get vulkan swapchain image format 
         */
        vk::Format getVkSwapchainImageFormat() const;

        /**
         * @brief  get vulkan swapchain extent
         */
        vk::Extent2D getVkSwapchainExtent() const;

    private:  // methods
        /**
         * @brief  initialize GLFW window
         */
        void initWindow(const bool fullScreen);

        /**
         * @brief  create vulkan surface
         */
        void createSurface();

        /**
         * @brief  create vulkan swapchain
         */
        void createSwapChain();

        /**
         * @brief  create vulkan swapchain image views
         */
        void createImageViews();

        /**
         * @brief  create depth buffer
         */
        void createDepthBuffer();

        /**
         * @brief  transition the layout of each image in the swap chain
         */
        void setImageLayouts();

        /**
         * @brief  select a format for the swap chain from the available candidates
         */
        vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);

        /**
         * @brief  selecting the display format of the image for the swapchain
         */
        vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);

        /**
         * @brief  get extents of the swap chain that meet the requirements
         */
        vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

        /**
         * @brief  callback to notify when the frame buffer needs to be resized
         */
        static void framebufferResizeCallback(GLFWwindow* pWindow, int width, int height);

    private:  // member variables
        //! window width
        uint32_t mWindowWidth;
        //! window height
        uint32_t mWindowHeight;
        //! number of frames that can be rendered in parallel
        const uint32_t mMaxFramesInFlight;

        //! whether the window has been resized or not
        bool mResized;

        //! window name (string displayed at the top of the window)
        std::string_view mWindowName;

        //! reference to device
        Device& mDevice;

        //! pointer to GLFWWindow
        GLFWwindow* mpWindow;
        //! vulkan surface handle
        vk::UniqueSurfaceKHR mSurface;
        //! vulkan swapchain handle
        vk::UniqueSwapchainKHR mSwapChain;
        //! vulkan swapchain image handles
        std::vector<vk::Image> mSwapChainImages;
        //! vulkan swapchain image's format
        vk::Format mSwapChainImageFormat;
        //! vulkan swapchain extent
        vk::Extent2D mSwapChainExtent;
        //! vulkan swapchain image views
        std::vector<vk::UniqueImageView> mSwapChainImageViews;
    };
}  // namespace vk2s

#endif
