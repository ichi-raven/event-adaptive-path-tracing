/*****************************************************************/ /**
 * @file   Integrator.cpp
 * @brief  source file of Integrator class (abstruct)
 * 
 * @author ichi-raven
 * @date   March 2025
 *********************************************************************/

#include "../include/Integrator.hpp"

#include "omp.h"

#include <numbers>

namespace evr
{
    Integrator::Integrator(vk2s::Device& device, Scene& scene, Handle<vk2s::Image> outputImage)
        : mDevice(device)
        , mScene(scene)
        , mOutputImage(outputImage)
    {
    }

    Integrator::~Integrator()
    {
    }

}  // namespace evr
