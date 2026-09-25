/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <QApplication>
#include <QLibrary>
#include <QWidget>
#include <QColor>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
bool check(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bool passed = check(argc == 9, "eight independent effect DLL paths supplied");
    if (!passed) return 1;
    const char* expected[] = {"gate", "graphic-eq", "deesser", "compressor",
                              "limiter", "delay", "parametric-eq", "reverb"};
    for (int index = 0; index < 8; ++index) {
        QLibrary library(QString::fromLocal8Bit(argv[index + 1]));
        if (!check(library.load(), "effect DLL loads")) { passed = false; continue; }
        auto version = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_api_version"));
        auto count = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_count"));
        auto meta = reinterpret_cast<const WjnFxMeta* (*)(uint32_t)>(library.resolve("wjn_fx_meta"));
        auto nodeCount = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_get_node_count"));
        auto create = reinterpret_cast<WjnFxInstance* (*)(uint32_t)>(library.resolve("wjn_fx_create_mixer"));
        auto clone = reinterpret_cast<WjnFxInstance* (*)(const WjnFxInstance*)>(library.resolve("wjn_fx_clone_mixer"));
        auto destroy = reinterpret_cast<void (*)(WjnFxInstance*)>(library.resolve("wjn_fx_destroy_mixer"));
        auto prepare = reinterpret_cast<void (*)(WjnFxInstance*, double)>(library.resolve("wjn_fx_prepare"));
        auto process = reinterpret_cast<void (*)(WjnFxInstance*, uint32_t, const float*, float*, uint32_t)>(library.resolve("wjn_fx_process"));
        auto setChannel = reinterpret_cast<void (*)(WjnFxInstance*, int)>(library.resolve("wjn_fx_set_channel"));
        auto setTheme = reinterpret_cast<void (*)(WjnFxInstance*, const WjnFxTheme*)>(library.resolve("wjn_fx_set_theme"));
        auto editor = reinterpret_cast<QWidget* (*)(WjnFxInstance*, QWidget*)>(library.resolve("wjn_fx_create_mixer_editor"));
        auto presetCount = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_preset_count"));
        auto presetName = reinterpret_cast<const char* (*)(uint32_t)>(library.resolve("wjn_fx_preset_name"));
        auto currentPreset = reinterpret_cast<int (*)(const WjnFxInstance*)>(library.resolve("wjn_fx_current_preset"));
        auto selectPreset = reinterpret_cast<void (*)(WjnFxInstance*, int)>(library.resolve("wjn_fx_select_preset"));
        if (!check(version && count && meta && nodeCount && create && clone && destroy && prepare && process && setChannel && setTheme && editor,
                   "complete Native Fx API exports")) { passed = false; continue; }
        passed &= check(version() == WJN_FX_API_VERSION && count() == 1 && meta(1) == nullptr,
                        "one v1 effect record per DLL");
        const auto* info = meta(0);
        passed &= check(info && std::strcmp(info->id, expected[index]) == 0 && info->iconGlyph[0] != '\0'
                        && info->iconColor[0] == '#' && info->placements == (WJN_FX_MIXER | WJN_FX_NODE)
                        && nodeCount() == 1, "module metadata and node placement agree");
        auto* instance = create(0);
        passed &= check(instance && create(1) == nullptr, "mixer instance uses local module index zero");
        if (!instance) continue;
        prepare(instance, 48000);
        float input[] {0.1F, -0.2F, 0.3F}, output[3] {};
        process(instance, 0, input, output, 3);
        passed &= check(std::isfinite(output[0]) && std::isfinite(output[1]) && std::isfinite(output[2]),
                        "standalone mixer processor produces finite audio");
        auto* duplicate = clone(instance);
        passed &= check(duplicate && duplicate != instance && clone(nullptr) == nullptr,
                        "mixer instance clones independently");
        if (duplicate) destroy(duplicate);
        QWidget parent;
        auto* view = editor(instance, &parent);
        passed &= check(presetCount && presetName && currentPreset && selectPreset
                            && presetCount() >= 4 && presetName(0) && presetName(presetCount()) == nullptr,
                        "Native Fx exports its editor preset list");
        if (presetCount && presetName && currentPreset && selectPreset) {
            const int selected = currentPreset(instance);
            selectPreset(instance, 0);
            passed &= check(currentPreset(instance) == 0 && selected >= 0,
                            "Native Fx preset selection updates its embedded editor");
        }
        passed &= check(view && view->parentWidget() == &parent && view->findChild<QWidget*>()
                        && view->height() < view->findChild<QWidget*>()->height(),
                        "mixer editor clips its node-only channel selector");
        const WjnFxTheme theme {"#101112", "#202122", "#f0f1f2", "#12abcd", "#303132"};
        setTheme(instance, &theme);
        passed &= check(view && view->property("fxAccent").value<QColor>() == QColor("#12abcd"),
                        "theme entry applies host accent override");
        setChannel(instance, 3);
        destroy(instance);
    }
    return passed ? 0 : 1;
}
