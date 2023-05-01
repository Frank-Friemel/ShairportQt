#include "MainDlg.h"
#include <string>

#include <QApplication>
#include <QSizePolicy>
#include <QPainter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpacerItem>
#include <QDesktopServices>
#include <QSlider>
#include <QComboBox>
#include <QUrl>

#include "localization/StringIDs.h"
#include <spdlog/spdlog.h>
#include "RaopServer.h"
#include "libutils.h"
#include "TimeLabel.h"
#include "definitions.h"
#include "audio/PlaySound.h"
#include <time.h>

using namespace std;
using namespace literals;

MainDlg::MainDlg(const SharedPtr<IValueCollection>& config)
    : m_config{ config }
    , m_dnsSD{ MakeShared<DnsSD>() }
    , m_iconShairportQt{ ":/ShairportQt.ico" }
    , m_iconPlay{ ":/play.png" }
    , m_iconPause{ ":/pause.png" }
    , m_handleKeyboardHook{ KeyboardHook::Setup(this) }
{
    // pre-create pixmap logo
    {
        QImage surface(tr(":/AdShadow.png"));

        const QSize rectClient = surface.size();
        m_rectAlbumArt = QRect(3, 3, rectClient.width() - 11, rectClient.height() - 12);

        const QPixmap pm(":/ShairportQt.png");
        QPainter painter(&surface);

        painter.setRenderHint(QPainter::RenderHint::Antialiasing);
        painter.setBrush(Qt::gray);
        painter.drawRoundedRect(m_rectAlbumArt, 8, 8);
        painter.drawPixmap(m_rectAlbumArt, pm.scaled(m_rectAlbumArt.width(), m_rectAlbumArt.height()));
        painter.end();
        m_pixmapShairport = QPixmap::fromImage(surface);
    }
    // create signal/slot connections
    connect(this, &MainDlg::ShowMessage, this, &MainDlg::OnShowMessage);
    connect(this, &MainDlg::UpdateMMState, this, &MainDlg::OnUpdateMMState);
    connect(this, &MainDlg::UpdateWidgets, this, &MainDlg::OnUpdateWidgets);
    connect(this, &MainDlg::SetProgressInfo, this, &MainDlg::OnProgressInfo);
    connect(this, &MainDlg::ShowStatus, this, &MainDlg::OnShowStatus);
    connect(this, &MainDlg::SetPlayState, this, &MainDlg::OnPlayState);
    connect(this, &MainDlg::ShowDmapInfo, this, &MainDlg::OnDmapInfo);
    connect(this, &MainDlg::ShowAlbumArt, this, &MainDlg::OnAlbumArt);
    connect(this, &MainDlg::ShowAdArt, this, &MainDlg::OnShowAdArt);
    connect(this, &MainDlg::ShowToastMessage, this, &MainDlg::OnShowToastMessage);

    setWindowIcon(QIcon(":/ShairportQt.png"));
    setWindowTitle(tr("Shairport"));

    CreateMenuBar();
    WidgetCreateStatusGroup();
    WidgetCreateAirportGroup();
    WidgetCreateTitleInfoGroup();
    WidgetCreateMultimediaControlGroup();

    QPointer<QVBoxLayout> mainLayout = new QVBoxLayout;

    mainLayout->setMenuBar(m_menuBar);
    mainLayout->addWidget(m_groupBoxStatus);
    mainLayout->addWidget(m_groupBoxAirport);
    mainLayout->addWidget(m_groupBoxTitleInfo);
    mainLayout->addWidget(m_groupBoxMultimediaControl);

    setLayout(mainLayout);
    mainLayout->setParent(this);
    auto geometry = VariantValue::Key("WindowGeometry").TryGet<vector<BYTE>>(m_config);

    // restore Window Geometry
    if (geometry.has_value())
    {
        const auto geometryBlob = std::move(geometry.value());

        const QByteArray geo((const char*)geometryBlob.data(), geometryBlob.size());
        restoreGeometry(geo);
    }

    if (VariantValue::Key("KeepSticky").TryGet<bool>(m_config).value_or(false))
    {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
    }
    ConfigureSystemTray();

    // create a scheduler thread
    m_scheduler = make_unique<std::thread>([this]() { RunScheduler(); });

    // create the main RAOP service asynchronously, finally
    auto createService = async(launch::async, [this]() -> void
        {
            ConfigureDacpBrowser();

            auto raopServer = make_shared<RaopServer>(m_config, m_dnsSD, this);

            {
                const lock_guard<mutex> guard(m_mtx);
                raopServer.swap(m_raopServer);
            }
        });
    const lock_guard<mutex> guard(m_mtx);
    m_listAsyncOperations.emplace_back(std::move(createService));
}

MainDlg::~MainDlg()
{
    assert(m_dialogClosed);
    assert(!m_dacpBrowser);
    assert(!m_raopServer);
    assert(m_mapDacpService.empty());

    if (m_scheduler)
    {
        if (m_scheduler->joinable())
        {
            try
            {
                m_scheduler->join();
            }
            catch (...)
            {
                assert(false);
            }
        }
        m_scheduler.reset();
    }
}

// scheduler thread runs here
void MainDlg::RunScheduler() noexcept
{
    list<future<void>> listAsyncOperations;

    try
    {
        const QString   emptyString;
        uint64_t        garbageCounter  = 0;
        uint64_t        progressState   = 0;
        int             previousPos     = 0;
        bool            hasPlayingState = false;

        unique_lock<mutex> sync(m_mtx);

        for (;;)
        {
            // wait for event(s) with timeout of 1s
            if (m_condDatachange.WaitAndLock(sync, [this]() -> bool
                {
                    return m_dialogClosed;
                }, 1000) == cv_status::timeout)
            {
                auto raopServer = m_raopServer;
                sync.unlock();

                if (raopServer)
                {
                    int     duration{};
                    int     position{};
                    string  clientID;

                    if (raopServer->GetProgress(duration, position, clientID))
                    {
                        try
                        {
                            const auto client = raopServer->GetClient(clientID);

                            if (client.IsValid())
                            {
                                auto clientInfo = VariantValue::Key("clientInfo").TryGet<string>(client);

                                if (clientInfo.has_value())
                                {
                                    clientID = std::move(clientInfo.value());
                                }
                            }

                            emit SetProgressInfo(position, duration, clientID.c_str());
                            progressState = 0;

                            if (raopServer->IsPlaying())
                            {
                                if (!hasPlayingState)
                                {
                                    hasPlayingState = true;
                                    emit SetPlayState(true);
                                }
                            }
                            else
                            {
                                if (hasPlayingState && previousPos == position)
                                {
                                    hasPlayingState = false;
                                    emit SetPlayState(false);
                                }
                            }
                            previousPos = position;
                        }
                        catch (...)
                        {
                        }
                    }
                    else if (progressState++ == 1)
                    {
                        // When we get here ... there is non RAOP client connected.
                        // Just empty the GUI.
                        try
                        {
                            // empty the progress info
                            emit SetProgressInfo(0, 0, emptyString);

                            // empty the title info
                            emit ShowDmapInfo(emptyString, emptyString, emptyString);
                            
                            // show the default "Shairport" logo
                            emit ShowAdArt();

                            if (hasPlayingState)
                            {
                                hasPlayingState = false;
                                SetPlayState(false);
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                    raopServer.reset();
                }

                if (++garbageCounter % 10 == 0)
                {
                    // garbage out the asynchronous operations
                    assert(!sync.owns_lock());
                    sync.lock();

                    for (auto asyncOperation = m_listAsyncOperations.begin(); asyncOperation != m_listAsyncOperations.end(); )
                    {
                        if (asyncOperation->wait_for(0ms) == future_status::ready)
                        {
                            asyncOperation = m_listAsyncOperations.erase(asyncOperation);
                        }
                        else
                        {
                            ++asyncOperation;
                        }
                    }
                    sync.unlock();
                }

                const auto timePointShowAlbumArt = m_timePointShowAlbumArt.load();

                // check, if any "show album art" action is pending...
                if (timePointShowAlbumArt && ((uint64_t) ::time(NULL)) >= timePointShowAlbumArt)
                {
                    assert(!sync.owns_lock());
                    sync.lock();

                    // reset the timer
                    m_timePointShowAlbumArt = 0ull;

                    // if an image is still pending...
                    if (!m_imageQueue.empty())
                    {
                        sync.unlock();

                        // ... schedule a call to Qt slot "OnAlbumArt" 
                        try
                        {
                            emit ShowAlbumArt();
                        }
                        catch (...)
                        {
                        }
                    }
                    else
                    {
                        sync.unlock();
                    }
                }
                assert(!sync.owns_lock());

                // any toast message pending?
                if (m_timePointShowToastMessage)
                {
                    const auto tp = std::chrono::steady_clock::now();

                    unique_lock<recursive_mutex> syncTitleInfo(m_mtxTitleInfo);

                    if (m_timePointShowToastMessage && tp >= *m_timePointShowToastMessage)
                    {
                        m_timePointShowToastMessage.reset();
                        syncTitleInfo.unlock();
                        ShowToastMessage();
                    }
                }

                assert(!sync.owns_lock());
                sync.lock();
            }
            assert(sync.owns_lock());

            if (m_dialogClosed)
            {
                // join with the currently running asynchronous operations and exit
                listAsyncOperations = std::move(m_listAsyncOperations);
                break;
            }
        }
    }
    catch (const exception& e)
    {
        spdlog::error("unexpected exception in RunScheduler: {}", e.what());
    }
}

void MainDlg::CreateMenuBar()
{
    m_menuBar = new QMenuBar;
    
    QPointer<QMenu> fileMenu = new QMenu(GetString(StringID::MENU_FILE), this);

    const auto strQuit = GetString(StringID::MENU_QUIT);

#ifdef Q_OS_WIN
    const QKeySequence quitShortcutSequence(QKeySequence::StandardKey::Close);
#else
    const QKeySequence quitShortcutSequence(QKeySequence::StandardKey::Quit);
#endif
    fileMenu->addAction(QIcon(":/exit-16.bmp"), strQuit, this, &MainDlg::OnQuit, quitShortcutSequence);

    QPointer<QMenu> editMenu = new QMenu(GetString(StringID::MENU_EDIT), this);
    editMenu->addAction(QIcon(":/settings-16.bmp"), GetString(StringID::MENU_OPTIONS), this, &MainDlg::OnOptions);

    QPointer<QMenu> helpMenu = new QMenu(GetString(StringID::MENU_HELP), this);
    helpMenu->addAction(QIcon(":/info-16.bmp"),GetString( StringID::MENU_ABOUT), this, &MainDlg::OnAbout);

    m_menuBar->addMenu(fileMenu);
    m_menuBar->addMenu(editMenu);
    m_menuBar->addMenu(helpMenu);
}

void MainDlg::WidgetCreateStatusGroup()
{
    QPointer<QHBoxLayout> layout = new QHBoxLayout;

    m_labelStatus = new QLabel;
    layout->addWidget(m_labelStatus);

    m_groupBoxStatus = new QGroupBox;
    m_groupBoxStatus->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);
    m_groupBoxStatus->setLayout(layout);
}

void MainDlg::WidgetCreateAirportGroup()
{
    QPointer<QFormLayout> formLayout = new QFormLayout;

    m_editNameAirport = new QLineEdit;
    m_editNameAirport->setReadOnly(true);
    formLayout->addRow(new QLabel(GetString(StringID::LABEL_AIRPORT_NAME)), m_editNameAirport);

    m_editPasswordAirport = new QLineEdit;
    m_editPasswordAirport->setReadOnly(true);
    m_editPasswordAirport->setEchoMode(QLineEdit::Password);
    formLayout->addRow(new QLabel(GetString(StringID::LABEL_AIRPORT_PASSWORD)), m_editPasswordAirport);

    QPointer<QHBoxLayout> layout = new QHBoxLayout;
    layout->addLayout(formLayout);

    m_buttonChangeAirport = new QPushButton(GetString(StringID::LABEL_AIRPORT_CHANGE));
    connect(m_buttonChangeAirport, &QPushButton::clicked, this, &MainDlg::OnChangeAirport);

    m_buttonChangeAirport->setSizePolicy(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Expanding);
    layout->addWidget(m_buttonChangeAirport);

    m_groupBoxAirport = new QGroupBox;
    m_groupBoxAirport->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);
    m_groupBoxAirport->setLayout(layout);
}

void MainDlg::WidgetCreateTitleInfoGroup()
{
    const bool metaInfo = !VariantValue::Key("NoMetaInfo").TryGet<bool>(m_config).value_or(false);

    const lock_guard<recursive_mutex> guard(m_mtxTitleInfo);

    if (!m_albumArtLayout)
    {
        m_albumArtLayout = new QVBoxLayout;

        // checkbox and cover image
        m_checkboxEnabledTitleInfo = new QCheckBox(GetString(StringID::LABEL_TITLE_INFO));
#if Q_MOC_OUTPUT_REVISION <= 67
        connect(m_checkboxEnabledTitleInfo, &QCheckBox::stateChanged, this, &MainDlg::OnSettingTitleInfoView);
#else
        connect(m_checkboxEnabledTitleInfo, &QCheckBox::checkStateChanged, this, &MainDlg::OnSettingTitleInfoView);
#endif
        m_albumArtLayout->addWidget(m_checkboxEnabledTitleInfo);
        m_albumArtLayout->addStretch(1);
    }

    if (metaInfo)
    {
        if (!m_imageAlbumArt)
        {
            m_imageAlbumArt = new QLabel;

            if (m_currentAlbumArt)
            {
                m_imageAlbumArt->setPixmap(*m_currentAlbumArt);
            }
            else
            {
                m_imageAlbumArt->setPixmap(m_pixmapShairport);
            }
            m_albumArtLayout->insertWidget(1, m_imageAlbumArt);
        }
    }
    else
    {
        if (m_imageAlbumArt)
        {
            m_imageAlbumArt->setVisible(false);
            m_albumArtLayout->removeWidget(m_imageAlbumArt);
            m_imageAlbumArt.clear();
            
            m_albumArtLayout->update();
        }
    }

    // progress bar and time
    if (!m_titleInfoLayout)
    {
        m_titleInfoLayout = new QVBoxLayout;
        m_titleInfoLayout->setAlignment(Qt::AlignTop);

        m_labelProgessTimeTitleInfo = new QLabel(TimeLabel::undefinedTimeLabel);
        m_labelTotalTimeTitleInfo = new TimeLabel(m_config, TimeLabel::undefinedTimeLabel);

        m_progressBarTitleInfo = new QProgressBar;

        m_progressBarTitleInfo->setMinimum(0);
        m_progressBarTitleInfo->setMaximum(100);
        m_progressBarTitleInfo->setValue(0);
        m_progressBarTitleInfo->setTextVisible(false);

        QPointer<QHBoxLayout> progressLayout = new QHBoxLayout;
        progressLayout->addWidget(m_labelProgessTimeTitleInfo);
        progressLayout->addWidget(m_progressBarTitleInfo);
        progressLayout->addWidget(m_labelTotalTimeTitleInfo);

        progressLayout->setAlignment(Qt::AlignTop);
        m_titleInfoLayout->addLayout(progressLayout);

        QPointer<QHBoxLayout> outerLayout = new QHBoxLayout;

        outerLayout->addLayout(m_albumArtLayout);
        outerLayout->addLayout(m_titleInfoLayout);

        m_groupBoxTitleInfo = new QGroupBox;
        m_groupBoxTitleInfo->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        m_groupBoxTitleInfo->setLayout(outerLayout);
    }

    if (metaInfo)
    {
        if (!m_labelArtistTitleInfo)
        {
            assert(!m_labelTrackTitleInfo);
            assert(!m_labelAlbumTitleInfo);

            // track info
            m_labelArtistTitleInfo = new QLabel;
            m_labelTrackTitleInfo = new QLabel;
            m_labelAlbumTitleInfo = new QLabel;

            m_labelArtistTitleInfo->setWordWrap(true);
            m_labelTrackTitleInfo->setWordWrap(true);
            m_labelAlbumTitleInfo->setWordWrap(true);

            m_labelArtistTitleInfo->setAlignment(Qt::AlignTop);
            m_labelTrackTitleInfo->setAlignment(Qt::AlignTop);
            m_labelAlbumTitleInfo->setAlignment(Qt::AlignTop);

            m_labelArtistTitleInfo->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
            m_labelTrackTitleInfo->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
            m_labelAlbumTitleInfo->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);

            m_titleInfoLayout->addWidget(m_labelArtistTitleInfo);
            m_titleInfoLayout->addWidget(m_labelTrackTitleInfo);
            m_titleInfoLayout->addWidget(m_labelAlbumTitleInfo);

            if (!m_strCurrentArtist.isEmpty())
            {
                m_labelArtistTitleInfo->setText(m_strCurrentArtist);
            }
            if (!m_strCurrentTrack.isEmpty())
            {
                m_labelTrackTitleInfo->setText(m_strCurrentTrack);
            }
            if (!m_strCurrentAlbum.isEmpty())
            {
                m_labelAlbumTitleInfo->setText(m_strCurrentAlbum);
            }
        }
    }
    else
    {
        if (m_labelArtistTitleInfo)
        {
            assert(m_labelTrackTitleInfo);
            assert(m_labelAlbumTitleInfo);

            m_labelArtistTitleInfo->setVisible(false);
            m_labelTrackTitleInfo->setVisible(false);
            m_labelAlbumTitleInfo->setVisible(false);

            m_titleInfoLayout->removeWidget(m_labelArtistTitleInfo);
            m_titleInfoLayout->removeWidget(m_labelTrackTitleInfo);
            m_titleInfoLayout->removeWidget(m_labelAlbumTitleInfo);

            m_labelArtistTitleInfo.clear();
            m_labelTrackTitleInfo.clear();
            m_labelAlbumTitleInfo.clear();

            m_titleInfoLayout->update();
            m_groupBoxTitleInfo->layout()->update();
        }
    }
}


void MainDlg::WidgetCreateMultimediaControlGroup()
{
    //
    // https://openairplay.github.io/airplay-spec/audio/remote_control.html
    //
    m_buttonPreviousTrack   = new QPushButton(QIcon(":/skip_to_prev.png"), tr(""));
    connect(m_buttonPreviousTrack, &QPushButton::clicked, [this]() { SendDacpCommand("previtem"s); });
    
    m_buttonNextTrack       = new QPushButton(QIcon(":/skip_to_next.png"), tr(""));
    connect(m_buttonNextTrack, &QPushButton::clicked, [this]() { SendDacpCommand("nextitem"s); });

    m_buttonPlayPauseTrack  = new QPushButton(m_iconPlay, tr(""));
    connect(m_buttonPlayPauseTrack, &QPushButton::clicked, [this]() { SendDacpCommand("playpause"s); });

    m_buttonPreviousTrack->setEnabled(false);
    m_buttonNextTrack->setEnabled(false);
    m_buttonPlayPauseTrack->setEnabled(false);

    QPointer<QHBoxLayout> mmLayout = new QHBoxLayout;
    mmLayout->addWidget(m_buttonPreviousTrack);
    mmLayout->addWidget(m_buttonNextTrack);
    mmLayout->addWidget(m_buttonPlayPauseTrack);

    m_groupBoxMultimediaControl = new QGroupBox;
    m_groupBoxMultimediaControl->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);
    m_groupBoxMultimediaControl->setLayout(mmLayout);
}

void MainDlg::ConfigureSystemTray()
{
    // consider system which support a "tray", only
    if (QSystemTrayIcon::isSystemTrayAvailable())
    {
        if (!m_systemTray)
        {
            QPointer<QMenu> trayIconMenu = new QMenu(GetString(StringID::MENU_FILE), this);
#ifdef Q_OS_WIN
            connect(trayIconMenu, &QMenu::aboutToHide, [this]()
                {
                    m_timepointTrayContextMenuClosed = chrono::steady_clock::now();
                });
#endif

            trayIconMenu->addAction(GetString(StringID::MENU_SHOW_APP_WINDOW), [this]()
                {
                    setWindowState(Qt::WindowNoState);
                    
                    // force a hide/show sequence
                    hide();
                    show();
                    activateWindow();
                });

            if (QSystemTrayIcon::supportsMessages())
            {
                trayIconMenu->addAction(GetString(StringID::MENU_SHOW_TRACK_INFO_IN_TRAY), [this]()
                    {
                        const bool trayTrackInfo = !VariantValue::Key("TrayTrackInfo").TryGet<bool>(m_config).value_or(false);
                        VariantValue::Key("TrayTrackInfo").Set(m_config, trayTrackInfo);
                        m_systemTray->contextMenu()->actions().at(1)->setChecked(trayTrackInfo);
                    });
                trayIconMenu->actions().at(1)->setCheckable(true);
                trayIconMenu->actions().at(1)->setChecked(VariantValue::Key("TrayTrackInfo").TryGet<bool>(m_config).value_or(false));
            }
            trayIconMenu->addSeparator();
            trayIconMenu->addAction(QIcon(":/exit-16.bmp"), GetString(StringID::MENU_QUIT), this, &MainDlg::OnQuit);

            m_systemTray = new QSystemTrayIcon(this);
            m_systemTray->setContextMenu(trayIconMenu);
            m_systemTray->setIcon(m_iconShairportQt);
            m_systemTray->setToolTip(tr("ShairportQt"));
            trayIconMenu->setDefaultAction(trayIconMenu->actions().at(0));

            connect(m_systemTray, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason)
                {
                    // a "Trigger" toggles between "hidden" and "showing" state
                    if (reason == QSystemTrayIcon::Trigger)
                    {
#ifdef Q_OS_WIN
                        const auto diff = chrono::steady_clock::now() - m_timepointTrayContextMenuClosed;

                        // Windows bug: dismissing the Tray-Context menu leads to a "QSystemTrayIcon::Trigger" (which is wrong).
                        // So, we're investigating the case "Tray-Context menu dismissed" ourselves.
                        if (diff > 0ms && diff <= 500ms)
                        {
                            // the tray context had been closed previously. So, it's likely
                            // that is *not* a valid "Trigger"
                            return;
                        }
#endif
                        if (!m_isHidden)
                        {
                            // hide the main dialog
                            hide();
                        }
                        else
                        {
                            // show the main dialog
                            setWindowState(Qt::WindowNoState);
                            show();
                            activateWindow();
                        }
                    }
                });
        }
        if (VariantValue::Key("TrayIcon").TryGet<bool>(m_config).value_or(true))
        {
            m_systemTray->show();
        }
        else
        {
            m_systemTray->hide();
        }
    }
}

void MainDlg::ConfigureDacpBrowser()
{
     // enable/disable DACP browser according to config
    const bool enabled = !VariantValue::Key("NoMediaControl").TryGet<bool>(m_config).value_or(false);

    DnsHandlePtr dacpBrowser;

    if (enabled && !m_dacpBrowser)
    {
        dacpBrowser = m_dnsSD->BrowseForService("_dacp._tcp", this);

        if (!dacpBrowser->Succeeded())
        {
            spdlog::error("Main Dialog: failed to start DACP Browser with code: ", m_dacpBrowser->ErrorCode());

            if (!m_isHidden)
            {
                emit ShowMessage(StringID::FAILED_TO_START_DACP_BROWSER);
            }
        }
        else
        {
            spdlog::info("Main Dialog: succeeded to start DACP Browser");

            const lock_guard<mutex> guard(m_mtx);
            dacpBrowser.swap(m_dacpBrowser);
        }
    }
    else if (!enabled && m_dacpBrowser)
    {
        map<uint64_t, DacpServicePtr> mapDacpService;

        {
            const lock_guard<mutex> guard(m_mtx);
            dacpBrowser.swap(m_dacpBrowser);
            mapDacpService.swap(m_mapDacpService);
        }
        dacpBrowser.reset();
        mapDacpService.clear();
        spdlog::info("Main Dialog: stopped DACP Browser");

        OnUpdateMMState();
    }
}

QString MainDlg::GetString(int id) const
{
    return GetLanguageManager(m_config)->GetString(id).c_str();
}

void MainDlg::SendDacpCommand(const string& cmd)
{
    unique_lock<mutex> sync(m_mtx);

    auto entry = m_mapDacpService.find(m_currentDacpID.id);

    if (entry != m_mapDacpService.end())
    {
        DacpID dacpID;

        try
        {
            dacpID = m_currentDacpID;
        }
        catch (...)
        {
            return;
        }
        auto dacpService = entry->second;
        sync.unlock();

        const uint16_t port = dacpService->GetPort(300);

        if (port)
        {
            dacpID.port = port;

            try
            {
                auto asyncSendDacpCommand = async(launch::async, [this, cmd, dacpID = std::move(dacpID),
                    dacpService = std::move(dacpService)]() -> void
                    {
                        int status = RaopServer::SendDacpCommand(dacpID, cmd);
                        spdlog::debug("SendDacpCommand({}) result: {}", cmd, status);

                        if (status == -1)
                        {
                            // connection error
                            try
                            {
                                // the DACP port may have changed so we
                                // try to re-resolve the service
                                dacpService->Resolve();

                                auto copyDacpID = dacpID;
                                copyDacpID.port = dacpService->GetPort(500);

                                if (copyDacpID.port)
                                {
                                    unique_lock<mutex> sync(m_mtx);

                                    if (copyDacpID.id != m_currentDacpID.id)
                                    {
                                        // current dacpID has changed meanwhile -> do nothing
                                        return;
                                    }
                                    sync.unlock();

                                    status = RaopServer::SendDacpCommand(copyDacpID, cmd);
                                    spdlog::debug("retried SendDacpCommand({}) result: {}", cmd, status);
                                }
                                else
                                {
                                    spdlog::debug("retried SendDacpCommand({}) timed out", cmd);
                                    return;
                                }
                            }
                            catch (...)
                            {
                                return;
                            }
                        }
                        // Forbidden?
                        else if (status == 403)
                        {
                            unique_lock<mutex> sync(m_mtx);

                            if (m_currentDacpID.id == dacpID.id)
                            {
                                // clear current DACP id and update MM status
                                m_currentDacpID.id = 0;
                                sync.unlock();

                                try
                                {
                                    emit UpdateMMState();
                                }
                                catch (const exception& e)
                                {
                                    spdlog::error("failed to emit UpdateMMState: ", e.what());
                                }
                            }
                        }
                    });
                if (asyncSendDacpCommand.wait_for(300ms) == future_status::timeout)
                {
                    sync.lock();
                    m_listAsyncOperations.emplace_back(std::move(asyncSendDacpCommand));
                }
            }
            catch (const exception& e)
            {
                spdlog::error("Main Dialog: failed to SendDacpDommand({}): {}", cmd, e.what());
            }
        }
    }
    else
    {
        spdlog::info("Main Dialog: could not SendDacpDommand({}): found no remote DACP server for ID {}", cmd, m_currentDacpID.id);
    }
}

// Callback sent by RAOP service: "service created"
void MainDlg::OnCreateRaopService(bool success) noexcept
{
    if (!success)
    {
        // inform the user that we've failed to create the service
        spdlog::debug("Main Dialog: emitting RAOP Service check ShowMessage");

        if (!m_dialogClosed)
        {
            try
            {
                emit ShowMessage(StringID::TROUBLE_SHOOT_RAOP_SERVICE);
            }
            catch (const exception& e)
            {
                spdlog::error("failed to emit TROUBLE_SHOOT_RAOP_SERVICE: ", e.what());
            }
        }
    }
    else
    {
        ShowStatus(GetString(StringID::STATUS_READY));
    }
    spdlog::debug("Main Dialog: RAOP Service created: {}", success);
}

// Callback sent by RAOP service: DMAP info arrived
void MainDlg::OnSetCurrentDmapInfo(DmapInfo&& dmapInfo) noexcept
{
    try
    {
        ShowDmapInfo(dmapInfo.album.c_str(), dmapInfo.track.c_str(), dmapInfo.artist.c_str());
    }
    catch (const exception& e)
    {
        spdlog::error("failed to ShowDmapInfo: {}", e.what());
    }
}

// Callback sent by RAOP service: image info arrived
void MainDlg::OnSetCurrentImage(const char* data, size_t dataLen, string&& imageType) noexcept
{
    try
    {
        spdlog::debug("OnSetCurrentImage: {}", imageType);
        
        ImageQueueItemPtr item;
        uint64_t timePointShowAlbumArt = 0;

        if (imageType == "NONE"s)
        {
            // show standard image with a slight delay (2 seconds)
            item = make_unique<ImageQueueItem>(QByteArray{}, std::move(imageType));
            
            // setup a delay timer
            timePointShowAlbumArt = ((uint64_t) ::time(NULL)) + 2ull;
        }
        else
        {
            item = make_unique<ImageQueueItem>(QByteArray{ data, static_cast<qsizetype>(dataLen) }, std::move(imageType));
        }
        {
            const lock_guard<mutex> guard(m_mtx);
            
            // queue the image item
            m_imageQueue.emplace_back(std::move(item));

            // set/reset the timer
            m_timePointShowAlbumArt = timePointShowAlbumArt;
        }
        if (timePointShowAlbumArt == 0)
        {
            // call Qt slot immediately, if we have a real album art image
            ShowAlbumArt();
        }
    }
    catch (const exception& e)
    {
        spdlog::error("failed to ShowAlbumArt: {}", e.what());
    }
}

// Callback sent by RAOP service: DACP remote control announced
void MainDlg::OnSetCurrentDacpID(DacpID&& dacpID) noexcept
{
    spdlog::info("Main Dialog: OnSetCurrentDacpID {:X}, {}. From IP: {}", dacpID.id, dacpID.activeRemote, dacpID.remoteIP);

    bool bPostNewState = false;
    
    {
        // store the announced id as "current DacpID"
        const lock_guard<mutex> guard(m_mtx);
        m_currentDacpID = std::move(dacpID);

        if (m_currentDacpID.id == 0 || m_mapDacpService.find(m_currentDacpID.id) != m_mapDacpService.end())
        {
            bPostNewState = true;
        }
    }
    if (bPostNewState && !m_dialogClosed)
    {
        try
        {
            emit UpdateMMState();
        }
        catch (const exception& e)
        {
            spdlog::error("failed to emit UpdateMMState: ", e.what());
        }
    }
}

// Callback sent by DACP browser: a DACP server registered/unregistered
void MainDlg::OnDNSServiceBrowseReply(
    bool registered,
    uint32_t interfaceIndex,
    const char* servicename,
    const char* regtype,
    const char* replydomain) noexcept
{
    if (servicename)
    {
        try
        {
            string	serviceName{ servicename };
            const uint64_t dacpID = HexToInteger(serviceName);

            if (registered)
            {
                string	regType{ regtype ? regtype : ""s };
                string	replyDomain{ replydomain ? replydomain : ""s };

                spdlog::info("Main Dialog: OnDNSServiceBrowseReply registered: {}.{}{}"s, serviceName, regType, replydomain);

                // try to lookup an existing entry
                DacpServicePtr dacpService;

                {
                    const lock_guard<mutex> guard(m_mtx);
                    auto entry = m_mapDacpService.find(dacpID);

                    if (entry != m_mapDacpService.end())
                    {
                        dacpService = entry->second;
                    }
                }
                if (dacpService)
                {
                    if (!dacpService->IsResolved())
                    {
                        spdlog::debug("Main Dialog: {} is currently resolving, so we don't need to register a new entry", serviceName);
                        return;
                    }
                    dacpService.reset();
                }
                assert(!dacpService);

                // create new item which we will insert into the map
                dacpService = make_shared<DacpService>(m_dnsSD, interfaceIndex,
                    std::move(serviceName),
                    std::move(regType),
                    std::move(replyDomain));

                {
                    unique_lock<mutex> sync(m_mtx);

                    if (!m_dialogClosed)
                    {
                        dacpService.swap(m_mapDacpService[dacpID]);

                        if (dacpID == m_currentDacpID.id)
                        {
                            sync.unlock();

                            try
                            {
                                emit UpdateMMState();
                            }
                            catch (const exception& e)
                            {
                                spdlog::error("failed to emit UpdateMMState: ", e.what());
                            }
                        }
                    }
                }
            }
            else
            {
                spdlog::info("Main Dialog: OnDNSServiceBrowseReply unregistered: {}"s, serviceName);

                auto asyncRemove = async(launch::async, [this](const uint64_t dacpID) -> void
                    {
                        DacpServicePtr	dacpService;

                        {
                            const lock_guard<mutex> guard(m_mtx);
                            auto e = m_mapDacpService.find(dacpID);

                            if (e != m_mapDacpService.end())
                            {
                                dacpService = std::move(e->second);
                                m_mapDacpService.erase(e);
                                spdlog::debug("removed DACP service {:X} from map", dacpID);
                            }
                        }
                        if (dacpService)
                        {
                            dacpService.reset();

                            if (!m_dialogClosed)
                            {
                                try
                                {
                                    emit UpdateMMState();
                                }
                                catch (const exception& e)
                                {
                                    spdlog::error("failed to emit UpdateMMState: ", e.what());
                                }
                            }
                        }
                    }, dacpID);

                if (asyncRemove.wait_for(100ms) == future_status::timeout)
                {
                    const lock_guard<mutex> guard(m_mtx);
                    m_listAsyncOperations.emplace_back(std::move(asyncRemove));
                }
            }
        }
        catch (const exception& e)
        {
            spdlog::error("OnDNSServiceBrowseReply failed: {}", e.what());
        }
    }
}

// Widget slot: ShowMessage
void MainDlg::OnShowMessage(int text)
{
    if (text == StringID::RECONFIG_RAOP_SERVICE)
    {
        bool serviceWasStreaming = false;

        {
            // disable the RAOP service, so that no new clients can connect
            // while we're restarting the RAOP service
            shared_ptr<RaopServer> raopServer;
            {
                const lock_guard<mutex> guard(m_mtx);
                raopServer = m_raopServer;
            }
            serviceWasStreaming = raopServer && raopServer->EnableServer(false);
        }
        if (serviceWasStreaming)
        {
            // inform the user that the current playing stream will be interrupted
            ShowMessageBox(m_config, text, this);
        }
        spdlog::info("restarting the RAOP service...");

        // restart the service
        auto asyncRestartService = async(launch::async, [this]() -> void
            {
                shared_ptr<RaopServer> raopServer;

                {
                    const lock_guard<mutex> guard(m_mtx);
                    raopServer.swap(m_raopServer);
                }
                // terminate the old service
                raopServer.reset();

                // start new service with the changed config
                raopServer = make_shared<RaopServer>(m_config, m_dnsSD, this);

                {
                    const lock_guard<mutex> guard(m_mtx);
                    raopServer.swap(m_raopServer);
                }
            });

        const lock_guard<mutex> guard(m_mtx);
        m_listAsyncOperations.emplace_back(std::move(asyncRestartService));
    }
    else
    {
        spdlog::debug("Main Dialog: showing MessageBox: {}", GetLanguageManager(m_config)->GetString(text));
        ShowMessageBox(m_config, text, this);
    }
}

// Widget slot: UpdateMMState
void MainDlg::OnUpdateMMState()
{
    unique_lock<mutex> sync(m_mtx);

    auto entry = m_mapDacpService.find(m_currentDacpID.id);
    const bool enabled = entry != m_mapDacpService.end();
    
    sync.unlock();
    
    spdlog::debug("OnUpdateMMState {}", enabled);

    m_buttonPreviousTrack->setEnabled(enabled);
    m_buttonNextTrack->setEnabled(enabled);
    m_buttonPlayPauseTrack->setEnabled(enabled);
}

void MainDlg::OnPlayState(bool isPlaying)
{
    unique_lock<mutex> sync(m_mtx);

    const bool wasPlaying = m_isPlaying;
    m_isPlaying = isPlaying;

    auto entry = m_mapDacpService.find(m_currentDacpID.id);
    const bool enabled = entry != m_mapDacpService.end();

    sync.unlock();

    if (enabled)
    {
        m_buttonPlayPauseTrack->setIcon(isPlaying ? m_iconPause : m_iconPlay);
    }
    else
    {
        m_buttonPlayPauseTrack->setIcon(m_iconPlay);
    }
    if (wasPlaying != isPlaying)
    {
        OnUpdateTray();
    }
}

void MainDlg::OnDmapInfo(QString album, QString track, QString artist)
{
    const lock_guard<recursive_mutex> guard(m_mtxTitleInfo);

    m_strCurrentArtist = std::move(artist);
    m_strCurrentTrack = std::move(track);
    m_strCurrentAlbum = std::move(album);

    if (m_labelArtistTitleInfo)
    {
        assert(m_labelAlbumTitleInfo);
        assert(m_labelTrackTitleInfo);

        m_labelAlbumTitleInfo->setText(m_strCurrentAlbum);
        m_labelTrackTitleInfo->setText(m_strCurrentTrack);
        m_labelArtistTitleInfo->setText(m_strCurrentArtist);
    }
    OnUpdateTray();
}

void MainDlg::OnUpdateTray()
{
    if (m_systemTray && QSystemTrayIcon::supportsMessages() && VariantValue::Key("TrayTrackInfo").TryGet<bool>(m_config).value_or(false))
    {
        const lock_guard<recursive_mutex> guard(m_mtxTitleInfo);

        if (m_isPlaying)
        {
            if ((!m_strCurrentTrack.isEmpty() || !m_strCurrentArtist.isEmpty()) &&
                (m_strCurrentTrack != m_strCurrentTrackInTray || m_strCurrentArtist != m_strCurrentArtistInTray))
            {
                m_strCurrentTrackInTray = m_strCurrentTrack;
                m_strCurrentArtistInTray = m_strCurrentArtist;

                if (m_isHidden)
                {
                    // wait 1000ms to be sure the album art has arrived as well
                    m_timePointShowToastMessage = make_unique<TimePoint>(chrono::steady_clock::now() + 1000ms);
                }
                QString toolTip = tr("ShairportQt - ") + m_strCurrentArtistInTray + tr(" - ") + m_strCurrentTrack;

                if (toolTip.length() > 100)
                {
                    // shorten the tooltip if it's too long
                    toolTip = toolTip.left(97) + tr("...");
                }
                m_systemTray->setToolTip(toolTip);
            }
        }
        else
        {
            m_strCurrentTrackInTray.clear();
            m_strCurrentArtistInTray.clear();
            m_timePointShowToastMessage.reset();

            m_systemTray->setToolTip(tr("ShairportQt"));
        }
    }
}

// Widget slot: ShowToastMessage
void MainDlg::OnShowToastMessage()
{
    if (m_isHidden &&
        m_systemTray && QSystemTrayIcon::supportsMessages() && VariantValue::Key("TrayTrackInfo").TryGet<bool>(m_config).value_or(false))
    {
        const lock_guard<recursive_mutex> guard(m_mtxTitleInfo);

        // show toast, if ...
        //    - there is information to show
        //    - no new information is pending
        if ((!m_strCurrentArtistInTray.isEmpty() || !m_strCurrentTrackInTray.isEmpty()) && !m_timePointShowToastMessage)
        {
            QIcon ico;

            if (m_currentAlbumArt)
            {
                ico.addPixmap(*m_currentAlbumArt);
            }
            else
            {
                ico = m_iconShairportQt;
            }
            m_systemTray->showMessage(m_strCurrentArtistInTray, m_strCurrentTrackInTray, ico);
        }
    }
}

// Widget slot: state of CheckBox "Title Info" changed
#if Q_MOC_OUTPUT_REVISION <= 67
void MainDlg::OnSettingTitleInfoView(int state)
#else
void MainDlg::OnSettingTitleInfoView(Qt::CheckState state)
#endif
{
    // setting of "MetaInfo"
    const bool metaInfo = state == Qt::CheckState::Checked;
    const bool currentMetaInfo = !VariantValue::Key("NoMetaInfo").TryGet<bool>(m_config).value_or(false);

    if (metaInfo != currentMetaInfo)
    {
        VariantValue::Key("NoMetaInfo").Set(m_config, !metaInfo);

        if (metaInfo)
        {
            shared_ptr<RaopServer> raopServer;
            {
                const lock_guard<mutex> guard(m_mtx);
                raopServer = m_raopServer;
            }
            if (raopServer && !raopServer->m_metaInfo)
            {
                raopServer.reset();
                OnShowMessage(StringID::RECONFIG_RAOP_SERVICE);
            }
        }
        WidgetCreateTitleInfoGroup();
    }
}

// Widget slot: ShowAdArt
void MainDlg::OnShowAdArt()
{
    const lock_guard<recursive_mutex> guard(m_mtxTitleInfo);

    if (m_imageAlbumArt)
    {
        m_imageAlbumArt->setPixmap(m_pixmapShairport);
        m_currentAlbumArt.reset();
    }
}

// Widget slot: show album art
void MainDlg::OnAlbumArt()
{
    ImageQueueItemPtr item;
    {
        const lock_guard<mutex> guard(m_mtx);

        if (!m_imageQueue.empty())
        {
            do
            {
                item = std::move(m_imageQueue.front());
                m_imageQueue.pop_front();
            } while (!m_imageQueue.empty());
        }
        else
        {
            return;
        }
    }
    assert(item);

    try
    {
        const lock_guard<recursive_mutex> guard(m_mtxTitleInfo);

        if (item->second == "NONE"s)
        {
            if (m_imageAlbumArt)
            {
                m_imageAlbumArt->setPixmap(m_pixmapShairport);
                m_currentAlbumArt.reset();
            }
        }
        else
        {
            if (!m_currentAlbumArt)
            {
                m_currentAlbumArt = make_unique<QPixmap>();
            }

            if (m_currentAlbumArt->loadFromData(item->first, item->second.c_str()))
            {
                {
                    QImage surface(tr(":/ArtShadow.png"));
                    QPainter painter(&surface);

                    painter.drawPixmap(m_rectAlbumArt, m_currentAlbumArt->scaled(m_rectAlbumArt.width(), m_rectAlbumArt.height()));
                    painter.end();

                    *m_currentAlbumArt = QPixmap::fromImage(surface);
                }
                if (m_imageAlbumArt)
                {
                    m_imageAlbumArt->setPixmap(*m_currentAlbumArt);
                }
            }
        }
    }
    catch (const exception& e)
    {
        spdlog::error("failed to setPixmap: {}", e.what());
    }
}

// Widget slot: UpdateWidgets
void MainDlg::OnUpdateWidgets()
{
    if (VariantValue::Key("ShowToolTips").TryGet<bool>(m_config).value_or(false))
    {
        m_buttonPreviousTrack->setToolTip(GetString(StringID::TOOLTIP_PREV_TRACK));
        m_buttonNextTrack->setToolTip(GetString(StringID::TOOLTIP_NEXT_TRACK));
        m_buttonPlayPauseTrack->setToolTip(GetString(StringID::TOOLTIP_PLAY_PAUSE));
    }
    else
    {
        const QString emtpy;
        m_buttonPreviousTrack->setToolTip(emtpy);
        m_buttonNextTrack->setToolTip(emtpy);
        m_buttonPlayPauseTrack->setToolTip(emtpy);
    }
    m_editNameAirport->setText(VariantValue::Key("APname").Get<string>(m_config).c_str());
    m_editPasswordAirport->setText(VariantValue::Key("Password").Get<string>(m_config).c_str());
    m_checkboxEnabledTitleInfo->setChecked(!VariantValue::Key("NoMetaInfo").TryGet<bool>(m_config).value_or(false));
}

// Widget slot: ShowStatus
void MainDlg::OnShowStatus(QString status)
{
    m_labelStatus->setText(status);
}

// Widget slot: SetProgressInfo
void MainDlg::OnProgressInfo(int currentSeconds, int totalSeconds, QString connectedClient)
{
    m_labelTotalTimeTitleInfo->SetValue(currentSeconds, totalSeconds);

    if (totalSeconds > 0 && currentSeconds >= 0 && currentSeconds <= totalSeconds)
    {
        m_labelProgessTimeTitleInfo->setText(TimeLabel::FormatTimeInfo(currentSeconds));

        if (m_progressBarTitleInfo->maximum() != totalSeconds)
        {
            m_progressBarTitleInfo->setMaximum(totalSeconds);
        }
        m_progressBarTitleInfo->setValue(currentSeconds);
    }
    else
    {
        if (currentSeconds > 0)
        {
            m_labelProgessTimeTitleInfo->setText(TimeLabel::FormatTimeInfo(currentSeconds));
        }
        else
        {
            m_labelProgessTimeTitleInfo->setText(TimeLabel::undefinedTimeLabel);
        }
        m_progressBarTitleInfo->setValue(0);
    }

    if (connectedClient.isEmpty())
    {
        m_labelStatus->setText(GetString(StringID::STATUS_READY));
    }
    else
    {
        const QString strStatus = GetString(StringID::STATUS_CONNECTED) + connectedClient;
        m_labelStatus->setText(strStatus);
    }
}

// Widget slot: "This process should end"
void MainDlg::OnQuit()
{
    if (m_systemTray)
    {
        m_systemTray->hide();
    }
    close();
}

// Widget override: the dialog is closing -> end/shtutdown
void MainDlg::closeEvent(QCloseEvent* event)
{
    {
        // apply a wait cursor, because shutting down the services may take a few seconds (in edge cases)
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const ScopeContext restoreOverrideCursor([]() {
            QApplication::restoreOverrideCursor();
            });

        if (m_systemTray)
        {
            m_systemTray->hide();
        }
        map<uint64_t, DacpServicePtr> mapDacpService;
        DnsHandlePtr dacpBrowser;
        shared_ptr<RaopServer> raopServer;
        unique_ptr<std::thread> scheduler;

        {
            unique_lock<mutex> sync(m_mtx);

            // swap all objects which we want to shutdown
            raopServer.swap(m_raopServer);
            mapDacpService.swap(m_mapDacpService);
            dacpBrowser.swap(m_dacpBrowser);
            scheduler.swap(m_scheduler);

            // notify the scheduler thread that we're about to close
            m_dialogClosed = true;
            m_condDatachange.NotifyAndUnlock(sync);
        }

        if (scheduler)
        {
            if (scheduler->joinable())
            {
                try
                {
                    scheduler->join();
                }
                catch (...)
                {
                    assert(false);
                }
            }
            scheduler.reset();
        }
    
        // terminate the RAOP serivce synchronously during "close event"
        raopServer.reset();

        {
            const lock_guard<mutex> guard(m_mtx);
            m_imageQueue.clear();
            m_timePointShowAlbumArt = 0;
        }

        // shutdown DACP browser
        dacpBrowser.reset();

        // remove all collected DACP infos
        mapDacpService.clear();

        try
        {
            const QByteArray geometry = saveGeometry();

            if (!geometry.isEmpty())
            {
                const auto blob = MakeShared<BlobStream>(geometry.size(), geometry.data());
                VariantValue::Key("WindowGeometry").Set(m_config, blob);
            }
        }
        catch (...)
        {
            spdlog::error("failed to save geometry");
        }
    }
    QWidget::closeEvent(event);
}

// Widget override: Window shows up
void MainDlg::showEvent(QShowEvent* event)
{
    m_isHidden = false;
    spdlog::debug("showEvent");

    if (m_firstShowEvent && VariantValue::Key("StartMinimized").TryGet<bool>(m_config).value_or(false))
    {
        spdlog::info("start minimized");
        setWindowState(Qt::WindowMinimized);
    }
    m_firstShowEvent = false;
    emit UpdateWidgets();
    QWidget::showEvent(event);
}

// Widget override: Window is collapsing
void MainDlg::hideEvent(QHideEvent* event)
{
    m_isHidden = true;
    spdlog::debug("hideEvent");
    QWidget::hideEvent(event);
}

// Keyboard-Hook implementation
void MainDlg::OnKeyPressed(KeyboardHook::Key key) noexcept
{
    try
    {
        switch (key)
        {
        case KeyboardHook::Key::PlayPause:
        {
            SendDacpCommand("playpause"s);
        }
        break;

        case KeyboardHook::Key::MediaNextTrack:
        {
            SendDacpCommand("nextitem"s);
        }
        break;

        case KeyboardHook::Key::MediaPreviousTrack:
        {
            SendDacpCommand("previtem"s);
        }
        break;
        }
    }
    catch (...)
    {
        spdlog::error("OnKeyPressed: failed to send DACP command for key", (int)key);
    }
}

// Widget slot: show about dialog
void MainDlg::OnAbout()
{
    QPointer<QDialog> dlg = new QDialog(this);

    dlg->setWindowTitle(GetString(StringID::DIALOG_ABOUT));

    QPointer<QVBoxLayout> groupLayout = new QVBoxLayout;
    QPointer<QPushButton> closeButton = new QPushButton(GetString(StringID::CLOSE));

    closeButton->setDefault(true);
    closeButton->setSizePolicy(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Fixed);
    dlg->connect(closeButton, &QPushButton::clicked, dlg, &QDialog::close);

    QPointer<QLabel> labelPixmap = new QLabel;

    QPixmap pixmap(":/ShairportQt.ico");
    labelPixmap->setPixmap(pixmap.scaled(64, 64));

    QPointer<QLabel> versionLabel = new QLabel(tr("<p><a href=\"https://github.com/Frank-Friemel/ShairportQt\">ShairportQt</a> 1.0.0.1</p>"));

    dlg->connect(versionLabel, &QLabel::linkActivated, [](QString link)
        {
            const QUrl url(link);
            QDesktopServices::openUrl(url);
        });

    QPointer<QLabel> infoLabel = new QLabel(GetString(StringID::ABOUT_INFO));
    infoLabel->setAlignment(Qt::AlignmentFlag::AlignHCenter);

    groupLayout->addWidget(labelPixmap);
    groupLayout->addWidget(versionLabel);
    groupLayout->addWidget(infoLabel);
    groupLayout->addWidget(closeButton);

    groupLayout->setAlignment(labelPixmap, Qt::AlignmentFlag::AlignHCenter);
    groupLayout->setAlignment(versionLabel, Qt::AlignmentFlag::AlignHCenter);
    groupLayout->setAlignment(infoLabel, Qt::AlignmentFlag::AlignHCenter);
    groupLayout->setAlignment(closeButton, Qt::AlignmentFlag::AlignHCenter);

    QPointer<QGroupBox> group = new QGroupBox;
    group->setLayout(groupLayout);

    QPointer<QVBoxLayout> mainLayout = new QVBoxLayout(dlg);

    mainLayout->addWidget(group);

    dlg->setLayout(mainLayout);
    dlg->exec();
}

// Widget slot: change airport name/password
void MainDlg::OnChangeAirport()
{
    const auto apName           = VariantValue::Key("APname").Get<string>(m_config);
    const auto password         = VariantValue::Key("Password").Get<string>(m_config);
    const auto hasPassword      = VariantValue::Key("HasPassword").Get<bool>(m_config);
    const auto startMinimized   = VariantValue::Key("StartMinimized").Get<bool>(m_config);
    const auto keepSticky       = VariantValue::Key("KeepSticky").TryGet<bool>(m_config).value_or(false);
    const auto trayIcon         = VariantValue::Key("TrayIcon").TryGet<bool>(m_config).value_or(true);

#ifdef Q_OS_WIN
    bool autostart = false;
    {
        string strAutostart;

        if (GetValueFromRegistry(HKEY_CURRENT_USER, "ShairportQt", strAutostart, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"))
        {
            autostart = !strAutostart.empty();
        }
    }
#endif

    QPointer<QDialog> dlg = new QDialog(this);

    dlg->setWindowTitle(GetString(StringID::DIALOG_CHANGE_NAME_PASSWORD));

    QPointer<QFormLayout> formLayout = new QFormLayout;
    QPointer<QDialogButtonBox> buttonBox = new QDialogButtonBox;

    QPointer<QPushButton> okButton = buttonBox->addButton(GetString(StringID::OK), QDialogButtonBox::ButtonRole::AcceptRole);
    buttonBox->addButton(GetString(StringID::CANCEL), QDialogButtonBox::ButtonRole::RejectRole);

    QPointer<QLineEdit> editNameAirport = new QLineEdit;
    formLayout->addRow(new QLabel(GetString(StringID::LABEL_AIRPORT_NAME)), editNameAirport);

    formLayout->addItem(new QSpacerItem(1, 2));

    QPointer<QCheckBox> checkHasPassword = new QCheckBox;
    formLayout->addRow(new QLabel(GetString(StringID::LABEL_AIRPORT_PASSWORD)), checkHasPassword);

    QPointer<QLineEdit> editPasswordAirport = new QLineEdit;
    formLayout->addRow(tr(""), editPasswordAirport);

    formLayout->addItem(new QSpacerItem(1, 2));

    QPointer<QCheckBox> trayIconOption = new QCheckBox(GetString(StringID::LABEL_TRAY_ICON));
    formLayout->addWidget(trayIconOption);

    if (QSystemTrayIcon::isSystemTrayAvailable() && m_systemTray)
    {
        trayIconOption->setCheckState(trayIcon ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
    }
    else
    {
        trayIconOption->setCheckState(Qt::CheckState::Unchecked);
        trayIconOption->setEnabled(false);
    }
    QPointer<QCheckBox> startMinimizedOption = new QCheckBox(GetString(StringID::OPTION_MINIMIZED));
    formLayout->addWidget(startMinimizedOption);
    startMinimizedOption->setCheckState(startMinimized ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);

    QPointer<QCheckBox> keepStickyOption = new QCheckBox(GetString(StringID::LABEL_KEEP_STICKY));
    formLayout->addWidget(keepStickyOption);
    keepStickyOption->setCheckState(keepSticky ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);

#ifdef Q_OS_WIN
    QPointer<QCheckBox> autoStartOption = new QCheckBox(GetString(StringID::OPTION_AUTOSTART));
    formLayout->addWidget(autoStartOption);
    autoStartOption->setCheckState(autostart ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
#endif

    editNameAirport->setText(apName.c_str());
    editPasswordAirport->setText(password.c_str());
    editPasswordAirport->setReadOnly(!hasPassword);
    editPasswordAirport->setToolTip(GetString(StringID::LABEL_AIRPORT_PASSWORD));
    checkHasPassword->setChecked(hasPassword);

    dlg->connect(editPasswordAirport, &QLineEdit::textChanged, [&okButton](QString text)
        {
            okButton->setEnabled(!text.isEmpty());
        });

    dlg->connect(checkHasPassword, 
#if Q_MOC_OUTPUT_REVISION <= 67
        &QCheckBox::stateChanged
#else
        &QCheckBox::checkStateChanged
#endif
        , [&editPasswordAirport, &okButton](int checkState)
        {
            const bool hasPassword = checkState == Qt::CheckState::Checked;

            editPasswordAirport->setReadOnly(!hasPassword);

            if (hasPassword)
            {
                editPasswordAirport->setFocus(Qt::FocusReason::ShortcutFocusReason);
                okButton->setEnabled(!editPasswordAirport->text().isEmpty());
            }
            else
            {
                okButton->setEnabled(true);
            }
        });

    QPointer<QGroupBox> group = new QGroupBox;
    group->setLayout(formLayout);

    dlg->connect(buttonBox, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    dlg->connect(buttonBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    QPointer<QVBoxLayout> mainLayout = new QVBoxLayout(dlg);

    mainLayout->addWidget(group);
    mainLayout->addWidget(buttonBox);

    dlg->setLayout(mainLayout);

    if (QDialog::Accepted == dlg->exec())
    {
        spdlog::debug("OnChangeAirport accepted");

        const auto newApName            = editNameAirport->text().toStdString();
        const auto newPassword          = editPasswordAirport->text().toStdString();
        const auto newHasPassword       = checkHasPassword->checkState() == Qt::CheckState::Checked;
        const auto newStartMinimized    = startMinimizedOption->checkState() == Qt::CheckState::Checked;
        const auto newKeepSticky        = keepStickyOption->checkState() == Qt::CheckState::Checked;
        const auto newTrayIcon          = trayIconOption->checkState() == Qt::CheckState::Checked;

        if (newTrayIcon != trayIcon)
        {
            VariantValue::Key("TrayIcon").Set(m_config, newTrayIcon);
            ConfigureSystemTray();
        }
        if (newKeepSticky != keepSticky)
        {
            VariantValue::Key("KeepSticky").Set(m_config, newKeepSticky);

            if (newKeepSticky)
            {
                setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
            }
            else
            {
                setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
            }
            show();
        }
        if (newStartMinimized != startMinimized)
        {
            VariantValue::Key("StartMinimized").Set(m_config, newStartMinimized);
        }

#ifdef Q_OS_WIN
        const auto newAutostart = autoStartOption->checkState() == Qt::CheckState::Checked;

        if (newAutostart != autostart)
        {
            if (newAutostart)
            {
                char filePath[MAX_PATH]{};
                if (0 == ::GetModuleFileNameA(NULL, filePath, sizeof(filePath)))
                {
                    throw runtime_error("could not get file path");
                }

                const string autostartPath = "\""s + string(filePath) + "\""s;
                if (!PutValueToRegistry(HKEY_CURRENT_USER, "ShairportQt", autostartPath.c_str(), "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"))
                {
                    throw runtime_error("could not add autostart value to registry");
                }
            }
            else
            {
                if (!RemoveValueFromRegistry(HKEY_CURRENT_USER, "ShairportQt", "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"))
                {
                    throw runtime_error("could not remove autostart value from registry");
                }
            }
        }
#endif

        if (newApName != apName || newPassword != password || newHasPassword != hasPassword)
        {
            VariantValue::Key("APname").Set(m_config, newApName);
            VariantValue::Key("Password").Set(m_config, newPassword);
            VariantValue::Key("HasPassword").Set(m_config, newHasPassword);

            if (newApName != apName || newHasPassword != hasPassword || (hasPassword && newPassword != password))
            {
                // restart the RAOP service
                ShowMessage(StringID::RECONFIG_RAOP_SERVICE);
            }
        }
        UpdateWidgets();
    }
    else
    {
        spdlog::debug("OnChangeAirport dismissed");
    }
}

// Widget slot: show options dialog
void MainDlg::OnOptions()
{
    const auto buffering        = VariantValue::Key("StartFill").TryGet<int>(m_config).value_or(START_FILL_MS);
    const auto audioDevice      = VariantValue::Key("AudioDevice").TryGet<string>(m_config).value_or("default"s);
    const auto logToFile        = VariantValue::Key("DebugLogFile").TryGet<bool>(m_config).value_or(false);
    const auto noMediaControl   = VariantValue::Key("NoMediaControl").TryGet<bool>(m_config).value_or(false);

    QPointer<QDialog> dlg = new QDialog(this);

    dlg->setWindowTitle(GetString(StringID::DIALOG_OPTIONS));

    QPointer<QDialogButtonBox> buttonBox = new QDialogButtonBox;

    buttonBox->addButton(GetString(StringID::OK), QDialogButtonBox::ButtonRole::AcceptRole);
    buttonBox->addButton(GetString(StringID::CANCEL), QDialogButtonBox::ButtonRole::RejectRole);

    dlg->connect(buttonBox, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    dlg->connect(buttonBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    QPointer<QSlider> bufferingSlider = new QSlider(Qt::Orientation::Horizontal);

    bufferingSlider->setMinimum(MIN_FILL_MS);
    bufferingSlider->setMaximum(MAX_FILL_MS);
    bufferingSlider->setValue(buffering);

    QPointer<QLabel> bufferingValue = new QLabel;
    
    bufferingValue->setText((to_string(buffering) + " ms"s).c_str());

    dlg->connect(bufferingSlider, &QSlider::valueChanged, [&bufferingValue](int v)
        {
            bufferingValue->setText((to_string(v) + " ms"s).c_str());
        });

    QPointer<QHBoxLayout> bufferingLayout = new QHBoxLayout(dlg);

    bufferingLayout->addWidget(bufferingSlider);
    bufferingLayout->addWidget(bufferingValue);

    QPointer<QGroupBox> bufferingGroup = new QGroupBox(GetString(StringID::LABEL_DATA_BUFFER));
    bufferingGroup->setLayout(bufferingLayout);

    QPointer<QComboBox> soundDeviceDropList = new QComboBox;

    auto soundDevices = AlsaAudio::ListDevices();
    soundDevices["default"s] = GetString(StringID::LABEL_DEFAULT_DEVICE).toStdString();

    int idx = 0;
    int currentIdx = -1;
    int defaultIdx = 0;

    for (const auto& device : soundDevices)
    {
        soundDeviceDropList->addItem(device.second.c_str(), device.first.c_str());

        if (device.first == audioDevice)
        {
            currentIdx = idx;
        }
        if (device.first == "default"s)
        {
            defaultIdx = idx;
        }
        ++idx;
    }
    if (currentIdx < 0)
    {
        currentIdx = defaultIdx;
    }
    soundDeviceDropList->setCurrentIndex(currentIdx);

    QPointer<QHBoxLayout> soundDeviceLayout = new QHBoxLayout(dlg);

    soundDeviceLayout->addWidget(soundDeviceDropList);

    QPointer<QGroupBox> soundDeviceGroup = new QGroupBox(GetString(StringID::LABEL_SOUND_DEVICE));
    soundDeviceGroup->setLayout(soundDeviceLayout);

    QPointer<QCheckBox> logToFileOption = new QCheckBox(GetString(StringID::LABEL_LOG_TO_FILE));
    logToFileOption->setCheckState(logToFile ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);

    QPointer<QCheckBox> mediaControlOption = new QCheckBox(GetString(StringID::LABEL_DISABLE_MM_CONTROL));
    mediaControlOption->setCheckState(noMediaControl ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);

    QPointer<QVBoxLayout> mainLayout = new QVBoxLayout(dlg);

    mainLayout->addWidget(bufferingGroup);
    mainLayout->addWidget(soundDeviceGroup);
    mainLayout->addWidget(logToFileOption);
    mainLayout->addWidget(mediaControlOption);
    mainLayout->addWidget(buttonBox);

    dlg->setLayout(mainLayout);

    if (QDialog::Accepted == dlg->exec())
    {
        const int newBuffering          = bufferingSlider->value();
        const string newAudioDevice     = soundDeviceDropList->currentData().toString().toStdString();
        const bool newLogToFile         = logToFileOption->checkState() == Qt::CheckState::Checked;
        const bool newNoMediaControl    = mediaControlOption->checkState() == Qt::CheckState::Checked;

        if (newNoMediaControl != noMediaControl)
        {
            VariantValue::Key("NoMediaControl").Set(m_config, newNoMediaControl);
            ConfigureDacpBrowser();
        }
        if (newLogToFile != logToFile)
        {
            VariantValue::Key("DebugLogFile").Set(m_config, newLogToFile);
            EnableLogToFile(newLogToFile, LOG_FILE_NAME);
        }
        if (newBuffering != buffering || newAudioDevice != audioDevice)
        {
            VariantValue::Key("StartFill").Set(m_config, newBuffering);
            VariantValue::Key("AudioDevice").Set(m_config, newAudioDevice);

            // restart the RAOP service
            ShowMessage(StringID::RECONFIG_RAOP_SERVICE);
        }
    }
    else
    {
        spdlog::debug("OnOptions dismissed");
    }
}
