// SPDX-License-Identifier: AGPL-3.0-only
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <cstdio>

namespace BC = WinJACKNexus::Common::Broadcast;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int failures = 0;
    auto check = [&](bool value, const char *message)
    {
        std::printf("%s %s\n", value ? "PASS" : "FAIL", message);
        failures += !value;
    };
    BC::State state;
    const auto parent = state.addCategory(QStringLiteral("节目分区"));
    const auto child = state.addCategory(QStringLiteral("新闻"), parent);
    const auto stream = state.addStream(QStringLiteral("主输出"), BC::Layout::surround71, child);
    check(!stream.isEmpty() &&
              state.categoryPath(child) == QStringList{QStringLiteral("节目分区"), QStringLiteral("新闻")},
          "nested category path and stable stream identity");
    const auto ports = state.ports();
    check(ports.size() == 16 && ports.front().input && !ports.back().input,
          "7.1 has eight input and eight same-layout monitor ports");
    state.streams[0].name = QStringLiteral("改名");
    state.streams[0].gainDb = -6;
    state.streams[0].mute = true;
    state.streams[0].listen = true;
    state.streams[0].solo = true;
    auto json = state.toJson();
    json.insert(QStringLiteral("serverBaseUrl"), QStringLiteral("https://project-must-not-override"));
    BC::State restored;
    check(BC::State::fromJson(json, restored), "valid state roundtrip");
    check(restored.streams[0].id == stream && restored.streams[0].gainDb == -6 && restored.streams[0].mute &&
              !restored.streams[0].listen && !restored.streams[0].solo,
          "restore retains broadcast controls and always disarms Listen/S");
    check(restored.ports() == ports && !restored.toJson().contains(QStringLiteral("serverBaseUrl")),
          "display rename preserves port identity; project cannot persist global connection");
    auto invalid = restored;
    invalid.categories[0].parent = child;
    check(!invalid.validate(), "category cycle rejected");
    invalid = restored;
    invalid.streams.push_back(invalid.streams[0]);
    check(!invalid.validate(), "duplicate stream identity rejected");
    invalid = restored;
    invalid.streams[0].id = QStringLiteral("{%1}").arg(invalid.streams[0].id);
    check(!invalid.validate(), "stream identity must use a canonical URL-safe UUID");
    auto malformed = json;
    malformed.insert(QStringLiteral("version"), 99);
    const auto before = restored.toJson();
    check(!BC::State::fromJson(malformed, restored) && restored.toJson() == before,
          "failed restore leaves current state untouched");
    return failures ? 1 : 0;
}
