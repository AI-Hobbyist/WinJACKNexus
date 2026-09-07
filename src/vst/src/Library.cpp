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

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#if WJN_ENABLE_VST2
#include "pluginterfaces/vst2.x/aeffectx.h"
#endif

namespace {

constexpr auto kNodeName = "VST Plugin";
constexpr auto kNodeCategory = "VST";

enum class ProcessBits : int {
    automatic = 0,
    x64 = 64,
    x86 = 32
};

enum class PortDisplayMode : int {
    defaultNames = 0,
    busLabels,
    standardChannels,
    jackDefault
};

QString portDisplayModeArgument(PortDisplayMode mode)
{
    switch (mode) {
    case PortDisplayMode::busLabels: return QStringLiteral("bus-labels");
    case PortDisplayMode::standardChannels: return QStringLiteral("standard");
    case PortDisplayMode::jackDefault: return QStringLiteral("jack");
    default: return QStringLiteral("default");
    }
}

QString sandboxClientName(QString requestedName, const QString& instanceId)
{
    constexpr int jackClientNameMaximumLength = 127;
    const auto suffix = QStringLiteral("-") + instanceId.left(8);
    const auto maximumPluginNameLength = jackClientNameMaximumLength - suffix.size();
    requestedName = requestedName.trimmed();
    if (requestedName.size() > maximumPluginNameLength) {
        const auto preservedLength = (maximumPluginNameLength - 3) / 2;
        requestedName = requestedName.left(preservedLength) + QStringLiteral("...")
            + requestedName.right(maximumPluginNameLength - preservedLength - 3);
    }
    if (requestedName.isEmpty())
        requestedName = QStringLiteral("vst_") + instanceId.left(6);
    return requestedName + suffix;
}

#if WJN_ENABLE_VST2
VstIntPtr currentShellPluginId {};

VstIntPtr VSTCALLBACK shellHostCallback(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode == audioMasterCurrentId)
        return currentShellPluginId;
    return 0;
}

using Vst2EntryProc = AEffect* (VSTCALLBACK*) (audioMasterCallback);

struct InternalPlugin final {
    QString name;
    QString id;
};

QVector<InternalPlugin> shellPlugins(const QString& path, QString& error)
{
    const auto module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
        return {};
    auto entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module, "VSTPluginMain"));
    if (entry == nullptr)
        entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module, "main"));
    if (entry == nullptr) {
        FreeLibrary(module);
        return {};
    }

    currentShellPluginId = 0;
    auto* effect = entry(shellHostCallback);
    if (effect == nullptr || effect->magic != kEffectMagic || effect->dispatcher == nullptr) {
        FreeLibrary(module);
        return {};
    }
    effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0F);
    const auto isShell = effect->dispatcher(effect, effGetPlugCategory, 0, 0, nullptr, 0.0F) == kPlugCategShell;
    QVector<InternalPlugin> result;
    if (isShell) {
        char name[kVstMaxProductStrLen + 1] {};
        while (true) {
            std::fill(std::begin(name), std::end(name), '\0');
            const auto id = effect->dispatcher(effect, effShellGetNextPlugin, 0, 0, name, 0.0F);
            if (id == 0)
                break;
            const auto childName = QString::fromLocal8Bit(name).trimmed();
            if (!childName.isEmpty())
                result.push_back({ childName, QString::number(id) });
        }
        if (result.isEmpty())
            error = QStringLiteral("The VST2 Shell did not provide any internal plugins.");
    }
    effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0F);
    FreeLibrary(module);
    return result;
}

QString chooseInternalPlugin(const QString& title, const QString& description, const QVector<InternalPlugin>& plugins)
{
    QDialog dialog;
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(description, &dialog));
    auto* filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(QStringLiteral("Filter or enter plugin name / ID"));
    layout->addWidget(filter);
    auto* list = new QListWidget(&dialog);
    for (const auto& plugin : plugins) {
        auto* item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(plugin.name).arg(plugin.id), list);
        item->setData(Qt::UserRole, plugin.id);
        item->setData(Qt::UserRole + 1, plugin.name);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    QObject::connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString& text) {
        for (int index = 0; index < list->count(); ++index) {
            auto* item = list->item(index);
            item->setHidden(!text.isEmpty() && !item->text().contains(text, Qt::CaseInsensitive));
        }
    });
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, [filter](QListWidgetItem* current, QListWidgetItem*) {
        if (current != nullptr)
            filter->setText(current->data(Qt::UserRole + 1).toString());
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog](QListWidgetItem*) { dialog.accept(); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return {};
    if (const auto* item = list->currentItem(); item != nullptr && item->text().contains(filter->text(), Qt::CaseInsensitive))
        return item->data(Qt::UserRole).toString();
    return filter->text().trimmed();
}

QString displayNameForInternalPlugin(const QVector<InternalPlugin>& plugins, const QString& id)
{
    const auto selected = std::find_if(plugins.cbegin(), plugins.cend(), [&id](const InternalPlugin& plugin) {
        return plugin.id == id || plugin.name.compare(id, Qt::CaseInsensitive) == 0;
    });
    return selected == plugins.cend() ? id : selected->name;
}
#endif

QVector<InternalPlugin> vst3Plugins(const QString& path, QString& error)
{
    std::string moduleError;
    auto module = VST3::Hosting::Module::create(path.toStdString(), moduleError);
    if (module == nullptr) {
        error = QString::fromStdString(moduleError);
        return {};
    }
    auto host = Steinberg::owned(new Steinberg::Vst::HostApplication);
    module->getFactory().setHostContext(host);
    QVector<InternalPlugin> result;
    for (const auto& classInfo : module->getFactory().classInfos()) {
        if (classInfo.category() != "Audio Module Class")
            continue;
        result.push_back({ QString::fromStdString(classInfo.name()),
                           QString::fromStdString(classInfo.ID().toString(false)) });
    }
    if (result.isEmpty())
        error = QStringLiteral("The VST3 factory did not provide any audio plugin classes.");
    return result;
}

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
    QComboBox* portDisplayMode_ {};
    QLabel* status_ {};
    QPushButton* restart_ {};
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
        processWatcher_ = std::make_unique<QObject>();
        return reload();
    }

    void shutdown() noexcept override
    {
        stopProcess();
        processWatcher_.reset();
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }

    void destroyWindow(HWND) noexcept override
    {
    }

    bool applySettings(const QString& path, const QString& clientName, ProcessBits bits,
                       PortDisplayMode portDisplayMode, const QString& internalPlugin = {},
                       const QString& internalPluginName = {}) noexcept
    {
        const auto selectedPath = path.trimmed();
        if (path_ != selectedPath)
            internalPlugin_.clear();
        if (path_ != selectedPath)
            internalPluginName_.clear();
        path_ = selectedPath;
        clientName_ = clientName.trimmed();
        bits_ = bits;
        portDisplayMode_ = portDisplayMode;
        internalPlugin_ = internalPlugin.trimmed();
        internalPluginName_ = internalPluginName.trimmed();
        return !initialized_ || reload();
    }

    QString pluginPath() const { return path_; }
    QString clientName() const { return clientName_; }
    ProcessBits processBits() const noexcept { return bits_; }
    PortDisplayMode portDisplayMode() const noexcept { return portDisplayMode_; }
    QString internalPlugin() const { return internalPlugin_; }
    QString internalPluginName() const { return internalPluginName_; }
    QString status() const { return status_; }
    bool restartPlugin() noexcept
    {
        bypassed_ = false;
        return reload();
    }
    QString expectedClientId() const { return expectedClientId_; }

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
            { QStringLiteral("bits"), bits },
            { QStringLiteral("portDisplayMode"), static_cast<int>(portDisplayMode_) },
            { QStringLiteral("internalPlugin"), internalPlugin_ },
            { QStringLiteral("internalPluginName"), internalPluginName_ },
            { QStringLiteral("instanceId"), instanceId_ }
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
        const auto portDisplayMode = static_cast<PortDisplayMode>(std::clamp(
            values.value(QStringLiteral("portDisplayMode")).toInt(static_cast<int>(PortDisplayMode::defaultNames)),
            static_cast<int>(PortDisplayMode::defaultNames),
            static_cast<int>(PortDisplayMode::jackDefault)));
        const auto instanceId = values.value(QStringLiteral("instanceId")).toString();
        if (!instanceId.isEmpty())
            instanceId_ = instanceId;
        return applySettings(values.value(QStringLiteral("path")).toString(),
                             values.value(QStringLiteral("clientName")).toString(),
                             selectedBits, portDisplayMode,
                             values.value(QStringLiteral("internalPlugin")).toString(),
                             values.value(QStringLiteral("internalPluginName")).toString()) ? 1 : 0;
    }

    bool prepareForCanvas() noexcept
    {
        if (!path_.isEmpty())
            return true;
        if (QApplication::instance() == nullptr)
            return false;
        const auto path = QFileDialog::getOpenFileName(nullptr,
                                                      QStringLiteral("Choose VST plugin"),
                                                      {},
                                                      QStringLiteral("VST plugins (*.vst3 *.dll)"));
        if (path.isEmpty())
            return false;
        return applySettings(path, {}, ProcessBits::automatic, PortDisplayMode::defaultNames);
    }

    bool showPluginGui() noexcept
    {
        const auto response = sendPipeCommand("open-editor\n");
        if (response.contains(QStringLiteral("\"ok\":true"))) {
            status_ = QStringLiteral("The plugin editor is open in a separate window.");
            return true;
        }
        status_ = response.isEmpty() ? QStringLiteral("Unable to contact VSTProcess.") : response;
        return false;
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
        executable_ = executable;

        if (path_.endsWith(QStringLiteral(".vst3"), Qt::CaseInsensitive) && internalPlugin_.isEmpty()) {
            QString factoryError;
            const auto plugins = vst3Plugins(path_, factoryError);
            if (!factoryError.isEmpty()) {
                status_ = factoryError;
                return false;
            }
            if (plugins.size() > 1) {
                if (QApplication::instance() == nullptr) {
                    status_ = QStringLiteral("Choose a VST3 plugin class before starting this node.");
                    return false;
                }
                internalPlugin_ = chooseInternalPlugin(QStringLiteral("Choose VST3 Plugin"),
                                                        QStringLiteral("Choose a VST3 plugin class or enter its class ID."),
                                                        plugins);
                if (internalPlugin_.isEmpty()) {
                    status_ = QStringLiteral("VST3 plugin class selection was cancelled.");
                    return false;
                }
            }
            if (!internalPlugin_.isEmpty())
                internalPluginName_ = displayNameForInternalPlugin(plugins, internalPlugin_);
        }

#if WJN_ENABLE_VST2
        if (path_.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive) && internalPlugin_.isEmpty()) {
            QString shellError;
            const auto plugins = shellPlugins(path_, shellError);
            if (!shellError.isEmpty()) {
                status_ = shellError;
                return false;
            }
            if (!plugins.isEmpty()) {
                if (QApplication::instance() == nullptr) {
                    status_ = QStringLiteral("Choose an internal VST2 Shell plugin before starting this node.");
                    return false;
                }
                internalPlugin_ = chooseInternalPlugin(QStringLiteral("Choose VST2 Shell Plugin"),
                                                        QStringLiteral("Choose an internal plugin or enter its name / numeric ID."),
                                                        plugins);
                if (internalPlugin_.isEmpty()) {
                    status_ = QStringLiteral("VST2 Shell plugin selection was cancelled.");
                    return false;
                }
                internalPluginName_ = displayNameForInternalPlugin(plugins, internalPlugin_);
            }
        }
#endif

        return startProcess(false);
    }

private:
    bool startProcess(bool bypass) noexcept
    {
        if (process_ && process_->state() != QProcess::NotRunning) {
            status_ = QStringLiteral("The previous VSTProcess is still running.");
            return false;
        }
        process_.reset();
        const auto instance = nextInstance_.fetch_add(1, std::memory_order_relaxed);
        pipeName_ = QStringLiteral("WinJACKNexus.VST.%1.%2").arg(QCoreApplication::applicationPid()).arg(instance);
        process_ = std::make_unique<QProcess>();
        process_->setProgram(executable_);
        const auto pluginName = clientName_.isEmpty()
            ? (internalPluginName_.isEmpty() ? QFileInfo(path_).completeBaseName() : internalPluginName_)
            : clientName_;
        expectedClientId_ = sandboxClientName(pluginName, instanceId_);
        QStringList arguments { QStringLiteral("--path"), path_,
                                QStringLiteral("--pipe"), pipeName_,
                                QStringLiteral("--client"), expectedClientId_,
                                QStringLiteral("--port-display"), portDisplayModeArgument(portDisplayMode_) };
        if (!internalPlugin_.isEmpty())
            arguments += { QStringLiteral("--internal_plugin"), internalPlugin_ };
        if (bypass) {
            arguments += {
                QStringLiteral("--bypass"),
                QStringLiteral("--bypass-inputs"), QString::number(inputCount_),
                QStringLiteral("--bypass-outputs"), QString::number(outputCount_),
                QStringLiteral("--bypass-midi-inputs"), QString::number(midiInputCount_)
            };
            for (const auto& label : inputLabels_)
                arguments += { QStringLiteral("--bypass-input-label"), label };
            for (const auto& label : outputLabels_)
                arguments += { QStringLiteral("--bypass-output-label"), label };
        }
        process_->setArguments(arguments);
        const auto* launchedProcess = process_.get();
        QObject::connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), processWatcher_.get(),
                [this, launchedProcess, bypass](int exitCode, QProcess::ExitStatus) {
            if (stopping_ || process_.get() != launchedProcess)
                return;
            if (bypass) {
                status_ = QStringLiteral("The bypass VSTProcess stopped (exit %1). Restart the plugin to recover.").arg(exitCode);
                return;
            }
            bypassed_ = true;
            status_ = QStringLiteral("VSTProcess crashed (exit %1); switching this node to bypass.").arg(exitCode);
            if (inputCount_ <= 0 && outputCount_ <= 0 && midiInputCount_ <= 0)
                return;
            QTimer::singleShot(0, processWatcher_.get(), [this, launchedProcess] {
                if (!stopping_ && process_.get() == launchedProcess && launchedProcess->state() == QProcess::NotRunning)
                    startProcess(true);
            });
        });
        process_->start();
        if (!process_->waitForStarted(3000)) {
            status_ = QStringLiteral("Unable to start VSTProcess.");
            process_.reset();
            return false;
        }
        QString status;
        const auto startupDeadline = GetTickCount64() + 5000;
        while (process_->state() != QProcess::NotRunning && GetTickCount64() < startupDeadline) {
            status = sendPipeCommand("status\n", 250);
            if (status.contains(QStringLiteral("\"active\":true")))
                break;
            Sleep(25);
        }
        if (!status.contains(QStringLiteral("\"active\":true"))) {
            status_ = status.isEmpty() ? QStringLiteral("VSTProcess did not make its control pipe ready before the startup deadline.") : status;
            stopProcess();
            return false;
        }
        const auto document = QJsonDocument::fromJson(status.toUtf8());
        if (document.isObject()) {
            const auto details = document.object();
            inputCount_ = details.value(QStringLiteral("inputs")).toInt(inputCount_);
            outputCount_ = details.value(QStringLiteral("outputs")).toInt(outputCount_);
            midiInputCount_ = details.value(QStringLiteral("midiInputs")).toInt(midiInputCount_);
            inputLabels_.clear();
            for (const auto& label : details.value(QStringLiteral("inputLabels")).toArray())
                inputLabels_.push_back(label.toString());
            outputLabels_.clear();
            for (const auto& label : details.value(QStringLiteral("outputLabels")).toArray())
                outputLabels_.push_back(label.toString());
        }
        bypassed_ = bypass;
        status_ = bypass ? QStringLiteral("VSTProcess is bypassing plugin I/O after a plugin crash.")
                          : QStringLiteral("VSTProcess is running.");
        return true;
    }

    void requestShutdown() noexcept
    {
        sendPipeCommand("quit\n", 3000);
    }

    QString sendPipeCommand(const char* command, DWORD waitMilliseconds = 0) noexcept
    {
        if (pipeName_.isEmpty())
            return {};
        const auto fullPipeName = QStringLiteral("\\\\.\\pipe\\") + pipeName_;
        if (waitMilliseconds != 0 && !WaitNamedPipeW(reinterpret_cast<LPCWSTR>(fullPipeName.utf16()), waitMilliseconds))
            return {};
        const auto pipe = CreateFileW(reinterpret_cast<LPCWSTR>(fullPipeName.utf16()),
                                      GENERIC_READ | GENERIC_WRITE,
                                      0,
                                      nullptr,
                                      OPEN_EXISTING,
                                      0,
                                      nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return {};
        DWORD bytesWritten {};
        if (!WriteFile(pipe, command, static_cast<DWORD>(std::char_traits<char>::length(command)), &bytesWritten, nullptr)) {
            CloseHandle(pipe);
            return {};
        }
        char response[512] {};
        DWORD bytesRead {};
        ReadFile(pipe, response, sizeof(response) - 1, &bytesRead, nullptr);
        CloseHandle(pipe);
        return QString::fromUtf8(response, static_cast<qsizetype>(bytesRead));
    }

    void stopProcess() noexcept
    {
        if (!process_)
            return;
        stopping_ = true;
        requestShutdown();
        if (!process_->waitForFinished(5000) && process_->state() != QProcess::NotRunning) {
            status_ = QStringLiteral("VSTProcess did not exit cleanly; terminating its sandbox.");
            process_->kill();
            process_->waitForFinished(1000);
        }
        process_.reset();
        stopping_ = false;
    }

    inline static std::atomic_uint64_t nextInstance_ { 1 };
    bool initialized_ {};
    QString path_;
    QString clientName_;
    QString pipeName_;
    QString status_ { QStringLiteral("Not started.") };
    QString instanceId_ { QUuid::createUuid().toString(QUuid::WithoutBraces) };
    QString internalPlugin_;
    QString internalPluginName_;
    QString expectedClientId_;
    QString executable_;
    ProcessBits bits_ { ProcessBits::automatic };
    PortDisplayMode portDisplayMode_ { PortDisplayMode::defaultNames };
    std::unique_ptr<QProcess> process_;
    std::unique_ptr<QObject> processWatcher_;
    VstInt32 inputCount_ {};
    VstInt32 outputCount_ {};
    VstInt32 midiInputCount_ {};
    QStringList inputLabels_;
    QStringList outputLabels_;
    bool stopping_ {};
    bool bypassed_ {};
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
    portDisplayMode_ = new QComboBox(this);
    portDisplayMode_->addItem(QStringLiteral("Default plugin names"), static_cast<int>(PortDisplayMode::defaultNames));
    portDisplayMode_->addItem(QStringLiteral("Bus and channel labels"), static_cast<int>(PortDisplayMode::busLabels));
    portDisplayMode_->addItem(QStringLiteral("Standard channel names"), static_cast<int>(PortDisplayMode::standardChannels));
    portDisplayMode_->addItem(QStringLiteral("JACK default names"), static_cast<int>(PortDisplayMode::jackDefault));
    portDisplayMode_->setCurrentIndex(portDisplayMode_->findData(static_cast<int>(node.portDisplayMode())));
    form->addRow(QStringLiteral("Plugin"), pathRow);
    form->addRow(QStringLiteral("Client name"), clientName_);
    form->addRow(QStringLiteral("Process"), bits_);
    form->addRow(QStringLiteral("Port display"), portDisplayMode_);
    layout->addLayout(form);
    auto* reload = new QPushButton(QStringLiteral("Apply and reload"), this);
    restart_ = new QPushButton(QStringLiteral("Restart plugin"), this);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(reload);
    layout->addWidget(restart_);
    layout->addWidget(status_);
    setStyleSheet(QStringLiteral(
        "QLineEdit, QComboBox { color: #f4f7fb; background: #172432; border: 1px solid #6d91ae; min-height: 26px; padding: 1px 6px; }"
        "QComboBox::drop-down { border-left: 1px solid #6d91ae; width: 22px; }"
        "QPushButton { color: #ffffff; background: #3f6684; border: 1px solid #85b4d8; min-height: 28px; padding: 2px 8px; }"
        "QPushButton:hover { background: #527d9f; } QLabel { color: #e4edf5; }"));

    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose VST plugin"), path_->text(),
                                                       QStringLiteral("VST plugins (*.vst3 *.dll)"));
        if (!path.isEmpty())
            path_->setText(path);
    });
    connect(reload, &QPushButton::clicked, this, [this] {
        const auto selectedPath = path_->text().trimmed();
        node_.applySettings(path_->text(), clientName_->text(), static_cast<ProcessBits>(bits_->currentData().toInt()),
                            static_cast<PortDisplayMode>(portDisplayMode_->currentData().toInt()),
                            selectedPath == node_.pluginPath() ? node_.internalPlugin() : QString {},
                            selectedPath == node_.pluginPath() ? node_.internalPluginName() : QString {});
        refresh();
    });
    connect(restart_, &QPushButton::clicked, this, [this] {
        node_.restartPlugin();
        refresh();
    });
    refresh();
}

void SettingsWidget::refresh()
{
    status_->setText(node_.status());
    restart_->setEnabled(!node_.pluginPath().isEmpty());
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

extern "C" WINJACK_NODE_API int wjn_prepare_node_for_canvas(WinJACKNexus::Node::INode* node)
{
    auto* vstNode = dynamic_cast<VstHostNode*>(node);
    return vstNode == nullptr ? 0 : vstNode->prepareForCanvas() ? 1 : 0;
}

extern "C" WINJACK_NODE_API int wjn_show_node_gui(WinJACKNexus::Node::INode* node)
{
    auto* vstNode = dynamic_cast<VstHostNode*>(node);
    return vstNode == nullptr ? 0 : vstNode->showPluginGui() ? 1 : 0;
}

extern "C" WINJACK_NODE_API const char* wjn_get_node_client_id(WinJACKNexus::Node::INode* node)
{
    static thread_local QByteArray clientId;
    auto* vstNode = dynamic_cast<VstHostNode*>(node);
    clientId = vstNode == nullptr ? QByteArray {} : vstNode->expectedClientId().toUtf8();
    return clientId.constData();
}
