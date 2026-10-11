/* Native Fx load and UI boundary checks. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QApplication>
#include <QLibrary>
#include <QWidget>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
bool check(bool result, const char *message)
{
    if (!result)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return result;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (!check(argc == 10, "example and eight built-in DLL paths supplied"))
        return 1;
    bool passed = true;
    for (int i = 1; i < argc; ++i)
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
        const auto clientName =
            reinterpret_cast<const char *(*)(void *)>(library.resolve("wjn_fx_node_client_name"));
        const auto setClientName = reinterpret_cast<int (*)(void *, const char *)>(
            library.resolve("wjn_fx_node_set_client_name"));
        const auto createNode = reinterpret_cast<WinJACKNexus::Node::INode *(*)(void)>(
            library.resolve("wjn_create_node"));
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
                       createMixer && destroyMixer && editor,
                   "required dual-placement exports exist"))
        {
            passed = false;
            continue;
        }
        const auto *info = meta(0);
        passed &= check(version() == WJN_FX_API_VERSION && count() == 1 && info &&
                            info->placements == (WJN_FX_NODE | WJN_FX_MIXER) &&
                            std::strcmp(nodeVersion(), "3") == 0 &&
                            !library.resolve("wjn_get_node_count") &&
                            boundaryVersion() == WJN_FX_NODE_BOUNDARY_VERSION,
                        "placement and boundary versions agree");
        auto *node = createNode();
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
    return passed ? 0 : 1;
}
