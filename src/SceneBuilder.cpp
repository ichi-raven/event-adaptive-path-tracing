/*****************************************************************/ /**
 * @file   SceneBuilder.cpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   February 2025
 *********************************************************************/

#include "../include/SceneBuilder.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/string_cast.hpp>

#include "cxxopts.hpp"
#include "nlohmann/json.hpp"

#include <Scene.hpp>
#include <vk2s/Camera.hpp>

#include <Mesh.hpp>
#include <Material.hpp>
#include <Transform.hpp>
#include <Emitter.hpp>
#include <AnimatedTransform.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace evr
{

    using json   = nlohmann::json;
    using Vertex = Mesh::Vertex;

    enum struct SRT
    {
        eScale,
        eRotate,
        eTranslate
    };

    inline glm::vec3 convert(const aiVector3D v)
    {
        return glm::vec3(v.x, v.y, v.z);
    }

    inline glm::quat convert(const aiQuaternion q)
    {
        return glm::quat(q.w, q.x, q.y, q.z);
    }

    template <typename T>
    inline void checkAssign(std::optional<T>& opt, const T& val)
    {
        if (opt.has_value())
        {
            return;
        }
        opt = val;
    }

    template <typename T>
    T get(const json& j, std::string_view key)
    {
        if (j.contains(key))
        {
            const auto& e = j[key];
            return e.front().get<T>();
        }

        throw std::runtime_error("failed to load param " + std::string(key) + "!");
        return T{};
    }

    glm::vec2 getVec2(const json& j, std::string_view key)
    {
        if (j.contains(key))
        {
            const auto& e = j[key];
            return glm::vec2(e[0].get<float>(), e[1].get<float>());
        }

        throw std::runtime_error("failed to load vec3 param " + std::string(key) + "!");
        return glm::vec2(0.f);
    }

    glm::vec3 getVec3(const json& j, std::string_view key)
    {
        if (j.contains(key))
        {
            const auto& e = j[key];
            return glm::vec3(e[0].get<float>(), e[1].get<float>(), e[2].get<float>());
        }

        throw std::runtime_error("failed to load vec3 param " + std::string(key) + "!");
        return glm::vec3(0.f);
    }

    std::unique_ptr<Scene> buildScene(vk2s::Device& device, Options& options)
    {
        std::ifstream ifs(options.input.data());
        if (!ifs)
        {
            throw std::runtime_error("failed to load scene file!");
            return nullptr;
        }

        std::string jsonstr((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        json j = json::parse(jsonstr);

        // load film
        // WARN: Film parameters change the options parameter(affects Camera, etc.)
        if (j.contains("film"))
        {
            const auto& filmJson = j["film"];
            checkAssign(options.width, get<uint32_t>(filmJson, "width"));
            checkAssign(options.height, get<uint32_t>(filmJson, "height"));
            checkAssign(options.deltaTime, get<double>(filmJson, "deltatime"));
            checkAssign(options.frames, get<uint32_t>(filmJson, "frames"));
            checkAssign(options.spp, get<uint32_t>(filmJson, "spp"));

            const auto modeStr = filmJson["mode"].get<std::string>();

            std::vector<std::string> flagsStrs;

            if (filmJson.contains("flags"))
            {
                if (filmJson["flags"].is_array())
                {
                    flagsStrs.reserve(filmJson["flags"].size());
                    for (const auto& flagJson : filmJson["flags"])
                    {
                        flagsStrs.emplace_back(flagJson.get<std::string>());
                    }
                }
                else if (filmJson["flags"].is_string())
                {
                    flagsStrs.emplace_back(filmJson["flags"].get<std::string>());
                }
            }

            for (const auto& flagStr : flagsStrs)
            {
                if (flagStr == "baseline")
                {
                    checkAssign(options.optimize, false);
                }
                else if (flagStr == "discrete")
                {
                    checkAssign(options.optimize, false);
                    checkAssign(options.discrete, true);
                }
                else if (flagStr == "debug")
                {
                    checkAssign(options.eventDebug, true);
                }

                if (flagStr == "linLogL")
                {
                    checkAssign(options.linLogL, true);
                }

                if (flagStr == "exr")
                {
                    checkAssign(options.useEXR, true);
                }

                if (flagStr == "motion")
                {
                    checkAssign(options.motion, true);
                }

                if (flagStr == "scOnly")
                {
                    checkAssign(options.scOnly, true);
                }

                if (flagStr == "testOnly")
                {
                    checkAssign(options.testOnly, true);
                }

                if (flagStr == "denoise")
                {
                    checkAssign(options.denoise, true);
                }
            }

            // built-in default
            checkAssign(options.optimize, true);
            checkAssign(options.discrete, false);
            checkAssign(options.useEXR, false);
            checkAssign(options.linLogL, false);
            checkAssign(options.motion, false);
            checkAssign(options.eventDebug, false);
            checkAssign(options.scOnly, false);
            checkAssign(options.testOnly, false);
            checkAssign(options.denoise, false);

            // set film mode
            if (modeStr == "events")
            {
                checkAssign(options.mode, Options::Mode::eEvents);
            }
            else if (modeStr == "rgb")
            {
                checkAssign(options.mode, Options::Mode::eRGB);
            }
            else if (modeStr == "full")
            {
                checkAssign(options.mode, Options::Mode::eFull);
            }
            else
            {
                throw std::runtime_error("unknown film mode!");
            }
        }
        else
        {
            throw std::runtime_error("film empty!");
        }

        // Validate the merged CLI / JSON parameters before Scene creates GPU resources.
        const auto intMax = static_cast<uint32_t>(std::numeric_limits<int>::max());
        if (!options.width || *options.width == 0 || *options.width > intMax)
        {
            throw std::invalid_argument("film width must be in [1, INT_MAX]");
        }
        if (!options.height || *options.height == 0 || *options.height > intMax)
        {
            throw std::invalid_argument("film height must be in [1, INT_MAX]");
        }
        if (!options.frames || *options.frames == 0 || *options.frames > intMax)
        {
            throw std::invalid_argument("film frames must be in [1, INT_MAX]");
        }
        if (!options.spp || *options.spp == 0 || *options.spp > intMax)
        {
            throw std::invalid_argument("film spp must be in [1, INT_MAX]");
        }
        if (!options.deltaTime || !std::isfinite(*options.deltaTime) || *options.deltaTime <= 0.0)
        {
            throw std::invalid_argument("film deltatime must be finite and positive");
        }
        if (!std::isfinite(static_cast<double>(*options.frames) * *options.deltaTime))
        {
            throw std::invalid_argument("film duration must be finite");
        }

        Thresholds thres;
        if (j.contains("thresholds"))
        {
            thres.positive = get<float>(j["thresholds"], "positive");
            thres.negative = get<float>(j["thresholds"], "negative");
            if (j["thresholds"].contains("sigma"))
            {
                thres.sigma = get<float>(j["thresholds"], "sigma");
            }
        }

        if (options._thres)
        {
            thres.positive = thres.negative = *options._thres;
        }

        if (options._threspos)
        {
            thres.positive = *options._threspos;
        }

        if (options._thresneg)
        {
            thres.negative = *options._thresneg;
        }

        checkAssign(options.thresholds, thres);

        if (j.contains("light"))
        {
            const auto lmdAddEmitter = [&](const json& emitterJson)
            {
                std::string shape = emitterJson["shape"].get<std::string>();

                if (shape == "point")
                {
                    assert(!"TODO");
                    // const auto pos      = getVec3(emitterJson, "origin");
                    // const auto emissive = getVec3(emitterJson, "Le");

                    // const auto entity = scene.create();
                    // auto& emitter     = scene.add<palm::Emitter>(entity);
                    // auto& transform   = scene.add<palm::Transform>(entity);

                    // emitter.attachedEntity  = entity;
                    // emitter.params.emissive = emissive;
                    // emitter.params.pos      = pos;
                    // emitter.params.type     = static_cast<int32_t>(palm::Emitter::Type::ePoint);

                    // transform.pos = pos;
                }
                else if (shape == "infinite")
                {
                    const std::string originalPath = emitterJson["path"].get<std::string>();
                    std::filesystem::path p(options.input);
                    const auto envmapPath = p.parent_path().string() + "/" + originalPath;

                    if (std::filesystem::exists(envmapPath) == false)
                    {
                        throw std::runtime_error("failed to load environment map: " + envmapPath);
                    }

                    options.infiniteEmitterPath = envmapPath;
                }
            };

            const auto& emitterJsons = j["light"];

            if (emitterJsons.is_array())
            {
                for (const auto& emitterJson : emitterJsons)
                {
                    lmdAddEmitter(emitterJson);
                }
            }
            else
            {
                lmdAddEmitter(emitterJsons);
            }
        }

        // load scene model (gltf)
        std::unique_ptr<Scene> scene;
        if (j.contains("scene"))
        {
            std::filesystem::path p(options.input);
            const auto modelPath = p.parent_path().string() + "/" + j["scene"].get<std::string>();
            options.input        = modelPath;
            scene                = std::make_unique<Scene>(device, options);
        }
        else
        {
            throw std::runtime_error("scene model empty!");
        }

        // load camera
        if (j.contains("camera"))
        {
            auto& camera = scene->getCameraRef();

            const auto& cameraJson = j["camera"];
            const auto eye         = getVec3(cameraJson, "eye");
            const auto up          = getVec3(cameraJson, "up");
            const auto fov         = get<double>(cameraJson, "fov");

            camera.setPos(eye);
            camera.setUpVector(up);
            camera.setFOV(glm::radians(fov));
            camera.setAspect(1. * *options.width / *options.height);
            camera.setNear(0.1);
            camera.setFar(1e5);

            if (cameraJson.contains("center"))
            {
                const auto center = getVec3(cameraJson, "center");
                camera.setLookAt(center);
            }
            else if (cameraJson.contains("rotation"))
            {
                const auto rot = getVec2(cameraJson, "rotation");
                camera.setPhi(glm::radians(rot.x));
                camera.setTheta(glm::radians(rot.y));
            }
            else
            {
                throw std::runtime_error("camera target not specified!");
            }
        }
        else
        {
            throw std::runtime_error("camera empty!");
        }

        return scene;
    }
}  // namespace evr
