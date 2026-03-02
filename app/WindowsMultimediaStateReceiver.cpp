#include "WindowsMultimediaStateReceiver.h"
#include "IMultimediaStateProvider.h"
#include <spdlog/spdlog.h>
#include <systemmediatransportcontrolsinterop.h>
#include <time.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>

#include "libutils.h"

using namespace std;
using namespace literals;

using winrt::Windows::Media::MediaPlaybackStatus;
using winrt::Windows::Media::SystemMediaTransportControls;
using winrt::Windows::Media::SystemMediaTransportControlsButton;
using winrt::Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs;
using winrt::Windows::Storage::Streams::DataWriter;
using winrt::Windows::Storage::Streams::InMemoryRandomAccessStream;
using winrt::Windows::Storage::Streams::RandomAccessStreamReference;

MultimediaStateReceiver::MultimediaStateReceiver(bool enabledByConfig)
    : m_enabledByConfig{ enabledByConfig }
{
}

MultimediaStateReceiver::~MultimediaStateReceiver()
{
    Stop();
}

void MultimediaStateReceiver::Configure(bool enable) noexcept
{
    if (m_enabledByConfig != enable)
    {
        m_enabledByConfig = enable;

        if (enable)
        {
            Start();
        }
        else
        {
            Stop();
        }
    }
}

void MultimediaStateReceiver::Initialize(IMultimediaStateProvider* provider, QObject*, NativeWindowHandle nativeWindowHandle) noexcept
{
    assert(provider);
    swap(m_provider, provider);
    swap(m_windowHandle, nativeWindowHandle);

    if (m_enabledByConfig)
    {
        Start();
    }
}

void MultimediaStateReceiver::Start() noexcept
{
    try
    {
        unique_lock<mutex> guard{ m_mtx };
        
        if (!m_thread.joinable())
        {
            shared_ptr<promise<HRESULT>> promiseInitializedOk = make_shared<promise<HRESULT>>();
            auto initializedOk = promiseInitializedOk->get_future();
            m_stop = false;

            auto t = thread([this, promiseInitializedOk = move(promiseInitializedOk)]() noexcept {
                bool apartmentInitOk = false;
                bool startResultSignaled = false;

                try
                {
                    winrt::init_apartment(winrt::apartment_type::single_threaded);
                    apartmentInitOk = true;
                }
                catch (const exception& e)
                {
                    spdlog::warn("failed to initialize apartment MultimediaStateReceiver: {}", e.what());
                }
                const ScopeContext cleanupApartment([&apartmentInitOk]{
                    if (apartmentInitOk)
                    {
                        winrt::uninit_apartment();
                    }
                });

                SystemMediaTransportControls smtc{nullptr};
                HRESULT hr = E_FAIL;

                const ScopeContext disableSmtc([&smtc]{
                    if (smtc)
                    {
                        smtc.IsEnabled(false);
                    }
                });

                try
                {
                    {
                        auto activationFactory = winrt::get_activation_factory<
                            SystemMediaTransportControls,
                            ISystemMediaTransportControlsInterop>();

                        hr = activationFactory->GetForWindow(
                            reinterpret_cast<HWND>(m_windowHandle), winrt::guid_of<SystemMediaTransportControls>(),
                            winrt::put_abi(smtc));

                        if (FAILED(hr))
                        {
                           throw runtime_error("failed to activate SystemMediaTransportControls");
                        }
                        promiseInitializedOk->set_value(hr);
                        startResultSignaled = true;
                    }
                    atomic_int64_t lastPlayPauseButtonPress = 0;

                    smtc.IsStopEnabled(false);

                    smtc.ButtonPressed([this, &lastPlayPauseButtonPress, &smtc](const SystemMediaTransportControls&,
                        const SystemMediaTransportControlsButtonPressedEventArgs& args)
                        {
                            using Button = SystemMediaTransportControlsButton;

                            const auto button = args.Button();

                            switch (button)
                            {
                                case Button::Pause:
                                case Button::Play:
                                {
                                    lastPlayPauseButtonPress = ::time(nullptr);
                                    m_provider->PlayPause();
                                    smtc.IsPlayEnabled(button == Button::Pause);
                                    smtc.IsPauseEnabled(button == Button::Play);
                                    smtc.PlaybackStatus(button == Button::Play ? MediaPlaybackStatus::Playing : MediaPlaybackStatus::Paused);
                                }
                                break;

                                case Button::Next:
                                {
                                    m_provider->SkipNext();
                                }
                                break;
                                
                                case Button::Previous:
                                {
                                    m_provider->SkipPrevious();
                                }
                                break;
                            
                                default:
                                break;
                            }
                        });
    
                    unique_lock<mutex> guard{ m_mtx };

                    bool prevIsEnabled = m_isEnabled;
                    bool prevIsPlaying = m_isPlaying;
                    wstring prevTrack;
                    wstring prevArtist;
                    vector<unsigned char> prevArt;

                    smtc.IsPlayEnabled(m_isEnabled);
                    smtc.IsPauseEnabled(m_isEnabled);
                    smtc.IsPreviousEnabled(m_isEnabled);
                    smtc.IsNextEnabled(m_isEnabled);
                    smtc.PlaybackStatus(m_isPlaying ? MediaPlaybackStatus::Playing : MediaPlaybackStatus::Paused);

                    smtc.IsEnabled(m_isEnabled);

                    while (!m_stop)
                    {
                        const auto pred = [this]() noexcept { return m_stop || m_work; };

                        if (lastPlayPauseButtonPress == 0)
                        {
                            m_cv.wait(guard, pred);
                        }
                        else
                        {
                            m_cv.wait_for(guard, 1s, pred);
                        }
                        if (m_stop)
                        {
                            // stop request -> exit
                            return;
                        }
                        if (m_work)
                        {
                            // acknowledge work flag
                            m_work = false;
                        }

                        // capture state and unlock
                        const bool isEnabled = m_isEnabled;
                        const bool isPlaying = m_isPlaying;
                        const bool hasNewTrackInfo = m_hasNewTrackInfo;

                        if (hasNewTrackInfo)
                        {
                            // acknowledge track info flag
                            m_hasNewTrackInfo = false;
                        }
                        guard.unlock();

                        // handle state of multimedia controls
                        if (lastPlayPauseButtonPress == 0 || (::time(nullptr)-lastPlayPauseButtonPress) >= 3)
                        {
                            lastPlayPauseButtonPress = 0;

                            if (prevIsEnabled != isEnabled || prevIsPlaying != isPlaying)
                            {
                                spdlog::debug("SystemMediaTransportControls changes enabled to {} and playing to {}", isEnabled, isPlaying);

                                smtc.IsPlayEnabled(isEnabled && !isPlaying);
                                smtc.IsPauseEnabled(isEnabled && isPlaying);
                                smtc.IsPreviousEnabled(isEnabled);
                                smtc.IsNextEnabled(isEnabled);
                                smtc.PlaybackStatus(isPlaying ? MediaPlaybackStatus::Playing : MediaPlaybackStatus::Paused);

                                smtc.IsEnabled(isEnabled);

                                prevIsEnabled = isEnabled;
                                prevIsPlaying = isPlaying;
                            }
                        }
                        
                        // handle track info
                        if (hasNewTrackInfo)
                        {
                            try
                            {
                                wstring track;
                                wstring album;
                                wstring artist;
                                vector<unsigned char> art;

                                if (m_provider->GetTrackInfo(track, album, artist, art))
                                {
                                    auto updater = smtc.DisplayUpdater();
                                    updater.Type(winrt::Windows::Media::MediaPlaybackType::Music);

                                    auto musicProperties = updater.MusicProperties();
                                    bool commitUpdate = false;

                                    if (track != prevTrack)
                                    {
                                        musicProperties.Title(winrt::hstring(track));
                                        swap(track, prevTrack);
                                        commitUpdate = true;
                                    }

                                    if (artist != prevArtist)
                                    {
                                        musicProperties.Artist(winrt::hstring(artist));
                                        swap(artist, prevArtist);
                                        commitUpdate = true;
                                    }

                                    if (art != prevArt)
                                    {
                                        try
                                        {
                                            InMemoryRandomAccessStream thumbnailStream;
                                            DataWriter writer(thumbnailStream);
                                            
                                            auto store = async(launch::async, [&writer, &art]() -> bool {
                                                writer.WriteBytes(winrt::array_view<const uint8_t>(art.data(), art.data() + art.size()));
                                                writer.StoreAsync().get();
                                                return writer.FlushAsync().get();
                                            });
                                            const bool stored = store.get();

                                            writer.DetachStream();
                                            writer.DetachBuffer();
                                            thumbnailStream.Seek(0);
                                            
                                            if (stored)
                                            {
                                                const auto thumbnail = RandomAccessStreamReference::CreateFromStream(thumbnailStream);
                                                updater.Thumbnail(thumbnail);
                                                swap(art, prevArt);
                                                commitUpdate = true;
                                            }
                                            else
                                            {
                                                spdlog::error("could not store smtc thumbnail to stream");
                                            }
                                        }
                                        catch(const exception& e)
                                        {
                                            spdlog::error("failed to set smtc thumbnail: {}", e.what());                                       
                                        }
                                    }

                                    if (commitUpdate)
                                    {
                                        updater.Update();
                                    }
                                }
                            }
                            catch(const exception& e)
                            {
                                spdlog::error("failed to set smtc track info: {}", e.what());                                       
                            }
                        }

                        // lock before we enter the loop again
                        guard.lock();
                   }
                }
                catch (const exception& e)
                {
                    if (!startResultSignaled)
                    {
                        promiseInitializedOk->set_value(hr);
                    }
                }
            });

            const HRESULT hr = initializedOk.get();

            if (FAILED(hr))
            {
                m_stop = true;
                m_cv.notify_all();
                guard.unlock();
                t.join();
                char buf[256];
                sprintf_s(buf, 256, "error 0x%lx", hr);
                throw runtime_error(buf);
            }
            else
            {
                swap(m_thread, t);
                spdlog::info("succeeded to start MultimediaStateReceiver's SystemMediaTransportControls");
            }
        }
    }
    catch (const exception& e)
    {
         spdlog::error("failed to start MultimediaStateReceiver's SystemMediaTransportControls: {}", e.what());
    }
}

void MultimediaStateReceiver::Stop() noexcept
{
    thread t;
    {
        const lock_guard<mutex> guard{ m_mtx };

        if (m_thread.joinable())
        {
            swap(m_thread, t);
            m_stop = true;
            m_cv.notify_all();
        }
    }
    if (t.joinable())
    {
        t.join();
        spdlog::info("succeeded to cleanup MultimediaStateReceiver's SystemMediaTransportControls");
    }
}

void MultimediaStateReceiver::Cleanup() noexcept
{
    Stop();
    m_provider = nullptr;
    m_windowHandle = NativeWindowHandle{};
}

void MultimediaStateReceiver::OnPlayState(bool isPlaying) noexcept
{
    if (!m_stop)
    {
        const lock_guard<mutex> guard{ m_mtx };

        if (m_thread.joinable())
        {
            if (isPlaying != m_isPlaying)
            {
                m_isPlaying = isPlaying;
                m_work = true;
                m_cv.notify_all();
            }
        }
    }
}

void MultimediaStateReceiver::OnUpdateMMState(bool isEnabled) noexcept
{
    if (!m_stop)
    {
        const lock_guard<mutex> guard{ m_mtx };

        if (m_thread.joinable())
        {
            if (isEnabled != m_isEnabled)
            {
                m_isEnabled = isEnabled;
                m_work = true;
                m_cv.notify_all();
            }
        }
    }
}


void MultimediaStateReceiver::OnUpdateTrackInfo() noexcept
{
    if (!m_stop)
    {
        const lock_guard<mutex> guard{ m_mtx };

        // indicate new track info
        m_hasNewTrackInfo = true;
        m_work = true;
        m_cv.notify_all();
    }
}

void MultimediaStateReceiver::OnUpdateVolume(double) noexcept
{
    // not implemented
}