#ifndef EVENTRENDERER_INCLUDE_SHADERPATHS_HPP_
#define EVENTRENDERER_INCLUDE_SHADERPATHS_HPP_

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace evr
{
    inline std::string getShaderPath(const char* filename)
    {
        const char* overrideDirectory = std::getenv("EAPT_SHADER_DIR");

        const std::filesystem::path directory =
            overrideDirectory && overrideDirectory[0] != '\0' ? overrideDirectory : EAPT_SHADER_DIRECTORY;

        if (!directory.is_absolute())
        {
            throw std::runtime_error("EAPT_SHADER_DIR must be an absolute path");
        }

        return (directory / filename).string();
    }
}  // namespace evr

#endif  // EVENTRENDERER_INCLUDE_SHADERPATHS_HPP_
