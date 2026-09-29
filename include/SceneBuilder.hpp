/*****************************************************************//**
 * @file   SceneBuilder.hpp
 * @brief  Header file of SceneBuilder class
 * 
 * @author ichi-raven
 * @date   February 2025
 *********************************************************************/

#ifndef EVENTRENDERER_INCLUDE_SCENEBUILDER_HPP_
#define EVENTRENDERER_INCLUDE_SCENEBUILDER_HPP_

#include "Options.hpp"
#include "Scene.hpp"
#include <vk2s/Device.hpp>
#include <memory>

namespace evr
{
    std::unique_ptr<Scene> buildScene(vk2s::Device& device, Options& options);
}

#endif