/*****************************************************************/ /**
 * @file   Semaphore.cpp
 * @brief  source file of Semaphore class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#include "../include/vk2s/Semaphore.hpp"

#include "../include/vk2s/Device.hpp"

namespace vk2s
{
    Semaphore::Semaphore(Device& device)
        : mDevice(device)
    {
        mSemaphore = mDevice.getVkDevice()->createSemaphoreUnique(vk::SemaphoreCreateInfo{});
    }

    Semaphore::~Semaphore()
    {
    }

    const vk::UniqueSemaphore& Semaphore::getVkSemaphore()
    {
        return mSemaphore;
    }

}  // namespace vk2s
