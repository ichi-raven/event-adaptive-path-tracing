/*****************************************************************/ /**
 * @file   Options.hpp
 * @brief  header file of Option struct
 * 
 * @author ichi-raven
 * @date   February 2025
 *********************************************************************/
#ifndef EVENTRENDERER_INCLUDE_OPTIONS_HPP_
#define EVENTRENDERER_INCLUDE_OPTIONS_HPP_

#include "Thresholds.hpp"

#include <cstdint>
#include <string>
#include <optional>

struct Options
{
    enum class Mode
    {
        eRGB    = 0,
        eEvents = 1,
        eFull   = 2,
    };

    enum class ProgressMode
    {
        eAuto,
        eOn,
        eOff,
    };

    std::optional<std::string> parse(int argc, char** argv);

    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
    std::optional<double> deltaTime;
    std::optional<uint32_t> frames;
    std::optional<uint32_t> spp;
    std::optional<Mode> mode;
    std::optional<bool> optimize;
    std::optional<bool> discrete;
    std::optional<bool> useEXR;
    std::optional<bool> linLogL;
    std::optional<bool> motion;
    std::optional<bool> eventDebug;
    std::optional<bool> scOnly;    // for ablation study
    std::optional<bool> testOnly;  // for ablation study
    std::optional<bool> denoise;
    std::optional<Thresholds> thresholds;
    std::optional<float> _thres;     // for override
    std::optional<float> _threspos;  // for override
    std::optional<float> _thresneg;  // for override
    bool verbose          = false;
    ProgressMode progress = ProgressMode::eAuto;
    std::string input;
    std::string output;
    std::string infiniteEmitterPath;
    uint32_t eventBatchSize = 0;
};

#endif
