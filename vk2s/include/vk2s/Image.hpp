/*****************************************************************/ /**
 * @file   Image.hpp
 * @brief  header file of Image class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_IMAGE_HPP_
#define VK2S_INCLUDE_IMAGE_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "Macro.hpp"

namespace vk2s
{
    //! forward declaration
    class Device;

    /**
     * @brief  class representing an image on GPU memory 
     */
    class Image
    {
    public:  // methods
        /**
         * @brief  constructor
         */
        Image(Device& device, const vk::ImageCreateInfo& ii, const vk::MemoryPropertyFlags pbs, const size_t size,
              const vk::ImageAspectFlags aspectFlags);

        /**
         * @brief  constructor with imageview crate info
         */
        Image(Device& device, const vk::ImageCreateInfo& ii, const vk::MemoryPropertyFlags pbs, const size_t size,
              const vk::ImageViewType viewType, const vk::ImageSubresourceRange subresourceRange);

        /**
         * @brief  constructor with image data path
         */
        Image(Device& device, std::string_view path, vk::Format format = vk::Format::eR8G8B8A8Srgb);

        /**
         * @brief  destructor
         */
        ~Image();

        NONCOPYABLE(Image);
        NONMOVABLE(Image);

        /**
         * @brief  writes data to image
         * 
         * @param pSrc pointer to copy source memory area
         * @param size size of copy source memory area
         */
        void write(const void* pSrc, const size_t size);

        /**
         * @brief  load the specified image file
         */
        void loadFromFile(std::string_view path);

        /**
         * @brief  get vulkan image handle 
         */
        const vk::UniqueImage& getVkImage();

        /**
         * @brief  get vulkan device memory
         */
        const vk::UniqueDeviceMemory& getVkDeviceMemory();

        /**
         * @brief  get vulkan image view handle
         */
        const vk::UniqueImageView& getVkImageView();

        /**
         * @brief  get vulkan image format
         */
        vk::Format getVkFormat() const;

        /**
         * @brief  get vulkan image extent
         */
        vk::Extent3D getVkExtent() const;

        /**
         * @brief  get vulkan image view aspect flag
         */
        vk::ImageAspectFlags getVkAspectFlag() const;

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan image handle
        vk::UniqueImage mImage;
        //! vulkan device memory
        vk::UniqueDeviceMemory mMemory;
        //! vulkan image view handle
        vk::UniqueImageView mImageView;
        //! vulkan image extent(3D)
        vk::Extent3D mExtent;
        //! vulkan image format
        vk::Format mFormat;
        //! vulkan image aspect flag
        vk::ImageAspectFlags mAspectFlag;
    };
}  // namespace vk2s

#endif
