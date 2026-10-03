/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManagementDialog.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemePackage.hpp>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include "../../src/ui/src/LocalizedThemeMessages.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSpinBox>
#include <QTabWidget>

#include <cstdio>

using namespace WinJACKNexus::UI;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
QJsonObject metadata() {
    return {{"formatVersion", 1}, {"themeApiVersion", 1},
        {"id", "org.winjacknexus-management-test"}, {"name", "Management fixture"},
        {"version", "1.0.0"}, {"author", "Theme tests"},
        {"description", "Real-window management fixture"}, {"homepage", "https://example.com/theme"},
        {"kind", "full"}, {"namespaces", QJsonArray {"--wjn-theme-global-"}}};
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    check(QApplication::platformName() == QStringLiteral("windows"), "Theme UI verification requires Windows platform");
    application.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    check(application.primaryScreen() != nullptr, "actual Windows display is available");
    QTemporaryDir languageSettings;
    I18nOptions languageOptions;
    languageOptions.settingsFile = languageSettings.filePath(QStringLiteral("language.ini"));
    HostI18n languageHost(languageOptions);
    QString languageError;
    check(languageHost.initialize(QCoreApplication::applicationDirPath(), &languageError), "Theme i18n host initializes with isolated language settings");
    QDir().mkpath(QStringLiteral("tests/preview/S10"));
    ThemeCore core;
    ThemeManager::installThemeCore(&core);
    ThemeManagementDialog dialog(core);
    dialog.show();
    application.processEvents();
    const auto baseline = dialog.grab().toImage();
    check(baseline.save(QStringLiteral("tests/preview/S10/theme-management-unmodified.png")),
          "save actual management popup baseline");
    auto* overlay = dialog.findChild<QCheckBox*>(QStringLiteral("theme.upperOverlay"));
    auto* blur = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("theme.blurRadius"));
    auto* componentOpacity = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("theme.componentOpacity"));
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("theme.tabs"));
    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("theme.overlayHint"));
    auto* backgroundScope = dialog.findChild<QComboBox*>(QStringLiteral("theme.backgroundScope"));
    auto* imageMode = dialog.findChild<QComboBox*>(QStringLiteral("theme.imageMode"));
    auto* applyBackground = dialog.findChild<QPushButton*>(QStringLiteral("theme.applyBackground"));
    check(overlay && hint && blur && componentOpacity && backgroundScope && imageMode && applyBackground,
          "management background controls exist through shared UI API");
    auto* nameDraft = dialog.findChild<QLineEdit*>(QStringLiteral("theme.metadata.name"));
    auto* colorDraft = dialog.findChild<QLineEdit*>(QStringLiteral("theme.colorValue"));
    auto* diagnosticsView = dialog.findChild<QPlainTextEdit*>(QStringLiteral("theme.diagnostics"));
    ThemePackage diagnosticPackage;
    check(ThemePackage::create(metadata(), ":root { --wjn-theme-global-unrecognized-i18n: 1; }", {},
                               &diagnosticPackage, &languageError)
          && core.setFullTheme(diagnosticPackage, &languageError), "actual parser emits unknown-token diagnostic");
    const QString canonicalWarning = QStringLiteral("Unknown token retained in package but ignored for drawing");
    const auto diagnosticSnapshot = core.snapshot();
    bool canonicalFound = false;
    for (const auto& item : diagnosticSnapshot->diagnostics())
        canonicalFound |= item.code == QStringLiteral("unknown-token") && item.message == canonicalWarning;
    check(canonicalFound, "model warning tuple retains canonical English and stable code");
    if (nameDraft && colorDraft && backgroundScope && imageMode && tabs) {
        const auto originalName = nameDraft->text();
        const auto originalColor = colorDraft->text();
        const auto originalMode = imageMode->currentData();
        nameDraft->setText(QStringLiteral("未提交 Theme name")); nameDraft->setSelection(0, 3);
        colorDraft->setText(QStringLiteral("#123456 draft"));
        const auto selection = nameDraft->selectedText();
        const auto modeIndex = imageMode->findData(QStringLiteral("custom"));
        imageMode->setCurrentIndex(modeIndex);
        const auto scope = backgroundScope->currentData();
        const QStringList languages {"en-US", "zh-CN", "ja-JP", "ko-KR"};
        const QStringList titles {"Theme management", "主题管理", "テーマ管理", "테마 관리"};
        const QStringList warnings {canonicalWarning,
            QStringLiteral("未知 token 保留在主题包中，但绘制时忽略"),
            QStringLiteral("不明なトークンはパッケージに保持されますが、描画には使用されません"),
            QStringLiteral("알 수 없는 토큰은 패키지에 보존되지만 그리기에는 사용되지 않습니다")};
        for (int index = 0; index < languages.size(); ++index) {
            check(languageHost.manager().setLanguage(languages[index], &languageError), "Theme language switch");
            check(dialog.windowTitle() == titles[index], "Theme title has independent four-language expectation");
            check(nameDraft->text() == QStringLiteral("未提交 Theme name") && nameDraft->selectedText() == selection
                  && colorDraft->text() == QStringLiteral("#123456 draft"), "language refresh preserves metadata/color draft and selection");
            check(imageMode->currentData() == QStringLiteral("custom") && backgroundScope->currentData() == scope,
                  "language refresh preserves stable enum selection");
            check(diagnosticsView && diagnosticsView->toPlainText().contains(warnings[index]), "actual stored diagnostic display refreshes in four languages");
            check(core.snapshot() == diagnosticSnapshot, "language refresh does not rebuild or mutate Theme snapshot");
            const auto literalDetail = QStringLiteral("assets/{detail} %1.png");
            check(ThemeMessages::display(QStringLiteral("export resource is missing: ") + literalDetail).endsWith(literalDetail),
                  "named diagnostic argument retains literal placeholder-looking resource path");
            check(ThemeMessages::display(QStringLiteral("Opaque vendor diagnostic: 123")) == QStringLiteral("Opaque vendor diagnostic: 123"),
                  "unrecognized vendor diagnostic remains opaque");
        }
        nameDraft->setText(originalName); colorDraft->setText(originalColor);
        imageMode->setCurrentIndex(imageMode->findData(originalMode));
    } else check(false, "Theme authoring drafts exist for language call-site checks");
    core.restoreDefault();
    check(languageHost.manager().setLanguage(QStringLiteral("zh-CN"), &languageError),
          "select explicit language for existing Chinese authoring checks");
    if (tabs) {
        tabs->setCurrentIndex(1);
        application.processEvents();
        auto* colorValue = dialog.findChild<QLineEdit*>(QStringLiteral("theme.colorValue"));
        auto* colorChoice = dialog.findChild<QComboBox*>(QStringLiteral("theme.colorChoice"));
        check(colorValue && colorChoice && colorValue->height() == colorChoice->height(),
              "theme color text and choice controls have equal height");
        tabs->setCurrentIndex(2);
        application.processEvents();
        auto* backgroundChoice = dialog.findChild<QComboBox*>(QStringLiteral("theme.backgroundScope"));
        const auto backgroundTexts = tabs->widget(2)->findChildren<QLineEdit*>();
        bool backgroundTextHeightsMatch = backgroundChoice && !backgroundTexts.isEmpty();
        for (auto* field : backgroundTexts)
            if (field->parentWidget() == tabs->widget(2) && field->height() != backgroundChoice->height())
                backgroundTextHeightsMatch = false;
        check(backgroundTextHeightsMatch, "theme background text and choice controls have equal height");
        tabs->setCurrentIndex(3);
        application.processEvents();
        const auto exportTexts = tabs->widget(3)->findChildren<QLineEdit*>();
        auto* exportChoice = tabs->widget(3)->findChild<QComboBox*>();
        bool exportTextHeightsMatch = exportChoice && !exportTexts.isEmpty();
        for (auto* field : exportTexts)
            if (field->parentWidget() == tabs->widget(3) && field->height() != exportChoice->height())
                exportTextHeightsMatch = false;
        check(exportTextHeightsMatch, "theme export text and choice controls have equal height");
    }
    if (tabs) tabs->setCurrentIndex(2);
    if (overlay && hint) {
        overlay->setChecked(true);
        application.processEvents();
        check(hint->text().contains(QStringLiteral("透明背景图片最佳"))
              && hint->isVisible() && blur && !blur->isEnabled()
              && componentOpacity && !componentOpacity->isEnabled(),
              "upper overlay recommends transparent PNG and disables blur/component opacity");
        check(dialog.grab().toImage().save(
            QStringLiteral("tests/preview/S10/theme-overlay-hint-modified.png")),
            "save upper-overlay hint screenshot");
    }
    if (backgroundScope && imageMode && applyBackground) {
        const auto mixerIndex = backgroundScope->findData(QStringLiteral("mixer"));
        backgroundScope->setCurrentIndex(mixerIndex);
        imageMode->setCurrentIndex(imageMode->findData(QStringLiteral("none")));
        applyBackground->click();
        application.processEvents();
        check(core.snapshot()->value(QStringLiteral("--wjn-theme-mixer-background-image")) == "none",
              "background editor applies explicit none to selected module");
    }
    auto* colorName = dialog.findChild<QLineEdit*>(QStringLiteral("theme.colorValue"));
    auto* colorChoice = dialog.findChild<QComboBox*>(QStringLiteral("theme.colorChoice"));
    check(colorName && colorChoice, "management exposes token color editor");
    if (tabs) tabs->setCurrentIndex(1);
    if (colorName && colorChoice) {
        const auto colorIndex = colorChoice->findData(QStringLiteral("--wjn-theme-global-accent-primary"));
        check(colorIndex >= 0, "global accent is listed in color editor");
        colorChoice->setCurrentIndex(colorIndex);
        colorName->setText(QStringLiteral("#ff00ff"));
        bool clickedColor = false;
        for (auto* button : dialog.findChildren<QPushButton*>())
            if (button->text() == QStringLiteral("应用颜色")) { button->click(); clickedColor = true; break; }
        check(clickedColor, "apply color button is available");
        application.processEvents();
        check(core.snapshot()->value(QStringLiteral("--wjn-theme-global-accent-primary")) == "#ff00ff",
              "color editor applies valid token override");
        check(dialog.grab().toImage().save(
            QStringLiteral("tests/preview/S10/theme-color-modified.png")),
            "save modified management popup");
    }
    QString error;
    const auto defaultPath = QStringLiteral("build/S10-default-export.wjnskin");
    check(dialog.exportToFile(defaultPath, {}, true, &error), "export default theme package from management");
    ThemePackage defaultPackage;
    check(ThemePackage::loadFromFile(defaultPath, &defaultPackage, &error)
          && defaultPackage.styleCss().contains("--wjn-theme-global-font-source: system;")
          && defaultPackage.resourceNames().isEmpty(),
          "default export reimports with portable system font");
    QFile defaultArchive(defaultPath);
    check(defaultArchive.open(QIODevice::ReadOnly)
          && defaultArchive.readAll().contains("host/mixer.css")
          && QFileInfo::exists(defaultPath),
          "default export includes separate host module CSS");
    ThemePackage shippedDefault;
    check(ThemePackage::loadFromFile(QStringLiteral("themes/template/winjacknexus-default.wjnskin"),
          &shippedDefault, &error) && shippedDefault.metadata().value("kind") == "full"
          && shippedDefault.styleCss().contains("--wjn-theme-meter-bridge-meter-level-zone-low-active")
          && shippedDefault.styleCss().contains("--wjn-theme-native-fx-compressor-"),
          "shipped default package is portable and retains module-level meter/FX roles");
    QFile templateMetadata(QStringLiteral("themes/template/meta.json"));
    QFile templateStyle(QStringLiteral("themes/template/style.css"));
    QFile mixerStyle(QStringLiteral("themes/template/host/mixer.css"));
    QFile bridgeStyle(QStringLiteral("themes/template/host/meter-bridge.css"));
    QFile eqStyle(QStringLiteral("themes/template/native_fx/parametric-eq.css"));
    check(templateMetadata.open(QIODevice::ReadOnly) && templateStyle.open(QIODevice::ReadOnly)
          && mixerStyle.open(QIODevice::ReadOnly) && bridgeStyle.open(QIODevice::ReadOnly)
          && eqStyle.open(QIODevice::ReadOnly)
          && templateStyle.readAll().contains("--wjn-theme-global-font-source")
          && mixerStyle.readAll().contains("--wjn-theme-mixer-fader-master-thumb")
          && bridgeStyle.readAll().contains("--wjn-theme-meter-bridge-meter-level-zone-high-inactive")
          && eqStyle.readAll().contains("--wjn-theme-native-fx-parametric-eq-chart-series-band-4-fill"),
          "author template separates global, host, and Native Fx module roles");
    ThemePackage candidate;
    check(ThemePackage::create(metadata(),
          ":root { --wjn-theme-global-accent-primary: #112233; }", {}, &candidate, &error),
          "create candidate for preview workflow");
    const auto candidatePath = QStringLiteral("build/S10-preview-candidate.wjnskin");
    check(candidate.saveToFile(candidatePath, &error) && dialog.openPackage(candidatePath, &error),
          "import full theme in management dialog");
    check(dialog.preview(&error)
          && core.snapshot()->value(QStringLiteral("--wjn-theme-global-accent-primary")) == "#112233",
          "preview applies candidate without committing");
    application.processEvents();
    check(dialog.grab().toImage().save(
        QStringLiteral("tests/preview/S10/theme-preview-modified.png")),
        "save candidate preview popup");
    check(dialog.cancelPreview()
          && core.snapshot()->value(QStringLiteral("--wjn-theme-global-accent-primary")) == "#ff00ff",
          "cancel preview restores user override");
    check(dialog.apply(&error) && core.snapshot()->themeId() == candidate.id(),
          "apply commits imported full package");
    const auto installed = QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("themes/") + candidate.id() + QStringLiteral(".wjnskin"));
    check(QFileInfo::exists(installed), "applied package installs beside the main executable");
    const auto roundTripPath = QStringLiteral("build/S10-management-roundtrip.wjnskin");
    check(dialog.exportToFile(roundTripPath, {}, false, &error),
          "export current complete theme from management");
    ThemePackage roundTrip;
    check(ThemePackage::loadFromFile(roundTripPath, &roundTrip, &error)
          && roundTrip.metadata().value("name") == "Management fixture"
          && roundTrip.metadata().value("author") == "Theme tests"
          && roundTrip.metadata().value("homepage") == "https://example.com/theme",
          "export preserves visible candidate metadata");
    QFile roundTripArchive(roundTripPath);
    check(roundTripArchive.open(QIODevice::ReadOnly)
          && roundTripArchive.readAll().contains("host/mixer.css"),
          "current full theme export separates module CSS");
    dialog.close();
    QFile::remove(installed);
    ThemeManager::installThemeCore(nullptr);
    return failures ? 1 : 0;
}
