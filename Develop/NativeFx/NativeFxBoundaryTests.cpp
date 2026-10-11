/* Native Fx load and UI boundary checks. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QApplication>
#include <QLibrary>
#include <QWidget>
#include <QDir>
#include <QEventLoop>
#include <QFontInfo>
#include <QGlyphRun>
#include <QScreen>
#include <QTextLayout>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../Node/WindowShowCounter.hpp"

namespace
{
bool check(bool result, const char *message)
{
    if (!result)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return result;
}
bool capture(QWidget *window, const QString &path)
{
    if (!window)
    {
        return false;
    }
    window->show();
    window->raise();
    window->activateWindow();
    QEventLoop stable;
    QTimer::singleShot(450, &stable, &QEventLoop::quit);
    stable.exec();
    QTextLayout text(QStringLiteral("节点_日本語_한국어"), QApplication::font());
    text.beginLayout();
    text.createLine();
    text.endLayout();
    for (const auto &run : text.glyphRuns())
    {
        for (const auto glyph : run.glyphIndexes())
        {
            if (!glyph)
            {
                return false;
            }
        }
    }
    return window->isVisible() && IsWindowVisible(reinterpret_cast<HWND>(window->winId())) &&
           window->devicePixelRatioF() == 1.0 &&
           window->screen()->grabWindow(window->winId()).save(path);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    WindowShowCounter windowCounter;
    app.installEventFilter(&windowCounter);
    if (!check((argc == 10 || argc == 11) && app.platformName() == "windows",
               "Windows platform and example/eight built-in DLL paths supplied"))
        return 1;
    const QString captures =
        argc == 11 ? QString::fromUtf8(argv[10]) : QStringLiteral("NativeFxBoundary-real-window");
    if (!check(QDir().mkpath(captures), "real-window screenshot directory"))
        return 1;
    unsigned displays = 0;
    bool passed = true;
    for (int i = 1; i < 10; ++i)
    {
        QLibrary library(QString::fromLocal8Bit(argv[i]));
        if (!check(library.load(), "Native Fx DLL loads"))
        {
            passed = false;
            continue;
        }
        const auto version =
            reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_api_version"));
        const auto count = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_count"));
        const auto meta =
            reinterpret_cast<const WjnFxMeta *(*)(uint32_t)>(library.resolve("wjn_fx_meta"));
        const auto nodeVersion =
            reinterpret_cast<const char *(*)()>(library.resolve("wjn_get_node_api_version"));
        const auto boundaryVersion =
            reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_node_boundary_version"));
        const auto layout =
            reinterpret_cast<int (*)(void *)>(library.resolve("wjn_fx_node_validate_layout"));
        const auto modeLayout = reinterpret_cast<int (*)(void *, uint32_t)>(
            library.resolve("wjn_fx_node_validate_layout_mode"));
        const auto virtualApi = reinterpret_cast<decltype(&wjn_get_node_virtual_api_v1)>(
            library.resolve("wjn_get_node_virtual_api_v1"));
        const auto clientName =
            reinterpret_cast<const char *(*)(void *)>(library.resolve("wjn_fx_node_client_name"));
        const auto setClientName = reinterpret_cast<int (*)(void *, const char *)>(
            library.resolve("wjn_fx_node_set_client_name"));
        const auto createNode =
            reinterpret_cast<decltype(&wjn_create_node_v4)>(library.resolve("wjn_create_node_v4"));
        const auto nodeMetadata = reinterpret_cast<decltype(&wjn_get_node_metadata_v4)>(
            library.resolve("wjn_get_node_metadata_v4"));
        const auto destroyNode = reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(
            library.resolve("wjn_destroy_node"));
        const auto showGui = reinterpret_cast<int (*)(WinJACKNexus::Node::INode *)>(
            library.resolve("wjn_show_node_gui"));
        const auto createMixer =
            reinterpret_cast<WjnFxInstance *(*)(uint32_t)>(library.resolve("wjn_fx_create_mixer"));
        const auto destroyMixer =
            reinterpret_cast<void (*)(WjnFxInstance *)>(library.resolve("wjn_fx_destroy_mixer"));
        const auto editor = reinterpret_cast<QWidget *(*)(WjnFxInstance *, QWidget *)>(
            library.resolve("wjn_fx_create_mixer_editor"));
        if (!check(version && count && meta && nodeVersion && boundaryVersion && layout &&
                       clientName && setClientName && createNode && destroyNode && showGui &&
                       createMixer && destroyMixer && editor && nodeMetadata && modeLayout &&
                       virtualApi,
                   "required dual-placement exports exist"))
        {
            passed = false;
            continue;
        }
        const auto *info = meta(0);
        passed &= check(version() == WJN_FX_API_VERSION && count() == 1 && info &&
                            info->placements == (WJN_FX_NODE | WJN_FX_MIXER) &&
                            std::strcmp(nodeVersion(), "4") == 0 &&
                            !library.resolve("wjn_get_node_count") &&
                            boundaryVersion() == WJN_FX_NODE_BOUNDARY_VERSION,
                        "placement and boundary versions agree");
        WjnNodeMetadataV4 capabilities{};
        capabilities.structSize = sizeof(capabilities);
        passed &=
            check(nodeMetadata(&capabilities) == 1 &&
                      capabilities.capabilities == (WJN_NODE_JACK_NODE | WJN_NODE_VIRT_NODE) &&
                      capabilities.features == WJN_NODE_HAS_GUI,
                  "Fx metadata exposes implemented dual modes");
        auto *virtualNode = createNode(WJN_NODE_MODE_VIRT);
        const auto *processing = virtualApi();
        WjnNodePrepareV1 preparation{sizeof(preparation), 64, 48000, nullptr};
        passed &= check(virtualNode && processing && !virtualNode->init() &&
                            modeLayout(virtualNode, WJN_NODE_MODE_VIRT) == 0 &&
                            processing->prepare(virtualNode, &preparation) == 1 &&
                            modeLayout(virtualNode, WJN_NODE_MODE_VIRT) == 1 &&
                            modeLayout(virtualNode, WJN_NODE_MODE_JACK) == 0 &&
                            layout(virtualNode) == 0 && processing->portCount(virtualNode) == 16,
                        "virtual preparation has logical layout without JACK initialization");
        if (virtualNode && processing)
        {
            static constexpr const char *channels[]{"L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"};
            for (uint32_t index = 0; index < 16; ++index)
            {
                WjnNodePortV1 port{};
                port.structSize = sizeof(port);
                const auto expectedName =
                    std::string(index < 8 ? "in_" : "out_") + channels[index % 8];
                passed &= check(processing->port(virtualNode, index, &port) == 1 &&
                                    port.id == index + 1 && port.type == WJN_NODE_PORT_AUDIO &&
                                    port.channels == 1 && port.name == expectedName &&
                                    port.direction ==
                                        (index < 8 ? WJN_NODE_PORT_INPUT : WJN_NODE_PORT_OUTPUT),
                                "logical eight-channel order and directions");
            }
            const auto virtualWindow = virtualNode->createWindow(nullptr);
            passed &= check(virtualWindow &&
                                capture(QWidget::find(reinterpret_cast<WId>(virtualWindow)),
                                        captures + QStringLiteral("/virtual-node-%1.png").arg(i)),
                            "virtual node standalone GUI in real Windows window");
            ++displays;
            virtualNode->destroyWindow(virtualWindow);
            processing->stop(virtualNode);
            passed &= check(modeLayout(virtualNode, WJN_NODE_MODE_VIRT) == 0,
                            "stopped virtual layout is inactive");
            destroyNode(virtualNode);
        }
        auto *node = createNode(WJN_NODE_MODE_JACK);
        if (!check(node != nullptr, "node instance is created"))
        {
            passed = false;
            continue;
        }
        const auto *current = clientName(node);
        const std::string original = current ? current : "";
        passed &= check(!original.empty() && layout(node) == 0,
                        "uninitialized ports are rejected and the node has a name");
        passed &= check(setClientName(node, "BoundaryTest") == 1 &&
                            std::strcmp(clientName(node), "BoundaryTest") == 0 &&
                            setClientName(node, "") == 0,
                        "host-owned client-name editor can update and reject names");
        passed &=
            check(setClientName(node, original.c_str()) == 1, "original client name is restored");
        const auto window = node->createWindow(nullptr);
        passed &= check(window != nullptr, "standalone GUI exists");
        passed &= check(capture(QWidget::find(reinterpret_cast<WId>(window)),
                                captures + QStringLiteral("/node-%1.png").arg(i)),
                        "visible Windows node editor at 100% with system CJK fonts");
        ++displays;
        node->destroyWindow(window);
        destroyNode(node);
        auto *mixer = createMixer(0);
        passed &= check(mixer != nullptr, "separate mixer instance is created");
        if (mixer)
        {
            QWidget parent;
            auto *view = editor(mixer, &parent);
            passed &= check(view != nullptr && view->parentWidget() == &parent,
                            "mixer editor embeds under host ownership");
            if (view)
            {
                parent.resize(view->sizeHint().expandedTo(QSize(800, 400)));
                view->resize(parent.size());
                passed &= check(capture(&parent, captures + QStringLiteral("/mixer-%1.png").arg(i)),
                                "visible Windows mixer editor at 100%");
                ++displays;
                parent.close();
            }
            if (i == 1)
            {
                const auto snapshotSize = reinterpret_cast<uint32_t (*)()>(
                    library.resolve("wjn_fx_parameter_snapshot_size"));
                const auto exportSnapshot =
                    reinterpret_cast<int (*)(const WjnFxInstance *, void *, uint32_t)>(
                        library.resolve("wjn_fx_export_parameter_snapshot"));
                const auto importSnapshot =
                    reinterpret_cast<int (*)(WjnFxInstance *, const void *, uint32_t)>(
                        library.resolve("wjn_fx_import_parameter_snapshot"));
                const auto process =
                    reinterpret_cast<void (*)(WjnFxInstance *, uint32_t, const float *, float *,
                                              uint32_t)>(library.resolve("wjn_fx_process"));
                passed &= check(snapshotSize && exportSnapshot && importSnapshot && process,
                                "example mixer DSP and snapshot exports exist");
                if (snapshotSize && exportSnapshot && importSnapshot && process)
                {
                    std::vector<unsigned char> snapshot(snapshotSize());
                    passed &= check(exportSnapshot(mixer, snapshot.data(), snapshot.size()) == 1,
                                    "example exports eight-channel snapshot");
                    WjnFxParameterSnapshotHeader header{};
                    std::memcpy(&header, snapshot.data(), sizeof(header));
                    header.channelMask = 1;
                    std::memcpy(snapshot.data(), &header, sizeof(header));
                    const float half = 0.5F;
                    std::memcpy(snapshot.data() + sizeof(header), &half, sizeof(half));
                    passed &= check(importSnapshot(mixer, snapshot.data(), snapshot.size()) == 1,
                                    "example imports selected-channel gain");
                    const float input[]{1.0F, -1.0F};
                    float output[2]{};
                    process(mixer, 0, input, output, 2);
                    passed &= check(std::abs(output[0] - 0.5F) < 0.0001F &&
                                        std::abs(output[1] + 0.5F) < 0.0001F,
                                    "example mixer processes the selected gain");
                }
            }
            destroyMixer(mixer);
        }
    }
    std::printf("Native Fx editors=%u, all window/popup/tooltip shows=%u/100, "
                "platform=windows, DPR=1, systemFont=%s\n",
                displays, windowCounter.shows, QFontInfo(app.font()).family().toUtf8().constData());
    return passed && windowCounter.shows <= 100 ? 0 : 1;
}
