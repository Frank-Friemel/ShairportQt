#pragma once

#include "LayerCake.h"
#include "IMultimediaStateReceiver.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>

// see https://specifications.freedesktop.org/mpris/latest/
// see https://github.com/KDE/amarok/tree/master/src/dbus/mpris2

class MultimediaStateReceiver
    : public IMultimediaStateReceiver
{
private:
    std::mutex                  m_mtx;
    std::atomic_bool            m_stop{ true };
    std::atomic_bool            m_work{ false };
    std::atomic_bool            m_enabledByConfig;
    bool                        m_isPlaying{ false };
    bool                        m_isEnabled{ false };
    bool                        m_hasNewTrackInfo{ false };
    double                      m_volume{ 1.0 };
    std::condition_variable     m_cv;
    std::thread                 m_thread;
    IMultimediaStateProvider*   m_provider{ nullptr };
    QObject*                    m_parent{ nullptr };
    void*                       m_mprisHost{ nullptr };

public:
    MultimediaStateReceiver(bool enabledByConfig);
    ~MultimediaStateReceiver();

    MultimediaStateReceiver(const MultimediaStateReceiver&) = delete;
    MultimediaStateReceiver& operator=(const MultimediaStateReceiver&) = delete;

    // implementation
    void Configure(bool enable) noexcept override;
    void Initialize(IMultimediaStateProvider* provider, QObject* parent, NativeWindowHandle nativeWindowHandle) noexcept override;
    void Cleanup() noexcept override;
    void OnPlayState(bool isPlaying) noexcept override;
    void OnUpdateMMState(bool isEnabled) noexcept override;
    void OnUpdateTrackInfo() noexcept override;
    void OnUpdateVolume(double v) noexcept override;

private:
    void Start() noexcept;
    void Stop() noexcept;
};
