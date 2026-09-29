/*****************************************************************/ /**
 * \file   RenderPass.hpp
 * \brief  header file of RenderPass class
 * 
 * \author ichi-raven
 * \date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_RENDERPASS_HPP_
#define VK2S_INCLUDE_RENDERPASS_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "SlotMap.hpp"
#include "Macro.hpp"

namespace vk2s
{
    //! forward declaration
    class Device;

    class Image;
    class Window;

    /**
     * @brief  class that specifies the dependency between input and output images for each rendering pass
     */
    class RenderPass
    {
    public:  // methods
        /**
         * @brief  constructor (for offscreen(multi-pass) rendering, unified loadOp)
         */
        RenderPass(Device& device, const vk::ArrayProxy<Handle<Image>>& colorTargets,
                   const Handle<Image>& depthTarget  = Handle<Image>(),
                   const vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear);

        /**
         * @brief  constructor (for offscreen(multi-pass) rendering, individual loadOp)
         */
        RenderPass(Device& device, const vk::ArrayProxy<Handle<Image>>& colorTargets,
                   const vk::ArrayProxy<vk::AttachmentLoadOp>& loadOps,
                   const Handle<Image>& depthTarget = Handle<Image>());

        /**
         * @brief  constructor (for screen rendering)
         */
        RenderPass(Device& device, Window& window, const vk::AttachmentLoadOp colorLoadOp,
                   const Handle<Image>& depthTarget       = Handle<Image>(),
                   const vk::AttachmentLoadOp depthLoadOp = vk::AttachmentLoadOp::eClear);

        /**
         * @brief  destructor
         */
        ~RenderPass();

        NONCOPYABLE(RenderPass);
        NONMOVABLE(RenderPass);

        /**
         * @brief re-create by resizing the window (for screen frame buffer)
         */
        void recreateFrameBuffers(Window& window, const Handle<Image> depthTarget = Handle<Image>());

        /**
         * @brief re-create by resizing the window (for offscreen(image) frame buffer)
         */
        void recreateFrameBuffers(const vk::ArrayProxy<Handle<Image>>& colorTargets,
                                  const Handle<Image> depthTarget = Handle<Image>());

        /**
         * @brief  get vulkan renderpass handle
         */
        const vk::UniqueRenderPass& getVkRenderPass();

        /**
         * @brief  get vulkan frame buffer handles
         */
        const std::vector<vk::UniqueFramebuffer>& getVkFrameBuffers();

    private:  // methods
        /**
         * @brief  find supported formats for this renderpass from the specified candidates
         */
        vk::Format findSupportedFormat(const vk::ArrayProxy<vk::Format>& candidates, vk::ImageTiling tiling,
                                       vk::FormatFeatureFlags features);

    private:  // member variables
        //! rederence to device
        Device& mDevice;

        //! vulkan renderpass handle
        vk::UniqueRenderPass mRenderPass;
        //! vulkan framebuffer handles
        std::vector<vk::UniqueFramebuffer> mFrameBuffers;
    };
}  // namespace vk2s

#endif
