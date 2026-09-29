/*****************************************************************/ /**
 * @file   Device.hpp
 * @brief  header file of Device class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/

#ifndef VK2S_INCLUDE_DEVICE_HPP_
#define VK2S_INCLUDE_DEVICE_HPP_

#include <optional>
#include <array>
#include <utility>
#include <tuple>
#include <type_traits>

#include "SlotMap.hpp"
#include "Macro.hpp"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "Window.hpp"
#include "Buffer.hpp"
#include "DynamicBuffer.hpp"
#include "Fence.hpp"
#include "Image.hpp"
#include "Sampler.hpp"
#include "RenderPass.hpp"
#include "Shader.hpp"
#include "BindLayout.hpp"
#include "Pipeline.hpp"
#include "BindGroup.hpp"
#include "Command.hpp"
#include "Semaphore.hpp"
#include "AccelerationStructure.hpp"
#include "ShaderBindingTable.hpp"

namespace vk2s
{
    /**
     * @brief  type to terminate type expansion recursion
     */
    template <class T, class TypeList>
    struct IsContainedIn;

    /**
     * @brief  type to determine if type T is contained within variable-length argument Ts
     */
    template <class T, class... Ts>
    struct IsContainedIn<T, std::tuple<Ts...>> : std::bool_constant<(... || std::is_same<T, Ts>{})>
    {
    };

    /**
     * @brief  index of each command queue (with value only if appropriate one exists)
     */
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const
        {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }

        QueueFamilyIndices static findQueueFamilies(vk::PhysicalDevice device, const vk::UniqueSurfaceKHR& testSurface);
    };

    /**
     * @brief  class that manages Instance, (Physical/Logical)Device, etc. of Vulkan, owning all other objects
     */
    class Device
    {
    public:  // types
        /**
         * @brief  type to query whether the swap chain satisfies the various settings required by the swap chain
         */
        struct SwapChainSupportDetails
        {
            vk::SurfaceCapabilitiesKHR capabilities;
            std::vector<vk::SurfaceFormatKHR> formats;
            std::vector<vk::PresentModeKHR> presentModes;

            SwapChainSupportDetails static querySwapChainSupport(vk::PhysicalDevice device,
                                                                 const vk::UniqueSurfaceKHR& testSurface);
        };

        /**
         * @brief  Vulkan Extensions available in this library
         */
        struct Extensions
        {
            static Extensions useAll()
            {
                Extensions ext;
                ext.useRayTracingExt     = true;
                ext.useNVMotionBlurExt   = true;
                ext.useExternalMemoryExt = true;
                return ext;
            }

            static Extensions useNothing()
            {
                Extensions ext;
                ext.useRayTracingExt     = false;
                ext.useNVMotionBlurExt   = false;
                ext.useExternalMemoryExt = false;
                return ext;
            }

            bool useRayTracingExt     = false;
            bool useNVMotionBlurExt   = false;
            bool useExternalMemoryExt = false;
        };

    public:  // methods
        /**
         * @brief  constructor
         * @detail if you would not use window, pass false
         */
        Device(const bool useWindow = true);

        /**
         * @brief constructor (with extensions)
         */
        explicit Device(const Extensions extensions, const bool useWindow = true);

        /**
         * @brief  destructor
         */
        ~Device();

        NONCOPYABLE(Device);

        /**
         * @brief  creates an instance of the specified type T and returns a handle
         * @detail compile-time error if not a vk2s object
         */
        template <typename T, size_t PageSize = kDefaultPageSize, typename Allocator = DefaultAllocator,
                  typename... Args>
        Handle<T, PageSize, DefaultAllocator> create(Args&&... args)
        {
            static_assert(IsContainedIn<Pool<T, PageSize, DefaultAllocator>, decltype(mPools)>::value,
                          "invalid type of pool!");
            return std::get<Pool<T, PageSize, DefaultAllocator>>(mPools).allocate(*this, std::forward<Args>(args)...);
        }

        /**
         * @brief creates an instance of the specified type T and returns a "unique" handle
         * @detail compile-time error if not a vk2s object
         */
        template <typename T, size_t PageSize = kDefaultPageSize, typename Allocator = DefaultAllocator,
                  typename... Args>
        UniqueHandle<T, PageSize, DefaultAllocator> createUnique(Args&&... args)
        {
            static_assert(IsContainedIn<Pool<T, PageSize, DefaultAllocator>, decltype(mPools)>::value,
                          "invalid type of pool!");
            auto handle =
                std::get<Pool<T, PageSize, DefaultAllocator>>(mPools).allocate(*this, std::forward<Args>(args)...);

            return UniqueHandle<T, PageSize, DefaultAllocator>(std::move(handle));
        }

        /**
         * @brief  destroy an instance of the specified handle
         * @detail compile-time error if not a vk2s object
         */
        template <typename T, size_t PageSize = kDefaultPageSize, typename Allocator = DefaultAllocator>
        bool destroy(Handle<T, PageSize, DefaultAllocator>& handle)
        {
            static_assert(IsContainedIn<Pool<T, PageSize, DefaultAllocator>, decltype(mPools)>::value,
                          "invalid type of pool!");
            return std::get<Pool<T, PageSize, DefaultAllocator>>(mPools).deallocate(handle);
        }

        /**
         * @brief  initialize ImGui for a given window/renderpass
         */
        void initImGui(Window& window, RenderPass& renderpass);

        /**
         * @brief  release ImGui and its related objects
         */
        void destroyImGui();

        /**
         * @brief  wait for task submission to the GPU from the time this function is executed, and return processing when the GPU is idle
         */
        void waitIdle();

        /**
         * @brief  get the name of the physical device (GPU) on which it is running
         */
        std::string_view getPhysicalDeviceName() const;

        /**
         * @brief  get index for memory from Vulkan's requirements for device memory
         */
        uint32_t getVkMemoryTypeIndex(uint32_t requestBits, vk::MemoryPropertyFlags requestProps) const;

        /**
         * @brief  get the status of the active extension
         */
        Extensions getVkAvailableExtensions() const;

        /**
         * @brief get vulkan instance handle
         */
        const vk::UniqueInstance& getVkInstance();

        /**
         * @brief  get vulkan physical device handle
         */
        const vk::PhysicalDevice& getVkPhysicalDevice();

        /**
         * @brief  get vulkan logical device handle
         */
        const vk::UniqueDevice& getVkDevice();

        /**
         * @brief  get index of each vulkan command queue
         */
        const QueueFamilyIndices getVkQueueFamilyIndices() const;

        /**
         * @brief  get vulkan queue to submit graphics commands
         */
        const vk::Queue& getVkGraphicsQueue();

        /**
         * @brief  get vulkan queue to submit present commands
         */
        const vk::Queue& getVkPresentQueue();

        /**
         * @brief  get vulkan command pool
         */
        const vk::UniqueCommandPool& getVkCommandPool();

        /**
         * @brief  allocate a DescriptorSet that satisfies DescriptorPoolAllocationInfor
         */
        const std::pair<vk::DescriptorSet, size_t> allocateVkDescriptorSet(
            const vk::DescriptorSetLayout& layout, const DescriptorPoolAllocationInfo& allocInfo);

        /**
         * @brief  deallocate a DescriptorSet
         */
        void deallocateVkDescriptorSet(vk::DescriptorSet& set, const size_t poolIndex,
                                       const DescriptorPoolAllocationInfo& allocInfo);

        /**
         * @brief  aligns the size along the specified
         */
        template <class T>
        T align(T size, uint32_t align)
        {
            return (size + align - 1) & ~static_cast<T>((align - 1));
        }

    private:  // types
        /**
         * @brief  DescriptorPool and its current allocation status
         */
        struct DescriptorPool
        {
            vk::UniqueDescriptorPool descriptorPool;
            DescriptorPoolAllocationInfo now;
            uint32_t remainingSets = 0;
        };

    private:  // compile time constant
              //! flag to enable or disable the verification layer
#ifdef NDEBUG
        constexpr static bool enableValidationLayers = false;
#else
        constexpr static bool enableValidationLayers = true;
#endif

#ifdef WIN32
        // ADHOC?:
#define VK_KHR_EXTERNAL_MEMORY_PLATFORM_EXTENSION_NAME "VK_KHR_external_memory_win32"
#else
#define VK_KHR_EXTERNAL_MEMORY_PLATFORM_EXTENSION_NAME VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
#endif

#define VK_KHR_VALIDATION_LAYER_NAME "VK_LAYER_KHRONOS_validation"

        //! name of the validation layer
        constexpr static std::array validationLayers = { VK_KHR_VALIDATION_LAYER_NAME };

        //! normal device extensions required for vk2s
        constexpr static std::array baseDeviceExtensions = {
            //VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
        };

        //! device extensions required for hardware accelerated ray tracing
        constexpr static std::array rayTracingDeviceExtensions = {
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        };

        //! NV_Motion_blur extension
        constexpr static const char* nvRayTracingMotionBlurExtension = VK_NV_RAY_TRACING_MOTION_BLUR_EXTENSION_NAME;

        //! external memory instance extensions
        constexpr static std::array externalMemoryInstanceExtensions = {
            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
        };

        //! external memory device extensions
        constexpr static std::array externalMemoryDeviceExtensions = {
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_PLATFORM_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
        };

        //! all instance extensions
        constexpr static std::array allInstanceExtensions = {
            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
        };

        //! all device extensions
        constexpr static std::array allDeviceExtensions = {
            //VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_NV_RAY_TRACING_MOTION_BLUR_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_PLATFORM_EXTENSION_NAME,
        };

        //! maximum number of descriptors allocated by one DescriptorPool (adhoc)
        constexpr static uint32_t kMaxDescriptorNum = 256;

    private:  // methods
        /**
         * @brief  create vulkan instance
         */
        void createInstance(const bool useWindow);

        /**  
         * @brief set output callbacks for the verification layer for debugging
         */
        void setupDebugMessenger();

        /**
         * @brief summarizing vulkan physical/logical device creation
         */
        void pickAndCreateDevice(const bool useWindow);

        /**
         * @brief pick the vulkan physical device to be used from the surface criteria 
         */
        void pickPhysicalDevice(const vk::UniqueSurfaceKHR& testSurface);

        /**
         * @brief  create vulkan logical device from selected physical device
         */
        void createLogicalDevice(const vk::UniqueSurfaceKHR& testSurface);

        /**
         * @brief  create vulkan command pool
         */
        void createCommandPool();

        /**
         * @brief  create vulkan descriptor pool
         */
        void createDescriptorPool(const DescriptorPoolAllocationInfo& minimum = {});

        // imgui--------------
        /**
         * @brief  initialize ImGui
         */
        void initImGui();

        /**
         * @brief  create a vulkan descriptor pool to be used by ImGui
         */
        void createDescriptorPoolForImGui();
        // -------------------

        /**
         * @brief  get required extension names to GLFW, etc.
         */
        std::vector<const char*> getRequiredExtensions(const bool useWindow);
        /**
         * @brief  check if the instance layer is supported in your environment
         */
        bool checkInstanceLayerSupport();

        /**
         * @brief  check if the instance extension is supported in your environment
         */
        bool checkInstanceExtensionSupport();
        /**
         * @brief  verify that the specified physical devices and surfaces meet our requirements
         */
        bool isDeviceSuitable(vk::PhysicalDevice device, const vk::UniqueSurfaceKHR& testSurface);
        /**
         * @brief  check if the extension requested by vk2s is supported
         */
        bool checkDeviceExtensionSupport(vk::PhysicalDevice device) const;

/**
         * @brief  callback to obtain the output of vulkan's verification layer as a string
         */
#if VK_HEADER_VERSION >= 305
        static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugUtilsMessengerCallback(
            vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
            vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* pUserData);
#else
        static VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
            VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* pUserData);
#endif

    private:  // member variables
        //! vulkan instance
        vk::UniqueInstance mInstance;
        //! vulkan debug messenger
        vk::UniqueDebugUtilsMessengerEXT mDebugUtilsMessenger;

        //! whether hardware accelerated ray tracing is supported
        //const bool mRayTracingSupported;

        //! whether various extensions are requested (synonymous with available extensions if created on device success)
        const Extensions mQueriedExtensions;

        //! vulkan physical device
        vk::PhysicalDevice mPhysicalDevice;
        //! vulkan memory properties of the selected physical device
        vk::PhysicalDeviceMemoryProperties mPhysMemProps;
        //! vulkan logical device
        vk::UniqueDevice mDevice;

        //! index of each device queue
        QueueFamilyIndices mQueueFamilyIndices;
        //! vulkan device queue for graphics commands
        vk::Queue mGraphicsQueue;
        //! vulkan device queue for present commands
        vk::Queue mPresentQueue;

        //! vulkan command pool
        vk::UniqueCommandPool mCommandPool;

        //! vulkan descriptor pools (simple linear allocation)
        std::vector<DescriptorPool> mDescriptorPools;

        // imgui--------------
        //! vulkan descriptor pool only for Imgui
        vk::UniqueDescriptorPool mDescriptorPoolForImGui;
        //! indicates whether imGui is available
        bool mImGuiActive;

    private:  // pools
        //! tuple of pools where each instance of vk2s is stored
        std::tuple<Pool<Window>, Pool<Buffer>, Pool<Image>, Pool<Sampler>, Pool<RenderPass>, Pool<Shader>,
                   Pool<BindLayout>, Pool<BindGroup>, Pool<Pipeline>, Pool<Semaphore>, Pool<Fence>, Pool<Command>,
                   Pool<AccelerationStructure>, Pool<ShaderBindingTable>, Pool<DynamicBuffer>>
            mPools;
    };
}  // namespace vk2s

#endif
