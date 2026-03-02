#include "LinuxMultimediaStateReceiver.h"
#include "IMultimediaStateProvider.h"
#include <spdlog/spdlog.h>
#include <time.h>
#include <unistd.h>

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QMetaClassInfo>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QModelIndex>
#include <QUrl>

#include "libutils.h"

using namespace std;
using namespace literals;

// detail namespace
namespace Mpris2
{

class DBusAbstractAdaptor
    : public QDBusAbstractAdaptor
{
    Q_OBJECT

public:
    DBusAbstractAdaptor(QObject *parent);

    void SetDBusPath(const QString &path);

protected:
    void SignalPropertyChange(const QString &property, const QVariant &value);
    void SignalPropertyChange(const QString &property);

private Q_SLOTS:
    void EmitPropertiesChanged();

private:
    recursive_mutex m_mtx;
    QStringList     m_invalidatedProperties;
    QVariantMap     m_updatedProperties;
    QString         m_path;
    QDBusConnection m_connection;
};

DBusAbstractAdaptor::DBusAbstractAdaptor(QObject *parent)
    : QDBusAbstractAdaptor{ parent }
    , m_connection{ QDBusConnection::sessionBus() }
{
}

void DBusAbstractAdaptor::SetDBusPath(const QString &path)
{
    m_path = path;
}

void DBusAbstractAdaptor::SignalPropertyChange(const QString &property, const QVariant &value)
{
    const lock_guard<recursive_mutex> guard{ m_mtx };

    if (m_updatedProperties.isEmpty() && m_invalidatedProperties.isEmpty())
    {
        QMetaObject::invokeMethod(this, "EmitPropertiesChanged", Qt::QueuedConnection);
    }

    m_updatedProperties[property] = value;
}

void DBusAbstractAdaptor::SignalPropertyChange(const QString &property)
{
    const lock_guard<recursive_mutex> guard{ m_mtx };
    
    if (!m_invalidatedProperties.contains(property))
    {
        if (m_updatedProperties.isEmpty() && m_invalidatedProperties.isEmpty())
        {
            QMetaObject::invokeMethod(this, "EmitPropertiesChanged", Qt::QueuedConnection);
        }

        m_invalidatedProperties << property;
    }
}

void DBusAbstractAdaptor::EmitPropertiesChanged()
{
    assert(!m_path.isEmpty());

    const lock_guard<recursive_mutex> guard{ m_mtx };

    if (m_updatedProperties.isEmpty() && m_invalidatedProperties.isEmpty())
    {
        // nothing to do
        return;
    }

    int ifaceIndex = metaObject()->indexOfClassInfo("D-Bus Interface");
    
    if (ifaceIndex < 0)
    {
        spdlog::warn("No D-Bus interface given (probably missing Q_CLASSINFO)");
    } 
    else
    {
        QDBusMessage signal = QDBusMessage::createSignal(m_path,
             QStringLiteral("org.freedesktop.DBus.Properties"),
             QStringLiteral("PropertiesChanged"));
        signal << QLatin1String(metaObject()->classInfo(ifaceIndex).value());
        signal << m_updatedProperties;
        signal << m_invalidatedProperties;
        
        m_connection.send(signal);
    }

    m_updatedProperties.clear();
    m_invalidatedProperties.clear();
}

class MediaPlayer2
    : public DBusAbstractAdaptor
{
    Q_OBJECT

    // interface documentation
    // see https://specifications.freedesktop.org/mpris/latest/Media_Player.html
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

    // properties definition
    Q_PROPERTY(bool CanRaise READ CanRaise)
    Q_PROPERTY(bool CanQuit READ CanQuit)
    Q_PROPERTY(bool CanSetFullscreen READ CanSetFullscreen)
    Q_PROPERTY(bool Fullscreen READ Fullscreen)

    Q_PROPERTY(bool HasTrackList READ HasTrackList)

    Q_PROPERTY(QString Identity READ Identity)
    Q_PROPERTY(QString DesktopEntry READ DesktopEntry)

    Q_PROPERTY(QStringList SupportedUriSchemes READ SupportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ SupportedMimeTypes)

public:
    MediaPlayer2(IMultimediaStateProvider* provider, QObject* parent = nullptr);
    ~MediaPlayer2();

    // properties implementation
    bool CanRaise() const;
    bool CanQuit() const;
    bool CanSetFullscreen() const;
    bool Fullscreen() const;

    bool HasTrackList() const;

    QString Identity() const;
    QString DesktopEntry() const;

    QStringList SupportedUriSchemes() const;
    QStringList SupportedMimeTypes() const;

public Q_SLOTS:
    // invokable methods implementation
    void Raise() const;
    void Quit() const;

private:
    IMultimediaStateProvider* const m_provider;
};

MediaPlayer2::MediaPlayer2(IMultimediaStateProvider* provider, QObject* parent /*= nullptr*/)
    : DBusAbstractAdaptor{ parent }
    , m_provider{ provider }
{
    assert(m_provider);
}

MediaPlayer2::~MediaPlayer2()
{
}

bool MediaPlayer2::CanRaise() const
{
    return true;
}

void MediaPlayer2::Raise() const
{
    m_provider->ShowWindow();
}

bool MediaPlayer2::CanQuit() const
{
    return true;
}

void MediaPlayer2::Quit() const
{
    m_provider->QuitApp();
}

bool MediaPlayer2::CanSetFullscreen() const
{
    return false;
}

bool MediaPlayer2::Fullscreen() const
{
    return false;
}

bool MediaPlayer2::HasTrackList() const
{
    return false;
}

QString MediaPlayer2::Identity() const
{
    QString ident;

    try
    {
        string name;
        string subName;

        if (m_provider->GetServiceName(name, subName))
        {
            ident = name.c_str();
        }
    }
    catch(...)
    {
    }
    
    return ident;
}

QString MediaPlayer2::DesktopEntry() const
{
    string entry;
    m_provider->GetDesktopEntry(entry);
    return QString(entry.c_str());
}

QStringList MediaPlayer2::SupportedUriSchemes() const
{
    return QStringList() << QStringLiteral("file");
}

QStringList MediaPlayer2::SupportedMimeTypes() const
{
    return QStringList{};
}

class MediaPlayer2Player 
    : public DBusAbstractAdaptor
{
    Q_OBJECT

    // see https://specifications.freedesktop.org/mpris/latest/Player_Interface.html
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ PlaybackStatus)
    Q_PROPERTY(double Rate READ GetRate WRITE SetRate)
    Q_PROPERTY(QVariantMap Metadata READ GetMetadata)
    Q_PROPERTY(double Volume READ GetVolume WRITE SetVolume)
    Q_PROPERTY(qlonglong Position READ GetPosition)
    Q_PROPERTY(double MinimumRate READ GetMinimumRate)
    Q_PROPERTY(double MaximumRate READ GetMaximumRate)
    Q_PROPERTY(bool CanGoNext READ CanGoNext)
    Q_PROPERTY(bool CanGoPrevious READ CanGoPrevious)
    Q_PROPERTY(bool CanPlay READ CanPlay)
    Q_PROPERTY(bool CanPause READ CanPause)
    Q_PROPERTY(bool CanSeek READ CanSeek)
    Q_PROPERTY(bool CanControl READ CanControl)

public:
    friend class ::MultimediaStateReceiver;

    MediaPlayer2Player(IMultimediaStateProvider* provider, QObject* parent);
    ~MediaPlayer2Player();

    // properties implementation
    QString PlaybackStatus() const;
    double GetRate() const;
    void SetRate(double rate);
    QVariantMap GetMetadata() const;
    double GetVolume() const;
    void SetVolume(double volume);
    qlonglong GetPosition() const;
    double GetMinimumRate() const;
    double GetMaximumRate() const;
    bool CanGoNext() const;
    bool CanGoPrevious() const;
    bool CanPlay() const;
    bool CanPause() const;
    bool CanSeek() const;
    bool CanControl() const;

Q_SIGNALS:
    void Seeked(qlonglong Position) const;

public Q_SLOTS:
    // invokable methods implementation
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong Offset);
    void SetPosition(const QDBusObjectPath TrackId, qlonglong Position);
    void OpenUri(const QString Uri);

protected:
    void OnPlayState(bool isPlaying) noexcept;
    void OnUpdateMMState(bool isEnabled) noexcept;
    void OnUpdateTrackInfo(wstring&& track, wstring&& album, wstring&& artist, vector<unsigned char>&& art) noexcept;
    void OnUpdateVolume(double v) noexcept;

    void ButtonPressTimex(atomic_int64_t* timex) noexcept
    {
        m_timex = timex;
    }

private:
    QVariantMap MetadataForCurrentTrack() const;
    void DeleteCurrentAlbumArt() noexcept;

    void SetTimex() const noexcept
    {
        if (m_timex)
        {
            m_timex->store(::time(NULL));
        }
    }

    bool HasAlbumArtFile() const noexcept
    {
        return 0 != m_albumArtFile[0];
    }

private:
    IMultimediaStateProvider* const m_provider;
    atomic_bool                     m_isPlaying;
    atomic_bool                     m_isEnabled;
    atomic_int64_t*                 m_timex{ nullptr };
    mutable recursive_mutex         m_mtx;
    double                          m_currentVolume;
    wstring                         m_currentTrack;
    wstring                         m_currentAlbum;
    wstring                         m_currentArtist;
    vector<unsigned char>           m_currentAlbumArt;
    char                            m_albumArtFile[261];
};

MediaPlayer2Player::MediaPlayer2Player(IMultimediaStateProvider* provider, QObject* parent)
    : DBusAbstractAdaptor{ parent }
    , m_provider{ provider }
{
    assert(m_provider);
    memset(m_albumArtFile, 0, sizeof(m_albumArtFile));
    m_currentVolume = m_provider->GetVolume();
}

MediaPlayer2Player::~MediaPlayer2Player()
{
    DeleteCurrentAlbumArt();
}

void MediaPlayer2Player::DeleteCurrentAlbumArt() noexcept
{
    m_currentAlbumArt.clear();

    if (HasAlbumArtFile())
    {
        DeleteFileA(m_albumArtFile);
        memset(m_albumArtFile, 0, sizeof(m_albumArtFile));
    }
}

void MediaPlayer2Player::OnPlayState(bool isPlaying) noexcept
{
    try
    {
        if (m_isPlaying != isPlaying)
        {
            m_isPlaying = isPlaying;
            SignalPropertyChange(QStringLiteral("CanPause"), CanPause());
            SignalPropertyChange(QStringLiteral("CanPlay"), CanPlay());
            SignalPropertyChange(QStringLiteral("PlaybackStatus"), PlaybackStatus());
        }
    }
    catch(...)
    {
        spdlog::error("failed to signal play state changes");
    }
}

void MediaPlayer2Player::OnUpdateMMState(bool isEnabled) noexcept
{
    try
    {
        if (m_isEnabled != isEnabled)
        {
            m_isEnabled = isEnabled;
            SignalPropertyChange(QStringLiteral("CanPause"), CanPause());
            SignalPropertyChange(QStringLiteral("CanPlay"), CanPlay());
            SignalPropertyChange(QStringLiteral("CanGoNext"), CanGoNext());
            SignalPropertyChange(QStringLiteral("CanGoPrevious"), CanGoPrevious());
            SignalPropertyChange(QStringLiteral("PlaybackStatus"), PlaybackStatus());
        }
    }
    catch(...)
    {
        spdlog::error("failed to signal MM state changes");
    }        
}

void MediaPlayer2Player::OnUpdateVolume(double v) noexcept
{
    const lock_guard<recursive_mutex> guard{ m_mtx };

    if (m_currentVolume != v)
    {
        m_currentVolume = v;
        SignalPropertyChange(QStringLiteral("Volume"), v);
    }
}

void MediaPlayer2Player::OnUpdateTrackInfo(wstring&& track, wstring&& album, wstring&& artist, vector<unsigned char>&& art) noexcept
{
    const lock_guard<recursive_mutex> guard{ m_mtx };

    bool commit = false;

    if (m_currentTrack != track)
    {
        m_currentTrack = move(track);
        commit = true;
    }

    if (commit || m_currentAlbum != album)
    {
        m_currentAlbum = move(album);
        commit = true;
    }

    if (commit || m_currentArtist != artist)
    {
        m_currentArtist = move(artist);
        commit = true;
    }

    if (m_currentAlbumArt != art)
    {
        // delete old album art
        DeleteCurrentAlbumArt();

        m_currentAlbumArt = move(art);
        commit = true;

        if (!m_currentAlbumArt.empty())
        {
            // create new album art inside a temporary file
            GetTempPathA(sizeof(m_albumArtFile), m_albumArtFile);
            strcat(m_albumArtFile, "ShairportQt_TempAlbumArt_XXXXXX");
            int fd = mkstemp(m_albumArtFile);

            if (fd < 0 || write(fd, m_currentAlbumArt.data(), m_currentAlbumArt.size()) != m_currentAlbumArt.size())
            {
                spdlog::error("failed to write current album art temporarily to disk: {}. Last error: {}", m_albumArtFile, GetLastError());

                // something went wrong -> rollback
                if (fd >= 0)
                {
                    close(fd);
                }
                DeleteCurrentAlbumArt();
            }
            else
            {
                // album art ok
                close(fd);
            }
        }
    }

    if (commit)
    {
        try
        {
            SignalPropertyChange(QStringLiteral("Metadata"), MetadataForCurrentTrack());
        }
        catch(const exception& e)
        {
            spdlog::error("failed to update current Metadata");
        }
    }
}

bool MediaPlayer2Player::CanGoNext() const
{
    return m_isEnabled;
}

void MediaPlayer2Player::Next()
{
    SetTimex();
    m_provider->SkipNext();
}

bool MediaPlayer2Player::CanGoPrevious() const
{
    return m_isEnabled;
}

void MediaPlayer2Player::Previous()
{
    SetTimex();
    m_provider->SkipPrevious();
}

bool MediaPlayer2Player::CanPause() const
{
    return m_isEnabled && m_isPlaying;
}

void MediaPlayer2Player::Pause()
{
    if (m_isPlaying)
    {
        SetTimex();
        m_provider->PlayPause();
        OnPlayState(false);
    }
}

void MediaPlayer2Player::PlayPause()
{
    SetTimex();
    m_provider->PlayPause();
    OnPlayState(!m_isPlaying);
}

void MediaPlayer2Player::Stop()
{
    if (m_isPlaying)
    {
        SetTimex();
        m_provider->PlayPause();
        OnPlayState(false);
    }
}

bool MediaPlayer2Player::CanPlay() const
{
    return m_isEnabled && !m_isPlaying;
}

void MediaPlayer2Player::Play()
{
    if (!m_isPlaying)
    {
        SetTimex();
        m_provider->PlayPause();
        OnPlayState(true);
    }
}

void MediaPlayer2Player::SetPosition(const QDBusObjectPath, qlonglong)
{
}

void MediaPlayer2Player::OpenUri(const QString)
{
}

QString MediaPlayer2Player::PlaybackStatus() const
{
    if (m_isPlaying)
    {
        return QStringLiteral("Playing");
    }
    else if (m_isEnabled)
    {
        return QStringLiteral("Paused");
    }
    return QStringLiteral("Stopped");
}

double MediaPlayer2Player::GetRate() const
{
    return 1.0;
}

void MediaPlayer2Player::SetRate(double)
{
}

QVariantMap MediaPlayer2Player::MetadataForCurrentTrack() const
{
    QVariantMap metaData;

    // https://www.freedesktop.org/wiki/Specifications/mpris-spec/metadata/
    {
        const lock_guard<recursive_mutex> guard{ m_mtx };

        metaData[QStringLiteral("mpris:trackid")] = QVariant::fromValue<QDBusObjectPath>(QDBusObjectPath("/current"));

        if (!m_currentTrack.empty())
        {
            metaData[QStringLiteral("xesam:title")] = QString::fromStdWString(m_currentTrack);
        }
        if (!m_currentAlbum.empty())
        {
            metaData[QStringLiteral("xesam:album")] = QString::fromStdWString(m_currentAlbum);
        }
        if (!m_currentArtist.empty())
        {
            metaData[QStringLiteral("xesam:artist")] = QStringList() << QString::fromStdWString(m_currentArtist);
        }
        if (HasAlbumArtFile())
        {
            metaData[QStringLiteral("mpris:artUrl")] = QString::fromLatin1(QUrl::fromLocalFile(m_albumArtFile).toEncoded());
        }
    }
    return metaData;
}

QVariantMap MediaPlayer2Player::GetMetadata() const
{
    return MetadataForCurrentTrack();
}

double MediaPlayer2Player::GetVolume() const
{
    const lock_guard<recursive_mutex> guard{ m_mtx };
    return m_currentVolume;
}

void MediaPlayer2Player::SetVolume(double v)
{
    {
        const lock_guard<recursive_mutex> guard{ m_mtx };
        m_currentVolume = v;
    }
    m_provider->SetVolume(v);
}

qlonglong MediaPlayer2Player::GetPosition() const
{
    return 0;
}

double MediaPlayer2Player::GetMinimumRate() const
{
    return 1.0;
}

double MediaPlayer2Player::GetMaximumRate() const
{
    return 1.0;
}

bool MediaPlayer2Player::CanSeek() const
{
    return false;
}

void MediaPlayer2Player::Seek(qlonglong)
{
}

bool MediaPlayer2Player::CanControl() const
{
    return true;
}

class MprisHost
    : public QObject
{
    Q_OBJECT

public:
    MprisHost(IMultimediaStateProvider* provider, QObject * parent)
        : QObject{ parent }
    {
        // we host all adaptors as children
        m_media = new MediaPlayer2(provider, this);
        m_media->SetDBusPath(QStringLiteral("/org/mpris/MediaPlayer2"));
        m_player = new MediaPlayer2Player(provider, this);
        m_player->SetDBusPath(QStringLiteral("/org/mpris/MediaPlayer2"));

        // register this root node
        if (QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this, QDBusConnection::ExportAdaptors))
        {
            spdlog::info("succeeded to register Mpris object");
        }
        else
        {
            throw runtime_error("failed to register Mpris object");
        }
    }

    ~MprisHost()
    {
    }

public:
    MediaPlayer2*                   m_media{ nullptr };
    MediaPlayer2Player*             m_player{ nullptr };
};

} // namespace Mpris2

/************************************************
    MultimediaStateReceiver class
*************************************************/

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

void MultimediaStateReceiver::Initialize(IMultimediaStateProvider* provider, QObject* parent, NativeWindowHandle) noexcept
{
    assert(provider);
    swap(m_provider, provider);
    swap(m_parent, parent);

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
            shared_ptr<promise<bool>> promiseInitializedOk = make_shared<promise<bool>>();
            auto initializedOk = promiseInitializedOk->get_future();
            m_stop = false;

            if (!m_mprisHost)
            {
                m_mprisHost = new Mpris2::MprisHost(m_provider, m_parent);
            }

            auto t = thread([this, promiseInitializedOk = move(promiseInitializedOk)]() noexcept {
                string serviceName;

                const ScopeContext disableService([&serviceName]{
                    if (!serviceName.empty())
                    {
                        const bool success = QDBusConnection::sessionBus().unregisterService(serviceName.c_str());

                        if (success)
                        {
                            spdlog::info("succeeded to unregister Mpris service {}", serviceName);
                        }
                        else
                        {
                            spdlog::error("failed to unregister Mpris service {}", serviceName);
                        }
                        serviceName.clear();
                    }
                });

                try
                {
                    string name;
                    string subName;

                    if (!m_provider->GetServiceName(name, subName) || name.empty())
                    {
                        spdlog::error("failed to register Mpris service because service name could not be determined");
                        promiseInitializedOk->set_value(false);
                        return;
                    }
                    serviceName = "org.mpris.MediaPlayer2."s + name;

                    bool success = QDBusConnection::sessionBus().registerService(serviceName.c_str());

                    // If the above failed, it's likely because we're not the first instance
                    // and the name is already taken.
                    if (!success)
                    {
                        serviceName += ("@"s + subName);
                        success = QDBusConnection::sessionBus().registerService(serviceName.c_str());
                    }

                    if (success)
                    {
                        spdlog::info("succeeded to register Mpris service {}", serviceName);
                        promiseInitializedOk->set_value(true);
                    }
                    else
                    {
                        serviceName.clear();
                        throw runtime_error("failed to register Mpris service");
                    }
                }
                catch(const exception& e)
                {
                    spdlog::error("failed to start MultimediaStateReceiver's Mpris: {}", e.what());
                    promiseInitializedOk->set_value(false);
                    return;
                }

                try
                {
                    Mpris2::MprisHost* mprisHost = static_cast<Mpris2::MprisHost*>(m_mprisHost);
                    assert(mprisHost);

                    atomic_int64_t lastButtonPressTimex = 0;

                    const ScopeContext cleanupTimex([&mprisHost]()
                    {
                        mprisHost->m_player->ButtonPressTimex(nullptr);

                    });

                    unique_lock<mutex> guard{ m_mtx };

                    mprisHost->m_player->ButtonPressTimex(&lastButtonPressTimex);
                    
                    while (!m_stop)
                    {
                        const auto pred = [this]() noexcept { return m_stop || m_work; };

                        if (lastButtonPressTimex == 0)
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
                        const double volume = m_volume;

                        if (hasNewTrackInfo)
                        {
                            // acknowledge track info flag
                            m_hasNewTrackInfo = false;
                        }
                        guard.unlock();

                        // handle state of multimedia controls
                        if (lastButtonPressTimex == 0 || (::time(nullptr)-lastButtonPressTimex) >= 3)
                        {
                            lastButtonPressTimex = 0;
                            mprisHost->m_player->OnPlayState(isPlaying);
                            mprisHost->m_player->OnUpdateMMState(isEnabled);
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
                                    mprisHost->m_player->OnUpdateTrackInfo(move(track), move(album), move(artist), move(art));
                                }
                            }
                            catch(const exception& e)
                            {
                                spdlog::error("failed to set mpris track info: {}", e.what());                                       
                            }
                        }

                        // handle volume
                        mprisHost->m_player->OnUpdateVolume(volume);

                        // lock before we enter the loop again
                        guard.lock();
                   }
                }
                catch (const exception& e)
                {
                    spdlog::error("failed to loop MultimediaStateReceiver: {}", e.what());
                }
            });

            const bool success = initializedOk.get();

            if (!success)
            {
                m_stop = true;
                m_cv.notify_all();
                guard.unlock();
                t.join();
                throw runtime_error("failed to start MultimediaStateReceiver thread");
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
    m_parent = nullptr;
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

void MultimediaStateReceiver::OnUpdateVolume(double v) noexcept
{
    if (!m_stop)
    {
        const lock_guard<mutex> guard{ m_mtx };

        // indicate new track info
        m_volume = v;
        m_work = true;
        m_cv.notify_all();
    }        
}

#include "LinuxMultimediaStateReceiver.moc"