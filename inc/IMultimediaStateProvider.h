#pragma once

#include <string>
#include <vector>

class IMultimediaStateProvider
{
public:
    virtual bool GetServiceName(std::string& name, std::string& subName) const noexcept = 0;
    virtual void GetDesktopEntry(std::string& entry) const noexcept = 0;
    virtual void PlayPause() noexcept = 0;
    virtual void SkipNext() noexcept = 0;
    virtual void SkipPrevious() noexcept = 0;
    virtual double GetVolume() const noexcept = 0;
    virtual void SetVolume(double v) noexcept = 0;
    virtual bool GetTrackInfo(std::wstring& track, std::wstring& album, std::wstring& artist, std::vector<unsigned char>& art) noexcept = 0;
    virtual void ShowWindow() noexcept = 0;
    virtual void QuitApp() noexcept = 0;
};
