/* SPDX-License-Identifier: AGPL-3.0-only */
#include "State.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdio>

using namespace WinJACKNexus::SignalSwitch;
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int failures = 0;
    auto check = [&](bool passed, const char *name) {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
        failures += !passed;
    };
    State state;
    for (const auto &id : {"main/a", "sub/b", "sub/c"})
        state.add(id);
    check(state.setExclusive("radio", {"main/a", "sub/b"}), "cross-graph relation");
    const auto original = state.toBase64();
    check(!state.setExclusive("overlap", {"sub/b", "sub/c"}) && state.toBase64() == original,
          "overlap rejected atomically");
    QMap<QString, bool> result;
    check(state.resolve({{"main/a", true}}, {"sub/b"}, true, &result) && !result["main/a"] && result["sub/b"],
          "exclusive winner closes old member");
    check(state.resolve(result, {"sub/b"}, false, &result) && !result["sub/b"], "all exclusive members may be off");
    const auto before = result;
    check(!state.resolve(result, {"main/a", "sub/b"}, true, &result) && result == before,
          "batch conflict leaves state unchanged");
    check(state.resolve(result, {"sub/c"}, true, &result) && result["sub/c"], "independent switch");
    check(state.setGroup("group", QString::fromUtf8("演奏组"), {"main/a", "sub/c"}),
          "layout group independent of exclusive relation");
    state.window.geometry = {321, 234, 777, 555};
    state.window.frameless = true;
    state.window.autoHide = true;
    state.window.edge = "right";
    state.window.opacity = 65;
    state.cards["main/a"].color = QColor("#ff123456");
    state.cards["sub/b"].geometry = {123, 456, 210, 60};
    state.grid = true;
    state.locked = true;
    State restored;
    check(State::fromBase64(state.toBase64(), &restored) && restored.toJson() == state.toJson(),
          "all presets including window geometry roundtrip");
    const auto saved = restored.toBase64();
    check(!State::fromBase64("%%%", &restored) && restored.toBase64() == saved, "corrupt Base64 rejected atomically");
    auto invalid = state.toJson();
    invalid["version"] = 99;
    check(!State::fromBase64(QString::fromLatin1(QJsonDocument(invalid).toJson().toBase64()), &restored) &&
              restored.toBase64() == saved,
          "unsupported version rejected atomically");
    check(state.snapped({19, 35}) == QPoint(16, 32), "grid anchor snapping");
    state.remove("main/a");
    check(state.exclusive.isEmpty() && state.groups["group"].members == QStringList{"sub/c"} && state.validate(),
          "deletion cleans references and preserves single-member group");
    state.ungroup({"sub/c"});
    check(state.groups.isEmpty(), "empty group removed");
    return failures ? 1 : 0;
}
