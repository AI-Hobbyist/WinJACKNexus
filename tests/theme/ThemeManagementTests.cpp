/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeCatalog.hpp>
#include <WinJACKNexus/UI/ThemeManagementDialog.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemePackage.hpp>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include "../../src/ui/src/LocalizedThemeMessages.hpp"
#include "../../src/ui/src/LocalizedThemeDialogs.hpp"
#include "NativeDesktopCapture.hpp"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSpinBox>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <QEvent>

using namespace WinJACKNexus::UI;
namespace {
int failures = 0;
int checks = 0;
QJsonArray captureEvidence;
QString captureDirectory;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
class ShowBudget final : public QObject {
public:
    int shows = 0;
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            auto* widget = qobject_cast<QWidget*>(object);
            if (widget && widget->isWindow()) {
                check(++shows <= 100, "at most 100 window/menu/tooltip shows");
                if (shows > 100) std::exit(2);
            }
        }
        return false;
    }
};
void capture(QWidget& window, const QString& name) {
    QJsonObject evidence;
    const auto path = QDir(captureDirectory).filePath(name + QStringLiteral(".png"));
    check(ThemeTest::capture(window, path, &evidence), "save visible desktop pixels with five native ownership samples");
    if (!evidence.isEmpty()) captureEvidence.append(evidence);
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
    ShowBudget budget;
    application.installEventFilter(&budget);
    check(QApplication::platformName() == QStringLiteral("windows"), "Theme UI verification requires Windows platform");
    if (failures) return 2;
    application.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    check(application.primaryScreen() != nullptr, "actual Windows display is available");
    check(application.font().family() == QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(),
          "system font is configured before any window");
    if (failures) return 2;
    QTemporaryDir languageSettings;
    I18nOptions languageOptions;
    languageOptions.settingsFile = languageSettings.filePath(QStringLiteral("language.ini"));
    HostI18n languageHost(languageOptions);
    QString languageError;
    check(languageHost.initialize(QCoreApplication::applicationDirPath(), &languageError), "Theme i18n host initializes with isolated language settings");
    if (failures) return 2;
    captureDirectory = QStringLiteral("Docs/i18n/S6-theme-rendered");
    check(QDir().mkpath(captureDirectory), "capture directory exists");
    ThemeCore core;
    check(core.isValid(), "required default Theme DLL is loaded before UI verification");
    if (failures) return 2;
    ThemeManager::installThemeCore(&core);
    ThemeManagementDialog dialog(core);
    const auto available = application.primaryScreen()->availableGeometry();
    dialog.resize(qMin(860, available.width() - 40), qMin(680, available.height() - 70));
    dialog.move(available.topLeft() + QPoint(20, 20));
    dialog.show();
    ThemeTest::settle();
    check(dialog.isVisible() && dialog.windowHandle()->isExposed(), "management window is visible and exposed");
    capture(dialog, QStringLiteral("baseline"));
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
    auto* colorTokenDraft = dialog.findChild<QLineEdit*>(QStringLiteral("theme.colorName"));
    auto* colorApply = dialog.findChild<QPushButton*>(QStringLiteral("theme.applyColor"));
    auto* status = dialog.findChild<QLabel*>(QStringLiteral("theme.status"));
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
    ThemePackage rejectedPackage;
    QString packageFailure;
    check(!ThemePackage::loadFromBytes(QByteArray("not a ZIP"), &rejectedPackage, &packageFailure)
          && packageFailure == QStringLiteral("package size is outside 22 bytes..32 MiB"),
          "actual package failure retains canonical English");
    ThemeNamespaceRegistry isolatedNamespaces;
    ThemeNamespaceRegistry::Lease rejectedNamespace;
    QString namespaceFailure;
    check(!isolatedNamespaces.registerInternal(QByteArray(1, char(0xff)), QStringLiteral("i18n-invalid-fixture"),
                                               &rejectedNamespace, &namespaceFailure)
          && namespaceFailure == QStringLiteral("descriptor is not bounded UTF-8")
          && rejectedNamespace.isEmpty() && isolatedNamespaces.moduleIds().isEmpty(),
          "actual malformed namespace descriptor fails without registering identity");
    if (nameDraft && colorDraft && backgroundScope && imageMode && tabs) {
        const auto originalName = nameDraft->text();
        const auto originalColor = colorDraft->text();
        const auto originalToken = colorTokenDraft ? colorTokenDraft->text() : QString{};
        const auto originalMode = imageMode->currentData();
        check(colorTokenDraft && colorApply && status, "actual error status call-site controls exist");
        if (colorTokenDraft && colorApply) {
            colorTokenDraft->setText(QStringLiteral("--wjn-theme-global-invalid-fixture"));
            colorDraft->setText(QStringLiteral("#123456"));
            colorApply->click();
        }
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
        const QStringList packageMessages {packageFailure,
            QStringLiteral("主题包大小不在 22 字节至 32 MiB 范围内"),
            QStringLiteral("パッケージのサイズが 22 バイトから 32 MiB の範囲外です"),
            QStringLiteral("패키지 크기가 22바이트부터 32 MiB까지의 범위를 벗어납니다")};
        const QStringList namespaceMessages {namespaceFailure,
            QStringLiteral("描述符不是符合长度限制的 UTF-8"),
            QStringLiteral("記述子が長さの制限を満たす UTF-8 ではありません"),
            QStringLiteral("디스크립터가 길이 제한을 충족하는 UTF-8이 아닙니다")};
        const QStringList statusMessages {QStringLiteral("unknown override token"),
            QStringLiteral("未知的覆盖 token"), QStringLiteral("不明な上書きトークンです"),
            QStringLiteral("알 수 없는 재정의 토큰입니다")};
        for (int index = 0; index < languages.size(); ++index) {
            check(languageHost.manager().setLanguage(languages[index], &languageError), "Theme language switch");
            check(dialog.windowTitle() == titles[index], "Theme title has independent four-language expectation");
            check(nameDraft->text() == QStringLiteral("未提交 Theme name") && nameDraft->selectedText() == selection
                  && colorDraft->text() == QStringLiteral("#123456 draft"), "language refresh preserves metadata/color draft and selection");
            check(imageMode->currentData() == QStringLiteral("custom") && backgroundScope->currentData() == scope,
                  "language refresh preserves stable enum selection");
            check(diagnosticsView && diagnosticsView->toPlainText().contains(warnings[index]), "actual stored diagnostic display refreshes in four languages");
            check(core.snapshot() == diagnosticSnapshot, "language refresh does not rebuild or mutate Theme snapshot");
            check(status && status->text() == statusMessages[index]
                  && status->property("wjn.theme.statusCanonical").toString() == QStringLiteral("unknown override token"),
                  "actual stored error status retranslates without changing canonical payload");
            check(ThemeMessages::display(packageFailure) == packageMessages[index], "actual package failure display has independent four-language expectation");
            check(ThemeMessages::display(namespaceFailure) == namespaceMessages[index], "actual namespace failure display has independent four-language expectation");
            check(ThemeMessages::display(QStringLiteral("module.wjnskin: ") + packageFailure)
                  == QStringLiteral("module.wjnskin: ") + packageMessages[index], "wrapper filename stays original while nested project diagnostic translates");
            const auto literalDetail = QStringLiteral("assets/{detail} %1.png");
            check(ThemeMessages::display(QStringLiteral("export resource is missing: ") + literalDetail).endsWith(literalDetail),
                  "named diagnostic argument retains literal placeholder-looking resource path");
            check(ThemeMessages::display(QStringLiteral("Opaque vendor diagnostic: 123")) == QStringLiteral("Opaque vendor diagnostic: 123"),
                  "unrecognized vendor diagnostic remains opaque");
            ThemeTest::settle();
            capture(dialog, QStringLiteral("diagnostic-") + languages[index]);
        }
        nameDraft->setText(originalName); colorDraft->setText(originalColor);
        if (colorTokenDraft) colorTokenDraft->setText(originalToken);
        imageMode->setCurrentIndex(imageMode->findData(originalMode));
    } else check(false, "Theme authoring drafts exist for language call-site checks");
    core.restoreDefault();
    // Four file workflows in this same single module run; cancellation avoids file writes.
    struct FileCase {
        const char* key; const char* english; const char* filterKey; const char* filterEnglish;
        bool save;
    };
    const FileCase fileCases[] {
        {"package.import", "Import theme package", "file.themeFilter", "WinJACKNexus themes (*.wjnskin)", false},
        {"file.fontTitle", "Choose font", "file.fontFilter", "Fonts (*.ttf *.otf)", false},
        {"file.imageTitle", "Choose background image", "file.imageFilter", "Images (*.png *.jpg *.jpeg *.webp)", false},
        {"file.exportTitle", "Export theme", "file.themeFilter", "WinJACKNexus themes (*.wjnskin)", true}
    };
    for (const auto& item : fileCases) {
        check(languageHost.manager().setLanguage(QStringLiteral("en-US"), &languageError), "file dialog initial language");
        bool visited = false;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &dialog, [&] {
            check(false, "owned file dialog verification must finish within five seconds");
            if (auto* pending = dialog.findChild<QFileDialog*>(QStringLiteral("theme.fileDialog"))) pending->reject();
        });
        deadline.start(5000);
        QTimer::singleShot(100, &dialog, [&] {
            auto* fileDialog = dialog.findChild<QFileDialog*>(QStringLiteral("theme.fileDialog"));
            check(fileDialog && fileDialog->isVisible(), "owned file workflow has an actual visible Windows dialog");
            if (!fileDialog) return;
            visited = true;
            auto* edit = fileDialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            check(edit != nullptr, "file dialog filename editor exists");
            const auto directory = fileDialog->directory().absolutePath();
            if (edit) { edit->setText(QStringLiteral("draft {detail} %1.wjnskin")); edit->setSelection(8, -5); }
            const auto cursor = edit ? edit->cursorPosition() : 0;
            const auto selection = edit ? edit->selectedText() : QString{};
            for (const auto& language : {"en-US", "zh-CN", "ja-JP", "ko-KR"}) {
                check(languageHost.manager().setLanguage(QString::fromLatin1(language), &languageError), "open file dialog hot language switch");
                check(fileDialog->windowTitle() == localizedText("theme", item.key, item.english)
                      && fileDialog->nameFilters() == QStringList{localizedText("theme", item.filterKey, item.filterEnglish)},
                      "open file dialog title and wildcard filter follow current catalog");
                check(fileDialog->directory().absolutePath() == directory && edit
                      && edit->text() == QStringLiteral("draft {detail} %1.wjnskin")
                      && edit->cursorPosition() == cursor && edit->selectedText() == selection,
                      "open file language refresh preserves directory, filename and reverse selection");
                ThemeTest::settle();
                capture(*fileDialog, QString::fromLatin1(item.key) + QLatin1Char('-') + QString::fromLatin1(language));
            }
            fileDialog->reject();
        });
        const auto result = ThemeDialogs::file(&dialog, item.save, languageSettings.path(),
                                               item.key, item.english, item.filterKey, item.filterEnglish);
        deadline.stop();
        check(visited && result.isEmpty(), "cancelling translated file workflow returns no path");
    }
    {
        QTemporaryDir installed;
        QSettings isolatedSettings(languageSettings.filePath(QStringLiteral("catalog.ini")), QSettings::IniFormat);
        ThemeCatalog catalog(core, isolatedSettings, installed.path());
        QString canonical;
        check(!catalog.activate(QStringLiteral("missing-id"), &canonical)
              && canonical == QStringLiteral("主题不在安装目录中：missing-id"),
              "catalog API retains original diagnostic and theme identity");
        const auto fixturePath = installed.filePath(QStringLiteral("catalog.wjnskin"));
        check(diagnosticPackage.saveToFile(fixturePath, &languageError), "prepare isolated menu package");
        QString displayed;
        auto* menu = catalog.createMenu(&dialog, [] {}, [&](const QString& text) { displayed = text; });
        check(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection), "populate actual theme menu callback");
        QAction* fixtureAction = nullptr;
        for (auto* action : menu->actions())
            if (action->data().toString() == diagnosticPackage.id()) fixtureAction = action;
        auto changedMetadata = metadata();
        changedMetadata.insert(QStringLiteral("id"), QStringLiteral("org.winjacknexus-changed-fixture"));
        ThemePackage changed;
        check(ThemePackage::create(changedMetadata, ":root { }", {}, &changed, &languageError)
              && changed.saveToFile(fixturePath, &languageError), "change package identity after menu discovery");
        const QStringList languages {"en-US", "zh-CN", "ja-JP", "ko-KR"};
        const QStringList expected {QStringLiteral("The theme file has changed; refresh the list"),
            QStringLiteral("主题文件已改变，请刷新列表"),
            QStringLiteral("テーマファイルが変更されました。リストを更新してください"),
            QStringLiteral("테마 파일이 변경되었습니다. 목록을 새로 고치세요")};
        for (int index = 0; index < languages.size(); ++index) {
            check(languageHost.manager().setLanguage(languages[index], &languageError), "catalog menu language");
            displayed.clear();
            if (fixtureAction) fixtureAction->trigger();
            check(fixtureAction && displayed == expected[index], "actual menu failure callback translates in four languages");
            check(!catalog.activate(diagnosticPackage.id(), &canonical)
                  && canonical == QStringLiteral("主题文件已改变，请刷新列表"), "menu display translation leaves API diagnostic canonical");
        }
        delete menu;
    }
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
        capture(dialog, QStringLiteral("overlay-hint-zh-CN"));
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
        capture(dialog, QStringLiteral("color-zh-CN"));
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
    capture(dialog, QStringLiteral("preview-zh-CN"));
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
    const QJsonObject evidence {{"checks", checks}, {"failures", failures}, {"window_shows", budget.shows},
        {"platform", QApplication::platformName()}, {"font", application.font().family()},
        {"module_test_runs", 1}, {"scale_matrix_runs", 0}, {"captures", captureEvidence}};
    QFile report(QStringLiteral("Docs/i18n/S6-theme-runtime.json"));
    check(report.open(QIODevice::WriteOnly)
          && report.write(QJsonDocument(evidence).toJson()) > 0, "write actual runtime evidence");
    std::printf("S6 Theme %s: %d checks, %d failures, %d shows, %lld native captures; platform=%s; font=%s\n",
                failures ? "FAIL" : "PASS", checks, failures, budget.shows,
                static_cast<long long>(captureEvidence.size()), qPrintable(QApplication::platformName()),
                qPrintable(application.font().family()));
    return failures ? 1 : 0;
}
