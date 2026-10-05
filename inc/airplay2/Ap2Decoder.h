#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

namespace AirPlay2
{
    enum class AudioCodec
    {
        ALAC,
        AAC,
    };

    struct AudioFormat
    {
        AudioCodec  codec{ AudioCodec::ALAC };
        int         sampleRate{ 44100 };
        int         channels{ 2 };
        int         bitDepth{ 16 };
        int         framesPerPacket{ 352 };
    };

    // AirPlay 2 "audioFormat" bit (SETUP) to an audio format, false if not supported
    bool AudioFormatFromAirPlayFormat(uint64_t audioFormatBit, AudioFormat& format) noexcept;

    // buffered audio packets are tagged with an SSRC which identifies the format
    bool AudioFormatFromSsrc(uint32_t ssrc, AudioFormat& format) noexcept;

    // Decodes compressed audio into interleaved signed 16-bit stereo PCM at the output rate
    // (implemented with FFmpeg; Create() returns nullptr if FFmpeg isn't available)
    class Ap2Decoder
    {
    public:
        static constexpr int OutputSampleRate = 44100;
        static constexpr int OutputChannels = 2;

        virtual ~Ap2Decoder() = default;

        static bool IsAvailable() noexcept;
        static std::unique_ptr<Ap2Decoder> Create(const AudioFormat& format);

        // appends the decoded samples (interleaved int16, OutputChannels) to "pcm"
        virtual bool Decode(const uint8_t* data, size_t len, std::vector<int16_t>& pcm) = 0;

        // drops any internal state (e.g. after a flush)
        virtual void Reset() = 0;

        virtual const AudioFormat& Format() const noexcept = 0;
    };
}
