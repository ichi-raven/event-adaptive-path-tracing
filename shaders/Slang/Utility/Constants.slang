module Constants;

public namespace k
{
    public const static float pi        = 3.14159265358979323846;
    public const static float twoPi     = 6.28318530717958647692;
    public const static float invPi     = 0.31830988618379067154;
    public const static float invTwoPi  = 0.15915494309189533577;
    public const static float invFourPi = 0.07957747154594766788;
    public const static float piOver2   = 1.57079632679489661923;
    public const static float piOver4   = 0.78539816339744830961;
    public const static float sqrt2     = 1.41421356237309504880;

    public const static float eps         = 1e-3;
    public const static float infty       = 1e6;
    public const static uint32_t maxDepth = 16;

    public const static int32_t invalidTexIndex    = -1;
    public const static uint32_t MISHeuristicBeta  = 1;
    public const static uint32_t fixedEmitterLimit = 10;

    public const static float4 zeros = float4(0.0);
    public const static float4 ones  = float4(1.0);

    public const static float3 red     = float3(0.8, 0., 0.);
    public const static float3 green   = float3(0., 0.8, 0.);
    public const static float3 blue    = float3(0., 0., 0.8);
    public const static float3 yellow  = float3(0.8, 0.8, 0.);
    public const static float3 cyan    = float3(0.0, 0.8, 0.8);
    public const static float3 magenta = float3(0.8, 0.0, 0.8);
    public const static float3 gray    = float3(0.5, 0.5, 0.5);
    public const static float3 black   = float3(0.0);
    public const static float3 white   = float3(1.0);
}
