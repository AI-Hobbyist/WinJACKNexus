/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "DeprecatedNodeNotice.hpp"
#include "SynthResourceSettings.hpp"
#include <WinJACKNexus/Node/ModuleCompatibility.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/IconManager.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeCatalog.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeManagementDialog.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackServerGuard.hpp>
#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <Windows.h>
#include <TlHelp32.h>
#include <DbgHelp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <cwchar>
#include <string>

namespace {

wchar_t crashDumpDirectory[MAX_PATH] {};

LONG WINAPI writeCrashDump(EXCEPTION_POINTERS* exception) noexcept
{
    SYSTEMTIME now {};
    GetLocalTime(&now);
    wchar_t path[MAX_PATH + 128] {};
    std::swprintf(path, std::size(path), L"%ls\\WinJACKNexus-%04u%02u%02u-%02u%02u%02u-%03u-%lu.dmp",
        crashDumpDirectory,
        static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
        static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
        static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
        static_cast<unsigned>(now.wMilliseconds), GetCurrentProcessId());
    const auto file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const auto dbghelp = LoadLibraryW(L"DbgHelp.dll");
        if (dbghelp) {
            const auto write = reinterpret_cast<decltype(&MiniDumpWriteDump)>(
                GetProcAddress(dbghelp, "MiniDumpWriteDump"));
            if (write) {
                MINIDUMP_EXCEPTION_INFORMATION info { GetCurrentThreadId(), exception, FALSE };
                write(GetCurrentProcess(), GetCurrentProcessId(), file,
                      static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory
                          | MiniDumpWithThreadInfo), &info, nullptr, nullptr);
            }
            FreeLibrary(dbghelp);
        }
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void logQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    using WinJACKNexus::Common::LogLevel;
    const auto level = type == QtFatalMsg || type == QtCriticalMsg ? LogLevel::error
        : type == QtWarningMsg ? LogLevel::warning
        : type == QtDebugMsg ? LogLevel::trace : LogLevel::info;
    const auto text = message.toUtf8();
    WinJACKNexus::Common::Logger::instance().log(level,
        "Qt category=%s source=%s:%d function=%s message=%s",
        context.category ? context.category : "?", context.file ? context.file : "?",
        context.line, context.function ? context.function : "?", text.constData());
    if (type == QtFatalMsg) WinJACKNexus::Common::Logger::instance().flush();
}

class UsageTrace final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::MouseButtonPress) return false;
        const auto* widget = qobject_cast<QWidget*>(watched);
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (!widget) return false;
        WJN_LOG_TRACE("UI click window=%s widget=%s class=%s button=%d x=%.0f y=%.0f",
            widget->window()->objectName().toUtf8().constData(),
            widget->objectName().toUtf8().constData(), widget->metaObject()->className(),
            static_cast<int>(mouse->button()), mouse->position().x(), mouse->position().y());
        return false;
    }
};

class NodeModule final {
public:
    ~NodeModule()
    {
        unload();
    }

    bool load(const QString& filePath)
    {
        unload();
        WJN_LOG_INFO("Loading node module path=%s", filePath.toUtf8().constData());

        library_ = LoadLibraryW(reinterpret_cast<LPCWSTR>(filePath.utf16()));
        if (library_ == nullptr) {
            WJN_LOG_ERROR("Node module load failed path=%s win32=%lu",
                filePath.toUtf8().constData(), GetLastError());
            return false;
        }

        const auto apiVersion =
            reinterpret_cast<ApiVersion>(GetProcAddress(library_, "wjn_get_node_api_version"));
        const auto create =
            reinterpret_cast<Create>(GetProcAddress(library_, "wjn_create_node_v4"));
        destroy_ = reinterpret_cast<Destroy>(GetProcAddress(library_, "wjn_destroy_node"));
        if (apiVersion == nullptr || create == nullptr || destroy_ == nullptr ||
            !WinJACKNexus::Node::isSingleNodeModule(library_))
        {
            WinJACKNexus::Node::ModuleContract contract;
            const auto error = WinJACKNexus::Node::inspectNodeModule(library_, contract);
            WJN_LOG_ERROR("Node module API validation failed path=%s reason=%s",
                          filePath.toUtf8().constData(),
                          WinJACKNexus::Node::moduleErrorText(error));
            unload();
            return false;
        }

        QString themeError;
        WinJACKNexus::UI::ThemeNamespaceRegistry::instance().registerNodeModule(
            [this](const char* name) -> QFunctionPointer {
                return reinterpret_cast<QFunctionPointer>(GetProcAddress(library_, name));
            }, filePath, &themeLease_, &themeError);
        if (!themeError.isEmpty())
            qWarning().noquote() << "Node theme descriptor:" << themeError;

        node_ = WinJACKNexus::Node::createNodeForMode(library_, WJN_NODE_MODE_JACK);
        if (node_ == nullptr || !node_->init())
        {
            WJN_LOG_ERROR("Node module initialization failed path=%s", filePath.toUtf8().constData());
            unload();
            return false;
        }
        WJN_LOG_INFO("Node module ready path=%s", filePath.toUtf8().constData());
        return true;
    }

    WinJACKNexus::Node::INode* node() const noexcept
    {
        return node_;
    }

    HMODULE moduleHandle() const noexcept { return library_; }

private:
  using ApiVersion = const char *(*)();
  using Create = WinJACKNexus::Node::INode *(*)(uint32_t mode);
  using Destroy = void (*)(WinJACKNexus::Node::INode *);

  void unload() noexcept
  {
      if (node_ != nullptr)
      {
          node_->shutdown();
          destroy_(node_);
          node_ = nullptr;
      }
      destroy_ = nullptr;
      themeLease_.reset();
      if (library_ != nullptr)
      {
          FreeLibrary(library_);
          library_ = nullptr;
      }
    }

    HMODULE library_ = nullptr;
    WinJACKNexus::Node::INode* node_ = nullptr;
    Destroy destroy_ = nullptr;
    WinJACKNexus::UI::ThemeNamespaceRegistry::Lease themeLease_;
};

QString executableDirectory()
{
    wchar_t path[MAX_PATH] {};
    const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return length == 0 ? QString() : QFileInfo(QString::fromWCharArray(path, static_cast<qsizetype>(length))).absolutePath();
}

void notifyMeterbridgeSettingsChanged()
{
    const auto module = GetModuleHandleW(L"mod_meterbrigde.dll");
    if (module == nullptr)
        return;
    using ReloadSettings = void (*)();
    const auto reload = reinterpret_cast<ReloadSettings>(GetProcAddress(module, "wjn_meterbridge_reload_global_settings"));
    if (reload != nullptr)
        reload();
}

using WinJACKNexus::UI::hostText;
using WinJACKNexus::UI::bindHostText;
namespace Controls = WinJACKNexus::UI::Controls;
Controls::GroupPanel* hostGroup(const char* key,const char* english,QWidget* parent) {
    auto* group=new Controls::GroupPanel({},parent);
    bindHostText(group,"title",key,english);
    return group;
}
Controls::TextLabel* hostLabel(const char* key,const char* english,QWidget* parent) {
    auto* label=new Controls::TextLabel(parent);
    label->setWordWrap(true);
    bindHostText(label,"text",key,english);
    return label;
}
Controls::ActionButton* hostButton(const char* key,const char* english,QWidget* parent) {
    auto* button=new Controls::ActionButton({},parent);
    bindHostText(button,"text",key,english);
    return button;
}
Controls::ToggleField* hostToggle(const char* key,const char* english,QWidget* parent) {
    auto* toggle=new Controls::ToggleField({},parent);
    bindHostText(toggle,"text",key,english);
    return toggle;
}
void configureDirectoryDialog(QFileDialog& dialog,const char* key,const char* english,const QString& current) {
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    dialog.setDirectory(current);
    bindHostText(&dialog,"windowTitle",key,english);
    // Qt's LanguageChange resets the Directory accept label to the generic Open
    // label. Keep the directory-specific Qt translation explicit in its options.
    const auto refreshAccept=[&dialog] {
        dialog.setLabelText(QFileDialog::Accept,QCoreApplication::translate("QFileDialog","&Choose"));
    };
    if (auto* manager=WinJACKNexus::UI::I18nManager::installed()) manager->bind(&dialog,[&dialog,refreshAccept] {
        refreshAccept();
        // The translator posts LanguageChange; refresh once after Qt handles it.
        QTimer::singleShot(0,&dialog,refreshAccept);
    });
    else refreshAccept();
}
QString chooseDirectory(QWidget* parent,const char* key,const char* english,const QString& current) {
    QFileDialog dialog(parent);
    configureDirectoryDialog(dialog,key,english,current);
    return dialog.exec()==QDialog::Accepted?dialog.selectedFiles().value(0):QString{};
}

std::vector<DWORD> jackdProcessIds(DWORD ignoredProcessId = 0)
{
    std::vector<DWORD> processIds;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return processIds;

    PROCESSENTRY32W process {};
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) {
        do {
            if (process.th32ProcessID != ignoredProcessId
                && QString::fromWCharArray(process.szExeFile).compare(QStringLiteral("jackd.exe"), Qt::CaseInsensitive) == 0)
                processIds.push_back(process.th32ProcessID);
        } while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);
    return processIds;
}

bool jackdIsRunning(DWORD ignoredProcessId = 0)
{
    return !jackdProcessIds(ignoredProcessId).empty();
}

bool stopJackdProcesses(DWORD ignoredProcessId = 0)
{
    const auto processIds = jackdProcessIds(ignoredProcessId);
    if (processIds.empty())
        return true;

    for (const auto processId : processIds) {
        const auto process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
        if (process == nullptr)
            continue;
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
            TerminateProcess(process, 0);
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
    }
    return !jackdIsRunning(ignoredProcessId);
}

class JackServerSettingsDialog final : public QDialog {
public:
    explicit JackServerSettingsDialog(QWidget* parent = nullptr, bool offline = false)
        : QDialog(parent), offline_(offline)
    {
        namespace Controls = WinJACKNexus::UI::Controls;
        bindHostText(this,"windowTitle","jack.settings.title","JACK server settings");
        resize(726, 318);
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        jackdDirectory_ = new Controls::TextField(this);
        auto* browseButton = hostButton("action.browse","Browse",this);
        auto* directoryLayout = new QHBoxLayout;
        directoryLayout->addWidget(jackdDirectory_);
        directoryLayout->addWidget(browseButton);
        form->addRow(hostLabel("jack.directory","jackd directory",this), directoryLayout);

        driver_ = new Controls::ChoiceField(this);
        driver_->setEditable(true);
        driver_->addItems({ QStringLiteral("dummy"), QStringLiteral("portaudio") });
        form->addRow(hostLabel("jack.driver","Driver",this), driver_);
        device_ = new Controls::ChoiceField(this);
        device_->setEditable(true);
        device_->setInsertPolicy(QComboBox::NoInsert);
        bindHostText(device_->lineEdit(),"placeholderText","jack.deviceHint","PortAudio device, e.g. ASIO::FL Studio ASIO");
        deviceRefreshButton_ = hostButton("action.refresh","Refresh",this);
        auto* deviceLayout = new QHBoxLayout;
        deviceLayout->addWidget(device_);
        deviceLayout->addWidget(deviceRefreshButton_);
        form->addRow(hostLabel("jack.device","Device",this), deviceLayout);
        sampleRate_ = new Controls::IntegerField(this);
        sampleRate_->setRange(8000, 384000);
        sampleRate_->setSingleStep(1000);
        form->addRow(hostLabel("jack.sampleRate","Sample rate",this), sampleRate_);
        bufferSize_ = new Controls::IntegerField(this);
        bufferSize_->setRange(0, 8192);
        bufferSize_->setSingleStep(32);
        form->addRow(hostLabel("jack.periodFrames","Frames per period",this), bufferSize_);
        autoStart_ = hostToggle("jack.autoStart","Start jackd with WinJACKNexus",this);
        autoStart_->setObjectName(QStringLiteral("jackServer.autoStart"));
        bindHostText(autoStart_,"toolTip","jack.autoStart.help","Start jackd with these settings only if no ready JACK server is detected.");
        form->addRow(QString(), autoStart_);
        layout->addLayout(form);

        layout->addWidget(hostLabel("jack.command","Startup command (JACK2 arguments in README)",this));
        commandPreview_ = new WinJACKNexus::UI::Controls::ReadOnlyField(this);
        auto* copyButton = new Controls::IconActionButton(this);
        copyButton->setFixedSize(Controls::sidebarControlHeight,Controls::sidebarControlHeight);
        WinJACKNexus::UI::IconManager::bindThemedIcon(copyButton,QStringLiteral("copy"));
        bindHostText(copyButton,"toolTip","jack.copyCommand","Copy startup command");
        bindHostText(copyButton,"accessibleName","jack.copyCommand","Copy startup command");
        auto* commandLayout = new QHBoxLayout;
        commandLayout->addWidget(commandPreview_);
        commandLayout->addWidget(copyButton);
        layout->addLayout(commandLayout);

        auto* buttons = new Controls::DialogButtonBar(this);
        startButton_ = buttons->addButton(hostText("jack.start","Start JACK"), QDialogButtonBox::AcceptRole);
        bindHostText(startButton_,"text","jack.start","Start JACK");
        restartButton_ = buttons->addButton(hostText("jack.restart","Restart JACK"), QDialogButtonBox::ResetRole);
        bindHostText(restartButton_,"text","jack.restart","Restart JACK");
        stopButton_ = buttons->addButton(hostText("jack.stop","Stop JACK"), QDialogButtonBox::DestructiveRole);
        bindHostText(stopButton_,"text","jack.stop","Stop JACK");
        buttons->addButton(QDialogButtonBox::Close);
        layout->addWidget(buttons);

        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
        jackdDirectory_->setText(settings.value(QStringLiteral("directory"), QStringLiteral("C:/Program Files/JACK2")).toString());
        driver_->setCurrentText(settings.value(QStringLiteral("driver"), QStringLiteral("dummy")).toString());
        device_->setEditText(settings.value(QStringLiteral("device")).toString());
        sampleRate_->setValue(settings.value(QStringLiteral("sampleRate"), 48000).toInt());
        bufferSize_->setValue(settings.value(QStringLiteral("bufferSize"), 256).toInt());
        autoStart_->setChecked(settings.value(QStringLiteral("autoStart"), false).toBool());
        updateCommandPreview();
        updateDeviceControls();
        if (!offline && usesPortAudio())
            refreshDeviceList();
        if (!offline) updateServerState();
        else { startButton_->setEnabled(false); restartButton_->setEnabled(false); stopButton_->setEnabled(false); }
        WinJACKNexus::UI::bindRefreshTimer(statusTimer_,this);
        connect(&statusTimer_, &QTimer::timeout, this, [this] { updateServerState(); });
        if (!offline) statusTimer_.start();

        connect(browseButton, &QPushButton::clicked, this, [this] {
            const auto directory = chooseDirectory(this,"jack.chooseDirectory","Select the directory containing jackd.exe",jackdDirectory_->text());
            if (!directory.isEmpty())
                jackdDirectory_->setText(directory);
        });
        connect(driver_, &QComboBox::currentTextChanged, this, [this] {
            updateCommandPreview();
            updateDeviceControls();
            if (usesPortAudio())
                refreshDeviceList();
            saveSettings();
        });
        connect(device_, &QComboBox::currentTextChanged, this, [this] {
            updateCommandPreview();
            saveSettings();
        });
        connect(sampleRate_, &QSpinBox::valueChanged, this, [this] {
            updateCommandPreview();
            saveSettings();
        });
        connect(bufferSize_, &QSpinBox::valueChanged, this, [this] {
            updateCommandPreview();
            saveSettings();
        });
        connect(autoStart_, &QCheckBox::toggled, this, [this] { saveSettings(); });
        connect(jackdDirectory_, &QLineEdit::textChanged, this, [this] {
            updateCommandPreview();
            saveSettings();
        });
        connect(copyButton, &QToolButton::clicked, this, [this] {
            QApplication::clipboard()->setText(commandPreview_->text());
        });
        connect(deviceRefreshButton_, &QPushButton::clicked, this, [this] { refreshDeviceList(true); });
        connect(startButton_, &QPushButton::clicked, this, [this] { startJackd(); });
        connect(restartButton_, &QPushButton::clicked, this, [this] { restartJackd(); });
        connect(stopButton_, &QPushButton::clicked, this, [this] { stopJackd(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
        connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { process_.readAllStandardOutput(); });
        connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
        connect(&process_, &QProcess::stateChanged, this, [this](QProcess::ProcessState) {
            updateServerState();
        });
        connect(&deviceProbe_, &QProcess::readyReadStandardOutput, this, [this] {
            deviceProbeOutput_ += deviceProbe_.readAllStandardOutput();
        });
        connect(&deviceProbe_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            deviceProbeOutput_ += deviceProbe_.readAllStandardOutput();
            applyDeviceList(deviceProbeOutput_);
            updateDeviceControls();
        });
        connect(&deviceProbe_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            updateDeviceControls();
        });
    }

    bool startAutomaticallyIfConfigured()
    {
        if (offline_) return true;
        if (!autoStart_->isChecked())
            return true;
        if (!WinJACKNexus::Audio::JackServerGuard::isServerReady()
            && !serverRunning()
            && !startJackd(true, false)) {
            return false;
        }
        return waitForServerReady();
    }

private:
    static QString quotedCommandArgument(const QString& argument)
    {
        return argument.contains(QRegularExpression(QStringLiteral("\\s")))
            ? QStringLiteral("\"%1\"").arg(argument)
            : argument;
    }

    static QStringList portAudioDeviceNames(const QString& output)
    {
        const QRegularExpression pattern(QStringLiteral(R"(^\s*Name\s*[=:]\s*(.+?)\s*$)"),
                                         QRegularExpression::CaseInsensitiveOption);
        QStringList devices;
        for (const auto& line : output.split(QLatin1Char('\n'))) {
            const auto match = pattern.match(line);
            if (!match.hasMatch())
                continue;
            auto name = match.captured(1).trimmed();
            if (name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"')))
                name = name.mid(1, name.size() - 2);
            if (!name.isEmpty() && !devices.contains(name, Qt::CaseInsensitive))
                devices.push_back(name);
        }
        return devices;
    }

    QString jackdProgram() const
    {
        return QDir(QDir::cleanPath(jackdDirectory_->text())).filePath(QStringLiteral("jackd.exe"));
    }

    bool usesPortAudio() const
    {
        return driver_->currentText().trimmed().compare(QStringLiteral("portaudio"), Qt::CaseInsensitive) == 0;
    }

    bool serverRunning() const
    {
        return jackdIsRunning(static_cast<DWORD>(deviceProbe_.processId()))
            || process_.state() != QProcess::NotRunning;
    }

    QStringList jackdArguments() const
    {
        QStringList arguments { QStringLiteral("-R"), QStringLiteral("-P95"), QStringLiteral("-S"),
                                QStringLiteral("-d"), driver_->currentText().trimmed(),
                                QStringLiteral("-r"), QString::number(sampleRate_->value()),
                                QStringLiteral("-p"), QString::number(bufferSize_->value()) };
        if (usesPortAudio() && !device_->currentText().trimmed().isEmpty()) {
            arguments.insert(5, QStringLiteral("-d"));
            arguments.insert(6, device_->currentText().trimmed());
        }
        return arguments;
    }

    void updateCommandPreview()
    {
        QStringList displayedArguments;
        for (const auto& argument : jackdArguments())
            displayedArguments.push_back(quotedCommandArgument(argument));
        commandPreview_->setText(QStringLiteral("\"%1\" %2").arg(jackdProgram(), displayedArguments.join(QLatin1Char(' '))));
    }

    void updateDeviceControls()
    {
        const auto portAudio = usesPortAudio();
        device_->setEnabled(portAudio);
        deviceRefreshButton_->setEnabled(portAudio && deviceProbe_.state() == QProcess::NotRunning);
    }

    void refreshDeviceList(bool notifyOnFailure = false)
    {
        if (offline_) return;
        if (!usesPortAudio() || deviceProbe_.state() != QProcess::NotRunning)
            return;
        const auto program = jackdProgram();
        if (!QFileInfo(program).isExecutable()) {
            if (notifyOnFailure)
                QMessageBox::warning(this, windowTitle(), hostText("jack.notFound","jackd.exe was not found in the selected directory."));
            return;
        }

        deviceProbeOutput_.clear();
        deviceRefreshButton_->setEnabled(false);
        deviceProbe_.setProcessChannelMode(QProcess::MergedChannels);
        const auto probeName = QStringLiteral("WinJACKNexus-DeviceProbe-%1")
            .arg(QCoreApplication::applicationPid());
        deviceProbe_.start(program, { QStringLiteral("-n"), probeName,
                                      QStringLiteral("-R"), QStringLiteral("-S"),
                                      QStringLiteral("-d"), QStringLiteral("portaudio"), QStringLiteral("-l") });
    }

    void applyDeviceList(const QByteArray& output)
    {
        const auto names = portAudioDeviceNames(QString::fromLocal8Bit(output));
        if (names.isEmpty())
            return;

        const auto selectedDevice = device_->currentText().trimmed();
        const QSignalBlocker blocker(device_);
        device_->clear();
        device_->addItems(names);
        const auto selectedIndex = device_->findText(selectedDevice, Qt::MatchFixedString);
        if (selectedIndex >= 0)
            device_->setCurrentIndex(selectedIndex);
        else if (!selectedDevice.isEmpty())
            device_->setEditText(selectedDevice);
    }

    void updateServerState()
    {
        if (offline_) return;
        const auto running = serverRunning();
        startButton_->setEnabled(!running);
        restartButton_->setEnabled(running);
        stopButton_->setEnabled(running);
    }

    void saveSettings() const
    {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
        settings.setValue(QStringLiteral("directory"), QDir::cleanPath(jackdDirectory_->text()));
        settings.setValue(QStringLiteral("driver"), driver_->currentText().trimmed());
        settings.setValue(QStringLiteral("device"), device_->currentText().trimmed());
        settings.setValue(QStringLiteral("sampleRate"), sampleRate_->value());
        settings.setValue(QStringLiteral("bufferSize"), bufferSize_->value());
        settings.setValue(QStringLiteral("autoStart"), autoStart_->isChecked());
    }

    bool waitForServerReady() const
    {
        constexpr auto timeoutMilliseconds = ULONGLONG { 5000 };
        const auto startTime = GetTickCount64();
        do {
            if (WinJACKNexus::Audio::JackServerGuard::isServerReady())
                return true;
            Sleep(20);
        } while (GetTickCount64() - startTime < timeoutMilliseconds);
        return WinJACKNexus::Audio::JackServerGuard::isServerReady();
    }

    bool stopJackd()
    {
        if (offline_) return false;
        if (!serverRunning()) {
            updateServerState();
            return true;
        }
        if (process_.state() != QProcess::NotRunning) {
            process_.terminate();
            if (!process_.waitForFinished(2000))
                process_.kill();
        }
        const auto stopped = stopJackdProcesses(static_cast<DWORD>(deviceProbe_.processId()));
        updateServerState();
        if (!stopped && serverRunning())
            QMessageBox::warning(this, windowTitle(), hostText("jack.stopFailed","Unable to stop the running jackd.exe."));
        return stopped;
    }

    void restartJackd()
    {
        if (stopJackd())
            startJackd();
    }

    bool startJackd(bool waitForStart = false, bool notifyOnFailure = true)
    {
        if (offline_) return false;
        if (serverRunning()) {
            updateServerState();
            return true;
        }
        const auto program = jackdProgram();
        if (!QFileInfo(program).isExecutable()) {
            if (notifyOnFailure)
                QMessageBox::warning(this, windowTitle(), hostText("jack.notFound","jackd.exe was not found in the selected directory."));
            return false;
        }
        saveSettings();
        process_.start(program, jackdArguments());
        const auto started = !waitForStart || process_.waitForStarted(3000);
        updateServerState();
        if (!started && notifyOnFailure)
            QMessageBox::warning(this, windowTitle(), hostText("jack.startFailed","Unable to start jackd.exe."));
        return started;
    }

    QLineEdit* jackdDirectory_ = nullptr;
    QComboBox* driver_ = nullptr;
    QComboBox* device_ = nullptr;
    QPushButton* deviceRefreshButton_ = nullptr;
    QSpinBox* sampleRate_ = nullptr;
    QSpinBox* bufferSize_ = nullptr;
    QCheckBox* autoStart_ = nullptr;
    QLineEdit* commandPreview_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* restartButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QProcess process_;
    QProcess deviceProbe_;
    QByteArray deviceProbeOutput_;
    QTimer statusTimer_;
    bool offline_ = false;
};

class GlobalSettingsDialog final : public QDialog {
public:
    explicit GlobalSettingsDialog(WinJACKNexus::UI::ThemeManagementDialog& themes,
                                  QWidget* parent = nullptr)
        : QDialog(parent)
    {
        namespace Controls = WinJACKNexus::UI::Controls;
        bindHostText(this,"windowTitle","settings.global.title","Global settings");
        resize(620, 410);

        auto* layout = new QVBoxLayout(this);
        auto* applicationGroup = hostGroup("settings.application","Application settings",this);
        auto* applicationForm = new QFormLayout(applicationGroup);
        applicationForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto* language=new Controls::ChoiceField(applicationGroup);
        language->setObjectName(QStringLiteral("application.language"));
        language->setFixedHeight(Controls::sidebarControlHeight);
        applicationForm->addRow(hostLabel("settings.language","Language",applicationGroup),language);
        if (auto* host=WinJACKNexus::UI::HostI18n::installed()) host->bindLanguageChoice(language);

        frameRate_ = new Controls::ChoiceField(applicationGroup);
        frameRate_->setFixedHeight(Controls::sidebarControlHeight);
        frameRate_->setObjectName(QStringLiteral("application.frameRate"));
        frameRate_->addItem(QStringLiteral("30 FPS"), 30);
        frameRate_->addItem(QStringLiteral("60 FPS"), 60);
        frameRate_->addItem(QStringLiteral("120 FPS"), 120);
        const auto frameRateIndex = frameRate_->findData(WinJACKNexus::UI::ApplicationSettings::instance().frameRate());
        frameRate_->setCurrentIndex(frameRateIndex >= 0 ? frameRateIndex : 1);
        applicationForm->addRow(hostLabel("settings.frameRate","UI frame rate",this), frameRate_);

        peakValueRefresh_ = new Controls::IntegerField(applicationGroup);
        peakValueRefresh_->setFixedHeight(Controls::sidebarControlHeight);
        peakValueRefresh_->setObjectName(QStringLiteral("application.peakValueRefreshMilliseconds"));
        peakValueRefresh_->setRange(8, 1000);
        peakValueRefresh_->setSuffix(QStringLiteral(" ms"));
        peakValueRefresh_->setValue(WinJACKNexus::UI::ApplicationSettings::instance()
                                        .peakValueRefreshMilliseconds());
        applicationForm->addRow(hostLabel("settings.peakRefresh","Peak value refresh interval",this), peakValueRefresh_);

        peakHold_ = new Controls::DecimalField(applicationGroup);
        peakHold_->setFixedHeight(Controls::sidebarControlHeight);
        peakHold_->setObjectName(QStringLiteral("application.peakHoldSeconds"));
        peakHold_->setRange(0.0, 10.0);
        peakHold_->setSingleStep(0.1);
        peakHold_->setDecimals(1);
        peakHold_->setSuffix(QStringLiteral(" s"));
        peakHold_->setValue(WinJACKNexus::UI::ApplicationSettings::instance().peakHoldSeconds());
        applicationForm->addRow(hostLabel("settings.peakHold","Peak hold time",this), peakHold_);

        logTracing_ = hostToggle("settings.loggingEnabled","Enable detailed log tracing",applicationGroup);
        logTracing_->setFixedHeight(Controls::sidebarControlHeight);
        logTracing_->setObjectName(QStringLiteral("application.logTracingEnabled"));
        logTracing_->setChecked(QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"))
            .value(QStringLiteral("logTracingEnabled"), true).toBool());
        applicationForm->addRow(hostLabel("settings.logging","Diagnostic logging",this), logTracing_);

        opacitySlider_ = new Controls::RangeSlider(Qt::Horizontal, applicationGroup);
        opacitySlider_->setObjectName(QStringLiteral("application.quickAccessOpacity"));
        opacitySlider_->setRange(10, 100);
        opacityValue_ = new Controls::TextLabel(applicationGroup);
        auto* opacityLayout = new QHBoxLayout;
        opacityLayout->addWidget(opacitySlider_);
        opacityLayout->addWidget(opacityValue_);
        applicationForm->addRow(hostLabel("settings.opacity","Quick access bar opacity",this), opacityLayout);
        layout->addWidget(applicationGroup);

        synthResourceSettings_ = new WinJACKNexus::App::SynthResourceSettings(this);
        layout->addWidget(synthResourceSettings_);

        auto* meterbridgeGroup = hostGroup("view.meterbridge","Meter bridge",this);
        auto* meterbridgeForm = new QFormLayout(meterbridgeGroup);
        meterbridgeForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        meterbridgeSilenceThreshold_ = new Controls::DecimalField(meterbridgeGroup);
        meterbridgeSilenceThreshold_->setFixedHeight(Controls::sidebarControlHeight);
        meterbridgeSilenceThreshold_->setRange(-144.0, 0.0);
        meterbridgeSilenceThreshold_->setDecimals(1);
        meterbridgeSilenceThreshold_->setSuffix(QStringLiteral(" dBFS"));
        meterbridgeForm->addRow(hostLabel("settings.silenceThreshold","Silence threshold",this), meterbridgeSilenceThreshold_);
        meterbridgeSilenceDuration_ = new Controls::DecimalField(meterbridgeGroup);
        meterbridgeSilenceDuration_->setFixedHeight(Controls::sidebarControlHeight);
        meterbridgeSilenceDuration_->setRange(0.1, 60.0);
        meterbridgeSilenceDuration_->setDecimals(1);
        meterbridgeSilenceDuration_->setSuffix(QStringLiteral(" s"));
        meterbridgeForm->addRow(hostLabel("settings.silenceDuration","Silence duration",this), meterbridgeSilenceDuration_);
        meterbridgeLogRootDirectory_ = new WinJACKNexus::UI::Controls::ReadOnlyField(meterbridgeGroup);
        meterbridgeLogRootDirectory_->setFixedHeight(Controls::sidebarControlHeight);
        bindHostText(meterbridgeLogRootDirectory_,"placeholderText","settings.noCsvRoot","Not set; CSV logging unavailable");
        auto* meterbridgeLogBrowseButton = hostButton("action.browse","Browse",meterbridgeGroup);
        auto* meterbridgeLogClearButton = hostButton("action.clear","Clear",meterbridgeGroup);
        meterbridgeLogBrowseButton->setFixedHeight(Controls::sidebarControlHeight);
        meterbridgeLogClearButton->setFixedHeight(Controls::sidebarControlHeight);
        auto* meterbridgeLogLayout = new QHBoxLayout;
        meterbridgeLogLayout->addWidget(meterbridgeLogRootDirectory_, 1);
        meterbridgeLogLayout->addWidget(meterbridgeLogBrowseButton);
        meterbridgeLogLayout->addWidget(meterbridgeLogClearButton);
        meterbridgeForm->addRow(hostLabel("settings.logRoot","Log root directory",this), meterbridgeLogLayout);
        layout->addWidget(meterbridgeGroup);

        auto* themesButton = hostButton("action.manageThemes","Manage themes\u2026",this);
        layout->addWidget(themesButton);
        connect(themesButton, &QPushButton::clicked, this, [&themes] {
            themes.show();
            themes.raise();
            themes.activateWindow();
        });

        auto* buttons = new Controls::DialogButtonBar(QDialogButtonBox::Close, this);
        if (auto* closeButton = buttons->button(QDialogButtonBox::Close))
            closeButton->setFixedHeight(Controls::sidebarControlHeight);
        layout->addWidget(buttons);

        QSettings applicationSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
        QSettings legacyJackdSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
        const auto opacity = std::clamp(applicationSettings.value(
            QStringLiteral("quickAccessOpacity"),
            legacyJackdSettings.value(QStringLiteral("quickAccessOpacity"), WinJACKNexus::UI::TrayController::quickAccessOpacity())).toInt(),
            10,
            100);
        opacitySlider_->setValue(opacity);
        opacityValue_->setText(QStringLiteral("%1%").arg(opacity));
        if (!applicationSettings.contains(QStringLiteral("quickAccessOpacity")))
            applicationSettings.setValue(QStringLiteral("quickAccessOpacity"), opacity);

        QSettings meterbridgeSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
        meterbridgeSilenceThreshold_->setValue(std::clamp(meterbridgeSettings.value(QStringLiteral("silenceThresholdDbfs"), -70.0).toDouble(), -144.0, 0.0));
        meterbridgeSilenceDuration_->setValue(std::clamp(meterbridgeSettings.value(QStringLiteral("silenceDurationSeconds"), 3.0).toDouble(), 0.1, 60.0));
        const auto logRootDirectory = meterbridgeSettings.value(QStringLiteral("logRootDirectory")).toString().trimmed();
        meterbridgeLogRootDirectory_->setText(logRootDirectory.isEmpty()
            ? QString()
            : QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(logRootDirectory).absoluteFilePath())));

        connect(frameRate_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            WinJACKNexus::UI::ApplicationSettings::instance().setFrameRate(frameRate_->itemData(index).toInt());
        });
        connect(peakValueRefresh_, qOverload<int>(&QSpinBox::valueChanged), this, [](int milliseconds) {
            WinJACKNexus::UI::ApplicationSettings::instance().setPeakValueRefreshMilliseconds(milliseconds);
        });
        connect(peakHold_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [](double seconds) {
            WinJACKNexus::UI::ApplicationSettings::instance().setPeakHoldSeconds(seconds);
        });
        connect(logTracing_, &QCheckBox::toggled, this, [](bool enabled) {
            WinJACKNexus::Common::Logger::instance().setDetailedLoggingEnabled(enabled);
            WJN_LOG_INFO("Detailed log tracing %s", enabled ? "enabled" : "disabled");
            WinJACKNexus::Common::Logger::instance().flush();
            QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"))
                .setValue(QStringLiteral("logTracingEnabled"), enabled);
        });
        connect(opacitySlider_, &QSlider::valueChanged, this, [this](int value) {
            WinJACKNexus::UI::TrayController::setQuickAccessOpacity(value);
            opacityValue_->setText(QStringLiteral("%1%").arg(value));
            saveApplicationSettings();
        });
        connect(meterbridgeSilenceThreshold_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            saveMeterbridgeSettings();
        });
        connect(meterbridgeSilenceDuration_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            saveMeterbridgeSettings();
        });
        connect(meterbridgeLogBrowseButton, &QPushButton::clicked, this, [this] {
            const auto directory = chooseDirectory(this,"settings.chooseCsvRoot","Select meter bridge CSV log root directory",meterbridgeLogRootDirectory_->text());
            if (!directory.isEmpty()) {
                meterbridgeLogRootDirectory_->setText(QDir::fromNativeSeparators(QDir::cleanPath(directory)));
                saveMeterbridgeSettings();
            }
        });
        connect(meterbridgeLogClearButton, &QPushButton::clicked, this, [this] {
            meterbridgeLogRootDirectory_->clear();
            saveMeterbridgeSettings();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        synthResourceSettings_->refreshAvailability(
            QCoreApplication::applicationDirPath() + QStringLiteral("/nodes"));
        QDialog::showEvent(event);
    }

private:
    void saveApplicationSettings() const
    {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
        settings.setValue(QStringLiteral("quickAccessOpacity"), opacitySlider_->value());
    }

    void saveMeterbridgeSettings() const
    {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
        settings.setValue(QStringLiteral("silenceThresholdDbfs"), meterbridgeSilenceThreshold_->value());
        settings.setValue(QStringLiteral("silenceDurationSeconds"), meterbridgeSilenceDuration_->value());
        settings.setValue(QStringLiteral("logRootDirectory"), meterbridgeLogRootDirectory_->text());
        notifyMeterbridgeSettingsChanged();
    }

    QComboBox* frameRate_ = nullptr;
    WinJACKNexus::App::SynthResourceSettings* synthResourceSettings_ = nullptr;
    QSpinBox* peakValueRefresh_ = nullptr;
    QDoubleSpinBox* peakHold_ = nullptr;
    QCheckBox* logTracing_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QLabel* opacityValue_ = nullptr;
    QDoubleSpinBox* meterbridgeSilenceThreshold_ = nullptr;
    QDoubleSpinBox* meterbridgeSilenceDuration_ = nullptr;
    QLineEdit* meterbridgeLogRootDirectory_ = nullptr;
};

class JackMonitor final : public QObject {
public:
    explicit JackMonitor(WinJACKNexus::UI::TrayController& tray)
        : tray_(tray)
    {
        WinJACKNexus::UI::bindRefreshTimer(timer_,this);
        if (auto* manager=WinJACKNexus::UI::I18nManager::installed())
            manager->bind(this,[this] { render(); });
        QObject::connect(&timer_, &QTimer::timeout, [&] { refresh(); });
        timer_.start();
        refresh();
    }

private:
    static int onXRun(void* argument)
    {
        static_cast<JackMonitor*>(argument)->xruns_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    void refresh()
    {
        if (!client_.isOpen()) {
            const auto now=GetTickCount64();
            if (now<nextConnectionAttempt_) return;
            nextConnectionAttempt_=now+1000;
            if (!client_.open({ "WinJACKNexus-Monitor", false, true, nullptr })) {
                render();
                return;
            }
            client_.setXRunCallback(&JackMonitor::onXRun, this);
            client_.activate();
        }
        lastLoad_=client_.cpuLoad();
        render();
    }
    void render() const {
        WinJACKNexus::UI::TrayController::setQuickAccessStatus(client_.isOpen()
            ?hostText("status.jackLoad","DSP {load}% · XRun {count}",
                {{QStringLiteral("load"),QString::number(lastLoad_,'f',1)},
                 {QStringLiteral("count"),QString::number(xruns_.load(std::memory_order_relaxed))}})
            :hostText("status.jackOffline","JACK offline"));
    }

    WinJACKNexus::UI::TrayController& tray_;
    WinJACKNexus::Audio::JackClient client_;
    std::atomic_uint64_t xruns_ { 0 };
    QTimer timer_;
    ULONGLONG nextConnectionAttempt_=0;
    double lastLoad_=0;
};

}


#ifndef WJN_HOST_I18N_TEST
int main(int argc, char* argv[])
{
    auto& logger = WinJACKNexus::Common::Logger::instance();
    logger.flush();
    const auto dumpDirectory = WinJACKNexus::Common::PathUtils::logDirectory().wstring();
    wcsncpy_s(crashDumpDirectory, dumpDirectory.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(&writeCrashDump);
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("WinJACKNexus"));
    application.setQuitOnLastWindowClosed(false);
    WinJACKNexus::UI::HostI18n hostI18n;
    QString i18nError;
    if (!hostI18n.initialize(QCoreApplication::applicationDirPath(), &i18nError)) {
        QMessageBox::critical(nullptr, application.applicationName(),
            hostText("startup.i18nFailure","Unable to initialize language resources: {detail}",
                {{QStringLiteral("detail"),i18nError}}));
        return 1;
    }
    QObject::connect(&hostI18n,&WinJACKNexus::UI::HostI18n::languageSelectionFailed,
        &application,[](const QString& detail) {
            QMessageBox::warning(nullptr,hostText("language.failure.title","Language switch failed"),
                hostText("language.failure.message","Unable to change the language: {detail}",
                    {{QStringLiteral("detail"),detail}}));
        });
    const auto instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\WinJACKNexus.SingleInstance");
    if (instanceMutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (instanceMutex != nullptr)
            CloseHandle(instanceMutex);
        QMessageBox::information(nullptr,application.applicationName(),
            hostText("startup.alreadyRunning","WinJACKNexus is already running."));
        return 0;
    }
    WJN_LOG_INFO("Single-instance lock acquired");
    logger.flush();
    WJN_LOG_INFO("Qt application initialized");
    logger.flush();
    const bool logTracingEnabled = QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"),
        QStringLiteral("Application")).value(QStringLiteral("logTracingEnabled"), true).toBool();
    logger.setDetailedLoggingEnabled(logTracingEnabled);
    qInstallMessageHandler(logQtMessage);
    WJN_LOG_INFO("Application start logTracing=%d", logTracingEnabled ? 1 : 0);
    logger.flush();
    QTimer logDrainTimer;
    logDrainTimer.setInterval(250);
    QObject::connect(&logDrainTimer, &QTimer::timeout, &application, [&logger] { logger.flush(); });
    QObject::connect(&application, &QApplication::aboutToQuit, &application, [&logger] {
        WJN_LOG_INFO("Application stopping");
        logger.flush();
    });
    logDrainTimer.start();
    UsageTrace usageTrace;
    application.installEventFilter(&usageTrace);

    QIcon applicationIcon(QStringLiteral(":/WinJACKNexus/UI/icons/WinJACKNexus.png"));
    if (applicationIcon.isNull())
        applicationIcon = application.style()->standardIcon(QStyle::SP_ComputerIcon);
    application.setWindowIcon(applicationIcon);

    WinJACKNexus::UI::TrayController tray;
    const bool trayAvailable = tray.initialize(applicationIcon, application.applicationName());
    if (!trayAvailable) tray.initializeFallback();
    QObject::connect(&tray, &WinJACKNexus::UI::TrayController::quitRequested,
                     &application, &QApplication::quit);
    tray.setThemeLoadingProgress(0);

    std::unique_ptr<QSettings> applicationSettings;
    std::unique_ptr<WinJACKNexus::UI::ThemeCore> themeCore;
    std::unique_ptr<WinJACKNexus::UI::ThemeCatalog> themeCatalog;
    QString themeRestoreError;
    QEventLoop themeLoadLoop;
    const auto guiThread = application.thread();
    WJN_LOG_INFO("Theme loading started asynchronously");
    logger.flush();
    std::thread themeWorker([&] {
        auto report = [&](int progress) {
            QMetaObject::invokeMethod(&application, [&tray, progress] {
                tray.setThemeLoadingProgress(progress);
            }, Qt::QueuedConnection);
        };
        applicationSettings = std::make_unique<QSettings>(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"),
            QStringLiteral("Application"));
        themeCore = std::make_unique<WinJACKNexus::UI::ThemeCore>();
        report(30);
        if (themeCore->isValid()) {
            themeCatalog = std::make_unique<WinJACKNexus::UI::ThemeCatalog>(
                *themeCore, *applicationSettings);
            if (!themeCatalog->restoreSaved(&themeRestoreError))
                qWarning().noquote() << "Theme restore:" << themeRestoreError;
        }
        report(90);
        if (themeCatalog) themeCatalog->moveToThread(guiThread);
        themeCore->moveToThread(guiThread);
        applicationSettings->moveToThread(guiThread);
        QMetaObject::invokeMethod(&application, [&] {
            tray.setThemeLoadingProgress(100);
            themeLoadLoop.quit();
        }, Qt::QueuedConnection);
    });
    themeLoadLoop.exec();
    themeWorker.join();
    WJN_LOG_INFO("Theme loading finished valid=%d", themeCore->isValid() ? 1 : 0);
    logger.flush();
    if (!themeCore->isValid()) {
        WJN_LOG_ERROR("Default theme initialization failed: %s",
            themeCore->initializationError().toUtf8().constData());
        logger.flush();
        if (qEnvironmentVariableIsSet("WJN_THEME_STARTUP_PROBE"))
            return 1;
        QMessageBox::critical(nullptr,hostText("startup.themeFailure.title","WinJACKNexus theme startup failed"),
            hostText("startup.themeFailure.message","Unable to load the required default theme DLL. The application cannot start.\n{detail}",
                {{QStringLiteral("detail"),themeCore->initializationError()}}));
        return 1;
    }
    WinJACKNexus::UI::ThemeManager::installThemeCore(themeCore.get());
    WinJACKNexus::UI::ThemeManager themeManager;
    themeManager.watchApplication(&application);

    QSettings legacyJackdSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
    const auto quickAccessOpacity = std::clamp(applicationSettings->value(
        QStringLiteral("quickAccessOpacity"),
        legacyJackdSettings.value(QStringLiteral("quickAccessOpacity"), 50)).toInt(),
        10,
        100);
    if (!applicationSettings->contains(QStringLiteral("quickAccessOpacity")))
        applicationSettings->setValue(QStringLiteral("quickAccessOpacity"), quickAccessOpacity);
    WinJACKNexus::UI::TrayController::setQuickAccessOpacity(quickAccessOpacity);

    WinJACKNexus::UI::ThemeManagementDialog themeManagementDialog(*themeCore);
    GlobalSettingsDialog globalSettingsDialog(themeManagementDialog);
    WinJACKNexus::App::showDeprecatedNodeNoticeOnce(
        QCoreApplication::applicationDirPath() + QStringLiteral("/nodes"), *applicationSettings);

    JackServerSettingsDialog jackServerSettingsDialog;
#ifdef _DEBUG
    const auto arguments = application.arguments();
    const auto layoutTestArg = std::find_if(arguments.cbegin(),
        arguments.cend(), [](const QString& arg) {
            return arg.startsWith(QStringLiteral("--settings-layout-test="));
        });
    if (layoutTestArg != arguments.cend()) {
        const auto suffix = layoutTestArg->section('=', 1);
        const auto outputDirectory = QStringLiteral("tests/preview/ControlHeights");
        QDir().mkpath(outputDirectory);
        bool matched = true;
        QStringList mismatches;
        const auto capture = [&](QDialog& dialog, const QString& name) {
            dialog.show();
            application.processEvents();
            for (auto* control : dialog.findChildren<QWidget*>()) {
                const auto role = control->property("wjnRole").toString();
                if (!control->isVisible() || (role != QStringLiteral("field")
                    && role != QStringLiteral("choice") && role != QStringLiteral("number")
                    && role != QStringLiteral("action") && role != QStringLiteral("toggle")))
                    continue;
                if (control->height() != WinJACKNexus::UI::Controls::sidebarControlHeight) {
                    mismatches.append(QStringLiteral("%1 %2 %3: %4")
                        .arg(name, role, control->objectName()).arg(control->height()));
                    matched = false;
                }
            }
            const auto path = QStringLiteral("%1/%2-%3.png").arg(outputDirectory, name, suffix);
            if (!dialog.grab().save(path)) matched = false;
            dialog.hide();
        };
        capture(jackServerSettingsDialog, QStringLiteral("jack-settings"));
        capture(globalSettingsDialog, QStringLiteral("global-settings"));
        auto* tabs = themeManagementDialog.findChild<QTabWidget*>();
        const QStringList pages {QStringLiteral("theme-overview"), QStringLiteral("theme-colors"),
            QStringLiteral("theme-background"), QStringLiteral("theme-export")};
        if (tabs == nullptr || tabs->count() != pages.size()) {
            mismatches.append(QStringLiteral("theme page count: %1")
                .arg(tabs == nullptr ? 0 : tabs->count()));
            matched = false;
        }
        for (int index = 0; tabs != nullptr && index < tabs->count(); ++index) {
            tabs->setCurrentIndex(index);
            capture(themeManagementDialog, pages.value(index));
        }
        QFile diagnostic(QStringLiteral("build/settings-layout-%1.txt").arg(suffix));
        if (diagnostic.open(QIODevice::WriteOnly | QIODevice::Truncate))
            diagnostic.write(mismatches.join('\n').toUtf8());
        return matched ? 0 : 1;
    }
#endif
    if (!jackServerSettingsDialog.startAutomaticallyIfConfigured()) {
        WJN_LOG_WARN("Configured JACK server startup failed");
        QMessageBox::warning(nullptr, application.applicationName(),
                             hostText("jack.autoStartFailed","Unable to start or connect to jackd using the JACK server settings."));
    }
    if (tray.contextMenu()) {
        auto* themeAction = tray.addMenuAction(hostText("menu.theme","Theme"));
        bindHostText(themeAction,"text","menu.theme","Theme");
        themeAction->setMenu(themeCatalog->createMenu(tray.contextMenu(), [&themeManagementDialog] {
            themeManagementDialog.show();
            themeManagementDialog.raise();
            themeManagementDialog.activateWindow();
        }, [&tray](const QString& issue) {
            tray.showMessage(hostText("error.themeSwitch","Theme switch failed"), issue, 1);
        }));
        tray.setQuickAccessThemeMenu(themeCatalog->createMenu(tray.contextMenu(), [&themeManagementDialog] {
            themeManagementDialog.show();
            themeManagementDialog.raise();
            themeManagementDialog.activateWindow();
        }, [&tray](const QString& issue) {
            tray.showMessage(hostText("error.themeSwitch","Theme switch failed"), issue, 1);
        }));
        auto* globalSettingsAction = tray.addMenuAction(hostText("settings.global.title","Global settings"));
        bindHostText(globalSettingsAction,"text","settings.global.title","Global settings");
        tray.setQuickAccessSettingsAction(globalSettingsAction);
        QObject::connect(globalSettingsAction, &QAction::triggered, &application, [&globalSettingsDialog] {
            globalSettingsDialog.show();
            globalSettingsDialog.raise();
            globalSettingsDialog.activateWindow();
        });
        auto* jackServerSettingsAction = tray.addMenuAction(hostText("jack.settings.title","JACK server settings"));
        bindHostText(jackServerSettingsAction,"text","jack.settings.title","JACK server settings");
        QObject::connect(jackServerSettingsAction, &QAction::triggered, &application, [&jackServerSettingsDialog] {
            jackServerSettingsDialog.show();
            jackServerSettingsDialog.raise();
            jackServerSettingsDialog.activateWindow();
        });
    }
    JackMonitor jackMonitor(tray);

    NodeModule patchbayModule;
    const auto patchbayPath = QDir(executableDirectory()).filePath(QStringLiteral("mod_patchbay.dll"));
    if (!patchbayModule.load(patchbayPath)) {
        logger.flush();
        QMessageBox::critical(nullptr, application.applicationName(),
                              hostText("error.patchbayLoad","Unable to load the patchbay module."));
        return 1;
    }

    NodeModule meterbridgeModule;
    const auto meterbridgePath = QDir(executableDirectory()).filePath(QStringLiteral("mod_meterbrigde.dll"));
    if (!meterbridgeModule.load(meterbridgePath)) {
        logger.flush();
        QMessageBox::critical(nullptr, application.applicationName(),
                              hostText("error.meterbridgeLoad","Unable to load the meter bridge module."));
        return 1;
    }

    if (tray.contextMenu()) {
        tray.registerPlaceholder(tray.patchbayAction());
        QObject::connect(tray.patchbayAction(), &QAction::triggered, &application, [&tray, &patchbayModule] {
            if (patchbayModule.node()->createWindow(nullptr) == nullptr)
                tray.showMessage(hostText("view.patchbay","Patchbay"), hostText("error.patchbayWindow","Unable to create the patchbay window."), 2);
        });
        const std::array<QAction*,3> mixerViews {tray.mixerAction(),tray.mixerDetailsAction(),tray.mixerOverviewAction()};
        for(int view=0;view<static_cast<int>(mixerViews.size());++view)
            QObject::connect(mixerViews[view],&QAction::triggered,&application,[&tray,&patchbayModule,view] {
                using ShowView=int (*)(int);
                const auto show=reinterpret_cast<ShowView>(GetProcAddress(patchbayModule.moduleHandle(),"wjn_patchbay_show_digital_mixer_view"));
                const int result=show?show(view):-1;
                if(result==0)tray.showMessage(hostText("view.mixer","Mixer"),hostText("error.enableMixer","Enable the mixer in the patchbay under Tools > Audio tools first."),2);
                else if(result<0)tray.showMessage(hostText("view.mixer","Mixer"),hostText("error.mixerWindow","The Digital Mixer window is currently unavailable."),2);
            });
            tray.registerPlaceholder(tray.meterbridgeAction());
            QObject::connect(tray.meterbridgeAction(), &QAction::triggered, &application, [&tray, &meterbridgeModule] {
                if (meterbridgeModule.node()->createWindow(nullptr) == nullptr)
                tray.showMessage(hostText("view.meterbridge","Meter bridge"), hostText("error.meterbridgeWindow","Unable to create the meter bridge window."), 2);
            });
    }

    if (patchbayModule.node()->createWindow(nullptr) == nullptr) {
        WJN_LOG_ERROR("Initial patchbay window creation failed");
        logger.flush();
        QMessageBox::critical(nullptr, application.applicationName(),
                              hostText("error.patchbayWindow","Unable to create the patchbay window."));
        CloseHandle(instanceMutex);
        return 1;
    }

    const auto exitCode = application.exec();
    logger.flush();
    CloseHandle(instanceMutex);
    return exitCode;
}

#endif // WJN_HOST_I18N_TEST
