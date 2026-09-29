/*****************************************************************/ /**
 * @file   Pipeline.hpp
 * @brief  header file of Pipeline class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_PIPELINE_HPP_
#define VK2S_INCLUDE_PIPELINE_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "SlotMap.hpp"
#include "Macro.hpp"

#include <optional>

namespace vk2s
{
    //! forward declaration
    class Device;

    class BindLayout;
    class Shader;
    class RenderPass;

    /**
     * @brief  class that represents the graphics compute ray tracing pipeline in a unified type
     */
    class Pipeline
    {
    public:  // types
        /**
         * @brief  information needed to create a graphics pipeline
         */
        struct GraphicsPipelineInfo
        {
            //! vertex shader
            Handle<Shader> vs;
            //! fragment shader
            Handle<Shader> fs;
            //! bind layouts (used for pipeline layout creation)
            vk::ArrayProxyNoTemporaries<Handle<BindLayout>> bindLayouts;
            //! renderpass
            Handle<RenderPass> renderPass;
            //! indicates the format of the input vertex
            vk::PipelineVertexInputStateCreateInfo inputState;
            //! indicates how the vertex is handled as a shape
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
            //! Viewport and Scissor Settings
            vk::PipelineViewportStateCreateInfo viewportState;
            //! set the rasterization method
            vk::PipelineRasterizationStateCreateInfo rasterizer;
            //! specify how to perform multisampling for color/depth targets
            vk::PipelineMultisampleStateCreateInfo multiSampling;
            //! specify write and decision operations to deps and stencil buffers
            vk::PipelineDepthStencilStateCreateInfo depthStencil;
            //! specifies how to blend the original colors in the frame buffer with the new colors to be written
            vk::PipelineColorBlendStateCreateInfo colorBlending;
            //! specify states that are dynamically determined at runtime, optional
            vk::PipelineDynamicStateCreateInfo dynamicStates;
            //! specify push constant ranges
            vk::ArrayProxyNoTemporaries<vk::PushConstantRange> pushConstantRanges;
        };

        /**
         * @brief  information needed to create a compute pipeline
         */
        struct ComputePipelineInfo
        {
            //! compute shader
            Handle<Shader> cs;
            //! bind layouts (used for pipeline layout creation)
            vk::ArrayProxyNoTemporaries<Handle<BindLayout>> bindLayouts;
            //! specify push constant ranges
            vk::ArrayProxyNoTemporaries<vk::PushConstantRange> pushConstantRanges;
        };

        /**
         * @brief  information needed to create a ray tracing pipeline
         */
        struct RayTracingPipelineInfo
        {
            //! ray generation shaders
            std::vector<Handle<Shader>> raygenShaders;
            //! miss shaders
            std::vector<Handle<Shader>> missShaders;
            //! closest hit shaders
            std::vector<Handle<Shader>> chitShaders;
            //! callable shaders
            std::vector<Handle<Shader>> callableShaders;
            //! bind layouts (used for pipeline layout creation)
            vk::ArrayProxyNoTemporaries<Handle<BindLayout>> bindLayouts;
            //! shader groups (registration of each shader by shader stage)
            std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;
            //! specify push constant ranges
            vk::ArrayProxyNoTemporaries<vk::PushConstantRange> pushConstantRanges;
            //! pipeline cache (optional)
            std::string pipelineCachePath;
        };

    public:  // methods
        /**
         * @brief  constructor (as graphics pipeline)
         */
        Pipeline(Device& device, const GraphicsPipelineInfo& info);
        /**
         * @brief  constructor (as compute pipeline)
         */
        Pipeline(Device& device, const ComputePipelineInfo& info);
        /**
         * @brief  constructor (as ray tracing pipeline)
         */
        Pipeline(Device& device, const RayTracingPipelineInfo& info);

        NONCOPYABLE(Pipeline);
        NONMOVABLE(Pipeline);

        /**
         * @brief  destructor
         */
        ~Pipeline();

        /**
         * @brief  save pipeline cache for specified file
         */
        bool savePipelineCache(std::string_view cachePath);

        /**
         * @brief  get vulkan pipeline layout handle
         */
        const vk::UniquePipelineLayout& getVkPipelineLayout();

        /**
         * @brief  get vulkan pipeline handle
         */
        const vk::UniquePipeline& getVkPipeline();

        /**
         * @brief  get vulkan pipeline bindpoint
         */
        vk::PipelineBindPoint getVkPipelineBindPoint() const;

    private:
        void loadPipelineCacheIfExists(std::string_view cachePath);

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan pipeline layout handle
        vk::UniquePipelineLayout mLayout;
        //! vulkan pipeline handle
        vk::UniquePipeline mPipeline;
        //! vulkan pipeline bindpoint
        vk::PipelineBindPoint mBindPoint;
        //! pipeline cache
        vk::UniquePipelineCache mPipelineCache;
    };
}  // namespace vk2s

#endif
