// Production global settings in real Windows windows, without JACK startup.
// SPDX-License-Identifier: AGPL-3.0-only
#define WJN_HOST_I18N_TEST
#include "../../src/app/main.cpp"
#include <QFontDatabase>
#include <QGlyphRun>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <iostream>
#include <stdexcept>

void checkBudget(bool condition, const char *description)
{
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        checkBudget(argc == 3 && app.platformName() == "windows", "Windows/repo/output required");
        QTemporaryDir isolated;
        checkBudget(isolated.isValid(), "isolated settings root");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
        const auto systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        app.setFont(systemFont);
        WinJACKNexus::UI::HostI18n host({isolated.filePath("i18n.ini"), {"zh-CN"}, false});
        QString error;
        checkBudget(host.initialize(QString::fromUtf8(argv[1]), &error), qPrintable(error));
        WinJACKNexus::UI::ThemeCore core;
        checkBudget(core.isValid(), qPrintable(core.initializationError()));
        checkBudget(core.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                                     QStringLiteral("system"), {}, &error),
                    qPrintable(error));
        WinJACKNexus::UI::ThemeManager::installThemeCore(&core);
        WinJACKNexus::UI::ThemeManager manager;
        manager.watchApplication(&app);
        WinJACKNexus::UI::ThemeManagementDialog themes(core);
        GlobalSettingsDialog global(themes);
        global.show();
        auto *panel = dynamic_cast<WinJACKNexus::App::SynthResourceSettings *>(
            global.findChild<QWidget *>(QStringLiteral("sf2-loader-global-settings")));
        auto *budget =
            global.findChild<QSpinBox *>(QStringLiteral("application.sf2LoaderResourceBudgetMiB"));
        QTest::qWait(450);
        checkBudget(panel && budget && panel->isVisible() && budget->value() == 64,
                    "installed module exposes default 64 MiB in actual global settings");
        panel->refreshAvailability(isolated.path());
        checkBudget(!panel->isVisible(), "module absent hides the entire settings card");
        QFile incompatible(isolated.filePath("mod_sf2_loader.dll"));
        checkBudget(incompatible.open(QIODevice::WriteOnly), "invalid module fixture");
        incompatible.write("not a compatible single-node DLL");
        incompatible.close();
        panel->refreshAvailability(isolated.path());
        checkBudget(!panel->isVisible(), "invalid module does not expose settings");
        panel->refreshAvailability(QCoreApplication::applicationDirPath() + "/nodes");
        checkBudget(panel->isVisible(), "restored module availability");
        global.raise();
        global.activateWindow();
        budget->setFocus();
        QTest::keyClick(budget, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(budget, "256");
        QTest::keyClick(budget, Qt::Key_Tab);
        checkBudget(WinJACKNexus::Common::MidiTools::resourceBudgetMiB() == 256,
                    "real keyboard edit persists global budget");
        GlobalSettingsDialog reopened(themes);
        auto *restored = reopened.findChild<QSpinBox *>(
            QStringLiteral("application.sf2LoaderResourceBudgetMiB"));
        checkBudget(restored && restored->value() == 256, "fresh settings dialog restores value");
        const auto output = QString::fromUtf8(argv[2]);
        QDir().mkpath(output);
        for (const auto &language : {"zh-CN", "en-US", "ja-JP", "ko-KR"})
        {
            checkBudget(host.manager().setLanguage(QString::fromLatin1(language), &error),
                        qPrintable(error));
            global.raise();
            global.activateWindow();
            QTest::qWait(450);
            checkBudget(global.isVisible() &&
                            IsWindowVisible(reinterpret_cast<HWND>(global.winId())) &&
                            global.devicePixelRatioF() == 1.0,
                        "actual visible Windows global settings at 100 percent");
            checkBudget(budget->value() == 256 && !budget->toolTip().isEmpty(),
                        "language change preserves budget and localized explanation");
            const auto expected = host.manager().text("midi.sf2_loader", "globalBudget", "missing");
            checkBudget(expected != "missing" && budget->accessibleName() == expected,
                        "localized module settings label");
            for (const auto *label : panel->findChildren<QLabel *>())
            {
                QTextLayout text(label->text(), label->font());
                text.beginLayout();
                text.createLine();
                text.endLayout();
                for (const auto &run : text.glyphRuns())
                {
                    for (auto glyph : run.glyphIndexes())
                    {
                        checkBudget(glyph != 0, "system font renders CJK without tofu");
                    }
                }
                checkBudget(label->width() >= label->sizeHint().width(), "budget label fits");
            }
            checkBudget(global.screen()
                            ->grabWindow(global.winId())
                            .save(output + "/global-budget-" + language + ".png"),
                        "capture visible production global settings window");
        }
        global.close();
        std::cout << "PASS production global settings: installed/absent/incompatible module, "
                     "default64, keyboard edit/persistence, four languages; windows/DPR1, "
                     "system font; window batch below8/100; no JACK initialization\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
