/*****************************************************************/ /**
 * @file   RenderPass.cpp
 * @brief  source file of RenderPass class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#include "../include/vk2s/RenderPass.hpp"

#include "../include/vk2s/Device.hpp"

#include <array>

namespace vk2s
{
    RenderPass::RenderPass(Device& device, const vk::ArrayProxy<Handle<Image>>& colorTargets,
                           const Handle<Image>& depthTarget, const vk::AttachmentLoadOp loadOp)
        : RenderPass(device, colorTargets, std::array{ loadOp }, depthTarget)
    {
    }

    RenderPass::RenderPass(Device& device, const vk::ArrayProxy<Handle<Image>>& colorTargets,
                           const vk::ArrayProxy<vk::AttachmentLoadOp>& loadOps, const Handle<Image>& depthTarget)
        : mDevice(device)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        // TODO: control clear and dontcare

        std::vector<vk::AttachmentDescription> attachments;
        std::vector<vk::AttachmentReference> colorAttachmentRefs;
        attachments.reserve(colorTargets.size());
        colorAttachmentRefs.reserve(colorTargets.size());

        assert(loadOps.size() <= colorTargets.size() || !"invalid loadOps size!");

        for (size_t i = 0; i < colorTargets.size(); ++i)
        {
            const auto loadOp = loadOps.size() == 1 ? *loadOps.begin() : *(loadOps.begin() + i);

            attachments.emplace_back(
                vk::AttachmentDescription({}, colorTargets.front()->getVkFormat(), vk::SampleCountFlagBits::e1, loadOp,
                                          vk::AttachmentStoreOp::eStore, loadOp, vk::AttachmentStoreOp::eDontCare,
                                          vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal));
            colorAttachmentRefs.emplace_back(
                vk::AttachmentReference(colorAttachmentRefs.size(), vk::ImageLayout::eColorAttachmentOptimal));
        }

        if (depthTarget)
        {
            vk::AttachmentDescription depthAttachment(
                {}, depthTarget->getVkFormat(), vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
            attachments.emplace_back(depthAttachment);

            vk::AttachmentReference depthAttachmentRef(colorAttachmentRefs.size(),
                                                       vk::ImageLayout::eDepthStencilAttachmentOptimal);

            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRefs, {},
                                           &depthAttachmentRef);
            const auto srcStageMask =
                vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
            const auto dstStageMask =
                vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
            const auto dstAccessMask =
                vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0, srcStageMask, dstStageMask, {}, dstAccessMask);
            vk::RenderPassCreateInfo renderPassInfo({}, attachments, subpass, dependency);
            mRenderPass = vkDevice->createRenderPassUnique(renderPassInfo);
        }
        else
        {
            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRefs);
            const auto srcStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            const auto dstStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            const auto dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0, srcStageMask, dstStageMask, {}, dstAccessMask);
            vk::RenderPassCreateInfo renderPassInfo({}, attachments, subpass, dependency);
            mRenderPass = vkDevice->createRenderPassUnique(renderPassInfo);
        }

        // share implements
        recreateFrameBuffers(colorTargets, depthTarget);
    }

    RenderPass::RenderPass(Device& device, Window& window, const vk::AttachmentLoadOp colorLoadOp,
                           const Handle<Image>& depthTarget, const vk::AttachmentLoadOp depthLoadOp)
        : mDevice(device)
    {
        const auto& vkDevice = mDevice.getVkDevice();

        vk::AttachmentDescription colorAttachment({}, window.getVkSwapchainImageFormat(), vk::SampleCountFlagBits::e1,
                                                  colorLoadOp, vk::AttachmentStoreOp::eStore,
                                                  vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                                  vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
        vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

        if (depthTarget)
        {
            vk::AttachmentDescription depthAttachment(
                {}, depthTarget->getVkFormat(), vk::SampleCountFlagBits::e1, depthLoadOp,
                vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

            vk::AttachmentReference depthAttachmentRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef, {},
                                           &depthAttachmentRef);
            const auto srcStageMask =
                vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
            const auto dstStageMask =
                vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
            const auto dstAccessMask =
                vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0, srcStageMask, dstStageMask, {}, dstAccessMask);
            const std::array attachments = { colorAttachment, depthAttachment };
            vk::RenderPassCreateInfo renderPassInfo({}, attachments, subpass, dependency);
            mRenderPass = vkDevice->createRenderPassUnique(renderPassInfo);
        }
        else
        {
            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef);
            const auto srcStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            const auto dstStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            const auto dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0, srcStageMask, dstStageMask, {}, dstAccessMask);
            vk::RenderPassCreateInfo renderPassInfo({}, colorAttachment, subpass, dependency);
            mRenderPass = vkDevice->createRenderPassUnique(renderPassInfo);
        }

        recreateFrameBuffers(window, depthTarget);
    }

    RenderPass::~RenderPass()
    {
    }

    void RenderPass::recreateFrameBuffers(Window& window, const Handle<Image> depthTarget)
    {
        const auto& swapchainImageViews = window.getVkImageViews();
        const auto extent               = window.getVkSwapchainExtent();
        const auto& vkDevice            = mDevice.getVkDevice();

        mFrameBuffers.clear();
        mFrameBuffers.reserve(swapchainImageViews.size());

        for (const auto& view : swapchainImageViews)
        {
            std::vector attachments = { view.get() };
            if (depthTarget)
            {
                attachments.emplace_back(depthTarget->getVkImageView().get());
            }

            vk::FramebufferCreateInfo framebufferInfo({}, mRenderPass.get(), attachments, extent.width, extent.height,
                                                      1);
            mFrameBuffers.emplace_back(vkDevice->createFramebufferUnique(framebufferInfo));
        }
    }

    void RenderPass::recreateFrameBuffers(const vk::ArrayProxy<Handle<Image>>& colorTargets,
                                          const Handle<Image> depthTarget)
    {
        const auto extent    = colorTargets.front()->getVkExtent();
        const auto& vkDevice = mDevice.getVkDevice();

        std::vector<vk::ImageView> views;
        views.reserve(colorTargets.size() + 1);
        for (const auto& ct : colorTargets)
        {
            views.emplace_back(ct->getVkImageView().get());
        }
        if (depthTarget)
        {
            views.emplace_back(depthTarget->getVkImageView().get());
        }

        vk::FramebufferCreateInfo framebufferInfo({}, mRenderPass.get(), views, extent.width, extent.height, 1);
        mFrameBuffers.resize(1);
        mFrameBuffers.front() = vkDevice->createFramebufferUnique(framebufferInfo);
    }

    const vk::UniqueRenderPass& RenderPass::getVkRenderPass()
    {
        return mRenderPass;
    }

    const std::vector<vk::UniqueFramebuffer>& RenderPass::getVkFrameBuffers()
    {
        return mFrameBuffers;
    }

    vk::Format RenderPass::findSupportedFormat(const vk::ArrayProxy<vk::Format>& candidates, vk::ImageTiling tiling,
                                               vk::FormatFeatureFlags features)
    {
        const auto& physDev = mDevice.getVkPhysicalDevice();

        for (const auto format : candidates)
        {
            vk::FormatProperties props = physDev.getFormatProperties(format);
            if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        assert(!"failed to find supported format!");
        return candidates.front();
    }
}  // namespace vk2s
