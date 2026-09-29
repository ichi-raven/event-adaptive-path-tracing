
import "../Event/Event";

[[vk::binding(0, 0)]]
RWTexture2D<uint8_t> srcImage;
[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> prefixSums;
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> blockSums;
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint2> dstBuffer;
[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> indirectBuffer;
[[vk::binding(5, 0)]]
RWStructuredBuffer<uint> candidateCounts;

struct PushConstant
{
    uint2 imageSize;
    uint eventID;
    uint padding;
}

[[push_constant]]
PushConstant pushConstants;

[ForceInline]
inline uint flatten(const uint2 id, const uint width)
{
    return id.y * width + id.x;
}

[ForceInline]
inline uint2 restore(const uint id, const uint width)
{
    return uint2(id % width, id / width);
}

[ForceInline]
inline bool pred(const uint8_t x)
{
    return x != 0;
}

/*
    The MIT License (MIT)

    Copyright (c) 2004-2019 Microsoft Corp
    Modified by Carl Emil Carlsen 2018.

    Permission is hereby granted, free of charge, to any person obtaining a copy of this
    software and associated documentation files (the "Software"), to deal in the Software
    without restriction, including without limitation the rights to use, copy, modify,
    merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be included in all copies
    or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
    PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
    OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
    From directx-sdk-samples by Chuck Walbourn:
    https://github.com/walbourn/directx-sdk-samples/blob/master/AdaptiveTessellationCS40/ScanCS.hlsl
*/

// parallel implementation of prefix sum

const static uint kNumThreads = 512;  // 512 threads per block
groupshared uint bucket[kNumThreads];
groupshared uint blockCarry;

// Every thread in the workgroup must call this function
uint exclusiveScan(const uint x, const uint gi)
{
    GroupMemoryBarrierWithGroupSync();
    bucket[gi] = x;

    [unroll]
    for (uint stride = 1; stride < kNumThreads; stride <<= 1)
    {
        GroupMemoryBarrierWithGroupSync();

        let index = (gi + 1) * (stride << 1) - 1;
        if (index < kNumThreads)
        {
            bucket[index] += bucket[index - stride];
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (gi == kNumThreads - 1)
    {
        bucket[gi] = 0;
    }

    [unroll]
    for (uint stride = kNumThreads / 2; stride > 0; stride >>= 1)
    {
        GroupMemoryBarrierWithGroupSync();

        let index = (gi + 1) * (stride << 1) - 1;
        if (index < kNumThreads)
        {
            let t                  = bucket[index - stride];
            bucket[index - stride] = bucket[index];
            bucket[index] += t;
        }
    }

    GroupMemoryBarrierWithGroupSync();
    return bucket[gi];
}

[numthreads(kNumThreads, 1, 1)]
[shader("compute")]
void prefixSumPass1(uint id: SV_DispatchThreadID, uint gi: SV_GroupIndex)
{
    let width = pushConstants.imageSize.x;
    let count = width * pushConstants.imageSize.y;

    uint x = 0;
    if (id < count)
    {
        x = uint(pred(srcImage[restore(id, width)]));
    }

    let offset = exclusiveScan(x, gi);

    if (id < count)
    {
        prefixSums[id] = offset;
    }

    if (gi == kNumThreads - 1)
    {
        blockSums[id / kNumThreads] = offset + x;
    }
}

[numthreads(kNumThreads, 1, 1)]
[shader("compute")]
void scanBlockSums(uint gi: SV_GroupIndex)
{
    let count      = pushConstants.imageSize.x * pushConstants.imageSize.y;
    let blockCount = (count + kNumThreads - 1) / kNumThreads;

    if (gi == 0)
    {
        blockCarry = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint base = 0; base < blockCount; base += kNumThreads)
    {
        let index = base + gi;

        uint x = 0;
        if (index < blockCount)
        {
            x = blockSums[index];
        }

        let offset = exclusiveScan(x, gi);

        if (index < blockCount)
        {
            blockSums[index] = blockCarry + offset;
        }

        GroupMemoryBarrierWithGroupSync();
        if (gi == kNumThreads - 1)
        {
            blockCarry += offset + x;
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

[numthreads(kNumThreads, 1, 1)]
[shader("compute")]
void prefixSumPass2(uint id: SV_DispatchThreadID, uint gi: SV_GroupIndex)
{
    let width  = pushConstants.imageSize.x;
    let height = pushConstants.imageSize.y;

    if (id >= width * height)
    {
        return;
    }

    let blockID = id / kNumThreads;
    prefixSums[id] += blockSums[blockID];
}

[numthreads(16, 16, 1)]
[shader("compute")]
void compaction(uint2 id: SV_DispatchThreadID, uint3 groupID: SV_GroupID)
{
    let width  = pushConstants.imageSize.x;
    let height = pushConstants.imageSize.y;

    if (id.x >= width || id.y >= height)
    {
        return;
    }

    let val = srcImage[id];

    if (pred(val))
    {
        let newPos        = prefixSums[flatten(id, width)];
        dstBuffer[newPos] = id;
    }

    if (all(id == 0))
    {
        let last  = width * height - 1;
        let count = prefixSums[last] + uint(pred(srcImage[restore(last, width)]));

        indirectBuffer[0]                      = count;
        candidateCounts[pushConstants.eventID] = count;
    }
}
