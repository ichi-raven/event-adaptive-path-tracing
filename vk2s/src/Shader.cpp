/*****************************************************************/ /**
 * @file   Shader.cpp
 * @brief  source file of Shader class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#include "../include/vk2s/Shader.hpp"

#include "../include/vk2s/Device.hpp"

namespace vk2s
{
    Shader::Shader(Device& device, std::string_view path, std::string_view entryPoint)
        : mDevice(device)
        , mEntryPoint(entryPoint)
    {
        const auto code = Compiler::compileFile(path, entryPoint);

        vk::ShaderModuleCreateInfo createInfo({}, code.size() * sizeof(Compiler::SPIRVCode::value_type), code.data());

        mShaderModule = mDevice.getVkDevice()->createShaderModuleUnique(createInfo);

        //mReflection = Compiler::getReflection(code);
    }

    Shader::~Shader()
    {
    }

    const vk::UniqueShaderModule& Shader::getVkShaderModule()
    {
        return mShaderModule;
    }

    const std::string& Shader::getEntryPoint() const
    {
        return mEntryPoint;
    }

    const Compiler::ReflectionResult& Shader::getReflection()
    {
        return *mReflection;
    }
}  // namespace vk2s
