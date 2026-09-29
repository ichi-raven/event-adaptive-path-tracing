/*****************************************************************/ /**
 * @file   Fence.cpp
 * @brief  source file of Fence class
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/
#include "../include/vk2s/Fence.hpp"

#include "../include/vk2s/Device.hpp"

namespace vk2s
{
    Fence::Fence(Device& device, const bool signaled)
        : mDevice(device)
    {
        if (signaled)
        {
            mFence = mDevice.getVkDevice()->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
        }
        else
        {
            mFence = mDevice.getVkDevice()->createFenceUnique(vk::FenceCreateInfo());
        }
    }

    Fence::~Fence()
    {
    }

    bool Fence::wait()
    {
        const auto res = mDevice.getVkDevice()->waitForFences(mFence.get(), true, UINT64_MAX);

        return res == vk::Result::eSuccess;
    }

    void Fence::reset()
    {
        mDevice.getVkDevice()->resetFences(mFence.get());
    }

    bool Fence::signaled()
    {
        return mDevice.getVkDevice()->getFenceStatus(mFence.get()) == vk::Result::eSuccess;
    }

    const vk::UniqueFence& Fence::getVkFence()
    {
        return mFence;
    }
}  // namespace vk2s
