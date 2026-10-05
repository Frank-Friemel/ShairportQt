#include "airplay2/Ap2Decoder.h"
#include <spdlog/spdlog.h>
#include <cstring>

#ifdef SHAIRPORT_HAS_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
#define AP2_HAS_CH_LAYOUT 1
#endif
#endif

using namespace std;

namespace AirPlay2
{
    bool AudioFormatFromAirPlayFormat(uint64_t audioFormatBit, AudioFormat& format) noexcept
    {
        switch (audioFormatBit)
        {
        case 0x40000:   format = { AudioCodec::ALAC, 44100, 2, 16, 352 }; return true;
        case 0x200000:  format = { AudioCodec::ALAC, 48000, 2, 24, 352 }; return true;
        case 0x400000:  format = { AudioCodec::AAC, 44100, 2, 16, 1024 }; return true;
        case 0x800000:  format = { AudioCodec::AAC, 48000, 2, 16, 1024 }; return true;
        default:        return false;
        }
    }

    bool AudioFormatFromSsrc(uint32_t ssrc, AudioFormat& format) noexcept
    {
        switch (ssrc)
        {
        case 0x0000FACE: format = { AudioCodec::ALAC, 44100, 2, 16, 352 }; return true;
        case 0x15000000: format = { AudioCodec::ALAC, 48000, 2, 24, 352 }; return true;
        case 0x16000000: format = { AudioCodec::AAC, 44100, 2, 16, 1024 }; return true;
        case 0x17000000: format = { AudioCodec::AAC, 48000, 2, 16, 1024 }; return true;
        default:         return false;
        }
    }

#ifdef SHAIRPORT_HAS_FFMPEG
    namespace
    {
        int SampleRateIndex(int rate)
        {
            static const int rates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

            for (int i = 0; i < static_cast<int>(sizeof(rates) / sizeof(rates[0])); ++i)
            {
                if (rates[i] == rate)
                {
                    return i;
                }
            }
            return 4;
        }

        void PutBE32(uint8_t* p, uint32_t v)
        {
            p[0] = static_cast<uint8_t>(v >> 24);
            p[1] = static_cast<uint8_t>(v >> 16);
            p[2] = static_cast<uint8_t>(v >> 8);
            p[3] = static_cast<uint8_t>(v);
        }

        vector<uint8_t> MakeExtradata(const AudioFormat& f)
        {
            vector<uint8_t> x;

            if (f.codec == AudioCodec::ALAC)
            {
                // ALAC "magic cookie": 'alac' atom followed by the ALACSpecificConfig
                x.resize(36, 0);
                PutBE32(&x[0], 36);
                memcpy(&x[4], "alac", 4);
                PutBE32(&x[12], static_cast<uint32_t>(f.framesPerPacket));
                x[16] = 0;                                  // compatible version
                x[17] = static_cast<uint8_t>(f.bitDepth);
                x[18] = 40;                                 // pb
                x[19] = 10;                                 // mb
                x[20] = 14;                                 // kb
                x[21] = static_cast<uint8_t>(f.channels);
                x[22] = 0; x[23] = 255;                     // max run
                PutBE32(&x[24], 0);                         // max frame bytes
                PutBE32(&x[28], 0);                         // avg bit rate
                PutBE32(&x[32], static_cast<uint32_t>(f.sampleRate));
            }
            else
            {
                // AudioSpecificConfig: AAC-LC (object type 2)
                const int idx = SampleRateIndex(f.sampleRate);
                const uint16_t asc = static_cast<uint16_t>((2 << 11) | (idx << 7) | (f.channels << 3));
                x = { static_cast<uint8_t>(asc >> 8), static_cast<uint8_t>(asc & 0xff) };
            }
            return x;
        }

        class FfmpegDecoder : public Ap2Decoder
        {
        public:
            explicit FfmpegDecoder(const AudioFormat& format)
                : m_format(format)
            {
                Open();
            }

            ~FfmpegDecoder() override
            {
                Close();
            }

            const AudioFormat& Format() const noexcept override { return m_format; }

            void Reset() override
            {
                Close();
                Open();
            }

            bool Decode(const uint8_t* data, size_t len, vector<int16_t>& pcm) override
            {
                if (!m_ctx || !m_packet || !m_frame)
                {
                    return false;
                }
                if (av_new_packet(m_packet, static_cast<int>(len)) < 0)
                {
                    return false;
                }
                memcpy(m_packet->data, data, len);

                int ret = avcodec_send_packet(m_ctx, m_packet);
                av_packet_unref(m_packet);

                if (ret < 0)
                {
                    return false;
                }
                while ((ret = avcodec_receive_frame(m_ctx, m_frame)) == 0)
                {
                    if (!m_swr && !OpenResampler())
                    {
                        av_frame_unref(m_frame);
                        return false;
                    }
                    const int maxOut = swr_get_out_samples(m_swr, m_frame->nb_samples);

                    if (maxOut > 0)
                    {
                        const size_t offset = pcm.size();
                        pcm.resize(offset + static_cast<size_t>(maxOut) * OutputChannels);
                        uint8_t* out = reinterpret_cast<uint8_t*>(pcm.data() + offset);

                        const int n = swr_convert(m_swr, &out, maxOut,
                            const_cast<const uint8_t**>(m_frame->extended_data), m_frame->nb_samples);

                        pcm.resize(offset + static_cast<size_t>(max(n, 0)) * OutputChannels);
                    }
                    av_frame_unref(m_frame);
                }
                return ret == AVERROR(EAGAIN) || ret == AVERROR_EOF;
            }

        private:
            void Open()
            {
                const AVCodec* codec = avcodec_find_decoder(m_format.codec == AudioCodec::ALAC ? AV_CODEC_ID_ALAC : AV_CODEC_ID_AAC);

                if (!codec)
                {
                    spdlog::error("AirPlay2: FFmpeg decoder for {} not found", m_format.codec == AudioCodec::ALAC ? "ALAC" : "AAC");
                    return;
                }
                m_ctx = avcodec_alloc_context3(codec);

                if (!m_ctx)
                {
                    return;
                }
                const auto extra = MakeExtradata(m_format);
                m_ctx->extradata = static_cast<uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
                memcpy(m_ctx->extradata, extra.data(), extra.size());
                m_ctx->extradata_size = static_cast<int>(extra.size());
                m_ctx->sample_rate = m_format.sampleRate;
#ifdef AP2_HAS_CH_LAYOUT
                av_channel_layout_default(&m_ctx->ch_layout, m_format.channels);
#else
                m_ctx->channels = m_format.channels;
                m_ctx->channel_layout = av_get_default_channel_layout(m_format.channels);
#endif
                if (avcodec_open2(m_ctx, codec, nullptr) < 0)
                {
                    spdlog::error("AirPlay2: failed to open the audio decoder");
                    avcodec_free_context(&m_ctx);
                    return;
                }
                m_packet = av_packet_alloc();
                m_frame = av_frame_alloc();
            }

            bool OpenResampler()
            {
#ifdef AP2_HAS_CH_LAYOUT
                AVChannelLayout outLayout;
                av_channel_layout_default(&outLayout, OutputChannels);

                if (swr_alloc_set_opts2(&m_swr, &outLayout, AV_SAMPLE_FMT_S16, OutputSampleRate,
                    &m_frame->ch_layout, static_cast<AVSampleFormat>(m_frame->format), m_frame->sample_rate, 0, nullptr) < 0)
                {
                    return false;
                }
#else
                const int64_t inLayout = m_frame->channel_layout ? m_frame->channel_layout : av_get_default_channel_layout(m_frame->channels);
                m_swr = swr_alloc_set_opts(nullptr, av_get_default_channel_layout(OutputChannels), AV_SAMPLE_FMT_S16, OutputSampleRate,
                    inLayout, static_cast<AVSampleFormat>(m_frame->format), m_frame->sample_rate, 0, nullptr);

                if (!m_swr)
                {
                    return false;
                }
#endif
                if (swr_init(m_swr) < 0)
                {
                    swr_free(&m_swr);
                    return false;
                }
                return true;
            }

            void Close()
            {
                if (m_swr)
                {
                    swr_free(&m_swr);
                }
                if (m_frame)
                {
                    av_frame_free(&m_frame);
                }
                if (m_packet)
                {
                    av_packet_free(&m_packet);
                }
                if (m_ctx)
                {
                    avcodec_free_context(&m_ctx);
                }
            }

            const AudioFormat   m_format;
            AVCodecContext*     m_ctx{ nullptr };
            AVPacket*           m_packet{ nullptr };
            AVFrame*            m_frame{ nullptr };
            SwrContext*         m_swr{ nullptr };
        };
    }

    bool Ap2Decoder::IsAvailable() noexcept
    {
        return avcodec_find_decoder(AV_CODEC_ID_AAC) != nullptr && avcodec_find_decoder(AV_CODEC_ID_ALAC) != nullptr;
    }

    unique_ptr<Ap2Decoder> Ap2Decoder::Create(const AudioFormat& format)
    {
        return make_unique<FfmpegDecoder>(format);
    }
#else
    bool Ap2Decoder::IsAvailable() noexcept
    {
        return false;
    }

    unique_ptr<Ap2Decoder> Ap2Decoder::Create(const AudioFormat&)
    {
        return nullptr;
    }
#endif
}
