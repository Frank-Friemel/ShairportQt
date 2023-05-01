#include <iostream>
#include <memory>
#include <string>
#include <stdexcept>

#include "MainDlg.h"
#include "LayerCake.h"
#include "dnssd.h"
#include "libutils.h"
#include "definitions.h"
#include "localization/StringIDs.h"
#include "localization/LanguageManager.h"
#include "Trim.h"

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>

// this project uses Qt
// https://download.qt.io/official_releases/online_installers/
//
#include <QApplication>
#include <QMessageBox>

using namespace std;
using namespace string_literals;
    
static SharedPtr<IValueCollection> LoadConfig();
static void SaveConfig(const SharedPtr<IValueCollection>& config);
static void InitializeConfig(const SharedPtr<IValueCollection>& config);

// commandline parameters for debugging (Visual Studio):
// .../ShairportQt/.vs/launch.vs.json
// https://stackoverflow.com/questions/30104520/adding-command-line-arguments-to-project
//
int main(int argc, char** argv) 
{
    bool debugLogToFile = false;

    if (argv)
    {
        for (int nArg = 1; nArg < argc; ++nArg)
        {
            if (argv[nArg])
            {
                if (strstr(argv[nArg], "-log"))
                {
                    debugLogToFile = true;
                }
            }
        }
    }
    // set random seed
    srand(static_cast<unsigned int>(time(nullptr)));

    setlocale(LC_ALL, "C");

    int result = -1;
    shared_ptr<spdlog::async_logger> logger;
    SharedPtr<IValueCollection> config;
    unique_ptr<QApplication> app;

    try
    {
        // we have to create the app-object here
        // because QT has to get to know the "main thread"        
        app = make_unique<QApplication>(argc, argv);

        // init logger thread-pool
        spdlog::init_thread_pool(4096, 1);

        // create the logger
        logger = make_shared<spdlog::async_logger>(
            "ShairportQt"s,
            spdlog::sinks_init_list
            {
#ifdef _WIN32
                make_shared<spdlog::sinks::windebug_sink_mt>(),
#endif
                make_shared<spdlog::sinks::stdout_color_sink_mt>()
            },
            spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);

#ifndef NDEBUG
        logger->set_level(spdlog::level::level_enum::debug);
#endif
        // make our logger the default-logger
        spdlog::set_default_logger(logger);

        spdlog::info("Starting ...");
        
        // loading the config
        config = LoadConfig();

        if (!debugLogToFile && VariantValue::Key("DebugLogFile").Has(config))
        {
            // enable "log to file" option, if config says so
            if (VariantValue::Key("DebugLogFile").Get<bool>(config))
            {
                debugLogToFile = true;
            }
        }

        // enable log to file, if enabled
        if (debugLogToFile)
        {
            EnableLogToFile(true, LOG_FILE_NAME);
        }
        // initializing the config
        InitializeConfig(config);

        if (app)
        {
            // create GUI
            MainDlg mainDialog(config);

            // QT best practices:
            // https://de.slideshare.net/slideshow/how-to-make-your-qt-app-look-native/2622616
            mainDialog.show();

            // enter the main loop (which blocks)
            result = app->exec();
        }
        else
        {
            assert(false);
        }

        // saving the config
        SaveConfig(config);

        spdlog::info("Terminating with result {}", result);
    }
    catch (bad_alloc)
    {
        spdlog::error("The system is out of memory");

        // no message box without having QT initialized, sorry
        if (app)
        {
            QMessageBox msgBox;
            msgBox.setText("The system is out of memory.");
            msgBox.addButton("Close", QMessageBox::AcceptRole);
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.exec();
        }
    }
    catch (runtime_error e)
    {
        spdlog::error("Runtime error: {}", e.what());

        // no message box without having QT initialized, sorry
        if (app)
        {
            if (strstr(e.what(), "dnssd"))
            {
                // Bonjour/Avahi is probably lacking
                ShowMessageBox(config, StringID::BONJOUR_INSTALL);
            }
            else
            {
                QMessageBox msgBox;

                msgBox.setText(e.what());
                msgBox.setIcon(QMessageBox::Critical);
                msgBox.exec();
            }
        }
    }
    catch (...)
    {
        spdlog::dump_backtrace();
    }

    // teardown
    app.reset();
    config.Clear();
    logger.reset();
    spdlog::shutdown();

    return result;
}

static string GetConfigPath()
{
    return GetHomeDirectoryA() + GetPathDelimiter() + ".ShairportQt_Config.json"s;
}

static SharedPtr<IValueCollection> LoadConfig()
{
    auto config = MakeShared<ValueCollection>();
    auto configStream = MakeShared<BlobStream>();

    const auto configPath = GetConfigPath();
    spdlog::debug("read config file: {}", configPath);

    if (configStream->FromFile(configPath))
    {
        if (!FromJson(config, configStream))
        {
            spdlog::error("Failed to parse config: {}", Stringify<string>(configStream));
            assert(false);
        }
    }
    else
    {
        const auto error = GetLastError();

        if (error != ERROR_FILE_NOT_FOUND)
        {
            spdlog::error("Failed to load config: {}", error);
            assert(false);
        }
    }
    return config;
}

static void SaveConfig(const SharedPtr<IValueCollection>& config)
{
    auto configStream = MakeShared<BlobStream>();

    if (!ToJson(config, configStream, JsonFormat::humanreadable))
    {
        spdlog::error("Failed to create config");
    }
    else
    {
        const auto configPath = GetConfigPath();
        const auto newConfigfile = configPath + ".new"s;

        if (!configStream->ToFile(newConfigfile))
        {
            spdlog::error("Failed (error: {}) to write new config: {}", GetLastError(), Stringify<string>(configStream));
            DeleteFileA((newConfigfile).c_str());
        }
        else
        {
            if (!DeleteFileA(configPath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                spdlog::error("Failed (error: {}) to delete old config: {}", GetLastError(), configPath);
            }

            if (!MoveFileA((newConfigfile).c_str(), configPath.c_str()))
            {
                spdlog::error("Failed (error: {}) to rename new config: {} to {}", GetLastError(),
                    newConfigfile, configPath);
            }
#ifdef _WIN32
            if (!::SetFileAttributesA(configPath.c_str(), FILE_ATTRIBUTE_HIDDEN))
            {
                spdlog::error("Failed (error: {}) to set config hidden", GetLastError());
            }
#endif
        }
    }
}

static void InitializeConfig(const SharedPtr<IValueCollection>& config)
{
    // create Language Manager
    VariantValue::Key("LanguageManager").Set(config, MakeShared<Localization::LanguageManager>());

    if (VariantValue::Key("Language").Has(config))
    {
        // set language from config (language override)
        const auto language = VariantValue::Key("Language").Get<string>(config);

        GetLanguageManager(config)->SetCurrentLanguage(language);
        spdlog::debug("set language from config: {}", language);
    }
    else
    {
        // test
        // GetLanguageManager(config)->SetCurrentLanguage("en-en"s);
        // GetLanguageManager(config)->SetCurrentLanguage("ja-jp"s);

        // log the system language
        spdlog::debug("current language is: {}", GetLanguageManager(config)->GetCurrentLanguage());
    }

    // create HW Address if not available yet
    if (!VariantValue::Key("HWaddress").Has(config) ||
        VariantValue::Key("HWaddress").Get<vector<uint8_t>>(config).size() != 6)
    {
        auto hwaddr = MakeShared<BlobStream>();

        BYTE b = 0;
        hwaddr->Write(&b, 1, nullptr);

        while (hwaddr->GetSize() != 6)
        {
            b = static_cast<BYTE>(CreateRand(255));
            hwaddr->Write(&b, 1, nullptr);
        }
        VariantValue::Key("HWaddress").Set(config, hwaddr);
    }

    // create AP Name if not available yet
    if (!VariantValue::Key("APname").Has(config) || VariantValue::Key("APname").Get<string>(config).empty())
    {
        char hostname[64] = { 0 };
        DWORD dwHostname = 64;

        if (GetComputerNameA(hostname, &dwHostname) && hostname[0])
        {
            string apName{ hostname, strlen(hostname) };

#ifdef _WIN32
            string legacyApName;
            GetValueFromRegistry(HKEY_CURRENT_USER, "ApName", legacyApName, "Software\\Shairport4w");

            if (CopyToLower(legacyApName) == CopyToLower(apName))
            {
                apName += "Qt"s;
            }
#endif
            VariantValue::Key("APname").Set(config, apName);
        }
        else
        {
            VariantValue::Key("APname").Set(config, "ShairportQt"s);
        }
    }

    // initialize fill buffer threshold [ms]
    if (!VariantValue::Key("StartFill").Has(config) ||
        (VariantValue::Key("StartFill").Get<int>(config) < MIN_FILL_MS ||
            VariantValue::Key("StartFill").Get<int>(config) > MAX_FILL_MS
            )
        )
    {
        // 500 ms
        VariantValue::Key("StartFill").Set(config, START_FILL_MS);
    }

    // Low Level for RTP Queue
    if (!VariantValue::Key("LowLevelRTP").Has(config) ||
        VariantValue::Key("LowLevelRTP").Get<int>(config) < LOW_LEVEL_RTP_QUEUE)
    {
        VariantValue::Key("LowLevelRTP").Set(config, LOW_LEVEL_RTP_QUEUE);
    }

    // Level offset for RTP Queue
    if (!VariantValue::Key("LevelOffsetRTP").Has(config) ||
        VariantValue::Key("LevelOffsetRTP").Get<int>(config) < MIN_RTP_LEVEL_OFFSET)
    {
        VariantValue::Key("LevelOffsetRTP").Set(config, MIN_RTP_LEVEL_OFFSET);
    }

    // create volume value if necessary
    // limits are 0db and -144db (factored 1000)
    if (!VariantValue::Key("Volume").Has(config) ||
        (VariantValue::Key("Volume").Get<double>(config) > (MAX_DB_VOLUME * 1000) ||
            VariantValue::Key("Volume").Get<double>(config) < (MIN_DB_VOLUME * 1000)
            )
        )
    {
        // 0 db
        VariantValue::Key("Volume").Set(config, 0);
    }
}