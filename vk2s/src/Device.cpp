#include "../include/vk2s/Device.hpp"

#include <GLFW/glfw3.h>

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <set>
#include <iostream>
#include <algorithm>
#include <stdexcept>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vk2s
{
    Device::Device(const bool useWindow)
        : Device(Extensions::useNothing(), useWindow)
    {
    }

    Device::Device(const Extensions extensions, const bool useWindow)
        : mQueriedExtensions(extensions)
        , mImGuiActive(false)
    {
        glfwInit();

        createInstance(useWindow);
        setupDebugMessenger();
        pickAndCreateDevice(useWindow);
        createCommandPool();

        createDescriptorPoolForImGui();

        createDescriptorPool();
    }

    template <size_t N = 0, typename T>
    void iterateTupleAndClear(T& t)
    {
        if constexpr (N < std::tuple_size<T>::value)
        {
            auto& x = std::get<N>(t);
            x.clear();
            iterateTupleAndClear<N + 1>(t);
        }
    }

    Device::~Device()
    {
        mDevice->waitIdle();

        iterateTupleAndClear(mPools);

        destroyImGui();
        if (ImGui::GetCurrentContext())
        {
            ImGui::DestroyContext();
        }

        glfwTerminate();
    }

    void Device::waitIdle()
    {
        mDevice->waitIdle();
    }

    void Device::createDescriptorPoolForImGui()
    {
        vk::DescriptorPoolSize size(vk::DescriptorType::eCombinedImageSampler, 1);

        vk::DescriptorPoolCreateInfo ci(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, size);

        mDescriptorPoolForImGui = mDevice->createDescriptorPoolUnique(ci);
    }

    void Device::initImGui(Window& window, RenderPass& renderpass)
    {
        // if the context is not set, create a new one
        if (!ImGui::GetCurrentContext())
        {
            ImGui::CreateContext();
        }

        ImGui_ImplGlfw_InitForVulkan(window.getpGLFWWindow(), true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance                  = mInstance.get();
        init_info.PhysicalDevice            = mPhysicalDevice;
        init_info.Device                    = mDevice.get();
        init_info.QueueFamily               = mQueueFamilyIndices.graphicsFamily.value();
        init_info.Queue                     = mGraphicsQueue;
        init_info.PipelineCache             = VK_NULL_HANDLE;
        init_info.DescriptorPool            = mDescriptorPoolForImGui.get();
        init_info.Allocator                 = VK_NULL_HANDLE;
        init_info.MinImageCount             = 2;
        init_info.ImageCount                = window.getFrameCount();
        init_info.CheckVkResultFn           = nullptr;
        //init_info.RenderPass                = renderpass.getVkRenderPass().get();
        ImGui_ImplVulkan_Init(&init_info);

        mImGuiActive = true;
    }

    void Device::destroyImGui()
    {
        if (mImGuiActive)
        {
            //ImGui_ImplVulkan_DestroyFontsTexture();
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            mImGuiActive = false;
        }
    }

    std::string_view Device::getPhysicalDeviceName() const
    {
        return mPhysicalDevice.getProperties().deviceName;
    }

    const vk::UniqueInstance& Device::getVkInstance()
    {
        return mInstance;
    }

    const vk::PhysicalDevice& Device::getVkPhysicalDevice()
    {
        return mPhysicalDevice;
    }

    const vk::UniqueDevice& Device::getVkDevice()
    {
        return mDevice;
    }

    const QueueFamilyIndices Device::getVkQueueFamilyIndices() const
    {
        return mQueueFamilyIndices;
    }

    const vk::Queue& Device::getVkGraphicsQueue()
    {
        return mGraphicsQueue;
    }

    const vk::Queue& Device::getVkPresentQueue()
    {
        return mPresentQueue;
    }

    const vk::UniqueCommandPool& Device::getVkCommandPool()
    {
        return mCommandPool;
    }

    const std::pair<vk::DescriptorSet, size_t> Device::allocateVkDescriptorSet(
        const vk::DescriptorSetLayout& layout, const DescriptorPoolAllocationInfo& allocInfo)
    {
        if (!mQueriedExtensions.useRayTracingExt && allocInfo.accelerationStructureNum != 0)
        {
            throw std::runtime_error("acceleration structure descriptors require ray tracing support");
        }

        size_t idx = 0;
        for (; idx < mDescriptorPools.size(); ++idx)
        {
            const auto& available = mDescriptorPools[idx].now;

            const bool isEnough = mDescriptorPools[idx].remainingSets > 0 &&
                                  available.accelerationStructureNum >= allocInfo.accelerationStructureNum &&
                                  available.combinedImageSamplerNum >= allocInfo.combinedImageSamplerNum &&
                                  available.sampledImageNum >= allocInfo.sampledImageNum &&
                                  available.samplerNum >= allocInfo.samplerNum &&
                                  available.storageBufferNum >= allocInfo.storageBufferNum &&
                                  available.storageImageNum >= allocInfo.storageImageNum &&
                                  available.uniformBufferNum >= allocInfo.uniformBufferNum &&
                                  available.uniformBufferDynamicNum >= allocInfo.uniformBufferDynamicNum;

            if (isEnough)
            {
                break;
            }
        }

        if (idx == mDescriptorPools.size())
        {
            createDescriptorPool(allocInfo);
        }

        auto& allocatedPool = mDescriptorPools[idx];

        vk::DescriptorSetAllocateInfo ai(allocatedPool.descriptorPool.get(), layout);
        const auto descriptorSet = mDevice->allocateDescriptorSets(ai).front();

        // Update book-keeping only after allocation succeeded
        allocatedPool.now.accelerationStructureNum -= allocInfo.accelerationStructureNum;
        allocatedPool.now.combinedImageSamplerNum -= allocInfo.combinedImageSamplerNum;
        allocatedPool.now.sampledImageNum -= allocInfo.sampledImageNum;
        allocatedPool.now.samplerNum -= allocInfo.samplerNum;
        allocatedPool.now.storageBufferNum -= allocInfo.storageBufferNum;
        allocatedPool.now.storageImageNum -= allocInfo.storageImageNum;
        allocatedPool.now.uniformBufferNum -= allocInfo.uniformBufferNum;
        allocatedPool.now.uniformBufferDynamicNum -= allocInfo.uniformBufferDynamicNum;

        --allocatedPool.remainingSets;

        return { descriptorSet, idx };
    }

    void Device::deallocateVkDescriptorSet(vk::DescriptorSet& set, const size_t poolIndex,
                                           const DescriptorPoolAllocationInfo& allocInfo)
    {
        auto& allocatedPool = mDescriptorPools[poolIndex];

        mDevice->freeDescriptorSets(allocatedPool.descriptorPool.get(), set);

        allocatedPool.now.accelerationStructureNum += allocInfo.accelerationStructureNum;
        allocatedPool.now.combinedImageSamplerNum += allocInfo.combinedImageSamplerNum;
        allocatedPool.now.sampledImageNum += allocInfo.sampledImageNum;
        allocatedPool.now.samplerNum += allocInfo.samplerNum;
        allocatedPool.now.storageBufferNum += allocInfo.storageBufferNum;
        allocatedPool.now.storageImageNum += allocInfo.storageImageNum;
        allocatedPool.now.uniformBufferNum += allocInfo.uniformBufferNum;
        allocatedPool.now.uniformBufferDynamicNum += allocInfo.uniformBufferDynamicNum;

        ++allocatedPool.remainingSets;
    }

#if VK_HEADER_VERSION >= 301
    using VulkanDynamicLoader = vk::detail::DynamicLoader;
#else
    using VulkanDynamicLoader = vk::DynamicLoader;
#endif

    void Device::createInstance(const bool useWindow)
    {
        // TODO: change application name
        constexpr std::string_view applicationName = "vk2s application";

        // get the instance independent function pointers
        static VulkanDynamicLoader dl;
        auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        const bool instanceLayerSupported     = checkInstanceLayerSupport();
        const bool instanceExtensionSupported = checkInstanceExtensionSupport();
        if (enableValidationLayers)
        {
            if (!instanceLayerSupported)
            {
                throw std::runtime_error("instance layers requested, but not available!");
            }
            if (!instanceExtensionSupported)
            {
                throw std::runtime_error("instance extensions requested, but not available!");
            }

            if (!(checkInstanceLayerSupport() && checkInstanceExtensionSupport()))
            {
                throw std::runtime_error("instance extension layers requested, but not available!");
            }
        }

        vk::ApplicationInfo appInfo(applicationName.data(), VK_MAKE_VERSION(1, 0, 0), "vk2s", VK_MAKE_VERSION(1, 0, 0),
                                    VK_API_VERSION_1_3);

        const auto extensions = getRequiredExtensions(useWindow);

        if (enableValidationLayers)
        {
            // in debug mode, use the debugUtilsMessengerCallback
            vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);

            vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                               vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                                               vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

            vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> createInfo(
                { {}, &appInfo, validationLayers, extensions },
                { {}, severityFlags, messageTypeFlags, &debugUtilsMessengerCallback });
            mInstance = vk::createInstanceUnique(createInfo.get<vk::InstanceCreateInfo>());
        }
        else
        {
            // in non-debug mode
            vk::InstanceCreateInfo createInfo({}, &appInfo, {}, extensions);
            mInstance = vk::createInstanceUnique(createInfo, nullptr);
        }

        // get all the other function pointers
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*mInstance);
    }

    void Device::setupDebugMessenger()
    {
        if (!enableValidationLayers)
        {
            return;
        }

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                                           vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

        mDebugUtilsMessenger = mInstance->createDebugUtilsMessengerEXTUnique(
            vk::DebugUtilsMessengerCreateInfoEXT({}, severityFlags, messageTypeFlags, &debugUtilsMessengerCallback));
    }

    void Device::pickAndCreateDevice(const bool useWindow)
    {
        // HACK: create test surface
        if (useWindow)
        {
            VkSurfaceKHR surface;

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            const auto pTestWindow = glfwCreateWindow(1, 1, "surface test", nullptr, nullptr);
            auto res = glfwCreateWindowSurface(VkInstance(mInstance.get()), pTestWindow, nullptr, &surface);
            if (res != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create window surface!");
            }
            const auto testSurface = vk::UniqueSurfaceKHR(surface, { mInstance.get() });

            pickPhysicalDevice(testSurface);

            createLogicalDevice(testSurface);

            glfwDestroyWindow(pTestWindow);
        }
        else
        {
            pickPhysicalDevice(vk::UniqueSurfaceKHR());

            createLogicalDevice(vk::UniqueSurfaceKHR());
        }
    }

    void Device::pickPhysicalDevice(const vk::UniqueSurfaceKHR& testSurface)
    {
        std::vector<vk::PhysicalDevice> physDevs = mInstance->enumeratePhysicalDevices();
        std::vector<vk::PhysicalDevice> goodDevs;

        for (const auto& physDev : physDevs)
        {
            if (isDeviceSuitable(physDev, testSurface))
            {
                goodDevs.push_back(physDev);
            }
        }

        if (goodDevs.empty())
        {
            throw std::runtime_error("failed to find a suitable GPU!");
        }

        if (goodDevs.size() == 1)
        {
            mPhysicalDevice = goodDevs[0];
        }
        else
        {
            std::cout << "=== choose the physical device ===\n";
            for (size_t i = 0; i < goodDevs.size(); ++i)
            {
                std::cout << "  [" << i << "] " << goodDevs[i].getProperties().deviceName << "\n";
            }
            std::cout << "Choose the index: ";

            size_t choice = 0;
            while (true)
            {
                std::cin >> choice;
                if (choice < goodDevs.size())
                {
                    break;
                }
                std::cout << "Invalid choice. Choose device index: ";
            }
            mPhysicalDevice = goodDevs[choice];
        }

        mPhysMemProps = mPhysicalDevice.getMemoryProperties();
    }

    void Device::createLogicalDevice(const vk::UniqueSurfaceKHR& testSurface)
    {
        // sanity check
        if (mQueriedExtensions.useNVMotionBlurExt && !mQueriedExtensions.useRayTracingExt)
        {
            throw vk2s::VkException("VK_NV_ray_tracing_motion_blur extension requires VK_KHR_ray_tracing_pipeline");
        }

        mQueueFamilyIndices = QueueFamilyIndices::findQueueFamilies(mPhysicalDevice, testSurface);

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = { mQueueFamilyIndices.graphicsFamily.value(),
                                                   mQueueFamilyIndices.presentFamily.value() };

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            vk::DeviceQueueCreateInfo queueCreateInfo({}, queueFamily, 1, &queuePriority);
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // list of required features
        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR supportedRT;
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR supportedAS;
        vk::PhysicalDeviceDescriptorIndexingFeatures supportedDI;
        vk::PhysicalDeviceBufferDeviceAddressFeaturesKHR supportedBDA;
        vk::PhysicalDeviceRayTracingMotionBlurFeaturesNV supportedMotionBlur;
        vk::PhysicalDeviceRobustness2FeaturesEXT supportedRobustness2;
        vk::PhysicalDeviceFloat16Int8FeaturesKHR supportedFloat16Int8;
        vk::PhysicalDeviceVulkan13Features supportedVulkan13;

        // create a chain of features to enable
        vk::PhysicalDeviceFeatures2 supportedFeatures;
        supportedFeatures.pNext    = &supportedVulkan13;
        supportedVulkan13.pNext    = &supportedFloat16Int8;
        supportedFloat16Int8.pNext = &supportedRobustness2;
        supportedRobustness2.pNext = &supportedMotionBlur;
        supportedMotionBlur.pNext  = &supportedBDA;
        supportedBDA.pNext         = &supportedDI;
        supportedDI.pNext          = &supportedAS;
        supportedAS.pNext          = &supportedRT;

        mPhysicalDevice.getFeatures2(&supportedFeatures);

        // check mandatory features
        if (!supportedRobustness2.robustBufferAccess2 || !supportedRobustness2.robustImageAccess2 ||
            !supportedRobustness2.nullDescriptor)
        {
            throw vk2s::VkException("required robustness features are not supported");
        }

        if (!supportedFeatures.features.robustBufferAccess)
        {
            throw vk2s::VkException("robustBufferAccess is not supported");
        }

        if (!supportedFeatures.features.shaderInt16 || !supportedFeatures.features.shaderInt64 ||
            !supportedFloat16Int8.shaderInt8)
        {
            throw vk2s::VkException("required shader integer features are not supported");
        }

        if (!supportedVulkan13.maintenance4)
        {
            throw vk2s::VkException("required vulkan 1.3 features are not supported");
        }

        if (mQueriedExtensions.useRayTracingExt)
        {
            if (!supportedRT.rayTracingPipeline || !supportedAS.accelerationStructure ||
                !supportedBDA.bufferDeviceAddress)
            {
                throw vk2s::VkException("required ray tracing features are not supported");
            }

            if (!supportedRT.rayTracingPipelineTraceRaysIndirect)
            {
                throw vk2s::VkException("indirect ray tracing is not supported");
            }

            const bool descriptorIndexingSupported =
                supportedDI.shaderUniformBufferArrayNonUniformIndexing &&
                supportedDI.shaderSampledImageArrayNonUniformIndexing && supportedDI.descriptorBindingPartiallyBound &&
                supportedDI.descriptorBindingVariableDescriptorCount && supportedDI.runtimeDescriptorArray;

            if (!descriptorIndexingSupported)
            {
                throw vk2s::VkException("required descriptor indexing features are not supported");
            }
        }

        if (mQueriedExtensions.useNVMotionBlurExt)
        {
            if (!supportedMotionBlur.rayTracingMotionBlur)
            {
                throw vk2s::VkException("VK_NV_ray_tracing_motion_blur extension is not supported");
            }

            if (!supportedMotionBlur.rayTracingMotionBlurPipelineTraceRaysIndirect)
            {
                throw vk2s::VkException("indirect ray tracing with motion blur is not supported");
            }
        }

        vk::PhysicalDeviceBufferDeviceAddressFeaturesKHR enabledBufferDeviceAddressFeatures(VK_TRUE);

        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures(VK_TRUE);
        enabledRayTracingPipelineFeatures.pNext                               = &enabledBufferDeviceAddressFeatures;
        enabledRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect = VK_TRUE;

        vk::PhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerataionStuctureFeatures(VK_TRUE);
        enabledAccelerataionStuctureFeatures.pNext = &enabledRayTracingPipelineFeatures;

        vk::PhysicalDeviceDescriptorIndexingFeatures enabledDescriptorIndexingFeatures;
        enabledDescriptorIndexingFeatures.pNext = &enabledAccelerataionStuctureFeatures;
        enabledDescriptorIndexingFeatures.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
        enabledDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing  = VK_TRUE;
        enabledDescriptorIndexingFeatures.descriptorBindingPartiallyBound            = VK_TRUE;
        enabledDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount   = VK_TRUE;
        enabledDescriptorIndexingFeatures.runtimeDescriptorArray                     = VK_TRUE;

        // for NV motion blur extension
        vk::PhysicalDeviceRayTracingMotionBlurFeaturesNV enabledRTMotionBlurFeatures;
        enabledRTMotionBlurFeatures.rayTracingMotionBlur                          = VK_TRUE;
        enabledRTMotionBlurFeatures.rayTracingMotionBlurPipelineTraceRaysIndirect = VK_TRUE;

        vk::PhysicalDeviceRobustness2FeaturesEXT robustness2Features(VK_TRUE, VK_TRUE, VK_TRUE);

        vk::PhysicalDeviceFloat16Int8FeaturesKHR float16Int8Features;
        float16Int8Features.pNext      = &robustness2Features;
        float16Int8Features.shaderInt8 = VK_TRUE;

        vk::PhysicalDeviceVulkan13Features vk1_3features;
        vk1_3features.pNext        = &float16Int8Features;
        vk1_3features.maintenance4 = VK_TRUE;

        vk::PhysicalDeviceFeatures2 enabledFeatures;
        enabledFeatures.features.robustBufferAccess = VK_TRUE;
        enabledFeatures.features.shaderInt16        = VK_TRUE;
        enabledFeatures.features.shaderInt64        = VK_TRUE;
        enabledFeatures.pNext                       = &vk1_3features;

        vk::DeviceCreateInfo createInfo;
        createInfo.pQueueCreateInfos    = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pNext                = &enabledFeatures;
        createInfo.pEnabledFeatures     = nullptr;

        std::vector<const char*> extensionNames(baseDeviceExtensions.begin(), baseDeviceExtensions.end());
        extensionNames.reserve(allDeviceExtensions.size());

        void** ppNext = &(robustness2Features.pNext);

        if (mQueriedExtensions.useExternalMemoryExt)
        {
            extensionNames.resize(extensionNames.size() + externalMemoryDeviceExtensions.size());
            std::copy(externalMemoryDeviceExtensions.begin(), externalMemoryDeviceExtensions.end(),
                      extensionNames.end() - externalMemoryDeviceExtensions.size());
        }

        if (mQueriedExtensions.useRayTracingExt)
        {
            *ppNext = &enabledDescriptorIndexingFeatures;
            ppNext  = &(enabledBufferDeviceAddressFeatures.pNext);

            extensionNames.resize(extensionNames.size() + rayTracingDeviceExtensions.size());
            std::copy(rayTracingDeviceExtensions.begin(), rayTracingDeviceExtensions.end(),
                      extensionNames.end() - rayTracingDeviceExtensions.size());

            if (mQueriedExtensions.useNVMotionBlurExt)
            {
                *ppNext = &enabledRTMotionBlurFeatures;
                ppNext  = &(enabledRTMotionBlurFeatures.pNext);
                extensionNames.emplace_back(nvRayTracingMotionBlurExtension);
            }
        }

        createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensionNames.size());
        createInfo.ppEnabledExtensionNames = extensionNames.data();

        if (enableValidationLayers)
        {
            createInfo.setPEnabledLayerNames(validationLayers);
        }

        mDevice = mPhysicalDevice.createDeviceUnique(createInfo);
        assert(mDevice || !"failed to create logical device!");

        mGraphicsQueue = mDevice->getQueue(mQueueFamilyIndices.graphicsFamily.value(), 0);
        mPresentQueue  = mDevice->getQueue(mQueueFamilyIndices.presentFamily.value(), 0);
    }

    void Device::createCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo({}, mQueueFamilyIndices.graphicsFamily.value());
        poolInfo.flags |= vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

        mCommandPool = mDevice->createCommandPoolUnique(poolInfo);
    }

    void Device::createDescriptorPool(const DescriptorPoolAllocationInfo& minimum)
    {
        DescriptorPoolAllocationInfo capacity(kMaxDescriptorNum);

        capacity.accelerationStructureNum =
            std::max(capacity.accelerationStructureNum, minimum.accelerationStructureNum);
        capacity.combinedImageSamplerNum = std::max(capacity.combinedImageSamplerNum, minimum.combinedImageSamplerNum);
        capacity.sampledImageNum         = std::max(capacity.sampledImageNum, minimum.sampledImageNum);
        capacity.samplerNum              = std::max(capacity.samplerNum, minimum.samplerNum);
        capacity.storageBufferNum        = std::max(capacity.storageBufferNum, minimum.storageBufferNum);
        capacity.storageImageNum         = std::max(capacity.storageImageNum, minimum.storageImageNum);
        capacity.uniformBufferNum        = std::max(capacity.uniformBufferNum, minimum.uniformBufferNum);
        capacity.uniformBufferDynamicNum = std::max(capacity.uniformBufferDynamicNum, minimum.uniformBufferDynamicNum);

        if (!mQueriedExtensions.useRayTracingExt)
        {
            capacity.accelerationStructureNum = 0;
        }

        std::vector poolSize = {
            vk::DescriptorPoolSize(vk::DescriptorType::eSampler, capacity.samplerNum),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, capacity.combinedImageSamplerNum),
            vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, capacity.sampledImageNum),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, capacity.storageImageNum),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, capacity.storageBufferNum),
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, capacity.uniformBufferDynamicNum),
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, capacity.uniformBufferNum),
        };

        if (mQueriedExtensions.useRayTracingExt)
        {
            poolSize.emplace_back(vk::DescriptorType::eAccelerationStructureKHR, capacity.accelerationStructureNum);
        }

        vk::DescriptorPoolCreateInfo ci(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, kMaxDescriptorNum,
                                        poolSize);

        auto descriptorPool = mDevice->createDescriptorPoolUnique(ci);

        auto& added          = mDescriptorPools.emplace_back();
        added.descriptorPool = std::move(descriptorPool);
        added.now            = capacity;
        added.remainingSets  = kMaxDescriptorNum;
    }

    // utility----------------------------------------------

    bool Device::checkInstanceLayerSupport()
    {
        std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();

        std::vector<const char*> requestedLayers(validationLayers.begin(), validationLayers.end());

        for (const char* layerName : requestedLayers)
        {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }
            if (!layerFound)
            {
                return false;
            }
        }
        return true;
    }

    bool Device::checkInstanceExtensionSupport()
    {
        const auto availableExtensions = vk::enumerateInstanceExtensionProperties();

        std::vector<const char*> requiredExtensions(allInstanceExtensions.begin(), allInstanceExtensions.end());

        for (const char* extension : requiredExtensions)
        {
            bool extFound = false;
            for (const auto& extensionProperties : availableExtensions)
            {
                if (strcmp(extension, extensionProperties.extensionName) == 0)
                {
                    extFound = true;
                    break;
                }
            }
            if (!extFound)
            {
                return false;
            }
        }
        return true;
    }

    std::vector<const char*> Device::getRequiredExtensions(const bool useWindow)
    {
        std::vector<const char*> extensions;

        if (useWindow)
        {
            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensions;
            glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            for (uint32_t i = 0; i < glfwExtensionCount; i++)
            {
                extensions.push_back(glfwExtensions[i]);
            }
        }

        if (enableValidationLayers)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        if (mQueriedExtensions.useExternalMemoryExt)
        {
            std::copy(externalMemoryInstanceExtensions.begin(), externalMemoryInstanceExtensions.end(),
                      std::back_inserter(extensions));
        }

        return extensions;
    }

    bool Device::isDeviceSuitable(vk::PhysicalDevice physDevice, const vk::UniqueSurfaceKHR& testSurface)
    {
        QueueFamilyIndices indices = QueueFamilyIndices::findQueueFamilies(physDevice, testSurface);

        bool extensionsSupported = checkDeviceExtensionSupport(physDevice);

        bool swapChainAdequate = false;

        if (!testSurface)
        {
            return indices.isComplete() && extensionsSupported;
        }

        if (extensionsSupported)
        {
            SwapChainSupportDetails swapChainSupport =
                SwapChainSupportDetails::querySwapChainSupport(physDevice, testSurface);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }
        else
        {
            throw std::runtime_error("extension is not supported!");
        }

        return indices.isComplete() && extensionsSupported && swapChainAdequate;
    }

    QueueFamilyIndices QueueFamilyIndices::findQueueFamilies(vk::PhysicalDevice physDev,
                                                             const vk::UniqueSurfaceKHR& testSurface)
    {
        QueueFamilyIndices indices;

        std::vector<vk::QueueFamilyProperties> queueFamilies = physDev.getQueueFamilyProperties();

        uint32_t i = 0;
        for (const auto& queueFamily : queueFamilies)
        {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
            {
                indices.graphicsFamily = i;
            }

            if (testSurface)
            {
                const auto presentSupport = physDev.getSurfaceSupportKHR(i, testSurface.get());
                if (presentSupport)
                {
                    indices.presentFamily = i;
                }
            }
            else
            {
                // don't care about presenting
                indices.presentFamily = indices.graphicsFamily;
            }

            if (indices.isComplete())
            {
                break;
            }

            i++;
        }

        return indices;
    }

    Device::SwapChainSupportDetails Device::SwapChainSupportDetails::querySwapChainSupport(
        vk::PhysicalDevice physDev, const vk::UniqueSurfaceKHR& testSurface)
    {
        SwapChainSupportDetails details;
        if (testSurface)
        {
            details.capabilities = physDev.getSurfaceCapabilitiesKHR(testSurface.get());
            details.formats      = physDev.getSurfaceFormatsKHR(testSurface.get());
            details.presentModes = physDev.getSurfacePresentModesKHR(testSurface.get());
        }

        return details;
    }

    bool Device::checkDeviceExtensionSupport(vk::PhysicalDevice physDevice) const
    {
        std::vector<vk::ExtensionProperties> availableExtensions = physDevice.enumerateDeviceExtensionProperties();

        std::set<std::string> requiredExtensions(baseDeviceExtensions.begin(), baseDeviceExtensions.end());

        for (const auto& extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        if (mQueriedExtensions.useRayTracingExt)
        {
            std::set<std::string> requiredRTExtensions(rayTracingDeviceExtensions.begin(),
                                                       rayTracingDeviceExtensions.end());
            for (const auto& extension : availableExtensions)
            {
                requiredRTExtensions.erase(extension.extensionName);
            }

            if (mQueriedExtensions.useNVMotionBlurExt)
            {
                for (const auto& extension : availableExtensions)
                {
                    if (std::strcmp(nvRayTracingMotionBlurExtension, extension.extensionName) == 0)
                    {
                        return requiredExtensions.empty() && requiredRTExtensions.empty();
                    }
                }

                return false;
            }

            return requiredExtensions.empty() && requiredRTExtensions.empty();
        }

        return requiredExtensions.empty();
    }

    uint32_t Device::getVkMemoryTypeIndex(uint32_t requestBits, vk::MemoryPropertyFlags requestProps) const
    {
        for (std::uint32_t i = 0; i < mPhysMemProps.memoryTypeCount; ++i)
        {
            if ((requestBits & (uint32_t{ 1 } << i)) == 0)
            {
                continue;
            }

            const auto properties = mPhysMemProps.memoryTypes[i].propertyFlags;
            if ((properties & requestProps) == requestProps)
            {
                return i;
            }
        }

        throw std::runtime_error("no compatible Vulkan memory type");
    }

    Device::Extensions Device::getVkAvailableExtensions() const
    {
        return mQueriedExtensions;
    }

#if VK_HEADER_VERSION >= 305
    VKAPI_ATTR vk::Bool32 VKAPI_CALL Device::debugUtilsMessengerCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
        vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* pUserData)
#else
    VKAPI_ATTR VkBool32 VKAPI_CALL Device::debugUtilsMessengerCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* pUserData)
#endif
    {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

        return VK_FALSE;
    }

}  // namespace vk2s
