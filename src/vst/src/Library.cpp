/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Node/NodeApi.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace {

constexpr auto kNodeName = "VST Plugin";
constexpr auto kNodeCategory = "VST";

enum class ProcessBits : int {
    automatic = 0,
    x64 = 64,
    x86 = 32
};

ProcessBits detectPluginBits(const QString& path) noexcept
{
    if (!path.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
        return ProcessBits::x64;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return ProcessBits::x64;
    const auto data = file.read(512);
    if (data.size() < 64 || data[0] != 'M' || data[1] != 'Z')
        return ProcessBits::x64;
    const auto peOffset = *reinterpret_cast<const std::uint32_t*>(data.constData() + 0x3c);
    if (!file.seek(peOffset + 4))
        return ProcessBits::x64;
    const auto machine = file.read(2);
    if (machine.size() != 2)
        return ProcessBits::x64;
    const auto value = static_cast<std::uint16_t>(static_cast<unsigned char>(machine[0]))
        | (static_cast<std::uint16_t>(static_cast<unsigned char>(machine[1])) << 8);
    return value == 0x14c ? ProcessBits::x86 : ProcessBits::x64;
}

class VstHostNode;

class SettingsWidget final : public QWidget {
public:
    explicit SettingsWidget(VstHostNode& node, QWidget* parent = nullptr);

private:
    void refresh();

    VstHostNode& node_;
    QLineEdit* path_ {};
    QLineEdit* clientName_ {};
    QComboBox* bits_ {};
    QLabel* status_ {};
};

class VstHostNode final : public WinJACKNexus::Node::INode {
public:
    ~VstHostNode() override
    {
        shutdown();
    }

    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { kNodeName, kNodeCategory, "0.1.0", "WinJACKNexus",
                 "Loads one VST plugin through a native VSTProcess sandbox." };
    }

    bool init() noexcept override
    {
        initialized_ = true;
        return reload();
    }

    void shutdown() noexcept override
    {
        stopProcess();
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }

    void destroyWindow(HWND) noexcept override
    {
    }

    bool applySettings(const QString& path, const QString& clientName, ProcessBits bits) noexcept
    {
        path_ = path.trimmed();
        clientName_ = clientName.trimmed();
        bits_ = bits;
        return !initialized_ || reload();
    }

    QString pluginPath() const { return path_; }
    QString clientName() const { return clientName_; }
    ProcessBits processBits() const noexcept { return bits_; }
    QString status() const { return status_; }

    QWidget* createSettingsWidget(QWidget* parent) noexcept
    {
        if (QApplication::instance() == nullptr)
            return nullptr;
        try {
            auto* widget = new SettingsWidget(*this, parent);
            widget->setAttribute(Qt::WA_DeleteOnClose, true);
            return widget;
        } catch (...) {
            return nullptr;
        }
    }

    const char* settingsJson() noexcept
    {
        const auto bits = bits_ == ProcessBits::automatic ? QStringLiteral("auto")
            : bits_ == ProcessBits::x86 ? QStringLiteral("32") : QStringLiteral("64");
        serializedSettings_ = QJsonDocument(QJsonObject {
            { QStringLiteral("path"), path_ },
            { QStringLiteral("clientName"), clientName_ },
            { QStringLiteral("bits"), bits }
        }).toJson(QJsonDocument::Compact).toStdString();
        return serializedSettings_.c_str();
    }

    int restoreSettingsJson(const char* json) noexcept
    {
        if (json == nullptr)
            return 0;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return 0;
        const auto values = document.object();
        const auto bits = values.value(QStringLiteral("bits")).toString();
        const auto selectedBits = bits == QStringLiteral("32") ? ProcessBits::x86
            : bits == QStringLiteral("64") ? ProcessBits::x64 : ProcessBits::automatic;
        return applySettings(values.value(QStringLiteral("path")).toString(),
                             values.value(QStringLiteral("clientName")).toString(),
                             selectedBits) ? 1 : 0;
    }

    bool reload() noexcept
    {
        stopProcess();
        if (path_.isEmpty()) {
            status_ = QStringLiteral("Choose a VST plugin to start its sandbox.");
            return true;
        }
        if (!QFileInfo::exists(path_)) {
            status_ = QStringLiteral("The configured plugin path does not exist.");
            return false;
        }

        const auto resolvedBits = bits_ == ProcessBits::automatic ? detectPluginBits(path_) : bits_;
        const auto executable = QCoreApplication::applicationDirPath()
            + QStringLiteral("/VSTProcess") + (resolvedBits == ProcessBits::x86 ? QStringLiteral("32.exe") : QStringLiteral("64.exe"));
        if (!QFileInfo::exists(executable)) {
            status_ = QStringLiteral("The required VSTProcess executable is not available.");
            return false;
        }

        const auto instance = nextInstance_.fetch_add(1, std::memory_order_relaxed);
        pipeName_ = QStringLiteral("WinJACKNexus.VST.%1.%2").arg(QCoreApplication::applicationPid()).arg(instance);
        process_ = std::make_unique<QProcess>();
        process_->setProgram(executable);
        process_->setArguments({ QStringLiteral("--path"), path_,
                                 QStringLiteral("--pipe"), pipeName_,
                                 QStringLiteral("--client"), clientName_.isEmpty() ? QFileInfo(path_).completeBaseName() : clientName_ });
        process_->start();
        if (!process_->waitForStarted(3000)) {
            status_ = QStringLiteral("Unable to start VSTProcess.");
            process_.reset();
            return false;
        }
        status_ = QStringLiteral("VSTProcess is running.");
        return true;
    }

private:
    void requestShutdown() noexcept
    {
        if (pipeName_.isEmpty())
            return;
        const auto fullPipeName = QStringLiteral("\\\\.\\pipe\\") + pipeName_;
        const auto pipe = CreateFileW(reinterpret_cast<LPCWSTR>(fullPipeName.utf16()),
                                      GENERIC_WRITE,
                                      0,
                                      nullptr,
                                      OPEN_EXISTING,
                                      0,
                                      nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return;
        constexpr char command[] = "quit\n";
        DWORD bytesWritten {};
        WriteFile(pipe, command, sizeof(command) - 1, &bytesWritten, nullptr);
        CloseHandle(pipe);
    }

    void stopProcess() noexcept
    {
        if (!process_)
            return;
        requestShutdown();
        process_->waitForFinished(1000);
        if (process_->state() != QProcess::NotRunning)
            process_->kill();
        process_.reset();
    }

    inline static std::atomic_uint64_t nextInstance_ { 1 };
    bool initialized_ {};
    QString path_;
    QString clientName_;
    QString pipeName_;
    QString status_ { QStringLiteral("Not started.") };
    ProcessBits bits_ { ProcessBits::automatic };
    std::unique_ptr<QProcess> process_;
    std::string serializedSettings_;
};

SettingsWidget::SettingsWidget(VstHostNode& node, QWidget* parent)
    : QWidget(parent)
    , node_(node)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    path_ = new QLineEdit(node.pluginPath(), this);
    auto* browse = new QPushButton(QStringLiteral("Browse"), this);
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->addWidget(path_);
    pathLayout->addWidget(browse);
    clientName_ = new QLineEdit(node.clientName(), this);
    bits_ = new QComboBox(this);
    bits_->addItem(QStringLiteral("Auto"), static_cast<int>(ProcessBits::automatic));
    bits_->addItem(QStringLiteral("64-bit"), static_cast<int>(ProcessBits::x64));
    bits_->addItem(QStringLiteral("32-bit"), static_cast<int>(ProcessBits::x86));
    bits_->setCurrentIndex(bits_->findData(static_cast<int>(node.processBits())));
    form->addRow(QStringLiteral("Plugin"), pathRow);
    form->addRow(QStringLiteral("Client name"), clientName_);
    form->addRow(QStringLiteral("Process"), bits_);
    layout->addLayout(form);
    auto* reload = new QPushButton(QStringLiteral("Apply and reload"), this);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(reload);
    layout->addWidget(status_);

    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose VST plugin"), path_->text(),
                                                       QStringLiteral("VST plugins (*.vst3 *.dll)"));
        if (!path.isEmpty())
            path_->setText(path);
    });
    connect(reload, &QPushButton::clicked, this, [this] {
        node_.applySettings(path_->text(), clientName_->text(), static_cast<ProcessBits>(bits_->currentData().toInt()));
        refresh();
    });
    refresh();
}

void SettingsWidget::refresh()
{
    status_->setText(node_.status());
}

}

const char* wjn_get_node_api_version(void)
{
    return "2";
}

std::uint32_t wjn_get_node_count(void)
{
    return 1;
}

const char* wjn_get_node_name(std::uint32_t index)
{
    return index == 0 ? kNodeName : "";
}

const char* wjn_get_node_category(std::uint32_t index)
{
    return index == 0 ? kNodeCategory : "";
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    return index == 0 ? new VstHostNode : nullptr;
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
    delete node;
}

void* wjn_create_settings_view(WinJACKNexus::Node::INode* node, void*)
{
    auto* vstNode = dynamic_cast<VstHostNode*>(node);
    auto* widget = vstNode == nullptr ? nullptr : vstNode->createSettingsWidget(nullptr);
    if (widget != nullptr) {
        widget->setAttribute(Qt::WA_NativeWindow, true);
        widget->show();
    }
    return widget == nullptr ? nullptr : reinterpret_cast<void*>(static_cast<quintptr>(widget->winId()));
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode*, void*)
{
}

QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode* node, QWidget* parent)
{
    auto* vstNode = dynamic_cast<VstHostNode*>(node);
    return vstNode == nullptr ? nullptr : vstNode->createSettingsWidget(parent);
}

void wjn_destroy_settings_widget(WinJACKNexus::Node::INode*, QWidget* widget)
{
    delete widget;
}

const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode* node)
{
    auto* vstNode = dynamic_cast<VstHostNode*>(node);
    return vstNode == nullptr ? "" : vstNode->settingsJson();
}

int wjn_set_node_settings_json(WinJACKNexus::Node::INode* node, const char* json)
{
    auto* vstNode = dynamic_cast<VstHostNode*>(node);
    return vstNode == nullptr ? 0 : vstNode->restoreSettingsJson(json);
}