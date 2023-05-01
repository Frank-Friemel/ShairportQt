#pragma once

#include <string>

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <assert.h>

#include <memory>
#include <iostream>
#include <vector>
#include <sstream>

#include <alsa/asoundlib.h>

#include <iostream>
#include <system_error>


#include <cmath>

#include "WaveHeader.h"

namespace AlsaAudio
{
    void handle_error_code(int err_code, bool throws, std::string error_desc);

    enum class AudioChannels : int
    {
        MONO = 1,
        STEREO = 2,
        STEREO_PLUS_SUB = 3,
        STEREO_SURROUND = 4,
        FULL_SURROUND = 5,
        FULL_SURROUND_PLUS_SUB = 6
    };

    class Mixer
    {
        public:
        Mixer(std::string hw_device, std::string volume_element_name);
        ~Mixer();

        static bool device_exists(std::string hw_device);
        static bool element_exists(std::string hw_device, std::string element_name);

        double inc_vol_pct(double pct, snd_mixer_selem_channel_id_t channel = SND_MIXER_SCHN_MONO);
        double dec_vol_pct(double pct, snd_mixer_selem_channel_id_t channel = SND_MIXER_SCHN_MONO);
        double set_vol_pct(double pct);
        double get_cur_vol_pct(snd_mixer_selem_channel_id_t channel = SND_MIXER_SCHN_MONO);
        double mute();
        double unmute();

        private:
        int                   err;
        std::string           device_name;
        snd_mixer_t*          mixer_handle;
        snd_mixer_selem_id_t* simple_mixer_handle;
        std::string           simple_elem_name;
        snd_mixer_elem_t*     element_handle;
        long                  mute_vol;

        void trim_pct(double& pct);
        void set_vol_raw(long vol);
        long get_cur_vol_raw(snd_mixer_selem_channel_id_t channel = SND_MIXER_SCHN_MONO);
        void get_vol_range(long* min_vol, long* max_vol);
    };

    class HwParams
    {
    public:
        HwParams()
        {
            access_type = SND_PCM_ACCESS_RW_INTERLEAVED;
            bufsize     = 0;
        }

        inline size_t GetBufSize() const
        {
            assert(bufsize);
            return bufsize;
        }

        inline void SetBufSize(size_t n)
        {
            bufsize = n;
        }

        inline void InitFrom(const WaveHeader& wav_hdr)
        {
            // WAV data is always stored in Little-Endian byte order so we choose
            // the format based on the bits per sample. <=8 bits/sample is unsigned,
            // >8 bits/sample is signed. See
            // http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/Docs/riffmci.pdf
            switch (wav_hdr.myData.bitsPerSample)
            {
            case 8:
                format_type = SND_PCM_FORMAT_U8;
                break;
            case 16:
                format_type = SND_PCM_FORMAT_S16_LE;
                break;
            case 24:
                format_type = SND_PCM_FORMAT_S24_LE;
                break;
            case 32:
                format_type = SND_PCM_FORMAT_S32_LE;
                break;
            default:
                throw std::invalid_argument("wrong Bits per Sample");
            }

            sample_rate_hz  = wav_hdr.myData.sampleRate;
            channels        = static_cast<AudioChannels>(wav_hdr.myData.numChannels);
            byteRate        = wav_hdr.myData.byteRate;
            bitsPerSample   = wav_hdr.myData.bitsPerSample;
            period_time_us  = 0;
        }

    public:
        snd_pcm_access_t    access_type;
        snd_pcm_format_t    format_type;
        unsigned int        sample_rate_hz;
        AudioChannels       channels;
        unsigned int        period_time_us;
        unsigned int        byteRate;
        unsigned int        bitsPerSample;

    private:
        size_t              bufsize;
    };

  class PCMDevice
  { 
    public:
      PCMDevice(std::string hw_device, snd_pcm_stream_t stream_type);
      ~PCMDevice();

      int set_hardware_params(HwParams& params);

    protected:
      int xrun_recovery();

      int                   err;
      unsigned long         frame_size_bytes;
      snd_pcm_uframes_t     period_size_in_frames; // number of frames between interrupts
      std::string           device_name;
      snd_pcm_t*            pcm_handle;
      snd_pcm_hw_params_t*  hw_params;
  };

  class PCMPlayer : public PCMDevice
  {
    public:
      PCMPlayer(std::string hw_device);

    void play_interleaved(const char* buffer, size_t buf_size);
    void flush();

  };
} 

