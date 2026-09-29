/*****************************************************************/ /**
 * @file   Command.cpp
 * @brief  source file of Command class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/

#include "../include/vk2s/Command.hpp"

#include "../include/vk2s/Device.hpp"

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stdexcept>

namespace vk2s
{
    Command::Command(Device& device)
        : mDevice(device)
    {
        vk::CommandBufferAllocateInfo allocInfo(mDevice.getVkCommandPool().get(), vk::CommandBufferLevel::ePrimary, 1);

        mCommandBuffer = std::move(mDevice.getVkDevice()->allocateCommandBuffersUnique(allocInfo).front());
    }

    Command::~Command() = default;

    void Command::reset()
    {
        mCommandBuffer->reset();
    }

    void Command::begin(const bool singleTimeUse, const bool secondaryUse, const bool simultaneousUse)
    {
        vk::CommandBufferUsageFlags usage{};
        if (singleTimeUse)
        {
            usage |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        }
        if (secondaryUse)
        {
            usage |= vk::CommandBufferUsageFlagBits::eRenderPassContinue;
        }
        if (simultaneousUse)
        {
            usage |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
        }

        mCommandBuffer->begin(vk::CommandBufferBeginInfo(usage));
    }

    void Command::end()
    {
        mCommandBuffer->end();
    }

    void Command::beginRenderPass(RenderPass& renderpass, const uint32_t frameBufferIndex, const vk::Rect2D& area,
                                  const vk::ArrayProxyNoTemporaries<const vk::ClearValue>& clearValues)
    {
        vk::RenderPassBeginInfo bi(renderpass.getVkRenderPass().get(),
                                   renderpass.getVkFrameBuffers()[frameBufferIndex].get(), area, clearValues);
        mCommandBuffer->beginRenderPass(bi, vk::SubpassContents::eInline);
    }

    void Command::endRenderPass()
    {
        mCommandBuffer->endRenderPass();
    }

    void Command::setPipeline(Handle<Pipeline> pipeline)
    {
        mCommandBuffer->bindPipeline(pipeline->getVkPipelineBindPoint(), pipeline->getVkPipeline().get());
        mNowPipeline = pipeline;
    }

    void Command::setBindGroup(const uint8_t set, BindGroup& bindGroup,
                               vk::ArrayProxy<const uint32_t> const& dynamicOffsets)
    {
        if (!mNowPipeline)
        {
            throw std::runtime_error("pipeline isn't set yet!");
            return;
        }

        mCommandBuffer->bindDescriptorSets(mNowPipeline->getVkPipelineBindPoint(),
                                           mNowPipeline->getVkPipelineLayout().get(), set,
                                           bindGroup.getVkDescriptorSet(), dynamicOffsets);
    }

    void Command::setViewport(const uint32_t firstViewport, const vk::ArrayProxy<vk::Viewport> viewports)
    {
        if (!mNowPipeline)
        {
            throw std::runtime_error("pipeline isn't set yet!");
            return;
        }

        mCommandBuffer->setViewport(firstViewport, viewports);
    }

    void Command::setScissor(const uint32_t firstScissor, const vk::ArrayProxy<vk::Rect2D> scissors)
    {
        if (!mNowPipeline)
        {
            throw std::runtime_error("pipeline isn't set yet!");
            return;
        }

        mCommandBuffer->setScissor(firstScissor, scissors);
    }

    void Command::setPushConstant(const vk::ShaderStageFlags shaderStage, const size_t offset, const size_t size,
                                  const void* const pData)
    {
        if (!mNowPipeline)
        {
            throw std::runtime_error("pipeline isn't set yet!");
            return;
        }

        mCommandBuffer->pushConstants(mNowPipeline->getVkPipelineLayout().get(), shaderStage, offset, size, pData);
    }

    void Command::bindVertexBuffer(Buffer& vertexBuffer)
    {
        mCommandBuffer->bindVertexBuffers(0, vertexBuffer.getVkBuffer().get(), { 0 });
    }

    void Command::bindIndexBuffer(Buffer& indexBuffer)
    {
        mCommandBuffer->bindIndexBuffer(indexBuffer.getVkBuffer().get(), 0, vk::IndexType::eUint32);
    }

    void Command::draw(const uint32_t vertexCount, const uint32_t instanceCount, const uint32_t firstVertex,
                       const uint32_t firstInstance)
    {
        mCommandBuffer->draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void Command::drawIndirect(Buffer& infoBuffer, const vk::DeviceSize offset, const uint32_t drawCount,
                               const uint32_t stride)
    {
        mCommandBuffer->drawIndirect(infoBuffer.getVkBuffer().get(), infoBuffer.getOffset(), drawCount, stride);
    }

    void Command::drawIndexed(const uint32_t indexCount, const uint32_t instanceCount, const uint32_t firstIndex,
                              const uint32_t vertexOffset, const uint32_t firstInstance)
    {
        mCommandBuffer->drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void Command::drawIndexedIndirect(Buffer& infoBuffer, const vk::DeviceSize offset, const uint32_t drawCount,
                                      const uint32_t stride)
    {
        mCommandBuffer->drawIndexedIndirect(infoBuffer.getVkBuffer().get(), offset, drawCount, stride);
    }

    void Command::traceRays(const ShaderBindingTable& shaderBindingTable, const uint32_t width, const uint32_t height,
                            const uint32_t depth)
    {
        const auto& sbtInfo = shaderBindingTable.getVkSBTInfo();
        mCommandBuffer->traceRaysKHR(sbtInfo.rgen, sbtInfo.miss, sbtInfo.hit, sbtInfo.callable, width, height, depth);
    }

    void Command::traceRaysIndirect(const ShaderBindingTable& shaderBindingTable,
                                    const vk::DeviceAddress infoBufferDeviceAddress)
    {
        const auto& sbtInfo = shaderBindingTable.getVkSBTInfo();
        mCommandBuffer->traceRaysIndirectKHR(sbtInfo.rgen, sbtInfo.miss, sbtInfo.hit, sbtInfo.callable,
                                             infoBufferDeviceAddress);
    }

    void Command::traceRaysIndirect2(const vk::DeviceAddress infoBufferDeviceAddress)
    {
        mCommandBuffer->traceRaysIndirect2KHR(infoBufferDeviceAddress);
    }

    void Command::dispatch(const uint32_t groupCountX, const uint32_t groupCountY, const uint32_t groupCountZ,
                           const uint32_t countBaseX, const uint32_t countBaseY, const uint32_t countBaseZ)
    {
        mCommandBuffer->dispatchBase(countBaseX, countBaseY, countBaseZ, groupCountX, groupCountY, groupCountZ);
    }

    void Command::dispatchIndirect(Buffer& infoBuffer, vk::DeviceSize offset)
    {
        mCommandBuffer->dispatchIndirect(infoBuffer.getVkBuffer().get(), offset);
    }

    void Command::resetQueryPool(vk::QueryPool queryPool, const uint32_t firstQuery, const uint32_t queryCount)
    {
        mCommandBuffer->resetQueryPool(queryPool, firstQuery, queryCount);
    }

    void Command::writeTimestamp(const vk::PipelineStageFlagBits stage, vk::QueryPool queryPool, const uint32_t query)
    {
        mCommandBuffer->writeTimestamp(stage, queryPool, query);
    }

    void Command::globalPipelineBarrier(const vk::MemoryBarrier barrier, const vk::PipelineStageFlags from,
                                        const vk::PipelineStageFlags to)
    {
        mCommandBuffer->pipelineBarrier(from, to, {}, barrier, {}, {});
    }

    void Command::bufferPipelineBarrier(const vk::BufferMemoryBarrier barrier, const vk::PipelineStageFlags from,
                                        const vk::PipelineStageFlags to)
    {
        mCommandBuffer->pipelineBarrier(from, to, {}, {}, barrier, {});
    }

    void Command::imagePipelineBarrier(const vk::ImageMemoryBarrier barrier, const vk::PipelineStageFlags from,
                                       const vk::PipelineStageFlags to)
    {
        mCommandBuffer->pipelineBarrier(from, to, {}, {}, {}, barrier);
    }

    void Command::transitionImageLayout(Image& image, const vk::ImageLayout from, const vk::ImageLayout to)
    {
        transitionLayoutInternal(image.getVkImage().get(), image.getVkAspectFlag(), from, to);
    }

    void Command::copyBuffer(Buffer& src, Buffer& dst, const vk::BufferCopy& copyInfo)
    {
        mCommandBuffer->copyBuffer(src.getVkBuffer().get(), dst.getVkBuffer().get(), copyInfo);
    }

    void Command::copyBufferToImage(Buffer& buffer, Image& image, const uint32_t width, const uint32_t height)
    {
        vk::BufferImageCopy region;
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask     = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;

        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(width, height, 1);

        mCommandBuffer->copyBufferToImage(buffer.getVkBuffer().get(), image.getVkImage().get(),
                                          vk::ImageLayout::eTransferDstOptimal, region);
    }

    void Command::copyImageToBuffer(Image& image, Buffer& buffer, const vk::BufferImageCopy& copyInfo)
    {
        mCommandBuffer->copyImageToBuffer(image.getVkImage().get(), vk::ImageLayout::eTransferSrcOptimal,
                                          buffer.getVkBuffer().get(), copyInfo);

        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask       = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask       = vk::AccessFlagBits::eHostRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = buffer.getVkBuffer().get();
        barrier.offset              = 0;
        barrier.size                = VK_WHOLE_SIZE;

        bufferPipelineBarrier(barrier, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost);
    }

    void Command::copyImage(Image& src, Image& dst, const vk::ImageCopy& region)
    {
        mCommandBuffer->copyImage(src.getVkImage().get(), vk::ImageLayout::eTransferSrcOptimal, dst.getVkImage().get(),
                                  vk::ImageLayout::eTransferDstOptimal, region);
    }

    void Command::copyImageToSwapchain(Image& src, Window& window, const vk::ImageCopy& region,
                                       const uint32_t frameBufferIndex)
    {
        auto swapchainImage = window.getVkImages().at(frameBufferIndex);
        transitionLayoutInternal(swapchainImage, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::ePresentSrcKHR,
                                 vk::ImageLayout::eTransferDstOptimal);
        mCommandBuffer->copyImage(src.getVkImage().get(), vk::ImageLayout::eTransferSrcOptimal, swapchainImage,
                                  vk::ImageLayout::eTransferDstOptimal, region);
    }

    void Command::clearImage(Image& target, const vk::ImageLayout layout, const vk::ClearValue& clearValue,
                             const vk::ArrayProxy<vk::ImageSubresourceRange>& ranges)
    {
        const auto aspect = target.getVkAspectFlag();

        if (aspect & vk::ImageAspectFlagBits::eColor)
        {
            mCommandBuffer->clearColorImage(target.getVkImage().get(), layout, clearValue.color, ranges);
        }
        else if (aspect & (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
        {
            mCommandBuffer->clearDepthStencilImage(target.getVkImage().get(), layout, clearValue.depthStencil, ranges);
        }
        else
        {
            throw vk2s::VkException("unsupported image aspect");
        }
    }

    void Command::fillBuffer(Buffer& buffer, const vk::DeviceSize offset, const vk::DeviceSize size,
                             const uint32_t value)
    {
        mCommandBuffer->fillBuffer(buffer.getVkBuffer().get(), offset, size, value);
    }

    void Command::drawImGui()
    {
        // ImGui command write
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), mCommandBuffer.get());
    }

    void Command::execute(const Handle<Fence>& fence, const Handle<Semaphore>& wait, const Handle<Semaphore>& signal)
    {
        vk::SubmitInfo submitInfo(nullptr, nullptr, mCommandBuffer.get(), nullptr);

        if (wait)
        {
            // TODO: change wait stage
            constexpr vk::PipelineStageFlags waitStage(vk::PipelineStageFlagBits::eColorAttachmentOutput);

            submitInfo.setWaitSemaphores(wait->getVkSemaphore().get());
            submitInfo.setWaitDstStageMask(waitStage);
        }

        if (signal)
        {
            submitInfo.setSignalSemaphores(signal->getVkSemaphore().get());
        }

        if (fence)
        {
            mDevice.getVkGraphicsQueue().submit(submitInfo, fence->getVkFence().get());
        }
        else
        {
            mDevice.getVkGraphicsQueue().submit(submitInfo);
        }
    }

    void Command::executeAndWait()
    {
        auto fence = mDevice.createUnique<vk2s::Fence>(false);
        execute(fence);

        if (!fence->wait())
        {
            throw std::runtime_error("command execution wait failed");
        }
    }

    const vk::UniqueCommandBuffer& Command::getVkCommandBuffer()
    {
        return mCommandBuffer;
    }

    inline void Command::transitionLayoutInternal(vk::Image image, vk::ImageAspectFlags flag,
                                                  const vk::ImageLayout from, const vk::ImageLayout to)
    {
        vk::ImageMemoryBarrier barrier;
        barrier.oldLayout                       = from;
        barrier.newLayout                       = to;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = flag;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        if (from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage      = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (from == vk::ImageLayout::eColorAttachmentOptimal && to == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage      = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else if (from == vk::ImageLayout::eShaderReadOnlyOptimal && to == vk::ImageLayout::eColorAttachmentOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

            sourceStage      = vk::PipelineStageFlagBits::eFragmentShader;
            destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        }
        else if (from == vk::ImageLayout::eTransferDstOptimal && to == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage      = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }
        else if (from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::eColorAttachmentOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask =
                vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

            sourceStage      = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        }
        else if (from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::eDepthStencilAttachmentOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask =
                vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

            sourceStage      = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        }
        else if (from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::eGeneral)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eNone;

            sourceStage      = vk::PipelineStageFlagBits::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }
        else if (from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage      = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }
        else if (from == vk::ImageLayout::eGeneral && to == vk::ImageLayout::eTransferSrcOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

            sourceStage      = vk::PipelineStageFlagBits::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (from == vk::ImageLayout::eGeneral && to == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage      = vk::PipelineStageFlagBits::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (from == vk::ImageLayout::ePresentSrcKHR && to == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage      = vk::PipelineStageFlagBits::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (from == vk::ImageLayout::eTransferSrcOptimal && to == vk::ImageLayout::eGeneral)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eNone;

            sourceStage      = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }
        else if (from == vk::ImageLayout::eTransferDstOptimal && to == vk::ImageLayout::eGeneral)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eNone;

            sourceStage      = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }
        else if (from == vk::ImageLayout::eShaderReadOnlyOptimal && to == vk::ImageLayout::eGeneral)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eNone;

            sourceStage      = vk::PipelineStageFlagBits::eFragmentShader;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }
        else if (from == vk::ImageLayout::eGeneral && to == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage      = vk::PipelineStageFlagBits::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else if (from == vk::ImageLayout::eTransferDstOptimal && to == vk::ImageLayout::eTransferSrcOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

            sourceStage      = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (from == vk::ImageLayout::eTransferSrcOptimal && to == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage      = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::ePresentSrcKHR)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eNone;

            sourceStage      = vk::PipelineStageFlagBits::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        mCommandBuffer->pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);
    }

}  // namespace vk2s
