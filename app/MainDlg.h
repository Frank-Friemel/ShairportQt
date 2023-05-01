#pragma once

#include <QLabel>
#include <QMenuBar>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QPixmap>
#include <QString>
#include <QByteArray>
#include <QFormLayout>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QAction>

#include <future>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <list>
#include <chrono>
#include <stdint.h>
#include <LayerCake.h>
#include "RaopServer.h"
#include "Condition.h"
#include "dnssd.h"
#include "DacpService.h"
#include "KeyboardHook.h"

class TimeLabel;

//
// based on Qt example: https://doc.qt.io/qt-6/qtwidgets-layouts-basiclayouts-example.html
//
class MainDlg 
    : public QWidget
    , public IRaopEvents
    , public IDnsSDEvents
    , protected KeyboardHook::ICallback
{
    Q_OBJECT

public:
    MainDlg(const SharedPtr<IValueCollection>& config);
    ~MainDlg();

private:
    void RunScheduler() noexcept;

    void CreateMenuBar();
    void WidgetCreateStatusGroup();
    void WidgetCreateAirportGroup();
    void WidgetCreateTitleInfoGroup();
    void WidgetCreateMultimediaControlGroup();
    
    void ConfigureDacpBrowser();
    void ConfigureSystemTray();
    void SendDacpCommand(const std::string& cmd);

    QString GetString(int id) const;

protected:
    // implemenation of IRaopEvents
    void OnCreateRaopService(bool success) noexcept override;
    void OnSetCurrentDacpID(DacpID&& dacpID) noexcept override;
    void OnSetCurrentDmapInfo(DmapInfo&& dmapInfo) noexcept override;
    void OnSetCurrentImage(const char* data, size_t dataLen, std::string&& imageType) noexcept override;

protected:
    // implemenation of IDnsSDEvents
    void OnDNSServiceBrowseReply(
        bool registered,
        uint32_t interfaceIndex,
        const char* serviceName,
        const char* regtype,
        const char* replyDomain) noexcept override;

protected:
    // QWidget overrides
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

    // Keyboard-Hook implementation
    void OnKeyPressed(KeyboardHook::Key key) noexcept override;

signals:
    void ShowMessage(int text) const;
    void UpdateMMState() const;
    void UpdateWidgets() const;
    void SetProgressInfo(int currentSeconds, int totalSeconds, QString connectedClient);
    void ShowStatus(QString status);
    void SetPlayState(bool isPlaying);
    void ShowDmapInfo(QString album, QString track, QString artist);
    void ShowAlbumArt();
    void ShowAdArt();
    void ShowToastMessage();

private slots:
    void OnQuit();
    void OnShowMessage(int text);
    void OnUpdateMMState();
    void OnOptions();
    void OnAbout();
    void OnChangeAirport();
    void OnUpdateWidgets();
    void OnProgressInfo(int currentSeconds, int totalSeconds, QString connectedClient);
    void OnShowStatus(QString status);
    void OnPlayState(bool isPlaying);
    void OnDmapInfo(QString album, QString track, QString artist);
    void OnAlbumArt();
    void OnShowAdArt();
    void OnUpdateTray();
    void OnShowToastMessage();
#if Q_MOC_OUTPUT_REVISION <= 67
    void OnSettingTitleInfoView(int state);
#else
    void OnSettingTitleInfoView(Qt::CheckState state);
#endif

private:
    using ImageQueueItem = std::pair<QByteArray, std::string>;
    using ImageQueueItemPtr = std::unique_ptr<ImageQueueItem>;
    using TimePoint = std::chrono::steady_clock::time_point;

    const SharedPtr<IValueCollection>  	m_config;
    const SharedPtr<DnsSD>              m_dnsSD;
    const KeyboardHook::Handle          m_handleKeyboardHook;
    std::mutex                          m_mtx;
    DacpID                              m_currentDacpID;
    std::map<uint64_t, DacpServicePtr>  m_mapDacpService;
    Condition                           m_condDatachange;
    std::list<ImageQueueItemPtr>        m_imageQueue;
    std::atomic_uint64_t                m_timePointShowAlbumArt{ 0 };
    std::unique_ptr<TimePoint>          m_timePointShowToastMessage;
    std::atomic_bool                    m_dialogClosed{ false };
    std::atomic_bool                    m_firstShowEvent{ true };
    std::atomic_bool                    m_isHidden{ false };
    std::atomic_bool                    m_isPlaying{ false };
    std::list<std::future<void>>        m_listAsyncOperations;
    DnsHandlePtr                        m_dacpBrowser;
    std::shared_ptr<RaopServer>         m_raopServer;
    std::unique_ptr<std::thread>        m_scheduler;
    
    // Qt Widgets
    // Menu
    // read about the Qt Object Model: https://doc.qt.io/qt-6/object.html
    QPointer<QMenuBar>                  m_menuBar;

    // Status Group
    QPointer<QGroupBox>                 m_groupBoxStatus;
    QPointer<QLabel>                    m_labelStatus;
    
    // Airport Group
    QPointer<QGroupBox>                 m_groupBoxAirport;
    QPointer<QPushButton>               m_buttonChangeAirport;
    QPointer<QLineEdit>                 m_editNameAirport;
    QPointer<QLineEdit>                 m_editPasswordAirport;

    // TitleInfo Group
    std::recursive_mutex                m_mtxTitleInfo;
    QPointer<QGroupBox>                 m_groupBoxTitleInfo;
    QPointer<QCheckBox>                 m_checkboxEnabledTitleInfo;
    QPointer<QLabel>                    m_imageAlbumArt;
    QPixmap                             m_pixmapShairport;
    QRect                               m_rectAlbumArt;
    std::unique_ptr<QPixmap>            m_currentAlbumArt;

    QPointer<QVBoxLayout>               m_albumArtLayout;

    QPointer<QLabel>                    m_labelProgessTimeTitleInfo;
    QPointer<TimeLabel>                 m_labelTotalTimeTitleInfo;
    QPointer<QProgressBar>              m_progressBarTitleInfo;
    
    QPointer<QLabel>                    m_labelArtistTitleInfo;
    QString                             m_strCurrentArtist;
    QString                             m_strCurrentArtistInTray;

    QPointer<QLabel>                    m_labelTrackTitleInfo;
    QString                             m_strCurrentTrack;
    QString                             m_strCurrentTrackInTray;

    QPointer<QLabel>                    m_labelAlbumTitleInfo;
    QString                             m_strCurrentAlbum;

    QPointer<QVBoxLayout>               m_titleInfoLayout;

    // MultimediaControl Group
    QPointer<QGroupBox>                 m_groupBoxMultimediaControl;
    const QIcon                         m_iconShairportQt;
    const QIcon                         m_iconPlay;
    const QIcon                         m_iconPause;
    QPointer<QPushButton>               m_buttonPreviousTrack;
    QPointer<QPushButton>               m_buttonNextTrack;
    QPointer<QPushButton>               m_buttonPlayPauseTrack;

    // Tray
    QPointer<QSystemTrayIcon>           m_systemTray;
    TimePoint                           m_timepointTrayContextMenuClosed;
};