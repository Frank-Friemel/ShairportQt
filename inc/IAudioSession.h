#pragma once

#include <cstdint>
#include <string>

// common interface of the AirPlay 1 (HairTunes) and AirPlay 2 audio sessions
class IAudioSession
{
public:
    virtual ~IAudioSession() = default;

    virtual void Flush(unsigned int seq = 0) noexcept = 0;
    virtual const std::string& GetClientID() const noexcept = 0;
    virtual void ResetProgess() noexcept = 0;
    virtual int GetProgressTime() const noexcept = 0;
    virtual bool IsPlaying() const noexcept = 0;
    virtual uint64_t GetSamplingFreq() const noexcept = 0;
};
