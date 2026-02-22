#pragma once

#include <string>
#include <vector>

class IMultimediaStateProvider
{
public:
    virtual void PlayPause() noexcept = 0;
    virtual void SkipNext() noexcept = 0;
    virtual void SkipPrevious() noexcept = 0;
    virtual bool GetTrackInfo(std::wstring& track, std::wstring& album, std::wstring& artist, std::vector<unsigned char>& art) noexcept = 0;
};
