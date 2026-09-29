module SurfaceInteraction;

import "Frame";

public float getArea(const float3 p0, const float3 p1, const float3 p2)
{
    return 0.5 * length(cross(p2 - p0, p1 - p0));
}

public struct SurfaceInteraction
{
    public __init(const float3 pos_, const float3 wi_, const float3 ns_, const float3 ng_, const float2 uv_,
                  const float area_, const uint32_t instanceIndex_, const Frame frame_)
    {
        pos           = pos_;
        wi            = normalize(wi_);
        ns            = normalize(ns_);
        ng            = normalize(ng_);
        uv            = uv_;
        area          = area_;
        instanceIndex = instanceIndex_;
        frame         = frame_;
    }

    public SurfaceInteraction toLocal()
    {
        return SurfaceInteraction(pos, normalize(frame.toLocal(wi)), normalize(frame.toLocal(ns)),
                                  normalize(frame.toLocal(ng)), uv, area, instanceIndex, frame);
    }

    public SurfaceInteraction toWorld()
    {
        return SurfaceInteraction(pos, normalize(frame.toWorld(wi)), normalize(frame.toWorld(ns)),
                                  normalize(frame.toWorld(ng)), uv, area, instanceIndex, frame);
    }

    public float3 pos;
    public float3 wi;
    public float3 ns;
    public float3 ng;
    public float2 uv;
    public float area;
    public uint32_t instanceIndex;
    public Frame frame;
}
