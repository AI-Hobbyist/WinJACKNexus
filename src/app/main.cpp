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
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackServerGuard.hpp>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
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
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>

namespace {

class NodeModule final {
public:
    ~NodeModule()
    {
        unload();
    }

    bool load(const QString& filePath)
    {
        unload();

        library_ = LoadLibraryW(reinterpret_cast<LPCWSTR>(filePath.utf16()));
        if (library_ == nullptr)
            return false;

        const auto apiVersion = reinterpret_cast<ApiVersion>(GetProcAddress(library_, "wjn_get_node_api_version"));
        const auto nodeCount = reinterpret_cast<NodeCount>(GetProcAddress(library_, "wjn_get_node_count"));
        const auto create = reinterpret_cast<Create>(GetProcAddress(library_, "wjn_create_node"));
        destroy_ = reinterpret_cast<Destroy>(GetProcAddress(library_, "wjn_destroy_node"));
        if (apiVersion == nullptr || nodeCount == nullptr || create == nullptr || destroy_ == nullptr
            || std::strcmp(apiVersion(), "2") != 0 || nodeCount() == 0) {
            unload();
            return false;
        }

        node_ = create(0);
        if (node_ == nullptr || !node_->init()) {
            unload();
            return false;
        }
        return true;
    }

    WinJACKNexus::Node::INode* node() const noexcept
    {
        return node_;
    }

private:
    using ApiVersion = const char* (*)();
    using NodeCount = std::uint32_t (*)();
    using Create = WinJACKNexus::Node::INode* (*)(std::uint32_t index);
    using Destroy = void (*)(WinJACKNexus::Node::INode*);

    void unload() noexcept
    {
        if (node_ != nullptr) {
            node_->shutdown();
            destroy_(node_);
            node_ = nullptr;
        }
        destroy_ = nullptr;
        if (library_ != nullptr) {
            FreeLibrary(library_);
            library_ = nullptr;
        }
    }

    HMODULE library_ = nullptr;
    WinJACKNexus::Node::INode* node_ = nullptr;
    Destroy destroy_ = nullptr;
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

class CopyIconButton final : public QToolButton {
public:
    explicit CopyIconButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto bounds = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        painter.setPen(QPen(underMouse() ? QColor(QStringLiteral("#5db1d7")) : QColor(QStringLiteral("#5a6570"))));
        painter.setBrush(underMouse() ? QColor(QStringLiteral("#3d4854")) : QColor(QStringLiteral("#303841")));
        painter.drawRoundedRect(bounds, 4.0, 4.0);

        painter.setPen(QPen(QColor(QStringLiteral("#f1f5f8")), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(10.0, 6.0, 10.0, 12.0), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(7.0, 10.0, 10.0, 12.0), 1.5, 1.5);
    }
};

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
    explicit JackServerSettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("JACK 服务器设置"));
        resize(726, 318);
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        jackdDirectory_ = new QLineEdit(this);
        auto* browseButton = new QPushButton(QStringLiteral("浏览"), this);
        auto* directoryLayout = new QHBoxLayout;
        directoryLayout->addWidget(jackdDirectory_);
        directoryLayout->addWidget(browseButton);
        form->addRow(QStringLiteral("jackd 目录"), directoryLayout);

        driver_ = new QComboBox(this);
        driver_->setEditable(true);
        driver_->addItems({ QStringLiteral("dummy"), QStringLiteral("portaudio") });
        form->addRow(QStringLiteral("驱动"), driver_);
        device_ = new QComboBox(this);
        device_->setEditable(true);
        device_->setInsertPolicy(QComboBox::NoInsert);
        device_->setPlaceholderText(QStringLiteral("PortAudio 设备，例如 ASIO::FL Studio ASIO"));
        deviceRefreshButton_ = new QPushButton(QStringLiteral("刷新"), this);
        auto* deviceLayout = new QHBoxLayout;
        deviceLayout->addWidget(device_);
        deviceLayout->addWidget(deviceRefreshButton_);
        form->addRow(QStringLiteral("设备"), deviceLayout);
        sampleRate_ = new QSpinBox(this);
        sampleRate_->setRange(8000, 384000);
        sampleRate_->setSingleStep(1000);
        form->addRow(QStringLiteral("采样率"), sampleRate_);
        bufferSize_ = new QSpinBox(this);
        bufferSize_->setRange(0, 8192);
        bufferSize_->setSingleStep(32);
        form->addRow(QStringLiteral("每周期帧数"), bufferSize_);
        autoStart_ = new QCheckBox(QStringLiteral("启动 WinJACKNexus 时自动启动 jackd"), this);
        autoStart_->setObjectName(QStringLiteral("jackServer.autoStart"));
        autoStart_->setToolTip(QStringLiteral("仅在未检测到已就绪的 JACK 服务器时使用当前配置启动 jackd。"));
        form->addRow(QString(), autoStart_);
        layout->addLayout(form);

        layout->addWidget(new QLabel(QStringLiteral("启动命令（对应 README 的 JACK2 参数）"), this));
        commandPreview_ = new QLineEdit(this);
        commandPreview_->setReadOnly(true);
        auto* copyButton = new CopyIconButton(this);
        copyButton->setToolTip(QStringLiteral("复制启动命令"));
        copyButton->setAccessibleName(QStringLiteral("复制启动命令"));
        auto* commandLayout = new QHBoxLayout;
        commandLayout->addWidget(commandPreview_);
        commandLayout->addWidget(copyButton);
        layout->addLayout(commandLayout);

        auto* buttons = new QDialogButtonBox(this);
        startButton_ = buttons->addButton(QStringLiteral("启动 JACK"), QDialogButtonBox::AcceptRole);
        restartButton_ = buttons->addButton(QStringLiteral("重启 JACK"), QDialogButtonBox::ResetRole);
        stopButton_ = buttons->addButton(QStringLiteral("停止 JACK"), QDialogButtonBox::DestructiveRole);
        buttons->addButton(QDialogButtonBox::Close);
        layout->addWidget(buttons);

        QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
        jackdDirectory_->setText(settings.value(QStringLiteral("directory"), QStringLiteral("C:/Program Files/JACK2")).toString());
        driver_->setCurrentText(settings.value(QStringLiteral("driver"), QStringLiteral("dummy")).toString());
        device_->setEditText(settings.value(QStringLiteral("device")).toString());
        sampleRate_->setValue(settings.value(QStringLiteral("sampleRate"), 48000).toInt());
        bufferSize_->setValue(settings.value(QStringLiteral("bufferSize"), 256).toInt());
        autoStart_->setChecked(settings.value(QStringLiteral("autoStart"), false).toBool());
        updateCommandPreview();
        updateDeviceControls();
        if (usesPortAudio())
            refreshDeviceList();
        updateServerState();
        statusTimer_.setInterval(1000);
        connect(&statusTimer_, &QTimer::timeout, this, [this] { updateServerState(); });
        statusTimer_.start();

        connect(browseButton, &QPushButton::clicked, this, [this] {
            const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 jackd.exe 所在目录"), jackdDirectory_->text());
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
        if (!usesPortAudio() || deviceProbe_.state() != QProcess::NotRunning)
            return;
        const auto program = jackdProgram();
        if (!QFileInfo(program).isExecutable()) {
            if (notifyOnFailure)
                QMessageBox::warning(this, windowTitle(), QStringLiteral("未在所选目录中找到 jackd.exe。"));
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
        const auto running = serverRunning();
        startButton_->setEnabled(!running);
        restartButton_->setEnabled(running);
        stopButton_->setEnabled(running);
    }

    void saveSettings() const
    {
        QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
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
            QMessageBox::warning(this, windowTitle(), QStringLiteral("无法停止正在运行的 jackd.exe。"));
        return stopped;
    }

    void restartJackd()
    {
        if (stopJackd())
            startJackd();
    }

    bool startJackd(bool waitForStart = false, bool notifyOnFailure = true)
    {
        if (serverRunning()) {
            updateServerState();
            return true;
        }
        const auto program = jackdProgram();
        if (!QFileInfo(program).isExecutable()) {
            if (notifyOnFailure)
                QMessageBox::warning(this, windowTitle(), QStringLiteral("未在所选目录中找到 jackd.exe。"));
            return false;
        }
        saveSettings();
        process_.start(program, jackdArguments());
        const auto started = !waitForStart || process_.waitForStarted(3000);
        updateServerState();
        if (!started && notifyOnFailure)
            QMessageBox::warning(this, windowTitle(), QStringLiteral("无法启动 jackd.exe。"));
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
};

class GlobalSettingsDialog final : public QDialog {
public:
    explicit GlobalSettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("全局设置"));
        resize(620, 410);

        auto* layout = new QVBoxLayout(this);
        auto* applicationGroup = new QGroupBox(QStringLiteral("应用设置"), this);
        auto* applicationForm = new QFormLayout(applicationGroup);
        applicationForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        frameRate_ = new QComboBox(applicationGroup);
        frameRate_->setObjectName(QStringLiteral("application.frameRate"));
        frameRate_->addItem(QStringLiteral("30 FPS"), 30);
        frameRate_->addItem(QStringLiteral("60 FPS"), 60);
        frameRate_->addItem(QStringLiteral("120 FPS"), 120);
        const auto frameRateIndex = frameRate_->findData(WinJACKNexus::UI::ApplicationSettings::instance().frameRate());
        frameRate_->setCurrentIndex(frameRateIndex >= 0 ? frameRateIndex : 1);
        applicationForm->addRow(QStringLiteral("界面帧率"), frameRate_);

        opacitySlider_ = new QSlider(Qt::Horizontal, applicationGroup);
        opacitySlider_->setObjectName(QStringLiteral("application.quickAccessOpacity"));
        opacitySlider_->setRange(10, 100);
        opacityValue_ = new QLabel(applicationGroup);
        auto* opacityLayout = new QHBoxLayout;
        opacityLayout->addWidget(opacitySlider_);
        opacityLayout->addWidget(opacityValue_);
        applicationForm->addRow(QStringLiteral("状态栏透明度"), opacityLayout);
        layout->addWidget(applicationGroup);

        auto* meterbridgeGroup = new QGroupBox(QStringLiteral("电平表桥"), this);
        auto* meterbridgeForm = new QFormLayout(meterbridgeGroup);
        meterbridgeForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        meterbridgeSilenceThreshold_ = new QDoubleSpinBox(meterbridgeGroup);
        meterbridgeSilenceThreshold_->setRange(-144.0, 0.0);
        meterbridgeSilenceThreshold_->setDecimals(1);
        meterbridgeSilenceThreshold_->setSuffix(QStringLiteral(" dBFS"));
        meterbridgeForm->addRow(QStringLiteral("静音阈值"), meterbridgeSilenceThreshold_);
        meterbridgeSilenceDuration_ = new QDoubleSpinBox(meterbridgeGroup);
        meterbridgeSilenceDuration_->setRange(0.1, 60.0);
        meterbridgeSilenceDuration_->setDecimals(1);
        meterbridgeSilenceDuration_->setSuffix(QStringLiteral(" 秒"));
        meterbridgeForm->addRow(QStringLiteral("静音时长"), meterbridgeSilenceDuration_);
        meterbridgeLogRootDirectory_ = new QLineEdit(meterbridgeGroup);
        meterbridgeLogRootDirectory_->setReadOnly(true);
        meterbridgeLogRootDirectory_->setPlaceholderText(QStringLiteral("未设置，CSV 记录不可用"));
        auto* meterbridgeLogBrowseButton = new QPushButton(QStringLiteral("浏览"), meterbridgeGroup);
        auto* meterbridgeLogClearButton = new QPushButton(QStringLiteral("清除"), meterbridgeGroup);
        auto* meterbridgeLogLayout = new QHBoxLayout;
        meterbridgeLogLayout->addWidget(meterbridgeLogRootDirectory_, 1);
        meterbridgeLogLayout->addWidget(meterbridgeLogBrowseButton);
        meterbridgeLogLayout->addWidget(meterbridgeLogClearButton);
        meterbridgeForm->addRow(QStringLiteral("日志保存根目录"), meterbridgeLogLayout);
        layout->addWidget(meterbridgeGroup);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        layout->addWidget(buttons);

        QSettings applicationSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
        QSettings legacyJackdSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
        const auto opacity = std::clamp(applicationSettings.value(
            QStringLiteral("quickAccessOpacity"),
            legacyJackdSettings.value(QStringLiteral("quickAccessOpacity"), WinJACKNexus::UI::TrayController::quickAccessOpacity())).toInt(),
            10,
            100);
        opacitySlider_->setValue(opacity);
        opacityValue_->setText(QStringLiteral("%1%").arg(opacity));
        if (!applicationSettings.contains(QStringLiteral("quickAccessOpacity")))
            applicationSettings.setValue(QStringLiteral("quickAccessOpacity"), opacity);

        QSettings meterbridgeSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
        meterbridgeSilenceThreshold_->setValue(std::clamp(meterbridgeSettings.value(QStringLiteral("silenceThresholdDbfs"), -70.0).toDouble(), -144.0, 0.0));
        meterbridgeSilenceDuration_->setValue(std::clamp(meterbridgeSettings.value(QStringLiteral("silenceDurationSeconds"), 3.0).toDouble(), 0.1, 60.0));
        const auto logRootDirectory = meterbridgeSettings.value(QStringLiteral("logRootDirectory")).toString().trimmed();
        meterbridgeLogRootDirectory_->setText(logRootDirectory.isEmpty()
            ? QString()
            : QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(logRootDirectory).absoluteFilePath())));

        connect(frameRate_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            WinJACKNexus::UI::ApplicationSettings::instance().setFrameRate(frameRate_->itemData(index).toInt());
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
            const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 Meterbridge CSV 日志根目录"), meterbridgeLogRootDirectory_->text());
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

private:
    void saveApplicationSettings() const
    {
        QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
        settings.setValue(QStringLiteral("quickAccessOpacity"), opacitySlider_->value());
    }

    void saveMeterbridgeSettings() const
    {
        QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
        settings.setValue(QStringLiteral("silenceThresholdDbfs"), meterbridgeSilenceThreshold_->value());
        settings.setValue(QStringLiteral("silenceDurationSeconds"), meterbridgeSilenceDuration_->value());
        settings.setValue(QStringLiteral("logRootDirectory"), meterbridgeLogRootDirectory_->text());
        notifyMeterbridgeSettingsChanged();
    }

    QComboBox* frameRate_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QLabel* opacityValue_ = nullptr;
    QDoubleSpinBox* meterbridgeSilenceThreshold_ = nullptr;
    QDoubleSpinBox* meterbridgeSilenceDuration_ = nullptr;
    QLineEdit* meterbridgeLogRootDirectory_ = nullptr;
};

class JackMonitor final {
public:
    explicit JackMonitor(WinJACKNexus::UI::TrayController& tray)
        : tray_(tray)
    {
        timer_.setInterval(1000);
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
            if (!client_.open({ "WinJACKNexus-Monitor", false, true, nullptr })) {
                WinJACKNexus::UI::TrayController::setQuickAccessStatus(QStringLiteral("JACK 离线"));
                return;
            }
            client_.setXRunCallback(&JackMonitor::onXRun, this);
            client_.activate();
        }
        WinJACKNexus::UI::TrayController::setQuickAccessStatus(
            QStringLiteral("DSP %1% · XRun %2")
                .arg(client_.cpuLoad(), 0, 'f', 1)
                .arg(xruns_.load(std::memory_order_relaxed)));
    }

    WinJACKNexus::UI::TrayController& tray_;
    WinJACKNexus::Audio::JackClient client_;
    std::atomic_uint64_t xruns_ { 0 };
    QTimer timer_;
};

}

int main(int argc, char* argv[])
{
    const auto instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\WinJACKNexus.SingleInstance");
    if (instanceMutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (instanceMutex != nullptr)
            CloseHandle(instanceMutex);
        MessageBoxW(nullptr, L"WinJACKNexus 已在运行。", L"WinJACKNexus", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("WinJACKNexus"));
    application.setQuitOnLastWindowClosed(false);

    QIcon applicationIcon(QStringLiteral(":/WinJACKNexus/UI/icons/WinJACKNexus.png"));
    if (applicationIcon.isNull())
        applicationIcon = application.style()->standardIcon(QStyle::SP_ComputerIcon);
    application.setWindowIcon(applicationIcon);

    QSettings applicationSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
    QSettings legacyJackdSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
    const auto quickAccessOpacity = std::clamp(applicationSettings.value(
        QStringLiteral("quickAccessOpacity"),
        legacyJackdSettings.value(QStringLiteral("quickAccessOpacity"), 50)).toInt(),
        10,
        100);
    if (!applicationSettings.contains(QStringLiteral("quickAccessOpacity")))
        applicationSettings.setValue(QStringLiteral("quickAccessOpacity"), quickAccessOpacity);
    WinJACKNexus::UI::TrayController::setQuickAccessOpacity(quickAccessOpacity);
    WinJACKNexus::UI::TrayController tray;
    const bool trayAvailable = tray.initialize(applicationIcon, application.applicationName());
    if (trayAvailable) {
        QObject::connect(&tray, &WinJACKNexus::UI::TrayController::quitRequested,
                         &application, &QApplication::quit);
    }

    GlobalSettingsDialog globalSettingsDialog;
    JackServerSettingsDialog jackServerSettingsDialog;
    if (!jackServerSettingsDialog.startAutomaticallyIfConfigured()) {
        QMessageBox::warning(nullptr, application.applicationName(),
                             QStringLiteral("未能按 JACK 服务器设置启动或连接 jackd。"));
    }
    if (trayAvailable) {
        auto* globalSettingsAction = tray.addMenuAction(QStringLiteral("全局设置"));
        QObject::connect(globalSettingsAction, &QAction::triggered, &application, [&globalSettingsDialog] {
            globalSettingsDialog.show();
            globalSettingsDialog.raise();
            globalSettingsDialog.activateWindow();
        });
        auto* jackServerSettingsAction = tray.addMenuAction(QStringLiteral("JACK 服务器设置"));
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
        QMessageBox::critical(nullptr, application.applicationName(),
                              QStringLiteral("无法加载接线盘模块。"));
        return 1;
    }

    NodeModule meterbridgeModule;
    const auto meterbridgePath = QDir(executableDirectory()).filePath(QStringLiteral("mod_meterbrigde.dll"));
    if (!meterbridgeModule.load(meterbridgePath)) {
        QMessageBox::critical(nullptr, application.applicationName(),
                              QStringLiteral("无法加载电平表桥模块。"));
        return 1;
    }

    NodeModule mixerModule;
    const auto mixerPath = QDir(executableDirectory()).filePath(QStringLiteral("mod_mixer.dll"));
    if (!mixerModule.load(mixerPath)) {
        QMessageBox::critical(nullptr, application.applicationName(),
                              QStringLiteral("无法加载调音台模块。"));
        return 1;
    }

    if (trayAvailable) {
        tray.registerPlaceholder(tray.patchbayAction());
        QObject::connect(tray.patchbayAction(), &QAction::triggered, &application, [&tray, &patchbayModule] {
            if (patchbayModule.node()->createWindow(nullptr) == nullptr)
                tray.showMessage(QStringLiteral("接线盘"), QStringLiteral("无法创建接线盘窗口。"), 2);
        });
        tray.registerPlaceholder(tray.mixerAction());
        QObject::connect(tray.mixerAction(), &QAction::triggered, &application, [&tray, &mixerModule] {
            if (mixerModule.node()->createWindow(nullptr) == nullptr)
                tray.showMessage(QStringLiteral("调音台"), QStringLiteral("无法创建调音台窗口。"), 2);
        });
            tray.registerPlaceholder(tray.meterbridgeAction());
            QObject::connect(tray.meterbridgeAction(), &QAction::triggered, &application, [&tray, &meterbridgeModule] {
                if (meterbridgeModule.node()->createWindow(nullptr) == nullptr)
                tray.showMessage(QStringLiteral("电平表桥"), QStringLiteral("无法创建电平表桥窗口。"), 2);
            });
    }

    if (patchbayModule.node()->createWindow(nullptr) == nullptr) {
        QMessageBox::critical(nullptr, application.applicationName(),
                              QStringLiteral("无法创建接线盘窗口。"));
        CloseHandle(instanceMutex);
        return 1;
    }

    const auto exitCode = application.exec();
    CloseHandle(instanceMutex);
    return exitCode;
}