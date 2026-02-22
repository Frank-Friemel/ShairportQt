#pragma once

using NativeWindowHandle = void*;
class IMultimediaStateProvider;

class IMultimediaStateReceiver
{
public:
    virtual ~IMultimediaStateReceiver() = default;

    virtual void Configure(bool enable) noexcept = 0;
    virtual void Initialize(IMultimediaStateProvider* provider, NativeWindowHandle nativeWindowHandle) noexcept = 0;
    virtual void Cleanup() noexcept = 0;
    virtual void OnPlayState(bool isPlaying) noexcept = 0;
    virtual void OnUpdateMMState(bool isEnabled) noexcept = 0;
    virtual void OnUpdateTrackInfo() noexcept = 0;
};
