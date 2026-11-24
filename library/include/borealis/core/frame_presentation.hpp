#pragma once
#include <algorithm>
#include <cstdint>

namespace brls
{
// A held video frame need not be presented on every refresh when VRR is enabled.
// The UI/input loop still runs, and a keepalive refresh bounds overlay staleness.
// The keepalive stretches to 1.5 video frame intervals: at 30 fps a fixed 34 ms
// keepalive lands just before each new frame and pushes it a refresh late.
class FramePresentation
{
  public:
    static constexpr uint64_t kKeepaliveNs    = 34000000;
    static constexpr uint64_t kMaxKeepaliveNs = 100000000;

    void setFrameDriven(bool enabled)
    {
        frameDriven = enabled;
        lastPresent = 0;
        lastVideo = videoInterval = 0;
    }
    bool isFrameDriven() const { return frameDriven; }
    bool shouldPresent(bool video, bool changed, bool resized, uint64_t now) const
    {
        return !frameDriven || !video || changed || resized || !lastPresent
            || now - lastPresent >= keepaliveNs();
    }
    void presented(uint64_t now) { lastPresent = now; }
    // A new video frame was presented: learn the source cadence.
    void videoPresented(uint64_t now)
    {
        if (lastVideo && now > lastVideo && now - lastVideo < 250000000)
        {
            const uint64_t interval = now - lastVideo;
            videoInterval = videoInterval ? (videoInterval * 7 + interval) / 8 : interval;
        }
        lastVideo = now;
    }
    uint64_t keepaliveNs() const
    {
        return std::min(kMaxKeepaliveNs, std::max(kKeepaliveNs, videoInterval * 3 / 2));
    }
    uint64_t waitNs(uint64_t now) const
    {
        const uint64_t keepalive = keepaliveNs();
        const uint64_t age       = now - lastPresent;
        const uint64_t left      = age < keepalive ? keepalive - age : 0;
        return left < 8000000 ? left : 8000000;
    }

  private:
    bool frameDriven       = false;
    uint64_t lastPresent   = 0;
    uint64_t lastVideo     = 0;
    uint64_t videoInterval = 0;
};
}
