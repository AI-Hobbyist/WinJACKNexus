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
#include <WinJACKNexus/UI/TrayController.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <atomic>

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

class JackdManagerDialog final : public QDialog {
public:
    explicit JackdManagerDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("JACK 服务器管理"));
        resize(620, 440);
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
        device_ = new QLineEdit(this);
        device_->setPlaceholderText(QStringLiteral("PortAudio 设备，例如 ASIO::FL Studio ASIO"));
        form->addRow(QStringLiteral("设备"), device_);
        sampleRate_ = new QSpinBox(this);
        sampleRate_->setRange(8000, 384000);
        sampleRate_->setSingleStep(1000);
        form->addRow(QStringLiteral("采样率"), sampleRate_);
        bufferSize_ = new QSpinBox(this);
        bufferSize_->setRange(0, 8192);
        bufferSize_->setSingleStep(32);
        form->addRow(QStringLiteral("每周期帧数"), bufferSize_);
        layout->addLayout(form);

        layout->addWidget(new QLabel(QStringLiteral("启动命令（对应 README 的 JACK2 参数）"), this));
        commandPreview_ = new QPlainTextEdit(this);
        commandPreview_->setReadOnly(true);
        commandPreview_->setMaximumBlockCount(10);
        layout->addWidget(commandPreview_);
        log_ = new QPlainTextEdit(this);
        log_->setReadOnly(true);
        layout->addWidget(log_);

        auto* buttons = new QDialogButtonBox(this);
        startButton_ = buttons->addButton(QStringLiteral("启动 JACK"), QDialogButtonBox::AcceptRole);
        stopButton_ = buttons->addButton(QStringLiteral("停止 JACK"), QDialogButtonBox::DestructiveRole);
        buttons->addButton(QDialogButtonBox::Close);
        layout->addWidget(buttons);

        QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
        jackdDirectory_->setText(settings.value(QStringLiteral("directory"), QStringLiteral("C:/Program Files/JACK2")).toString());
        driver_->setCurrentText(settings.value(QStringLiteral("driver"), QStringLiteral("dummy")).toString());
        device_->setText(settings.value(QStringLiteral("device")).toString());
        sampleRate_->setValue(settings.value(QStringLiteral("sampleRate"), 48000).toInt());
        bufferSize_->setValue(settings.value(QStringLiteral("bufferSize"), 256).toInt());
        updateCommandPreview();

        connect(browseButton, &QPushButton::clicked, this, [this] {
            const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 jackd.exe 所在目录"), jackdDirectory_->text());
            if (!directory.isEmpty())
                jackdDirectory_->setText(directory);
        });
        connect(driver_, &QComboBox::currentTextChanged, this, [this] { updateCommandPreview(); });
        connect(device_, &QLineEdit::textChanged, this, [this] { updateCommandPreview(); });
        connect(sampleRate_, &QSpinBox::valueChanged, this, [this] { updateCommandPreview(); });
        connect(bufferSize_, &QSpinBox::valueChanged, this, [this] { updateCommandPreview(); });
        connect(jackdDirectory_, &QLineEdit::textChanged, this, [this] { updateCommandPreview(); });
        connect(startButton_, &QPushButton::clicked, this, [this] { startJackd(); });
        connect(stopButton_, &QPushButton::clicked, this, [this] { process_.terminate(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
        connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { appendProcessOutput(process_.readAllStandardOutput()); });
        connect(&process_, &QProcess::readyReadStandardError, this, [this] { appendProcessOutput(process_.readAllStandardError()); });
        connect(&process_, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
            startButton_->setEnabled(state == QProcess::NotRunning);
            stopButton_->setEnabled(state != QProcess::NotRunning);
        });
    }

private:
    QStringList jackdArguments() const
    {
        QStringList arguments { QStringLiteral("-R"), QStringLiteral("-P95"), QStringLiteral("-S"),
                                QStringLiteral("-d"), driver_->currentText().trimmed(),
                                QStringLiteral("-r"), QString::number(sampleRate_->value()),
                                QStringLiteral("-p"), QString::number(bufferSize_->value()) };
        if (driver_->currentText().trimmed() == QStringLiteral("portaudio") && !device_->text().trimmed().isEmpty()) {
            arguments.insert(5, QStringLiteral("-d"));
            arguments.insert(6, device_->text().trimmed());
        }
        return arguments;
    }

    void updateCommandPreview()
    {
        const auto program = QDir(jackdDirectory_->text()).filePath(QStringLiteral("jackd.exe"));
        commandPreview_->setPlainText(QStringLiteral("\"%1\" %2").arg(program, jackdArguments().join(QLatin1Char(' '))));
    }

    void startJackd()
    {
        const auto directory = QDir::cleanPath(jackdDirectory_->text());
        const auto program = QDir(directory).filePath(QStringLiteral("jackd.exe"));
        if (!QFileInfo(program).isExecutable()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("未在所选目录中找到 jackd.exe。"));
            return;
        }
        QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
        settings.setValue(QStringLiteral("directory"), directory);
        settings.setValue(QStringLiteral("driver"), driver_->currentText().trimmed());
        settings.setValue(QStringLiteral("device"), device_->text().trimmed());
        settings.setValue(QStringLiteral("sampleRate"), sampleRate_->value());
        settings.setValue(QStringLiteral("bufferSize"), bufferSize_->value());
        log_->clear();
        process_.start(program, jackdArguments());
    }

    void appendProcessOutput(const QByteArray& output)
    {
        log_->appendPlainText(QString::fromLocal8Bit(output));
    }

    QLineEdit* jackdDirectory_ = nullptr;
    QComboBox* driver_ = nullptr;
    QLineEdit* device_ = nullptr;
    QSpinBox* sampleRate_ = nullptr;
    QSpinBox* bufferSize_ = nullptr;
    QPlainTextEdit* commandPreview_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QProcess process_;
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

    WinJACKNexus::UI::TrayController tray;
    const bool trayAvailable = tray.initialize(applicationIcon, application.applicationName());
    if (trayAvailable) {
        QObject::connect(&tray, &WinJACKNexus::UI::TrayController::quitRequested,
                         &application, &QApplication::quit);
    }

    JackdManagerDialog jackdManager;
    if (trayAvailable) {
        auto* jackdAction = tray.addMenuAction(QStringLiteral("JACK 服务器管理"));
        QObject::connect(jackdAction, &QAction::triggered, &application, [&jackdManager] {
            jackdManager.show();
            jackdManager.raise();
            jackdManager.activateWindow();
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

    if (trayAvailable) {
        tray.registerPlaceholder(tray.patchbayAction());
        QObject::connect(tray.patchbayAction(), &QAction::triggered, &application, [&tray, &patchbayModule] {
            if (patchbayModule.node()->createWindow(nullptr) == nullptr)
                tray.showMessage(QStringLiteral("接线盘"), QStringLiteral("无法创建接线盘窗口。"), 2);
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