/*****************************************************************/ /**
 * \file   Shader.hpp
 * \brief  header file of Shader class
 * 
 * \author ichi-raven
 * \date   November 2023
 *********************************************************************/
#ifndef VK2S_INCLUDE_SHADER_HPP_
#define VK2S_INCLUDE_SHADER_HPP_

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#endif

#include "SlotMap.hpp"
#include "Compiler.hpp"
#include "Macro.hpp"

#include <optional>

namespace vk2s
{
    //! forward declaration
    class Device;

    /**
     * @brief class representing a shader (compiled SPIR-V) 
     */
    class Shader
    {
    public:  // methods
        /**
         * @brief  constructor
         */
        Shader(Device& device, std::string_view path, std::string_view entryPoint = "main");

        /**
         * @brief  destructor
         */
        ~Shader();

        NONCOPYABLE(Shader);
        NONMOVABLE(Shader);

        /**
         * @brief  get vulkan shader module handle
         */
        const vk::UniqueShaderModule& getVkShaderModule();

        /**
         * @brief  get entry point string
         */
        const std::string& getEntryPoint() const;

        /**
         * @brief  get shader reflection
         */
        const Compiler::ReflectionResult& getReflection();

    private:  // member variables
        //! reference to device
        Device& mDevice;

        //! vulkan shader module handle
        vk::UniqueShaderModule mShaderModule;
        //! entry point string
        std::string mEntryPoint;
        //! shader reflection
        std::optional<Compiler::ReflectionResult> mReflection;
    };
}  // namespace vk2s

#endif
