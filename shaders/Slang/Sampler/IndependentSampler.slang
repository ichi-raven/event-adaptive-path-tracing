
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Based on pcg32.h by Wenzel Jakob, February 2015.
 * PCG was developed by Melissa O'Neill.
 * Source: https://github.com/wjakob/pcg32/blob/master/pcg32.h
 *
 * Modified for EventAdaptivePT:
 * ported to Slang and adapted to the ISampler interface.
 *
 * See licenses/Apache-2.0.txt for the full license.
 */

implementing Sampler;

static const uint64_t PCG32_DEFAULT_STATE  = 0x853c49e6748fea9bULL;
static const uint64_t PCG32_DEFAULT_STREAM = 0xda3e39cb94b95bdbULL;
static const uint64_t PCG32_MULT           = 0x5851f42d4c957f2dULL;

public struct IndependentSampler : ISampler
{
    public __init()
    {
        state = PCG32_DEFAULT_STATE;
        inc   = PCG32_DEFAULT_STREAM;
    }

    public __init(uint64_t state_, uint64_t inc_ = 1u)
    {
        state = state_;
        inc   = inc_;
        scramble(state_, inc_);
    }

    [mutating]
    public void scramble(uint64_t initstate, uint64_t initseq = 1)
    {
        state = 0u;
        inc   = (initseq << 1u) | 1u;
        nextUint();
        state += initstate;
        nextUint();
    }

    public uint64_t getState()
    {
        return state;
    }

    [mutating]
    public void setState(uint64_t state_)
    {
        state = state_;
    }

    [mutating]
    public uint32_t nextUint()
    {
        uint64_t oldstate   = state;
        state               = oldstate * PCG32_MULT + inc;
        uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot        = uint32_t(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
    }

    [mutating]
    public uint32_t nextUint(uint32_t bound)
    {
        uint32_t threshold = (~bound + 1u) % bound;
        while (true)
        {
            uint32_t r = nextUint();
            if (r >= threshold)
            {
                return r % bound;
            }
        }
    }

    [mutating]
    public float nextFloat()
    {
        uint32_t u = (nextUint() >> 9) | 0x3f800000u;
        return asfloat(u) - 1.0;
    }

    [mutating]
    public float next1D()
    {
        return nextFloat();
    }

    [mutating]
    public float2 next2D()
    {
        let x = next1D();
        let y = next1D();
        return float2(x, y);
    }

    private uint64_t state;
    private uint64_t inc;
}
