#pragma once

using NativeWindowHandle = void*;
class IMultimediaStateProvider;
class QObject;

class IMultimediaStateReceiver
{
public:
    virtual ~IMultimediaStateReceiver() = default;

    virtual void Configure(bool enable) noexcept = 0;
    virtual void Initialize(IMultimediaStateProvider* provider, QObject* parent, NativeWindowHandle nativeWindowHandle) noexcept = 0;
    virtual void Cleanup() noexcept = 0;
    virtual void OnUpdatePlayState(bool isPlaying) noexcept = 0;
    virtual void OnUpdateMMState(bool isEnabled) noexcept = 0;
    virtual void OnUpdateTrackInfo() noexcept = 0;
    virtual void OnUpdateVolume(double v) noexcept = 0;
};
