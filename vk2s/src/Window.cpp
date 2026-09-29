
/*****************************************************************/ /**
 * @file   Window.cpp
 * @brief  source file of Window class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#include "../include/vk2s/Window.hpp"

#include "../include/vk2s/Device.hpp"

#include <iostream>

namespace vk2s
{
    Window::Window(Device& device, uint32_t width, uint32_t height, uint32_t frameNum, std::string_view windowName,
                   const bool fullScreen)
        : mWindowWidth(width)
        , mWindowHeight(height)
        , mMaxFramesInFlight(frameNum)
        , mResized(false)
        , mWindowName(windowName)
        , mDevice(device)
    {
        initWindow(fullScreen);
        createSurface();
        createSwapChain();
        createImageViews();
        setImageLayouts();
    }

    Window::~Window()
    {
        mDevice.getVkDevice()->waitIdle();
        glfwDestroyWindow(mpWindow);
    }

    bool Window::update()
    {
        glfwPollEvents();

        return !glfwWindowShouldClose(mpWindow);
    }

    void Window::resize()
    {
        {
            int w, h;
            glfwGetFramebufferSize(mpWindow, &w, &h);

            while (w == 0 || h == 0)
            {
                glfwGetFramebufferSize(mpWindow, &w, &h);
                glfwWaitEvents();
            }

            mWindowWidth  = static_cast<uint32_t>(w);
            mWindowHeight = static_cast<uint32_t>(h);
        }

        mDevice.getVkDevice()->waitIdle();

        // destroy swapchain explicitly
        mSwapChain.reset();

        createSurface();
        createSwapChain();
        createImageViews();
        setImageLayouts();

        mResized = false;
    }

    int Window::getKey(const int key) const
    {
        return glfwGetKey(mpWindow, key);
    }

    int Window::getMouseKey(const GLFWMouseKeyCode mouseKey) const
    {
        return glfwGetMouseButton(mpWindow, mouseKey);
    }

    std::pair<double, double> Window::getMousePos() const
    {
        static double x, y;
        glfwGetCursorPos(mpWindow, &x, &y);
        return { x, y };
    }

    std::pair<uint32_t, uint32_t> Window::getWindowSize() const
    {
        return { mWindowWidth, mWindowHeight };
    }

    uint32_t Window::getFrameCount() const
    {
        return mMaxFramesInFlight;
    }

    void Window::setFullScreen()
    {
        glfwSetWindowMonitor(mpWindow, glfwGetPrimaryMonitor(), 0, 0, mWindowWidth, mWindowHeight, GLFW_DONT_CARE);
    }

    void Window::setWindowed()
    {
        glfwSetWindowMonitor(mpWindow, NULL, 20, 20, mWindowWidth, mWindowHeight, GLFW_DONT_CARE);
    }

    std::pair<uint32_t, Window::ResizeFlag> Window::acquireNextImage(Semaphore& signalSem)
    {
        auto res = mDevice.getVkDevice()->acquireNextImageKHR(mSwapChain.get(), UINT64_MAX,
                                                              signalSem.getVkSemaphore().get(), {});

        return { res.value, (res.result == vk::Result::eErrorOutOfDateKHR || mResized) };
    }

    Window::ResizeFlag Window::present(const uint32_t frameBufferIndex, Semaphore& waitSem)
    {
        assert(frameBufferIndex < mMaxFramesInFlight || !"invalid framebuffer index!");

        vk::PresentInfoKHR presentInfo(waitSem.getVkSemaphore().get(), mSwapChain.get(), frameBufferIndex);

        const auto res = mDevice.getVkGraphicsQueue().presentKHR(presentInfo);

        return (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR || mResized);
    }

    GLFWwindow* Window::getpGLFWWindow()
    {
        return mpWindow;
    }

    const std::vector<vk::Image>& Window::getVkImages()
    {
        return mSwapChainImages;
    }

    const std::vector<vk::UniqueImageView>& Window::getVkImageViews()
    {
        return mSwapChainImageViews;
    }

    vk::Format Window::getVkSwapchainImageFormat() const
    {
        return mSwapChainImageFormat;
    }

    vk::Extent2D Window::getVkSwapchainExtent() const
    {
        return mSwapChainExtent;
    }

    void Window::framebufferResizeCallback(GLFWwindow* pWindow, int width, int height)
    {
        auto p = reinterpret_cast<Window*>(glfwGetWindowUserPointer(pWindow));

        p->mResized = true;
    }

    void Window::initWindow(const bool fullScreen)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        if (fullScreen)
        {
            mpWindow =
                glfwCreateWindow(mWindowWidth, mWindowHeight, mWindowName.data(), glfwGetPrimaryMonitor(), nullptr);
        }
        else
        {
            mpWindow = glfwCreateWindow(mWindowWidth, mWindowHeight, mWindowName.data(), nullptr, nullptr);
        }

        // hide cursor
        glfwSetInputMode(mpWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

        glfwSetFramebufferSizeCallback(mpWindow, framebufferResizeCallback);

        glfwSetWindowUserPointer(mpWindow, this);
    }

    void Window::createSurface()
    {
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(VkInstance(mDevice.getVkInstance().get()), mpWindow, nullptr, &surface) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("failed to create mpWindow mSurface!");
        }
        mSurface = vk::UniqueSurfaceKHR(surface, { mDevice.getVkInstance().get() });
    }

    void Window::createSwapChain()
    {
        const auto& physDev = mDevice.getVkPhysicalDevice();

        Device::SwapChainSupportDetails details =
            Device::SwapChainSupportDetails::querySwapChainSupport(physDev, mSurface);

        vk::SurfaceFormatKHR mSurfaceFormat = chooseSwapSurfaceFormat(details.formats);
        vk::PresentModeKHR presentMode      = chooseSwapPresentMode(details.presentModes);
        vk::Extent2D extent                 = chooseSwapExtent(details.capabilities);

        uint32_t imageCount = details.capabilities.minImageCount + 1;

        if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount)
        {
            imageCount = details.capabilities.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR createInfo(
            {}, mSurface.get(), imageCount, mSurfaceFormat.format, mSurfaceFormat.colorSpace, extent,
            /* imageArrayLayers = */ 1, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive, {}, details.capabilities.currentTransform,
            vk::CompositeAlphaFlagBitsKHR::eOpaque, presentMode,
            /* clipped = */ VK_TRUE, nullptr);

        QueueFamilyIndices indices = QueueFamilyIndices::findQueueFamilies(physDev, mSurface);
        if (indices.graphicsFamily != indices.presentFamily)
        {
            createInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
            std::array familyIndices = { indices.graphicsFamily.value(), indices.presentFamily.value() };
            createInfo.setQueueFamilyIndices(familyIndices);
        }

        const auto& device = mDevice.getVkDevice();

        mSwapChain            = device->createSwapchainKHRUnique(createInfo);
        mSwapChainImages      = device->getSwapchainImagesKHR(mSwapChain.get());
        mSwapChainImageFormat = mSurfaceFormat.format;
        mSwapChainExtent      = extent;
    }

    void Window::createImageViews()
    {
        const auto& device = mDevice.getVkDevice();

        mSwapChainImageViews.resize(mSwapChainImages.size());

        vk::ComponentMapping components(vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                                        vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity);
        vk::ImageSubresourceRange subresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        for (size_t i = 0; i < mSwapChainImages.size(); i++)
        {
            vk::ImageViewCreateInfo createInfo({}, mSwapChainImages[i], vk::ImageViewType::e2D, mSwapChainImageFormat,
                                               components, subresourceRange);
            mSwapChainImageViews[i] = device->createImageViewUnique(createInfo);
        }
    }

    void Window::setImageLayouts()
    {
        const auto& vkDevice      = mDevice.getVkDevice();
        const auto& vkCommandPool = mDevice.getVkCommandPool();

        vk::CommandBufferAllocateInfo allocInfo(vkCommandPool.get(), vk::CommandBufferLevel::ePrimary, 1);

        auto commandBuffers = vkDevice->allocateCommandBuffers(allocInfo);
        auto& commandBuffer = commandBuffers.back();

        vk::ImageMemoryBarrier barrier;
        barrier.oldLayout                       = vk::ImageLayout::eUndefined;
        barrier.newLayout                       = vk::ImageLayout::ePresentSrcKHR;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask                   = vk::AccessFlagBits::eNone;

        const auto sourceStage      = vk::PipelineStageFlagBits::eAllCommands;
        const auto destinationStage = vk::PipelineStageFlagBits::eAllCommands;

        commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        for (auto& swapChainImage : mSwapChainImages)
        {
            barrier.image = swapChainImage;
            commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);
        }
        commandBuffer.end();
        vk::SubmitInfo submitInfo(nullptr, nullptr, commandBuffer, nullptr);

        mDevice.getVkGraphicsQueue().submit(submitInfo);

        vkDevice->waitIdle();
        vkDevice->freeCommandBuffers(vkCommandPool.get(), commandBuffer);
    }

    vk::SurfaceFormatKHR Window::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
    {
        for (const auto& availableFormat : availableFormats)
        {
            if (availableFormat.format == vk::Format::eR8G8B8A8Unorm)
            {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    vk::PresentModeKHR Window::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
    {
        for (const auto& availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == vk::PresentModeKHR::eFifoRelaxed)
            {
                return availablePresentMode;
            }
        }

        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D Window::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(mpWindow, &width, &height);

            vk::Extent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

            actualExtent.width =
                std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height =
                std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

}  // namespace vk2s
