#ifndef _WIN32
#include <errno.h>
#include <fstream>
#include <vector>
#include "audio/AlsaAudio.h"
#include "audio/PlaySound.h"
#include "LayerCake.h"
#include <thread>
#include <chrono>

using namespace std;
using namespace chrono_literals;

void AlsaAudio::handle_error_code(int err_code, bool throws, string error_desc)
{
#ifndef NDEBUG
	cerr << error_desc << " (ALSA Description: " << snd_strerror(err_code) << ")" << endl;
#endif

	if (throws)
	{
		error_code ec(err_code, generic_category());
		system_error se(ec, error_desc);

		throw se;
	}
}

using namespace AlsaAudio;

PCMDevice::PCMDevice(string hw_device, snd_pcm_stream_t stream_type) :
    err(0),
    frame_size_bytes(0),
    period_size_in_frames(0),
    device_name(hw_device),
    pcm_handle(nullptr),
    hw_params(nullptr)
{
    if ((err = snd_pcm_open(&pcm_handle, device_name.c_str(), stream_type, 0)) < 0)
    {
        handle_error_code(err, true, "Cannot open handle to PCM audio device.");
    }
}

PCMDevice::~PCMDevice()
{
    if (hw_params)
    {
        snd_pcm_hw_params_free(hw_params);
    }

    if (pcm_handle)
    {
        snd_pcm_close(pcm_handle);
    }
}

int PCMDevice::set_hardware_params(HwParams& params)
{
    snd_pcm_state_t hw_state = snd_pcm_state(pcm_handle);

    if (hw_state == SND_PCM_STATE_OPEN)
    {
        frame_size_bytes = (snd_pcm_format_physical_width(params.format_type) * static_cast<int>(params.channels)) >> 3;
        assert(frame_size_bytes);

        if (!hw_params)
        {
            if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0)
            {
                handle_error_code(err, false, "Cannot allocate hardware parameter structure for PCM object.");
                return err;
            }
        }
        if ((err = snd_pcm_hw_params_any(pcm_handle, hw_params)) < 0)
        {
            handle_error_code(err, false, "Cannot initialize hardware parameter structure for PCM object.");
            return err;
        }

        if ((err = snd_pcm_hw_params_set_access(pcm_handle, hw_params, params.access_type)) < 0)
        {
            handle_error_code(err, false, "Cannot set access type for PCM object.");
            return err;
        }

        if ((err = snd_pcm_hw_params_set_format(pcm_handle, hw_params, params.format_type)) < 0)
        {
            handle_error_code(err, false, "Cannot set sample format for PCM object.");
            return err;
        }

        if ((err = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, static_cast<int>(params.channels))) < 0)
        {
            handle_error_code(err, false, "Cannot set channel count for PCM object.");
            return err;
        }

        unsigned int req_rate = params.sample_rate_hz;

        if ((err = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &req_rate, 0)) < 0)
        {
            handle_error_code(err, false, "Cannot set sample rate for PCM object.");
            return err;
        }
        else
        {
            assert(req_rate == params.sample_rate_hz);
        }

        // To calculate the period time in microseconds, we take the source
        // bytes per second divided by the size of the buffer (in bytes) to get the number
        // of buffer fills (periods) required per second. We then divide 1,000,000
        // (number of microseconds in a second) by the periods per second to get the
        // number of microseconds for each period (approximate)
        const double periods_per_sec = ((double)params.byteRate / (((double)(frame_size_bytes * 1024)) / (((double)params.bitsPerSample) / 8.0)));

        // Microseconds per period (requested)
        params.period_time_us = static_cast<unsigned int>(1000000.0l / periods_per_sec);

        if ((err = snd_pcm_hw_params_set_period_time_near(pcm_handle, hw_params, &params.period_time_us, 0)) < 0)
        {
            handle_error_code(err, false, "Cannot set period time for PCM object.");
            return err;
        }

        snd_pcm_uframes_t psize;

        if ((err = snd_pcm_hw_params_get_period_size(hw_params, &psize, 0)) < 0)
        {
            handle_error_code(err, false, "Could not get period size for PCM object.");
            return err;
        }
        else
        {
            period_size_in_frames = psize;
        }

        if ((err = snd_pcm_hw_params(pcm_handle, hw_params)) < 0)
        {
            handle_error_code(err, false, "Cannot apply hardware parameters to PCM device.");
            return err;
        }
        params.SetBufSize(period_size_in_frames * frame_size_bytes * 2);
        
        // the buffer size has to be aligned to the frame size
        assert(params.GetBufSize() % frame_size_bytes == 0);
    }
    else
    {
        ostringstream oss;
        oss << "Could not configure PCM device - device in state " << snd_pcm_state_name(hw_state) << " instead of SND_PCM_STATE_OPEN.";
        handle_error_code(static_cast<int>(errc::bad_file_descriptor), false, oss.str());
        return ENODEV;
    }
    return EXIT_SUCCESS;
}

int PCMDevice::xrun_recovery()
{
    if (err == -EPIPE)
    {
        err = snd_pcm_prepare(pcm_handle);

        if (err < 0)
        {
            handle_error_code(err, false, "Attempt to recover from underrun failed.");
            return 0;
        }
    }
    else if (err == -ESTRPIPE)
    {
        while ((err = snd_pcm_resume(pcm_handle)) == -EAGAIN)
        {
            // Try after 100ms until suspend is released
            this_thread::sleep_for(100ms);
        }
        if (err < 0)
        {
            err = snd_pcm_prepare(pcm_handle);

            if (err < 0)
            {
                handle_error_code(err, false, "Cannot recover from suspend during read.");
            }
        }
        return 0;
    }
    return err;
}

PCMPlayer::PCMPlayer(string hw_device) : PCMDevice(hw_device, SND_PCM_STREAM_PLAYBACK)
{
}

void PCMPlayer::flush()
{
    snd_pcm_drain(pcm_handle);
}

void PCMPlayer::play_interleaved(const char* buffer, size_t buf_size)
{
    assert(buffer);

    const snd_pcm_state_t hw_state = snd_pcm_state(pcm_handle);

    if (hw_state == SND_PCM_STATE_PREPARED || hw_state == SND_PCM_STATE_RUNNING)
    {
        unsigned long current_frames_written = 0;
        const auto frames_to_write = buf_size / frame_size_bytes;

        while (current_frames_written < frames_to_write)
        {
            err = (int) snd_pcm_writei(pcm_handle, (const void*)(buffer + (current_frames_written * frame_size_bytes)), frames_to_write - current_frames_written);

            if (err == -EAGAIN)
            {
                // Try again
                continue;
            }

            if (err < 0)
            {
                // Try to recover
                int xrun_err;

                if ((xrun_err = xrun_recovery()) < 0)
                {
                    handle_error_code(xrun_err, false, "Read error");
                    return;
                }
                else
                {
                    // Recovered - skip period
                    break;
                }
            }
            current_frames_written += err;
        }
    }
}

future<int> AlsaAudio::Play(IStream* stream, string device /*= "default"*/)
{
    assert(stream);

    stream->AddRef();
        
    return move(async(launch::async, [=]() -> int
        {
            // seek to begin
            stream->Seek({ 0 }, 0, NULL);

            WaveHeader wav_hdr;
            ULONG read = 0;
            ULONG fill = 0;

            if (SUCCEEDED(stream->Read(&wav_hdr, sizeof(wav_hdr), &read)) && read == sizeof(wav_hdr))
            {
                if (wav_hdr.myData.audioFormat != 1)
                {
                    stream->Release();
                    return EBADF;
                }
                try
                {
                    PCMPlayer player(device);
                    HwParams params;

                    params.InitFrom(wav_hdr);

                    int error = player.set_hardware_params(params);

                    if (error != EXIT_SUCCESS)
                    {
                        stream->Release();
                        return error;
                    }
                    const ULONG bufSize = static_cast<ULONG>(params.GetBufSize());
                    assert(bufSize);

                    vector<uint8_t> buffer;
                    buffer.resize(bufSize);

                    while (SUCCEEDED(stream->Read(buffer.data()+fill, bufSize-fill, &read)) && read > 0)
                    {
                        fill += read;

                        if (fill == bufSize)
                        {
                            player.play_interleaved((const char*)buffer.data(), bufSize);
                            fill = 0;
                        }
                    }
                    if (fill)
                    {
                        player.play_interleaved((const char*)buffer.data(), fill);
                    }
                    player.flush();
                }
                catch (...)
                {
                    stream->Release();
                    return EXIT_FAILURE;
                }
            }
            stream->Release();
            return 0;
        }));
}

future<int> AlsaAudio::Play(const void* buf, size_t bufsize, string device /*= "default"*/)
{
    return move(async(launch::async, [=]() -> int
        {
            if (buf && bufsize > sizeof(WaveHeader))
            {
                const WaveHeader* wav_hdr = (WaveHeader*)buf;

                if (wav_hdr->myData.audioFormat != 1)
                {
                    return EBADF;
                }

                try
                {
                    PCMPlayer player(device);
                    HwParams params;

                    params.InitFrom(*wav_hdr);

                    int error = player.set_hardware_params(params);

                    if (error != EXIT_SUCCESS)
                    {
                        return error;
                    }                    
                    size_t total = bufsize - sizeof(WaveHeader);
                    const char* buffer = (const char*)(wav_hdr + 1);

                    while (total > 0)
                    {
                        player.play_interleaved(buffer, total > params.GetBufSize() ? params.GetBufSize() : total);

                        if (total <= params.GetBufSize())
                            break;

                        total -= params.GetBufSize();
                        buffer += params.GetBufSize();
                    }
                    player.flush();
                }
                catch (...)
                {
                    return EXIT_FAILURE;
                }
            }
            return 0;
        }));
}

future<int> AlsaAudio::Play(string file_path, string device /*= "default"*/)
{
    return move(async(launch::async, [=]() -> int
        {
            WaveHeader wav_hdr;

            ifstream wav_file(file_path, ios::in | ifstream::binary);

            if (!wav_file.good())
            {
                return ENOENT;
            }

            wav_file.read((char*)&wav_hdr, wav_hdr.mySize());

            if (wav_hdr.myData.audioFormat != 1)
            {
                wav_file.close();
                return EBADF;
            }

            try
            {
                PCMPlayer player(device);
                HwParams params;

                params.InitFrom(wav_hdr);

                int error = player.set_hardware_params(params);

                if (error != EXIT_SUCCESS)
                {
                    return error;
                }

                vector<char> buffer;

                buffer.resize(params.GetBufSize());

                while (!wav_file.eof())
                {
                    memset(buffer.data(), 0, params.GetBufSize());
                    wav_file.read(buffer.data(), params.GetBufSize());

                    player.play_interleaved(buffer.data(), params.GetBufSize());
                }

                wav_file.close();
                player.flush();
            }
            catch (...)
            {
                return EXIT_FAILURE;
            }
            return 0;
        }));
}

constexpr long MINIMAL_VOLUME = 0;
constexpr long MAXIMUM_VOLUME = 65535;

Mixer::Mixer(string hw_device, string volume_element_name) :
    err(0),
    device_name(hw_device),
    simple_elem_name(volume_element_name)
{
    if ((err = snd_mixer_open(&mixer_handle, 0)) < 0)
        handle_error_code(err, true, "Cannot open handle to mixer device.");

    if ((err = snd_mixer_attach(mixer_handle, device_name.c_str())) < 0)
        handle_error_code(err, true, "Cannot attach mixer to device.");

    if ((err = snd_mixer_selem_register(mixer_handle, NULL, NULL)) < 0)
        handle_error_code(err, true, "Cannot register simple mixer object.");

    if ((err = snd_mixer_load(mixer_handle)) < 0)
        handle_error_code(err, true, "Cannot load sound mixer.");

    snd_mixer_selem_id_alloca(&simple_mixer_handle);
    snd_mixer_selem_id_set_index(simple_mixer_handle, 0);
    snd_mixer_selem_id_set_name(simple_mixer_handle, simple_elem_name.c_str());
    element_handle = snd_mixer_find_selem(mixer_handle, simple_mixer_handle);

    if (element_handle == NULL)
    {
        ostringstream oss;
        oss << "Could not find simple mixer element named " << simple_elem_name << ".";
        handle_error_code(static_cast<int>(errc::argument_out_of_domain), true, oss.str());
    }
    if ((err = snd_mixer_selem_set_playback_volume_range (element_handle, MINIMAL_VOLUME, MAXIMUM_VOLUME)) < 0)
        handle_error_code(err, true, "Cannot set element volume range.");
}

Mixer::~Mixer()
{
    snd_mixer_close(mixer_handle);
}

bool Mixer::device_exists(string hw_device)
{
    int err;
    snd_mixer_t* temp_handle;

    if ((err = snd_mixer_open(&temp_handle, 0)) < 0)
    {
        handle_error_code(err, false, "Cannot open handle to a mixer device.");
        return false;
    }

    if ((err = snd_mixer_attach(temp_handle, hw_device.c_str())) < 0)
    {
        snd_mixer_close(temp_handle);
        return false;
    }

    snd_mixer_close(temp_handle);
    return true;
}

bool Mixer::element_exists(string hw_device, string element_name)
{
    int err;
    snd_mixer_t* temp_handle;
    snd_mixer_selem_id_t* simple_temp_handle;

    if ((err = snd_mixer_open(&temp_handle, 0)) < 0)
    {
        handle_error_code(err, false, "Cannot open handle to a mixer device.");
        return false;
    }

    if ((err = snd_mixer_attach(temp_handle, hw_device.c_str())) < 0)
    {
        handle_error_code(err, true, "Cannot attach mixer to device.");
        snd_mixer_close(temp_handle);
        return false;
    }

    if ((err = snd_mixer_selem_register(temp_handle, NULL, NULL)) < 0)
    {
        handle_error_code(err, true, "Cannot register simple mixer object.");
        snd_mixer_close(temp_handle);
        return false;
    }

    if ((err = snd_mixer_load(temp_handle)) < 0)
    {
        handle_error_code(err, true, "Cannot load sound mixer.");
        snd_mixer_close(temp_handle);
        return false;
    }

    snd_mixer_selem_id_alloca(&simple_temp_handle);
    snd_mixer_selem_id_set_index(simple_temp_handle, 0);
    snd_mixer_selem_id_set_name(simple_temp_handle, element_name.c_str());
    snd_mixer_elem_t* elem_handle = snd_mixer_find_selem(temp_handle, simple_temp_handle);

    if (elem_handle == NULL)
    {
        snd_mixer_close(temp_handle);
        return false;
    }
  
    snd_mixer_close(temp_handle);
    return true;
}


double Mixer::dec_vol_pct(double pct, snd_mixer_selem_channel_id_t channel)
{
    trim_pct(pct);
    double cur_vol = get_cur_vol_pct(channel);
    return set_vol_pct(cur_vol - pct);
}

double Mixer::inc_vol_pct(double pct, snd_mixer_selem_channel_id_t channel)
{
    trim_pct(pct);
    double cur_vol = get_cur_vol_pct(channel);
    return set_vol_pct(cur_vol + pct);
}

double Mixer::set_vol_pct(double pct)
{
    long min, max;

    trim_pct(pct);
    get_vol_range(&min, &max);
    set_vol_raw(min + (long)(pct * (double)(max - min)));
    return get_cur_vol_pct();
}

double Mixer::get_cur_vol_pct(snd_mixer_selem_channel_id_t channel)
{
    long min, max, cur;
    get_vol_range(&min, &max);
    cur = get_cur_vol_raw(channel);
    return round((double)cur / (double)(max - min) * 100.0) / 100.0;
}

double Mixer::mute()
{
    mute_vol = get_cur_vol_raw();
    return set_vol_pct(0);
}

double Mixer::unmute()
{
    set_vol_raw(mute_vol);
    return get_cur_vol_pct();
}

void Mixer::trim_pct(double& pct)
{
    pct = (pct < 0) ? 0 : pct;
    pct = (pct > 1) ? 1 : pct;
}

void Mixer::set_vol_raw(long vol)
{
    err = snd_mixer_selem_set_playback_volume_all(element_handle, vol);

    if (err < 0)
        handle_error_code(err, false, "Cannot set volume to requested value.");
}

long Mixer::get_cur_vol_raw(snd_mixer_selem_channel_id_t channel)
{
    long cur_vol;

    err = snd_mixer_selem_get_playback_volume(element_handle, channel, &cur_vol);

    if (err < 0)
        handle_error_code(err, false, "Could not get volume for provided channel.");

    return cur_vol;
}

void Mixer::get_vol_range(long* min_vol, long* max_vol)
{
    err = snd_mixer_selem_get_playback_volume_range(element_handle, min_vol, max_vol);

    if (err < 0)
        handle_error_code(err, false, "Cannot get min/max volume range.");
}

map<string, string> AlsaAudio::ListDevices()
{
    map<string, string> result;

    char** hints = nullptr;

    // enumerate sound devices
    const int err = snd_device_name_hint(-1, "pcm", (void***)&hints);

    if (err == 0)
    {
        char** pp = hints;

        while (*pp)
        {
            char* name = snd_device_name_get_hint(*pp, "NAME");

            if (name)
            {
                char* desc = snd_device_name_get_hint(*pp, "DESC");

                if (0 != strcmp("null", name) && desc)
                {
                    try
                    {
                        string key{ name };
                        string value{ desc };
                        result[std::move(key)] = std::move(value);
                    }
                    catch (...)
                    {
                        free(name);
                        free(desc);
                        snd_device_name_free_hint((void**)hints);
                        throw;
                    }
                }
                free(name);

                if (desc)
                {
                    free(desc);
                }
            }
            ++pp;
        }
        // free hint buffer too
        snd_device_name_free_hint((void**)hints);
    }
    else
    {
        throw runtime_error("could not get sounde-device list");
    }
    return result;
}

#endif // _WIN32