// SPDX-License-Identifier: AGPL-3.0-only
#include <QCoreApplication>
#include <QJsonDocument>
#include <QLibrary>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <cstdio>

namespace BC = WinJACKNexus::Common::Broadcast;
using Node = WinJACKNexus::Node::INode;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int failures = 0;
    auto check = [&](bool passed, const char *message)
    {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", message);
        failures += !passed;
    };
    QLibrary library(application.applicationDirPath() + "/nodes/lan_remote_audio/mod_broadcast.dll");
    check(library.load(), "load actual broadcast node DLL from nodes/lan_remote_audio");
    if (!library.isLoaded())
    {
        std::printf("%s\n", library.errorString().toUtf8().constData());
        return 1;
    }
    const auto metadata =
        reinterpret_cast<int32_t (*)(WjnNodeMetadataV4 *)>(library.resolve("wjn_get_node_metadata_v4"));
    const auto create = reinterpret_cast<Node *(*)(uint32_t)>(library.resolve("wjn_create_node_v4"));
    const auto destroy = reinterpret_cast<void (*)(Node *)>(library.resolve("wjn_destroy_node"));
    const auto virtualApi =
        reinterpret_cast<const WjnNodeVirtualApiV1 *(*)()>(library.resolve("wjn_get_node_virtual_api_v1"));
    const auto get = reinterpret_cast<const char *(*)(Node *)>(library.resolve("wjn_get_node_settings_json"));
    const auto set =
        reinterpret_cast<int (*)(Node *, const char *)>(library.resolve("wjn_set_node_settings_json"));
    check(metadata && create && destroy && virtualApi && get && set, "required Node v4 and state exports");
    if (!metadata || !create || !destroy || !virtualApi || !get || !set)
        return 1;
    const auto exportedCategory =
        reinterpret_cast<const char *(*)()>(library.resolve("wjn_get_node_category"));
    check(exportedCategory && QByteArray(exportedCategory()) == "LAN & Remote Audio",
          "remote audio category exported by relocated node");
    WjnNodeMetadataV4 info{sizeof(info)};
    check(metadata(&info) && info.version == 4 && info.capabilities == WJN_NODE_JACK_NODE,
          "creation-free metadata advertises JACK only");
    check(!virtualApi() && !create(WJN_NODE_MODE_VIRT) && !create(0) && !create(3),
          "no VIRT processing table or unsupported factory modes");
    auto *first = create(WJN_NODE_MODE_JACK);
    auto *second = create(WJN_NODE_MODE_JACK);
    check(first && second && first != second, "independent JACK-mode instances without JACK init");
    if (!first || !second)
        return 1;
    BC::State source;
    const auto category = source.addCategory(QStringLiteral("节目"));
    const auto stream = source.addStream(QStringLiteral("主输出"), BC::Layout::stereo, category);
    const auto json = QJsonDocument(source.toJson()).toJson(QJsonDocument::Compact);
    check(set(first, json.constData()) == 1, "actual DLL accepts valid project state");
    check(set(second, json.constData()) == 1, "cloning an occupied live identity succeeds with remapping");
    BC::State firstState, secondState;
    check(BC::State::fromJson(QJsonDocument::fromJson(get(first)).object(), firstState) &&
              BC::State::fromJson(QJsonDocument::fromJson(get(second)).object(), secondState),
          "actual DLL exports parseable state");
    check(firstState.streams.size() == 1 && secondState.streams.size() == 1 &&
              firstState.streams[0].id == stream && secondState.streams[0].id != stream,
          "live clone cannot steal the original published stream identity");
    check(set(first, json.constData()) == 1 && QByteArray(get(first)) == json,
          "restoring the same instance retains stream and port identity");
    const auto before = QByteArray(get(first));
    check(!set(first, "{\"version\":99}") && QByteArray(get(first)) == before,
          "invalid project state does not mutate the instance");
    destroy(first);
    auto *recreated = create(WJN_NODE_MODE_JACK);
    check(recreated && set(recreated, json.constData()) && QByteArray(get(recreated)) == json,
          "destroyed instance releases original IDs for project recreation");
    destroy(recreated);
    destroy(second);
    return failures ? 1 : 0;
}
