/*****************************************************************/ /**
 * @file   Options.cpp
 * @brief  source file of Option struct
 * 
 * @author ichi-raven
 * @date   April 2025
 *********************************************************************/
#include "../include/Options.hpp"

#include "cxxopts.hpp"

#include <stdexcept>
#include <filesystem>

// nullopt: succeeded, otherwise: help message
std::optional<std::string> Options::parse(int argc, char** argv)
{
    cxxopts::Options options(argv[0], " - command line options");
    options.positional_help("[optional args]").show_positional_help();
    options.add_options()("w, width", "film width", cxxopts::value<uint32_t>())                                 //
        ("h, height", "film height", cxxopts::value<uint32_t>())                                                //
        ("i, input", "input scene file path", cxxopts::value<std::string>())                                    //
        ("o, output", "output directory path", cxxopts::value<std::string>())                                   //
        ("d, deltatime", "deltatime", cxxopts::value<double>())                                                 //
        ("f, frames", "all frame num", cxxopts::value<uint32_t>())                                              //
        ("spp", "samples per pixel", cxxopts::value<uint32_t>())                                                //
        ("m, mode", "rendering mode", cxxopts::value<std::string>())                                            //
        ("baseline", "do not use stream compaction & testing", cxxopts::value<bool>()->default_value("false"))  //
        ("discrete", "frame-based discrete event detection", cxxopts::value<bool>()->default_value("false"))    //
        ("exr", "export RGB images by EXR format", cxxopts::value<bool>()->default_value("false"))              //
        ("linLog", "apply lin-log transformation for luminance accumulation",
         cxxopts::value<bool>()->default_value("false"))                                                   //
        ("motion", "apply motion-blur for RGB rendering", cxxopts::value<bool>()->default_value("false"))  //
        ("debug", "for debugging, direct event frame output as RGB image",
         cxxopts::value<bool>()->default_value("false"))                                                             //
        ("scOnly", "for ablation study, stream compaction only", cxxopts::value<bool>()->default_value("false"))     //
        ("testOnly", "for ablation study, hypothesis testing only", cxxopts::value<bool>()->default_value("false"))  //
        ("denoise", "for RGB frames, use OIDN", cxxopts::value<bool>()->default_value("false"))                      //
        ("t, thres", "thresholds", cxxopts::value<float>())                                                          //
        ("tp, threspos", "positive thresholds", cxxopts::value<float>())                                             //
        ("tn, thresneg", "negative thresholds", cxxopts::value<float>())                                             //
        ("help", "show command line help")                                                                           //
        ("verbose", "show detailed logs with source locations", cxxopts::value<bool>()->default_value("false"))      //
        ("progress", "progress display: auto, on, or off", cxxopts::value<std::string>()->default_value("auto"))     //
        ("event_batch_size",
         "number of pixels to process in one batch for event calculation (0 means all pixels in one batch)",
         cxxopts::value<uint32_t>()->default_value("0"))  //
        ;

    const auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        return options.help();
    }

    verbose                = result["verbose"].as<bool>();
    const auto progressStr = result["progress"].as<std::string>();
    if (progressStr == "auto")
    {
        progress = ProgressMode::eAuto;
    }
    else if (progressStr == "on")
    {
        progress = ProgressMode::eOn;
    }
    else if (progressStr == "off")
    {
        progress = ProgressMode::eOff;
    }
    else
    {
        throw std::invalid_argument("invalid progress mode: " + progressStr + " (expected auto, on, or off)");
    }

    if (result.count("width"))
    {
        width = result["width"].as<uint32_t>();
    }

    if (result.count("height"))
    {
        height = result["height"].as<uint32_t>();
    }

    if (result.count("deltatime"))
    {
        deltaTime = result["deltatime"].as<double>();
    }

    if (result.count("frames"))
    {
        frames = result["frames"].as<uint32_t>();
    }

    if (result.count("spp"))
    {
        spp = result["spp"].as<uint32_t>();
    }

    if (result.count("mode"))
    {
        const auto modeStr = result["mode"].as<std::string>();

        if (modeStr == "events")
        {
            mode = Mode::eEvents;
        }
        else if (modeStr == "rgb")
        {
            mode = Mode::eRGB;
        }
        else if (modeStr == "full")
        {
            mode = Mode::eFull;
        }
        else
        {
            throw std::runtime_error("invalid rendering mode: " + modeStr + " (expected: events, rgb, full)");
        }
    }

    if (result.count("baseline"))
    {
        optimize = !result["baseline"].as<bool>();
    }

    if (result.count("discrete"))
    {
        discrete = result["discrete"].as<bool>();
        if (*discrete)
        {
            optimize = false;
        }
    }

    if (result.count("exr"))
    {
        useEXR = result["exr"].as<bool>();
    }

    if (result.count("linLog"))
    {
        linLogL = result["linLog"].as<bool>();
    }

    if (result.count("motion"))
    {
        motion = result["motion"].as<bool>();
    }

    if (result.count("debug"))
    {
        eventDebug = result["debug"].as<bool>();
    }
    if (result.count("scOnly"))
    {
        scOnly = result["scOnly"].as<bool>();
    }
    if (result.count("testOnly"))
    {
        testOnly = result["testOnly"].as<bool>();
    }
    if (result.count("denoise"))
    {
        denoise = result["denoise"].as<bool>();
    }

    if (result.count("event_batch_size"))
    {
        eventBatchSize = result["event_batch_size"].as<uint32_t>();
    }

    if (result.count("thres"))
    {
        _thres = result["thres"].as<float>();
    }

    if (result.count("threspos"))
    {
        _threspos = result["threspos"].as<float>();
    }

    if (result.count("thresneg"))
    {
        _thresneg = result["thresneg"].as<float>();
    }

    if (result.count("input"))
    {
        input = result["input"].as<std::string>();
    }
    else
    {
        throw std::runtime_error("missing required option: --input (use --help for usage)");
    }

    if (result.count("output"))
    {
        output = result["output"].as<std::string>();
    }
    else
    {
        output = std::filesystem::path(input).stem().string();
    }

    // succeeded
    return std::nullopt;
}
