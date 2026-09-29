/*****************************************************************/ /**
 * @file   Compiler.hpp
 * @brief  header files for functions that compiles GLSL/HLSL/Slang to SPIR-V binalies
 * 
 * @author ichi-raven
 * @date   October 2023
 *********************************************************************/

#ifndef VK2S_COMPILER_HPP_
#define VK2S_COMPILER_HPP_

#include <vulkan/vulkan.hpp>

//#include <shaderc/shaderc.hpp>

#include <vector>
#include <map>

namespace vk2s
{
    /**
     * @brief  namespace for shader compilation
     */
    namespace Compiler
    {
        //! type representing SPIR-V code
        using SPIRVCode = std::vector<uint32_t>;

        /**
         * @brief  reads the specified file as a string
         */
        std::string readFile(std::string_view path);

        ///**
        // * @brief  get shader stage from file path (for shaderc, GLSL/HLSL)
        // */
        //shaderc_shader_kind getShaderStage(std::string_view filepath);

        ///**
        // * @brief  compile from text shader code (shaderc, GLSL/HLSL)
        // */
        //SPIRVCode compileText(shaderc_shader_kind stage, const std::string& shaderCode);

        /**
         * @brief  front interface (compiles shaders for a given file path)
         */
        SPIRVCode compileFile(std::string_view path, const bool optimize = false);

        /**
         * @brief  front interface with entrypoint (compiles shaders for a given file path)
         */
        SPIRVCode compileFile(std::string_view path, std::string_view entrypoint, const bool optimize = false);

        //! types for SPIRV-Reflect (vertex layout, descriptor set layout bindings)
        using VertexInputAttributes = std::vector<vk::VertexInputAttributeDescription>;
        using ShaderResourceMap     = std::map<std::pair<uint32_t, uint32_t>, vk::DescriptorType>;
        using ReflectionResult      = std::tuple<VertexInputAttributes, ShaderResourceMap>;

        /**
         * @brief  get reflection from SPIR-V compiled code
         */
        /*ReflectionResult getReflection(const SPIRVCode& fileData);*/

        /**
         * @brief  creating a DescriptorSetLayoutBinding from a reflection
         */
        std::vector<std::vector<vk::DescriptorSetLayoutBinding>> createDescriptorSetLayoutBindings(
            const ShaderResourceMap& resourceMap);

        /**
         * @brief  obtain the byte size of each element corresponding to vk::Format
         */
        uint32_t getSizeOfFormat(const vk::Format format) noexcept;

    }  // namespace Compiler
}  // namespace vk2s

#endif
