#pragma once
#include <algorithm>
#include <cstdint>

namespace brls
{
// A held video frame need not be presented on every refresh when VRR is enabled.
// The UI/input loop still runs, and a keepalive refresh bounds overlay staleness.
// The keepalive stretches to 1.5 video frame intervals: at 30 fps a fixed 34 ms
// keepalive lands just before each new frame and pushes it a refresh late.
// Low framerate compensation: a VRR panel can't hold an image longer than its
// minimum refresh rate allows (Switch 2 handheld: ~25 ms), so below ~50 fps it
// refreshes the old image on its own and new frames land 25 or 50+ ms apart.
// Video slower than kLfcMaxNs is re-presented at even fractions of its interval
// (30 fps -> every 16.7 ms), keeping the panel in range and on the video cadence.
class FramePresentation
{
  public:
    static constexpr uint64_t kKeepaliveNs    = 34000000;
    static constexpr uint64_t kMaxKeepaliveNs = 100000000;
    static constexpr uint64_t kLfcMaxNs       = 20000000; // re-present below 50 fps
    static constexpr uint64_t kLfcGuardNs     = 4000000;  // never this close to the next frame

    void setFrameDriven(bool enabled)
    {
        frameDriven = enabled;
        lastPresent = 0;
        lastVideo = videoInterval = 0;
        presentsSinceVideo        = 0;
    }
    bool isFrameDriven() const { return frameDriven; }
    // nextVideo: when the next video frame is due (0: unknown).
    bool shouldPresent(bool video, bool changed, bool resized, uint64_t now, uint64_t nextVideo = 0) const
    {
        return !frameDriven || !video || changed || resized || !lastPresent
            || now - lastPresent >= keepaliveNs() || lfcDue(now, nextVideo);
    }
    // Interval between LFC re-presents of a held frame, or 0 when not needed.
    uint64_t lfcParts() const
    {
        if (videoInterval <= kLfcMaxNs || videoInterval > kMaxKeepaliveNs)
            return 1;
        return (videoInterval + kLfcMaxNs - 1) / kLfcMaxNs;
    }
    uint64_t lfcStepNs() const
    {
        const uint64_t parts = lfcParts();
        return parts > 1 ? videoInterval / parts : 0;
    }
    // At most parts-1 re-presents per video frame (one at 30 fps), and none
    // close to the next frame: its due time when queued, else the cadence's
    // prediction. An extra re-present just before a frame makes VRR show it
    // one refresh late.
    bool lfcDue(uint64_t now, uint64_t nextVideo) const
    {
        const uint64_t step = lfcStepNs();
        if (!step || !lastPresent || !lastVideo || now - lastPresent + 500000 < step
            || presentsSinceVideo + 1 >= lfcParts())
            return false;
        const uint64_t next = nextVideo ? nextVideo : lastVideo + videoInterval;
        return next > now && next - now > kLfcGuardNs;
    }
    void presented(uint64_t now)
    {
        lastPresent = now;
        ++presentsSinceVideo;
    }
    // A new video frame was presented: learn the source cadence.
    void videoPresented(uint64_t now)
    {
        if (lastVideo && now > lastVideo && now - lastVideo < 250000000)
        {
            const uint64_t interval = now - lastVideo;
            videoInterval = videoInterval ? (videoInterval * 7 + interval) / 8 : interval;
        }
        lastVideo          = now;
        presentsSinceVideo = 0;
    }
    uint64_t keepaliveNs() const
    {
        return std::min(kMaxKeepaliveNs, std::max(kKeepaliveNs, videoInterval * 3 / 2));
    }
    uint64_t waitNs(uint64_t now) const
    {
        const uint64_t keepalive = keepaliveNs();
        const uint64_t age       = now - lastPresent;
        uint64_t left            = age < keepalive ? keepalive - age : 0;
        if (const uint64_t step = lfcStepNs(); step && presentsSinceVideo + 1 < lfcParts())
            left = std::min(left, age + 500000 < step ? step - 500000 - age : 0);
        return left < 8000000 ? left : 8000000;
    }

  private:
    bool frameDriven       = false;
    uint64_t lastPresent   = 0;
    uint64_t lastVideo     = 0;
    uint64_t videoInterval = 0;
    // Re-presents since the last new video frame (0 right after it).
    uint64_t presentsSinceVideo = 0;
};
}
