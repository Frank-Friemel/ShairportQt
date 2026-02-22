#pragma once

#include "LayerCake.h"
#include "IMultimediaStateReceiver.h"

// see https://specifications.freedesktop.org/mpris/latest/
// see https://github.com/audacious-media-player/audacious-plugins/tree/master/src/mpris2
// see https://github.com/KDE/amarok/tree/master/src/dbus/mpris2

class MultimediaStateReceiver
    : public IMultimediaStateReceiver
{
private:
    public:
    MultimediaStateReceiver(bool enabledByConfig);
    ~MultimediaStateReceiver();

    MultimediaStateReceiver(const MultimediaStateReceiver&) = delete;
    MultimediaStateReceiver& operator=(const MultimediaStateReceiver&) = delete;

    // implementation
    void Configure(bool enable) noexcept override;
    void Initialize(IMultimediaStateProvider* provider, NativeWindowHandle nativeWindowHandle) noexcept override;
    void Cleanup() noexcept override;
    void OnPlayState(bool isPlaying) noexcept override;
    void OnUpdateMMState(bool isEnabled) noexcept override;
    void OnUpdateTrackInfo() noexcept override;
};
