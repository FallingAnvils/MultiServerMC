#include "MultiServerMC.h"
#include "BuildConfig.h"
#include "MainWindow.h"
#include "InstanceWindow.h"

#include "groupview/AccessibleGroupView.h"
#include <QAccessible>

#include "pages/BasePageProvider.h"
#include "pages/global/MultiServerMCPage.h"
#include "pages/global/MinecraftPage.h"
#include "pages/global/JavaPage.h"
#include "pages/global/LanguagePage.h"
#include "pages/global/ProxyPage.h"
#include "pages/global/ExternalToolsPage.h"
#include "pages/global/PasteEEPage.h"
#include "pages/global/CustomCommandsPage.h"

#include "themes/ITheme.h"
#include "themes/SystemTheme.h"

#include "setupwizard/SetupWizard.h"
#include "setupwizard/LanguageWizardPage.h"
#include "setupwizard/JavaWizardPage.h"

#include <iostream>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QTranslator>
#include <QLibraryInfo>
#include <QList>
#include <QStringList>
#include <QDebug>
#include <QStyleFactory>

#include "dialogs/CustomMessageBox.h"
#include "InstanceList.h"

#include "icons/IconList.h"
#include "net/HttpMetaCache.h"
#include "Env.h"

#include "java/JavaUtils.h"

#include "updater/UpdateChecker.h"

#include "tools/JProfiler.h"
#include "tools/JVisualVM.h"
#include "tools/MCEditTool.h"

#include <xdgicon.h>
#include "settings/INISettingsObject.h"
#include "settings/Setting.h"

#include "translations/TranslationsModel.h"

#include <Commandline.h>
#include <FileSystem.h>
#include <DesktopServices.h>
#include <LocalPeer.h>

#include <sys.h>

#include "pagedialog/PageDialog.h"


#if defined Q_OS_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

static const QLatin1String liveCheckFile("live.check");

using namespace Commandline;

#define MACOS_HINT "If you are on macOS Sierra, you might have to move MultiServerMC.app to your /Applications or ~/Applications folder. "\
    "This usually fixes the problem and you can move the application elsewhere afterwards.\n"\
    "\n"

static void appDebugOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const char *levels = "DWCFIS";
    const QString format("%1 %2 %3\n");

    qint64 msecstotal = MSMC->timeSinceStart();
    qint64 seconds = msecstotal / 1000;
    qint64 msecs = msecstotal % 1000;
    QString foo;
    char buf[1025] = {0};
    ::snprintf(buf, 1024, "%5lld.%03lld", seconds, msecs);

    QString out = format.arg(buf).arg(levels[type]).arg(msg);

    MSMC->logFile->write(out.toUtf8());
    MSMC->logFile->flush();
    QTextStream(stderr) << out.toLocal8Bit();
    fflush(stderr);
}

MultiServerMC::MultiServerMC(int &argc, char **argv) : QApplication(argc, argv)
{
#if defined Q_OS_WIN32
    // attach the parent console
    if(AttachConsole(ATTACH_PARENT_PROCESS))
    {
        // if attach succeeds, reopen and sync all the i/o
        if(freopen("CON", "w", stdout))
        {
            std::cout.sync_with_stdio();
        }
        if(freopen("CON", "w", stderr))
        {
            std::cerr.sync_with_stdio();
        }
        if(freopen("CON", "r", stdin))
        {
            std::cin.sync_with_stdio();
        }
        auto out = GetStdHandle (STD_OUTPUT_HANDLE);
        DWORD written;
        const char * endline = "\n";
        WriteConsole(out, endline, strlen(endline), &written, NULL);
        consoleAttached = true;
    }
#endif
    setOrganizationName("MultiServerMC");
    setOrganizationDomain("multimc.org");
    setApplicationName("MultiServerMC5");
    setApplicationDisplayName("MultiServerMC 5");
    setApplicationVersion(BuildConfig.printableVersionString());

    startTime = QDateTime::currentDateTime();

#ifdef Q_OS_LINUX
    {
        QFile osrelease("/proc/sys/kernel/osrelease");
        if (osrelease.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream in(&osrelease);
            auto contents = in.readAll();
            if(
                contents.contains("WSL", Qt::CaseInsensitive) ||
                contents.contains("Microsoft", Qt::CaseInsensitive)
            ) {
                showFatalErrorMessage(
                    "Unsupported system detected!",
                    "Linux-on-Windows distributions are not supported.\n\n"
                    "Please use the Windows MultiServerMC binary when playing on Windows."
                );
                return;
            }
        }
    }
#endif

    // Don't quit on hiding the last window
    this->setQuitOnLastWindowClosed(false);

    // Commandline parsing
    QHash<QString, QVariant> args;
    {
        Parser parser(FlagStyle::GNU, ArgumentStyle::SpaceAndEquals);

        // --help
        parser.addSwitch("help");
        parser.addShortOpt("help", 'h');
        parser.addDocumentation("help", "Display this help and exit.");
        // --version
        parser.addSwitch("version");
        parser.addShortOpt("version", 'V');
        parser.addDocumentation("version", "Display program version and exit.");
        // --dir
        parser.addOption("dir");
        parser.addShortOpt("dir", 'd');
        parser.addDocumentation("dir", "Use the supplied folder as MultiServerMC root instead of "
                                       "the binary location (use '.' for current)");
        // --launch
        parser.addOption("launch");
        parser.addShortOpt("launch", 'l');
        parser.addDocumentation("launch", "Launch the specified instance (by instance ID)");
        // --port
        parser.addOption("port");
        parser.addShortOpt("port", 'p');
        parser.addDocumentation("port", "Set the specified server port "
                                          "(only valid in combination with --launch)");
        // --alive
        parser.addSwitch("alive");
        parser.addDocumentation("alive", "Write a small '" + liveCheckFile + "' file after MultiServerMC starts");
        // --import
        parser.addOption("import");
        parser.addShortOpt("import", 'I');
        parser.addDocumentation("import", "Import instance from specified zip (local path or URL)");

        // parse the arguments
        try
        {
            args = parser.parse(arguments());
        }
        catch (const ParsingError &e)
        {
            std::cerr << "CommandLineError: " << e.what() << std::endl;
            if(argc > 0)
                std::cerr << "Try '" << argv[0] << " -h' to get help on MultiServerMC's command line parameters."
                          << std::endl;
            m_status = MultiServerMC::Failed;
            return;
        }

        // display help and exit
        if (args["help"].toBool())
        {
            std::cout << qPrintable(parser.compileHelp(arguments()[0]));
            m_status = MultiServerMC::Succeeded;
            return;
        }

        // display version and exit
        if (args["version"].toBool())
        {
            std::cout << "Version " << BuildConfig.printableVersionString().toStdString() << std::endl;
            std::cout << "Git " << BuildConfig.GIT_COMMIT.toStdString() << std::endl;
            m_status = MultiServerMC::Succeeded;
            return;
        }
    }
    m_instanceIdToLaunch = args["launch"].toString();
    m_serverPort = args["port"].toInt();
    m_liveCheck = args["alive"].toBool();
    m_zipToImport = args["import"].toUrl();

    QString origcwdPath = QDir::currentPath();
    QString binPath = applicationDirPath();
    QString adjustedBy;
    QString dataPath;
    // change folder
    QString dirParam = args["dir"].toString();
    if (!dirParam.isEmpty())
    {
        // the dir param. it makes multiservermc data path point to whatever the user specified
        // on command line
        adjustedBy += "Command line " + dirParam;
        dataPath = dirParam;
    }
    else
    {
#ifdef MULTISERVERMC_LINUX_DATADIR
        QString xdgDataHome = QFile::decodeName(qgetenv("XDG_DATA_HOME"));
        if (xdgDataHome.isEmpty())
            xdgDataHome = QDir::homePath() + QLatin1String("/.local/share");
        dataPath = xdgDataHome + "/multiservermc";
        adjustedBy += "XDG standard " + dataPath;
#else
        dataPath = applicationDirPath();
        adjustedBy += "Fallback to binary path " + dataPath;
#endif
    }

    if (!FS::ensureFolderPathExists(dataPath))
    {
        showFatalErrorMessage(
            "MultiServerMC data folder could not be created.",
            "MultiServerMC data folder could not be created.\n"
            "\n"
#if defined(Q_OS_MAC)
            MACOS_HINT
#endif
            "Make sure you have the right permissions to the MultiServerMC data folder and any folder needed to access it.\n"
            "\n"
            "MultiServerMC cannot continue until you fix this problem."
        );
        return;
    }
    if (!QDir::setCurrent(dataPath))
    {
        showFatalErrorMessage(
            "MultiServerMC data folder could not be opened.",
            "MultiServerMC data folder could not be opened.\n"
            "\n"
#if defined(Q_OS_MAC)
            MACOS_HINT
#endif
            "Make sure you have the right permissions to the MultiServerMC data folder.\n"
            "\n"
            "MultiServerMC cannot continue until you fix this problem."
        );
        return;
    }

    if(m_instanceIdToLaunch.isEmpty() && m_serverPort)
    {
        std::cerr << "--port can only be used in combination with --launch!" << std::endl;
        m_status = MultiServerMC::Failed;
        return;
    }

    /*
     * Establish the mechanism for communication with an already running MultiServerMC that uses the same data path.
     * If there is one, tell it what the user actually wanted to do and exit.
     * We want to initialize this before logging to avoid messing with the log of a potential already running copy.
     */
    auto appID = ApplicationId::fromPathAndVersion(QDir::currentPath(), BuildConfig.printableVersionString());
    {
        // FIXME: you can run the same binaries with multiple data dirs and they won't clash. This could cause issues for updates.
        m_peerInstance = new LocalPeer(this, appID);
        connect(m_peerInstance, &LocalPeer::messageReceived, this, &MultiServerMC::messageReceived);
        if(m_peerInstance->isClient())
        {
            int timeout = 2000;

            if(m_instanceIdToLaunch.isEmpty())
            {
                m_peerInstance->sendMessage("activate", timeout);

                if(!m_zipToImport.isEmpty())
                {
                    m_peerInstance->sendMessage("import " + m_zipToImport.toString(), timeout);
                }
            }
            else
            {
                if(!m_serverPort)
                {
                    m_peerInstance->sendMessage(
                            "launch-with-port " + m_instanceIdToLaunch + " " + m_serverPort, timeout);
                }
                else
                {
                    m_peerInstance->sendMessage("launch " + m_instanceIdToLaunch, timeout);
                }
            }
            m_status = MultiServerMC::Succeeded;
            return;
        }
    }

    // init the logger
    {
        static const QString logBase = "MultiServerMC-%0.log";
        auto moveFile = [](const QString &oldName, const QString &newName)
        {
            QFile::remove(newName);
            QFile::copy(oldName, newName);
            QFile::remove(oldName);
        };

        moveFile(logBase.arg(3), logBase.arg(4));
        moveFile(logBase.arg(2), logBase.arg(3));
        moveFile(logBase.arg(1), logBase.arg(2));
        moveFile(logBase.arg(0), logBase.arg(1));

        logFile = std::unique_ptr<QFile>(new QFile(logBase.arg(0)));
        if(!logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            showFatalErrorMessage(
                "MultiServerMC data folder is not writable!",
                "MultiServerMC couldn't create a log file - the MultiServerMC data folder is not writable.\n"
                "\n"
    #if defined(Q_OS_MAC)
                MACOS_HINT
    #endif
                "Make sure you have write permissions to the MultiServerMC data folder.\n"
                "\n"
                "MultiServerMC cannot continue until you fix this problem."
            );
            return;
        }
        qInstallMessageHandler(appDebugOutput);
        qDebug() << "<> Log initialized.";
    }

    // Set up paths
    {
        // Root path is used for updates.
#ifdef Q_OS_LINUX
        QDir foo(FS::PathCombine(binPath, ".."));
        m_rootPath = foo.absolutePath();
#elif defined(Q_OS_WIN32)
        m_rootPath = binPath;
#elif defined(Q_OS_MAC)
        QDir foo(FS::PathCombine(binPath, "../.."));
        m_rootPath = foo.absolutePath();
        // on macOS, touch the root to force Finder to reload the .app metadata (and fix any icon change issues)
        FS::updateTimestamp(m_rootPath);
#endif

#ifdef MULTISERVERMC_JARS_LOCATION
        ENV.setJarsPath( TOSTRING(MULTISERVERMC_JARS_LOCATION) );
#endif

        qDebug() << "MultiServerMC 5, (c) 2013-2021 MultiServerMC Contributors";
        qDebug() << "Version                    : " << BuildConfig.printableVersionString();
        qDebug() << "Git commit                 : " << BuildConfig.GIT_COMMIT;
        qDebug() << "Git refspec                : " << BuildConfig.GIT_REFSPEC;
        if (adjustedBy.size())
        {
            qDebug() << "Work dir before adjustment : " << origcwdPath;
            qDebug() << "Work dir after adjustment  : " << QDir::currentPath();
            qDebug() << "Adjusted by                : " << adjustedBy;
        }
        else
        {
            qDebug() << "Work dir                   : " << QDir::currentPath();
        }
        qDebug() << "Binary path                : " << binPath;
        qDebug() << "Application root path      : " << m_rootPath;
        if(!m_instanceIdToLaunch.isEmpty())
        {
            qDebug() << "ID of instance to launch   : " << m_instanceIdToLaunch;
        }
        if(m_serverPort)
        {
            qDebug() << "Port of server  :" << m_serverPort;
        }
        qDebug() << "<> Paths set.";
    }

    do // once
    {
        if(m_liveCheck)
        {
            QFile check(liveCheckFile);
            if(!check.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                qWarning() << "Could not open" << liveCheckFile << "for writing!";
                break;
            }
            auto payload = appID.toString().toUtf8();
            if(check.write(payload) != payload.size())
            {
                qWarning() << "Could not write into" << liveCheckFile << "!";
                check.remove();
                break;
            }
            check.close();
        }
    } while(false);

    // Initialize application settings
    {
        m_settings.reset(new INISettingsObject("multiservermc.cfg", this));
        // Updates
        m_settings->registerSetting("UpdateChannel", BuildConfig.VERSION_CHANNEL);
        m_settings->registerSetting("AutoUpdate", true);

        // Theming
        m_settings->registerSetting("IconTheme", QString("multiservermc"));
        m_settings->registerSetting("ApplicationTheme", QString("system"));

        // Notifications
        m_settings->registerSetting("ShownNotifications", QString());

        // Remembered state
        m_settings->registerSetting("LastUsedGroupForNewInstance", QString());

        QString defaultMonospace;
        int defaultSize = 11;
#ifdef Q_OS_WIN32
        defaultMonospace = "Courier";
        defaultSize = 10;
#elif defined(Q_OS_MAC)
        defaultMonospace = "Menlo";
#else
        defaultMonospace = "Monospace";
#endif

        // resolve the font so the default actually matches
        QFont consoleFont;
        consoleFont.setFamily(defaultMonospace);
        consoleFont.setStyleHint(QFont::Monospace);
        consoleFont.setFixedPitch(true);
        QFontInfo consoleFontInfo(consoleFont);
        QString resolvedDefaultMonospace = consoleFontInfo.family();
        QFont resolvedFont(resolvedDefaultMonospace);
        qDebug() << "Detected default console font:" << resolvedDefaultMonospace
            << ", substitutions:" << resolvedFont.substitutions().join(',');

        m_settings->registerSetting("ConsoleFont", resolvedDefaultMonospace);
        m_settings->registerSetting("ConsoleFontSize", defaultSize);
        m_settings->registerSetting("ConsoleMaxLines", 100000);
        m_settings->registerSetting("ConsoleOverflowStop", true);

        // Folders
        m_settings->registerSetting("InstanceDir", "instances");
        m_settings->registerSetting({"CentralModsDir", "ModsDir"}, "mods");
        m_settings->registerSetting("IconsDir", "icons");

        // Editors
        m_settings->registerSetting("JsonEditor", QString());

        // Language
        m_settings->registerSetting("Language", QString());

        // Console
        m_settings->registerSetting("ShowConsole", false);
        m_settings->registerSetting("AutoCloseConsole", false);
        m_settings->registerSetting("ShowConsoleOnError", true);
        m_settings->registerSetting("LogPrePostOutput", true);

        // Window Size
        m_settings->registerSetting({"LaunchMaximized", "MCWindowMaximize"}, false);
        m_settings->registerSetting({"MinecraftWinWidth", "MCWindowWidth"}, 854);
        m_settings->registerSetting({"MinecraftWinHeight", "MCWindowHeight"}, 480);

        // Proxy Settings
        m_settings->registerSetting("ProxyType", "None");
        m_settings->registerSetting({"ProxyAddr", "ProxyHostName"}, "127.0.0.1");
        m_settings->registerSetting("ProxyPort", 8080);
        m_settings->registerSetting({"ProxyUser", "ProxyUsername"}, "");
        m_settings->registerSetting({"ProxyPass", "ProxyPassword"}, "");

        // Memory
        m_settings->registerSetting({"MinMemAlloc", "MinMemoryAlloc"}, 512);
        m_settings->registerSetting({"MaxMemAlloc", "MaxMemoryAlloc"}, 1024);
        m_settings->registerSetting("PermGen", 128);

        // Java Settings
        m_settings->registerSetting("JavaPath", "");
        m_settings->registerSetting("JavaTimestamp", 0);
        m_settings->registerSetting("JavaArchitecture", "");
        m_settings->registerSetting("JavaVersion", "");
        m_settings->registerSetting("JavaVendor", "");
        m_settings->registerSetting("LastHostname", "");
        m_settings->registerSetting("JvmArgs", "");

        // Native library workarounds
        m_settings->registerSetting("UseNativeOpenAL", false);
        m_settings->registerSetting("UseNativeGLFW", false);

        // Game time
        m_settings->registerSetting("ShowGameTime", true);
        m_settings->registerSetting("RecordGameTime", true);

        // Minecraft launch method
        m_settings->registerSetting("MCLaunchMethod", "LauncherPart");

        // Wrapper command for launch
        m_settings->registerSetting("WrapperCommand", "");

        // Custom Commands
        m_settings->registerSetting({"PreLaunchCommand", "PreLaunchCmd"}, "");
        m_settings->registerSetting({"PostExitCommand", "PostExitCmd"}, "");

        m_settings->registerSetting("InstSortMode", "Name");
        m_settings->registerSetting("SelectedInstance", QString());

        // Window state and geometry
        m_settings->registerSetting("MainWindowState", "");
        m_settings->registerSetting("MainWindowGeometry", "");

        m_settings->registerSetting("ConsoleWindowState", "");
        m_settings->registerSetting("ConsoleWindowGeometry", "");

        m_settings->registerSetting("SettingsGeometry", "");

        m_settings->registerSetting("PagedGeometry", "");

        m_settings->registerSetting("NewInstanceGeometry", "");

        m_settings->registerSetting("UpdateDialogGeometry", "");

        // paste.ee API key
        m_settings->registerSetting("PasteEEAPIKey", "multiservermc");

        // Init page provider
        {
            m_globalSettingsProvider = std::make_shared<GenericPageProvider>(tr("Settings"));
            m_globalSettingsProvider->addPage<MultiServerMCPage>();
            m_globalSettingsProvider->addPage<MinecraftPage>();
            m_globalSettingsProvider->addPage<JavaPage>();
            m_globalSettingsProvider->addPage<LanguagePage>();
            m_globalSettingsProvider->addPage<CustomCommandsPage>();
            m_globalSettingsProvider->addPage<ProxyPage>();
            m_globalSettingsProvider->addPage<ExternalToolsPage>();
            m_globalSettingsProvider->addPage<PasteEEPage>();
        }
        qDebug() << "<> Settings loaded.";
    }

#ifndef QT_NO_ACCESSIBILITY
    QAccessible::installFactory(groupViewAccessibleFactory);
#endif /* !QT_NO_ACCESSIBILITY */

    // load translations
    {
        m_translations.reset(new TranslationsModel("translations"));
        auto bcp47Name = m_settings->get("Language").toString();
        m_translations->selectLanguage(bcp47Name);
        qDebug() << "Your language is" << bcp47Name;
        qDebug() << "<> Translations loaded.";
    }

    // initialize the updater
    if(BuildConfig.UPDATER_ENABLED)
    {
        m_updateChecker.reset(new UpdateChecker(BuildConfig.CHANLIST_URL, BuildConfig.VERSION_CHANNEL, BuildConfig.VERSION_BUILD));
        qDebug() << "<> Updater started.";
    }

    // Instance icons
    {
        auto setting = MSMC->settings()->getSetting("IconsDir");
        QStringList instFolders =
        {
            ":/icons/multiservermc/32x32/instances/",
            ":/icons/multiservermc/50x50/instances/",
            ":/icons/multiservermc/128x128/instances/",
            ":/icons/multiservermc/scalable/instances/"
        };
        m_icons.reset(new IconList(instFolders, setting->get().toString()));
        connect(setting.get(), &Setting::SettingChanged,[&](const Setting &, QVariant value)
        {
            m_icons->directoryChanged(value.toString());
        });
        ENV.registerIconList(m_icons);
        qDebug() << "<> Instance icons intialized.";
    }

    // Icon themes
    {
        // TODO: icon themes and instance icons do not mesh well together. Rearrange and fix discrepancies!
        // set icon theme search path!
        auto searchPaths = QIcon::themeSearchPaths();
        searchPaths.append("iconthemes");
        QIcon::setThemeSearchPaths(searchPaths);
        qDebug() << "<> Icon themes initialized.";
    }

    // Initialize widget themes
    {
        auto insertTheme = [this](ITheme * theme)
        {
            m_themes.insert(std::make_pair(theme->id(), std::unique_ptr<ITheme>(theme)));
        };
        insertTheme(new SystemTheme());
        qDebug() << "<> Widget themes initialized.";
    }

    // initialize and load all instances
    {
        auto InstDirSetting = m_settings->getSetting("InstanceDir");
        // instance path: check for problems with '!' in instance path and warn the user in the log
        // and remember that we have to show him a dialog when the gui starts (if it does so)
        QString instDir = InstDirSetting->get().toString();
        qDebug() << "Instance path              : " << instDir;
        if (FS::checkProblemticPathJava(QDir(instDir)))
        {
            qWarning() << "Your instance path contains \'!\' and this is known to cause java problems!";
        }
        m_instances.reset(new InstanceList(m_settings, instDir, this));
        connect(InstDirSetting.get(), &Setting::SettingChanged, m_instances.get(), &InstanceList::on_InstFolderChanged);
        qDebug() << "Loading Instances...";
        m_instances->loadList();
        qDebug() << "<> Instances loaded.";
    }

    // init the http meta cache
    {
        ENV.initHttpMetaCache();
        qDebug() << "<> Cache initialized.";
    }

    // init proxy settings
    {
        QString proxyTypeStr = settings()->get("ProxyType").toString();
        QString addr = settings()->get("ProxyAddr").toString();
        int port = settings()->get("ProxyPort").value<qint16>();
        QString user = settings()->get("ProxyUser").toString();
        QString pass = settings()->get("ProxyPass").toString();
        ENV.updateProxySettings(proxyTypeStr, addr, port, user, pass);
        qDebug() << "<> Proxy settings done.";
    }

    // now we have network, download translation updates
    m_translations->downloadIndex();

    //FIXME: what to do with these?
    m_profilers.insert("jprofiler", std::shared_ptr<BaseProfilerFactory>(new JProfilerFactory()));
    m_profilers.insert("jvisualvm", std::shared_ptr<BaseProfilerFactory>(new JVisualVMFactory()));
    for (auto profiler : m_profilers.values())
    {
        profiler->registerSettings(m_settings);
    }

    // Create the MCEdit thing... why is this here?
    {
        m_mcedit.reset(new MCEditTool(m_settings));
    }

    connect(this, &MultiServerMC::aboutToQuit, [this](){
        if(m_instances)
        {
            // save any remaining instance state
            m_instances->saveNow();
        }
        if(logFile)
        {
            logFile->flush();
            logFile->close();
        }
    });

    {
        setIconTheme(settings()->get("IconTheme").toString());
        qDebug() << "<> Icon theme set.";
        setApplicationTheme(settings()->get("ApplicationTheme").toString(), true);
        qDebug() << "<> Application theme set.";
    }

    if(createSetupWizard())
    {
        return;
    }
    performMainStartupAction();
}

bool MultiServerMC::createSetupWizard()
{
    bool javaRequired = [&]()
    {
        QString currentHostName = QHostInfo::localHostName();
        QString oldHostName = settings()->get("LastHostname").toString();
        if (currentHostName != oldHostName)
        {
            settings()->set("LastHostname", currentHostName);
            return true;
        }
        QString currentJavaPath = settings()->get("JavaPath").toString();
        QString actualPath = FS::ResolveExecutable(currentJavaPath);
        if (actualPath.isNull())
        {
            return true;
        }
        return false;
    }();
    bool languageRequired = [&]()
    {
        if (settings()->get("Language").toString().isEmpty())
            return true;
        return false;
    }();
    bool wizardRequired = javaRequired || languageRequired;

    if(wizardRequired)
    {
        m_setupWizard = new SetupWizard(nullptr);
        if (languageRequired)
        {
            m_setupWizard->addPage(new LanguageWizardPage(m_setupWizard));
        }
        if (javaRequired)
        {
            m_setupWizard->addPage(new JavaWizardPage(m_setupWizard));
        }
        connect(m_setupWizard, &QDialog::finished, this, &MultiServerMC::setupWizardFinished);
        m_setupWizard->show();
        return true;
    }
    return false;
}

void MultiServerMC::setupWizardFinished(int status)
{
    qDebug() << "Wizard result =" << status;
    performMainStartupAction();
}

void MultiServerMC::performMainStartupAction()
{
    m_status = MultiServerMC::Initialized;
    if(!m_instanceIdToLaunch.isEmpty())
    {
        auto inst = instances()->getInstanceById(m_instanceIdToLaunch);
        if(inst)
        {
            if(m_serverPort)
            {
                qDebug() << "<> Instance" << m_instanceIdToLaunch << "launching with port" << m_serverPort;
            }
            else
            {
                qDebug() << "<> Instance" << m_instanceIdToLaunch << "launching";
            }

            launch(inst, true, nullptr, m_serverPort);
            return;
        }
    }
    if(!m_mainWindow)
    {
        // normal main window
        showMainWindow(false);
        qDebug() << "<> Main window shown.";
    }
    if(!m_zipToImport.isEmpty())
    {
        qDebug() << "<> Importing instance from zip:" << m_zipToImport;
        m_mainWindow->droppedURLs({ m_zipToImport });
    }
}

void MultiServerMC::showFatalErrorMessage(const QString& title, const QString& content)
{
    m_status = MultiServerMC::Failed;
    auto dialog = CustomMessageBox::selectable(nullptr, title, content, QMessageBox::Critical);
    dialog->exec();
}

MultiServerMC::~MultiServerMC()
{
    // kill the other globals.
    Env::dispose();

    // Shut down logger by setting the logger function to nothing
    qInstallMessageHandler(nullptr);

#if defined Q_OS_WIN32
    // Detach from Windows console
    if(consoleAttached)
    {
        fclose(stdout);
        fclose(stdin);
        fclose(stderr);
        FreeConsole();
    }
#endif
}

void MultiServerMC::messageReceived(const QString& message)
{
    if(status() != Initialized)
    {
        qDebug() << "Received message" << message << "while still initializing. It will be ignored.";
        return;
    }

    QString command = message.section(' ', 0, 0);

    if(command == "activate")
    {
        showMainWindow();
    }
    else if(command == "import")
    {
        QString arg = message.section(' ', 1);
        if(arg.isEmpty())
        {
            qWarning() << "Received" << command << "message without a zip path/URL.";
            return;
        }
        m_mainWindow->droppedURLs({ QUrl(arg) });
    }
    else if(command == "launch")
    {
        QString arg = message.section(' ', 1);
        if(arg.isEmpty())
        {
            qWarning() << "Received" << command << "message without an instance ID.";
            return;
        }
        auto inst = instances()->getInstanceById(arg);
        if(inst)
        {
            launch(inst, true, nullptr);
        }
    }
    else if(command == "launch-with-port")
    {
        QString instanceID = message.section(' ', 1, 1);
        int serverPort = message.section(' ', 2, 2).toInt();
        if(instanceID.isEmpty())
        {
            qWarning() << "Received" << command << "message without an instance ID.";
            return;
        }
        if(!serverPort)
        {
            qWarning() << "Received" << command << "message without a server port number.";
            return;
        }
        auto inst = instances()->getInstanceById(instanceID);
        if(inst)
        {
            launch(
                    inst,
                    true,
                    nullptr,
                    serverPort
            );
        }
    }
    else
    {
        qWarning() << "Received invalid message" << message;
    }
}

std::shared_ptr<TranslationsModel> MultiServerMC::translations()
{
    return m_translations;
}

std::shared_ptr<JavaInstallList> MultiServerMC::javalist()
{
    if (!m_javalist)
    {
        m_javalist.reset(new JavaInstallList());
    }
    return m_javalist;
}

std::vector<ITheme *> MultiServerMC::getValidApplicationThemes()
{
    std::vector<ITheme *> ret;
    auto iter = m_themes.cbegin();
    while (iter != m_themes.cend())
    {
        ret.push_back((*iter).second.get());
        iter++;
    }
    return ret;
}

void MultiServerMC::setApplicationTheme(const QString& name, bool initial)
{
    auto systemPalette = qApp->palette();
    auto themeIter = m_themes.find(name);
    if(themeIter != m_themes.end())
    {
        auto & theme = (*themeIter).second;
        theme->apply(initial);
    }
    else
    {
        qWarning() << "Tried to set invalid theme:" << name;
    }
}

void MultiServerMC::setIconTheme(const QString& name)
{
    XdgIcon::setThemeName(name);
}

QIcon MultiServerMC::getThemedIcon(const QString& name)
{
    return XdgIcon::fromTheme(name);
}

bool MultiServerMC::openJsonEditor(const QString &filename)
{
    const QString file = QDir::current().absoluteFilePath(filename);
    if (m_settings->get("JsonEditor").toString().isEmpty())
    {
        return DesktopServices::openUrl(QUrl::fromLocalFile(file));
    }
    else
    {
        //return DesktopServices::openFile(m_settings->get("JsonEditor").toString(), file);
        return DesktopServices::run(m_settings->get("JsonEditor").toString(), {file});
    }
}

bool MultiServerMC::launch(
        InstancePtr instance,
        bool online,
        BaseProfilerFactory *profiler,
        int serverPort
) {
    if(m_updateRunning)
    {
        qDebug() << "Cannot launch instances while an update is running. Please try again when updates are completed.";
    }
    else if(instance->canLaunch())
    {
        auto & extras = m_instanceExtras[instance->id()];
        auto & window = extras.window;
        if(window)
        {
            if(!window->saveAll())
            {
                return false;
            }
        }
        auto & controller = extras.controller;
        controller.reset(new LaunchController());
        controller->setInstance(instance);
        controller->setOnline(online);
        controller->setProfiler(profiler);
        controller->setServerPort(serverPort);
        if(window)
        {
            controller->setParentWidget(window);
        }
        else if(m_mainWindow)
        {
            controller->setParentWidget(m_mainWindow);
        }
        connect(controller.get(), &LaunchController::succeeded, this, &MultiServerMC::controllerSucceeded);
        connect(controller.get(), &LaunchController::failed, this, &MultiServerMC::controllerFailed);
        addRunningInstance();
        controller->start();
        return true;
    }
    else if (instance->isRunning())
    {
        showInstanceWindow(instance, "console");
        return true;
    }
    else if (instance->canEdit())
    {
        showInstanceWindow(instance);
        return true;
    }
    return false;
}

bool MultiServerMC::kill(InstancePtr instance)
{
    if (!instance->isRunning())
    {
        qWarning() << "Attempted to kill instance" << instance->id() << ", which isn't running.";
        return false;
    }
    auto & extras = m_instanceExtras[instance->id()];
    // NOTE: copy of the shared pointer keeps it alive
    auto controller = extras.controller;
    if(controller)
    {
        return controller->abort();
    }
    return true;
}

void MultiServerMC::addRunningInstance()
{
    m_runningInstances ++;
    if(m_runningInstances == 1)
    {
        emit updateAllowedChanged(false);
    }
}

void MultiServerMC::subRunningInstance()
{
    if(m_runningInstances == 0)
    {
        qCritical() << "Something went really wrong and we now have less than 0 running instances... WTF";
        return;
    }
    m_runningInstances --;
    if(m_runningInstances == 0)
    {
        emit updateAllowedChanged(true);
    }
}

bool MultiServerMC::shouldExitNow() const
{
    return m_runningInstances == 0 && m_openWindows == 0;
}

bool MultiServerMC::updatesAreAllowed()
{
    return m_runningInstances == 0;
}

void MultiServerMC::updateIsRunning(bool running)
{
    m_updateRunning = running;
}


void MultiServerMC::controllerSucceeded()
{
    auto controller = qobject_cast<LaunchController *>(QObject::sender());
    if(!controller)
        return;
    auto id = controller->id();
    auto & extras = m_instanceExtras[id];

    // on success, do...
    if (controller->instance()->settings()->get("AutoCloseConsole").toBool())
    {
        if(extras.window)
        {
            extras.window->close();
        }
    }
    extras.controller.reset();
    subRunningInstance();

    // quit when there are no more windows.
    if(shouldExitNow())
    {
        m_status = Status::Succeeded;
        exit(0);
    }
}

void MultiServerMC::controllerFailed(const QString& error)
{
    Q_UNUSED(error);
    auto controller = qobject_cast<LaunchController *>(QObject::sender());
    if(!controller)
        return;
    auto id = controller->id();
    auto & extras = m_instanceExtras[id];

    // on failure, do... nothing
    extras.controller.reset();
    subRunningInstance();

    // quit when there are no more windows.
    if(shouldExitNow())
    {
        m_status = Status::Failed;
        exit(1);
    }
}

void MultiServerMC::ShowGlobalSettings(class QWidget* parent, QString open_page)
{
    if(!m_globalSettingsProvider) {
        return;
    }
    emit globalSettingsAboutToOpen();
    {
        SettingsObject::Lock lock(MSMC->settings());
        PageDialog dlg(m_globalSettingsProvider.get(), open_page, parent);
        dlg.exec();
    }
    emit globalSettingsClosed();
}

MainWindow* MultiServerMC::showMainWindow(bool minimized)
{
    if(m_mainWindow)
    {
        m_mainWindow->setWindowState(m_mainWindow->windowState() & ~Qt::WindowMinimized);
        m_mainWindow->raise();
        m_mainWindow->activateWindow();
    }
    else
    {
        m_mainWindow = new MainWindow();
        m_mainWindow->restoreState(QByteArray::fromBase64(MSMC->settings()->get("MainWindowState").toByteArray()));
        m_mainWindow->restoreGeometry(QByteArray::fromBase64(MSMC->settings()->get("MainWindowGeometry").toByteArray()));
        if(minimized)
        {
            m_mainWindow->showMinimized();
        }
        else
        {
            m_mainWindow->show();
        }

        m_mainWindow->checkInstancePathForProblems();
        connect(this, &MultiServerMC::updateAllowedChanged, m_mainWindow, &MainWindow::updatesAllowedChanged);
        connect(m_mainWindow, &MainWindow::isClosing, this, &MultiServerMC::on_windowClose);
        m_openWindows++;
    }
    return m_mainWindow;
}

InstanceWindow *MultiServerMC::showInstanceWindow(InstancePtr instance, QString page)
{
    if(!instance)
        return nullptr;
    auto id = instance->id();
    auto & extras = m_instanceExtras[id];
    auto & window = extras.window;

    if(window)
    {
        window->raise();
        window->activateWindow();
    }
    else
    {
        window = new InstanceWindow(instance);
        m_openWindows ++;
        connect(window, &InstanceWindow::isClosing, this, &MultiServerMC::on_windowClose);
    }
    if(!page.isEmpty())
    {
        window->selectPage(page);
    }
    if(extras.controller)
    {
        extras.controller->setParentWidget(window);
    }
    return window;
}

void MultiServerMC::on_windowClose()
{
    m_openWindows--;
    auto instWindow = qobject_cast<InstanceWindow *>(QObject::sender());
    if(instWindow)
    {
        auto & extras = m_instanceExtras[instWindow->instanceId()];
        extras.window = nullptr;
        if(extras.controller)
        {
            extras.controller->setParentWidget(m_mainWindow);
        }
    }
    auto mainWindow = qobject_cast<MainWindow *>(QObject::sender());
    if(mainWindow)
    {
        m_mainWindow = nullptr;
    }
    // quit when there are no more windows.
    if(shouldExitNow())
    {
        exit(0);
    }
}
