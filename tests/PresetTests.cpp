/* SPDX-License-Identifier: AGPL-3.0-only */
#include "meter/MeterPresetRegression.hpp"
#include "theme/FxChainPresetRegression.hpp"
#include <WinJACKNexus/UI/ThemePackage.hpp>
#include <QFile>
#include <cstdio>

namespace {
bool passed = true;
bool check(bool condition, const char* message) {
    if (!condition) { passed = false; std::fprintf(stderr, "FAILED: %s\n", message); }
    return condition;
}
}
int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (QFile::exists(QStringLiteral("themes/org.example-my-theme.wjnskin"))) {
        WinJACKNexus::UI::ThemePackage theme;
        QString error;
        check(WinJACKNexus::UI::ThemePackage::loadFromFile(
                  QStringLiteral("themes/org.example-my-theme.wjnskin"), &theme, &error),
              "import the generated editable theme archive");
    }
    verifyFxChainPresets(app, check);
    verifyMeterPresets(check);
    if (!passed) return 1;
    std::puts("FX and LUFS preset, popup, height and PNG export tests passed");
    return 0;
}
