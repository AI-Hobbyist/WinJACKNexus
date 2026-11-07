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
    check(ports.front().name == QStringLiteral("节目分区-新闻-主输出-in-L") &&
              ports.back().name == QStringLiteral("节目分区-新闻-主输出-out-Rrb"),
          "port names include nested categories, stream name, direction and speaker");
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
    check(restored.ports().front().name == QStringLiteral("节目分区-新闻-改名-in-L") &&
              restored.ports().front().streamId == ports.front().streamId &&
              !restored.toJson().contains(QStringLiteral("serverBaseUrl")),
          "display rename updates port name while retaining stream identity");
    auto duplicateNames = restored;
    duplicateNames.addStream(duplicateNames.streams[0].name, BC::Layout::stereo, child);
    const auto duplicatePorts = duplicateNames.ports();
    check(duplicatePorts.front().name != duplicatePorts[16].name,
          "duplicate stream paths have distinct JACK names");
    auto longNames = restored;
    longNames.streams[0].name = QString(200, QChar(0x97f3));
    check(longNames.ports().front().name.toUtf8().size() < 190 &&
              !longNames.ports().front().name.contains(QChar::ReplacementCharacter),
          "long Unicode port names fit JACK limits without broken UTF-8");
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
