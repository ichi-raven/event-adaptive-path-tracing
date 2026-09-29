module Event;

import "../Sampler/Sampler.slang";
import "../Utility/Warp.slang";

public struct Event
{
    public __init(const int16_t t_)
    {
        t = t_;
    }

    public property p : int16_t
    {
        get {
            return int16_t(sign(t));
        }
    }

    public int16_t t;
};

public struct ThresholdParams
{
    public __init(const float muPos_, const float muNeg_, const float sigma_ = 0.03)
    {
        muPos = muPos_;
        muNeg = muNeg_;
        sigma = sigma_;
    }

    public float muPos = 1.0;
    public float muNeg = 1.0;
    public float sigma = 0.03;
}

public struct Thresholds
{
    public __init(inout IndependentSampler sampler, ThresholdParams params)
    {
        let r = Warp::toNormalDistribution(sampler.next2D());
        pos   = max(0.01, r * params.sigma + params.muPos);
        neg   = max(0.01, r * params.sigma + params.muNeg);
    }

    public __init(ThresholdParams params)
    {
        pos = max(0.01, params.muPos);
        neg = max(0.01, params.muNeg);
    }

    public float pos;
    public float neg;
}

public float toGray(const float3 c)
{
    // ITU-R BT.601 (Y for jpeg)
    static let coef = float3(0.299, 0.587, 0.114);
    return dot(c, coef);
}

public uint16_t pack(const int sign, const float t, const uint16_t maxVal)
{
    let clampedTime = clamp(t, 0.0, 1.0);
    let timestamp   = uint16_t(clampedTime * float(maxVal - 1)) + uint16_t(1);
    return timestamp | uint16_t(select(sign > 0, 0x8000, 0));
}

public uint4 packEvents(const uint16_t ts[8])
{
    return uint4((uint32_t(ts[0]) << 16) | uint32_t(ts[1]), (uint32_t(ts[2]) << 16) | uint32_t(ts[3]),
                 (uint32_t(ts[4]) << 16) | uint32_t(ts[5]), (uint32_t(ts[6]) << 16) | uint32_t(ts[7]));
}

public uint16_t[8] unpackEvents(uint4 packedEvents)
{
    uint16_t unpacked[8];

    [ForceUnroll]
    for (int i = 0; i < 4; ++i)
    {
        unpacked[i * 2]     = uint16_t(packedEvents[i] >> 16);
        unpacked[i * 2 + 1] = uint16_t(packedEvents[i] & 0x0000FFFF);
    }

    return unpacked;
}

// for lin-log (private)
static let threshold = 20;  // DN20?
static let rounding  = 1e8;
static let maxVal    = 255.0;

[ForceInline]
public inline float linLog(float x)
{
    x = max(x * maxVal, 1.0e-8);

    let f = (1. / threshold) * log(threshold);

    var y = select(x <= threshold, x * f, log(x));

    y = round(y * rounding) / rounding;

    return y;
}

[ForceInline]
public inline float sampleVariance(const float mean, const float squareMean, const uint n)
{
    if (n <= 1)
    {
        return 0.0;
    }
    return max(0.0, squareMean - mean * mean) * float(n) / float(n - 1);
}

[ForceInline]
inline Tuple<float, float> deltaMethod(const float mean, const float s2, const float n)
{
    float sampleMean = 0, sampleVar = 0;

    // manual differentiation
    sampleMean = linLog(mean);

    float derivative = 0;

    if (mean * maxVal <= threshold)  // linear
    {
        derivative = (1. / threshold) * log(threshold) * maxVal;  // f
    }
    else  // log
    {
        derivative = (1. / mean);  // 1/x
    }

    sampleVar = (derivative * derivative) * (s2 / n);

    return makeTuple(sampleMean, sampleVar);
}

[ForceInline]
public inline Spectral linLog<Spectral>(const Spectral c)
    where Spectral : IFloat
{
    if (Spectral is float3)
    {
        let cf3 = (c as float3).value;
        return (float3(linLog(cf3.x), linLog(cf3.y), linLog(cf3.z)) as Spectral).value;
    }
    else if (Spectral is float)
    {
        let cf1 = (c as float).value;
        return (linLog(cf1) as Spectral).value;
    }

    return Spectral(0.0);
}

public struct Range
{
    public __init()
    {
        t0   = 0;
        t1   = 1.0;
        L1   = none;
        sqL1 = none;
    }

    public __init(const float t0_, const float t1_)
    {
        t0   = t0_;
        t1   = t1_;
        L1   = none;
        sqL1 = none;
    }

    public __init(const float t0_, const float t1_, const float L1_, const float sqL1_)
    {
        t0   = t0_;
        t1   = t1_;
        L1   = L1_;
        sqL1 = sqL1_;
    }

    inline public float diff()
    {
        return t1 - t0;
    }

    inline public float mid()
    {
        return (t0 + t1) * 0.5;
    }

    inline public float lerp(const float t)
    {
        return lerp(t0, t1, t);
    }

    public float t0;
    public float t1;
    public Optional<float> L1;
    public Optional<float> sqL1;
};

public inline uint4 append(uint4 original, const uint16_t e, const uint idx)
{
    const uint32_t packed = select(idx % 2 == 0, uint32_t(e) << 16, uint32_t(e));

    if (idx < 2)
    {
        original.x |= packed;
    }
    else if (idx < 4)
    {
        original.y |= packed;
    }
    else if (idx < 6)
    {
        original.z |= packed;
    }
    else
    {
        original.w |= packed;
    }

    return original;
}

// WARNING: This method calculates Welch's DoF from two mean variances (NOT sample variances!!)
float welchsDegreesOfFreedom(const float varMean1, const float varMean2, const uint n1, const uint n2)
{
    if (n1 <= 1 || n2 <= 1)
    {
        return 0.0;
    }

    let numerator = (varMean1 + varMean2) * (varMean1 + varMean2);

    let denominator = (varMean1 * varMean1) / float(n1 - 1) + (varMean2 * varMean2) / float(n2 - 1);

    if (denominator < 1e-8)
    {
        return min(float(n1), float(n2)) - 1.0;
    }

    return numerator / denominator;
}

// true to continue the sample
public inline bool tTest(const float l1, const float l2, const float v1, const float v2, const uint n1, const uint n2,
                         const float threshold)
{
    // alpha = 0.05
    float tCritical = 1.645;

    let res1 = deltaMethod(l1, v1, float(n1));
    let res2 = deltaMethod(l2, v2, float(n2));

    let varMean1 = res1._1;
    let varMean2 = res2._1;

    // multiple linear interpolation
    let n = welchsDegreesOfFreedom(varMean1, varMean2, n1, n2);

    const float[] dofLevels = { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0, 1024.0, 2048.0 };
    const float[] sigLevels = { 6.31375, 2.91999, 2.13185, 1.85955, 1.74588, 1.69389,
                                1.66901, 1.65685, 1.65082, 1.64784, 1.64634, 1.64560 };
    if (n < 1.0)
    {
        return true;
    }

    for (int i = 0; i < dofLevels.getCount() - 1; ++i)
    {
        if (n >= dofLevels[i] && n < dofLevels[i + 1])
        {
            tCritical =
                lerp(sigLevels[i], sigLevels[i + 1], float(n - dofLevels[i]) / (dofLevels[i + 1] - dofLevels[i]));
            break;
        }
    }

    let diffMean = res1._0 - res2._0;
    let denom    = sqrt(res1._1 + res2._1);
    if (denom < 1e-8)
    {
        return abs(diffMean) >= threshold;
    }

    let tUpper  = (diffMean - threshold) / denom;
    let tLower  = (diffMean + threshold) / denom;
    let noEvent = (tLower > tCritical) && (tUpper < -tCritical);
    return !noEvent;
}
