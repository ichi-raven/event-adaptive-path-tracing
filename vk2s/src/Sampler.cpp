/*****************************************************************/ /**
 * @file   Sampler.cpp
 * @brief  source file of Sampler class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#include "../include/vk2s/Sampler.hpp"
#include "../include/vk2s/Device.hpp"

namespace vk2s
{
    Sampler::Sampler(Device& device, const vk::SamplerCreateInfo& ci)
        : mDevice(device)
    {
        mSampler = device.getVkDevice()->createSamplerUnique(ci);
    }

    Sampler::~Sampler()
    {
    }

    const vk::UniqueSampler& Sampler::getVkSampler()
    {
        return mSampler;
    }
}  // namespace vk2s
