// Actual DLL + isolated x86/x64 workers, no running JACK server.
// SPDX-License-Identifier: AGPL-3.0-only
#include <WinJACKNexus/Node/ModuleCompatibility.hpp>
#include <WinJACKNexus/Common/VstBlockIpc.hpp>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <TlHelp32.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool passed, const char *message)
{
    if (!passed)
        throw std::runtime_error(message);
}
std::vector<DWORD> children()
{
    std::vector<DWORD> result;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        if (entry.th32ParentProcessID == GetCurrentProcessId() &&
            (_wcsicmp(entry.szExeFile, L"VSTProcess32.exe") == 0 ||
             _wcsicmp(entry.szExeFile, L"VSTProcess64.exe") == 0))
            result.push_back(entry.th32ProcessID);
    CloseHandle(snapshot);
    return result;
}
QByteArray settings(const QString &path, int bits)
{
    return QJsonDocument(QJsonObject{{"path", path},
                                     {"bits", QString::number(bits)},
                                     {"clientName", "S5 offline fixture"}})
        .toJson(QJsonDocument::Compact);
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        require(argc == 2 || argc == 3, "runtime root required");
        require(QGuiApplication::platformName() == "windows", "native Windows Qt backend required");
        const auto runtime = QString::fromUtf8(argv[1]);
        if (argc == 2)
        {
            // Deploy only this test and workers beside private JACK replacements.
            QTemporaryDir deployment;
            require(deployment.isValid(), "private runtime deployment");
            require(QDir().mkpath(deployment.filePath("platforms")) &&
                        QFile::copy(runtime + "/platforms/qwindows.dll",
                                    deployment.filePath("platforms/qwindows.dll")),
                    "deploy matching native Windows Qt platform plugin");
            for (const auto name :
                 {"VSTProcess32.exe", "VSTProcess64.exe", "wjn_vst_mode_tests.exe"})
                require(QFile::copy(runtime + '/' + name, deployment.filePath(name)),
                        "copy test worker");
            require(QFile::copy(runtime + "/tests/vst-s5/JackFixture64.dll",
                                deployment.filePath("libjack64.dll")) &&
                        QFile::copy(runtime + "/tests/vst-s5/JackFixture32.dll",
                                    deployment.filePath("libjack.dll")),
                    "private offline JACK DLLs");
            QProcess inner;
            inner.setProgram(deployment.filePath("wjn_vst_mode_tests.exe"));
            inner.setArguments({runtime, "--inner"});
            inner.setProcessChannelMode(QProcess::ForwardedChannels);
            inner.start();
            const bool finished = inner.waitForStarted(3000) && inner.waitForFinished(90000);
            if (!finished || inner.exitCode() != 0)
            {
                deployment.setAutoRemove(false);
                std::cerr << "Private runtime: " << deployment.path().toStdString()
                          << "; exit=" << inner.exitCode() << "; status=" << int(inner.exitStatus())
                          << "; error=" << inner.errorString().toStdString() << '\n';
            }
            require(finished && inner.exitCode() == 0, "private actual-host test completes");
            return 0;
        }
        require(children().empty(), "no pre-existing child workers");
        const auto modulePath = runtime + "/nodes/vst/mod_vst_host.dll";
        const auto library = LoadLibraryW(reinterpret_cast<const wchar_t *>(modulePath.utf16()));
        require(library != nullptr, "actual VST Host DLL loads");
        namespace Node = WinJACKNexus::Node;
        Node::ModuleContract contract;
        require(Node::inspectNodeModule(library, contract) == Node::ModuleError::none &&
                    contract.metadata.capabilities == 3,
                "complete dual-mode VST contract");
        const auto symbol = [&](const char *name) {
            const auto pointer = GetProcAddress(library, name);
            require(pointer != nullptr, name);
            return pointer;
        };
        const auto destroy =
            reinterpret_cast<decltype(&wjn_destroy_node)>(symbol("wjn_destroy_node"));
        const auto set = reinterpret_cast<int (*)(Node::INode *, const char *)>(
            symbol("wjn_set_node_settings_json"));
        const auto get =
            reinterpret_cast<const char *(*)(Node::INode *)>(symbol("wjn_get_node_settings_json"));
        const auto counter = reinterpret_cast<uint64_t (*)(Node::INode *, uint32_t)>(
            symbol("wjn_vst_virtual_counter"));
        const auto *api = contract.processing;
        for (const int bits : {32, 64})
        {
            for (const int type : {2, 3})
            {
                const auto plugin = runtime + QString("/tests/vst-s5/Vst%1BlockFixture%2.%3")
                                                  .arg(type)
                                                  .arg(bits)
                                                  .arg(type == 2 ? "dll" : "vst3");
                require(QFile::exists(plugin), "architecture-specific fixture exists");
                const auto configured = settings(plugin, bits);
                std::cerr << "Checking VST" << type << " x" << bits
                          << " actual JACK/VIRT workers\n";
                QTemporaryDir record;
                const auto recordPath = record.filePath("jack.bin");
                qputenv("WJN_S5_JACK_RECORD", recordPath.toUtf8());
                auto *jack = Node::createNodeForMode(library, WJN_NODE_MODE_JACK);
                require(jack && set(jack, configured.constData()) == 1 && jack->init(),
                        "actual JACK-mode host starts private offline worker");
                WjnNodePrepareV1 preparation{sizeof(preparation), 64, 48000, nullptr};
                require(api->prepare(jack, &preparation) == 0,
                        "JACK instance rejects VIRT processing path");
                QTest::qWait(150);
                const QByteArray savedJack = get(jack);
                jack->shutdown();
                destroy(jack);
                QFile recorded(recordPath);
                require(recorded.open(QIODevice::ReadOnly),
                        "offline JACK registration and processing record");
                const auto bytes = recorded.readAll();
                uint32_t values[3]{};
                float sample{};
                require(bytes.size() == 16, "JACK trace has fixed size");
                std::memcpy(values, bytes.constData(), sizeof(values));
                std::memcpy(&sample, bytes.constData() + sizeof(values), sizeof(sample));
                require(values[0] == 1 && values[1] == 5 && values[2] > 0 &&
                            std::abs(sample - 1.048F) < 0.0001F,
                        "JACK-only worker registers five ports and processes fixture audio");
                recorded.close();
                recorded.remove();

                auto *virt = Node::createNodeForMode(library, WJN_NODE_MODE_VIRT);
                require(virt && !virt->init() && set(virt, savedJack.constData()) == 1 &&
                            api->prepare(virt, &preparation) == 1,
                        "VIRT restores JACK-saved identity/state without JACK init");
                require(api->portCount(virt) == 5,
                        "VIRT exposes stereo input/output and MIDI logical ports");
                for (uint32_t index = 0; index < 5; ++index)
                {
                    WjnNodePortV1 port{};
                    port.structSize = sizeof(port);
                    require(api->port(virt, index, &port) == 1 && port.id == index + 1 &&
                                port.direction == uint32_t(index < 2 || index == 4
                                                               ? WJN_NODE_PORT_INPUT
                                                               : WJN_NODE_PORT_OUTPUT) &&
                                port.type ==
                                    uint32_t(index == 4 ? WJN_NODE_PORT_MIDI : WJN_NODE_PORT_AUDIO),
                            "stable VST logical layout and direction");
                }
                std::array<float, 128> input{}, output{}, clock{};
                input.fill(2.0F);
                float *inputPointers[]{input.data()};
                float *outputPointers[]{output.data()};
                float *clockPointers[]{clock.data()};
                WjnNodeAudioBufferV1 in{1, 1, inputPointers};
                WjnNodeAudioBufferV1 out[]{{3, 1, outputPointers}, {4, 1, clockPointers}};
                uint8_t note[]{0x90, 60, 100};
                WjnNodeMidiEventV1 event{17, 3, note};
                WjnNodeMidiInputV1 midi{5, 1, &event};
                WjnNodeProcessV1 block{};
                block.structSize = sizeof(block);
                block.frames = 64;
                block.samplePosition = 123456;
                block.audioInputCount = 1;
                block.audioInputs = &in;
                block.audioOutputCount = 2;
                block.audioOutputs = out;
                block.midiInputCount = 1;
                block.midiInputs = &midi;
                const auto processUntil = [&](float expected) {
                    for (unsigned attempt = 0; attempt < 100; ++attempt)
                    {
                        api->process(virt, &block);
                        if (std::abs(output[0] - expected) < 0.0001F)
                            return true;
                        QTest::qWait(5);
                    }
                    return false;
                };
                require(
                    processUntil(1.048F) && clock[0] == 456 && clock[17] == 1017,
                    "actual VIRT worker preserves root rate/position, audio and MIDI frame offset");
                require(!QFile::exists(recordPath), "VIRT worker has zero JACK registrations");
                auto changed = QJsonDocument::fromJson(QByteArray(get(virt))).object();
                auto preset =
                    QByteArray::fromBase64(changed.value("presetData").toString().toLatin1());
                require(preset.size() >= 36, "plugin preset state captured through existing pipe");
                const float gain = 0.25F;
                std::memcpy(preset.data() + 32, &gain, sizeof(gain));
                if (type == 3 && preset.size() >= 40)
                    std::memcpy(preset.data() + 36, &gain, sizeof(gain));
                changed["presetData"] = QString::fromLatin1(preset.toBase64());
                const auto state = QJsonDocument(changed).toJson(QJsonDocument::Compact);
                require(set(virt, state.constData()) == 1 && processUntil(0.548F),
                        "existing preset state restores independently into VIRT worker");
                auto *independentJack = Node::createNodeForMode(library, WJN_NODE_MODE_JACK);
                require(independentJack && set(independentJack, savedJack.constData()) == 1 &&
                            independentJack->init(),
                        "same plugin runs in independent simultaneous JACK sandbox");
                const auto independentState =
                    QJsonDocument::fromJson(QByteArray(get(independentJack))).object();
                const auto independentPreset = QByteArray::fromBase64(
                    independentState.value("presetData").toString().toLatin1());
                float independentGain{};
                require(independentPreset.size() >= 36, "independent JACK preset captured");
                std::memcpy(&independentGain, independentPreset.data() + 32,
                            sizeof(independentGain));
                require(independentGain == 0.5F && processUntil(0.548F),
                        "JACK and VIRT plugin parameter and DSP states remain independent");
                independentJack->shutdown();
                destroy(independentJack);
                require(QFile::remove(recordPath), "remove independent JACK fixture record");
                std::array<WjnNodeMidiEventV1, 1100> overflow;
                overflow.fill(event);
                midi.eventCount = overflow.size();
                midi.events = overflow.data();
                api->reset(virt);
                for (unsigned attempt = 0; attempt < 5; ++attempt)
                {
                    api->process(virt, &block);
                    QTest::qWait(10);
                }
                require(counter(virt, 1) >= 76, "bounded MIDI overflow reports dropped events");
                midi.eventCount = 1;
                midi.events = &event;
                if (type == 2 && bits == 64)
                {
                    const auto owned = children();
                    require(owned.size() == 1, "one owned VIRT sandbox before crash");
                    const auto process =
                        OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, owned[0]);
                    require(process && TerminateProcess(process, 7),
                            "terminate only this test's worker");
                    WaitForSingleObject(process, 3000);
                    CloseHandle(process);
                    QTest::qWait(150);
                    require(processUntil(2.0F), "worker crash recreates existing isolated bypass");
                }
                api->stop(virt);
                output.fill(1.0F);
                api->process(virt, &block);
                require(output[0] == 0 && children().empty(),
                        "stop clears output and releases owned worker");
                destroy(virt);
                std::cout
                    << "PASS VST" << type << " x" << bits
                    << " JACK/VIRT actual workers, state, ports, clock/audio/MIDI, bounded overflow, cleanup\n";
            }
        }
        auto *missing = Node::createNodeForMode(library, WJN_NODE_MODE_VIRT);
        require(set(missing, settings(runtime + "/missing.vst3", 64).constData()) == 1,
                "missing plugin record retained");
        WjnNodePrepareV1 preparation{sizeof(preparation), 64, 48000, nullptr};
        require(api->prepare(missing, &preparation) == 0, "missing plugin fails VIRT preparation");
        api->stop(missing);
        destroy(missing);
        require(children().empty(), "failure leaves no test-owned processes");
        namespace IPC = WinJACKNexus::Common::VstBlockIpc;
        IPC::Mapping incompatible;
        const auto name = QString("WinJACKNexus.S5.reject.%1").arg(GetCurrentProcessId());
        require(incompatible.create(name.toStdWString(), 64, 48000), "protocol rejection fixture");
        incompatible.block()->protocol = 999;
        QProcess rejected;
        rejected.setProgram(QCoreApplication::applicationDirPath() + "/VSTProcess64.exe");
        rejected.setArguments({"--path", runtime + "/tests/vst-s5/Vst2BlockFixture64.dll", "--pipe",
                               name + ".control", "--virtual-blocks-v1", name});
        rejected.setProcessChannelMode(QProcess::ForwardedChannels);
        rejected.start();
        require(rejected.waitForStarted(3000) && rejected.waitForFinished(5000) &&
                    rejected.exitCode() != 0,
                "worker rejects incompatible shared-block protocol before processing");
        FreeLibrary(library);
        std::cout
            << "PASS VST dual-mode matrix; private JACK fixture only, no real server and no visible test windows\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        for (const auto pid : children())
        {
            const auto child = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
            if (child)
            {
                TerminateProcess(child, 8);
                WaitForSingleObject(child, 3000);
                CloseHandle(child);
            }
        }
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
