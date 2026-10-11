/* SPDX-License-Identifier: AGPL-3.0-only */
#define WJN_HOST_I18N_TEST
#include "../src/app/main.cpp"
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <cstdio>
#include <QFontDatabase>
#include <QTextLayout>
#include <QGlyphRun>

using namespace WinJACKNexus::UI;
namespace
{
int assertions = 0;
void check(bool condition, const char *description)
{
    ++assertions;
    if (!condition)
    {
        std::fprintf(stderr, "Host i18n assertion failed: %s\n", description);
        std::fflush(stderr);
        std::exit(1);
    }
}
QByteArray displayState(QDialog &dialog)
{
    QJsonArray rows;
    rows.append(dialog.windowTitle());
    if (auto *language = dialog.findChild<QComboBox *>(QStringLiteral("application.language")))
    {
        rows.append(QJsonArray{QStringLiteral("language.selection"),
                               language->currentData().toString(), language->currentText()});
        for (int i = 0; i < language->count(); ++i)
            rows.append(language->itemText(i));
    }
    for (auto *widget : dialog.findChildren<QWidget *>())
    {
        for (const auto *property :
             {"text", "title", "placeholderText", "toolTip", "accessibleName"})
        {
            if (!widget->property((QByteArray("wjn.i18n.") + property).constData()).isValid())
                continue;
            rows.append(
                QJsonArray{QString::fromLatin1(property), widget->property(property).toString()});
        }
    }
    return QJsonDocument(rows).toJson(QJsonDocument::Compact);
}
void checkBoundText(QObject &root)
{
    QList<QObject *> objects = root.findChildren<QObject *>();
    objects.prepend(&root);
    for (auto *object : objects)
        for (const auto &property : object->dynamicPropertyNames())
        {
            if (!property.startsWith("wjn.i18n."))
                continue;
            const auto key = object->property(property).toByteArray();
            const auto target = property.mid(9);
            check(object->property(target).toString() == hostText(key.constData(), "missing"),
                  "bound property uses active catalog");
        }
}
}
int main(int argc, char **argv)
{
    static_assert(sizeof(Controls::DialogButtonBar) == sizeof(QDialogButtonBox),
                  "shared button bar keeps its existing object size");
    QApplication app(argc, argv);
    check(app.platformName() == QStringLiteral("windows"),
          "real Windows platform is required; offscreen is forbidden");
    const auto systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    app.setFont(systemFont);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir isolated;
    check(isolated.isValid(), "isolated test settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
    const auto arguments = app.arguments();
    check(arguments.size() >= 2, "language package root supplied");
    const auto packageRoot = arguments.at(1);
    const bool autoChild = arguments.contains(QStringLiteral("--auto"));
    const bool child = arguments.contains(QStringLiteral("--cold")) ||
                       arguments.contains(QStringLiteral("--failure")) || autoChild;
    const QString settingsFile =
        child ? arguments.at(3) : isolated.filePath(QStringLiteral("language.ini"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QFileInfo(settingsFile).absolutePath());
    const QSettings settingsProbe(QSettings::defaultFormat(), QSettings::UserScope,
                                  QStringLiteral("WinJACKNexus"), QStringLiteral("Jackd"));
    check(settingsProbe.fileName().startsWith(QFileInfo(settingsFile).absolutePath()),
          "all host settings use the isolated INI root");
    const QStringList preferences{autoChild ? arguments.at(4) : QStringLiteral("en-US")};
    HostI18n host({settingsFile, preferences, false});
    QString error;
    check(host.initialize(packageRoot, &error), "host initializes before windows");
    if (autoChild)
    {
        check(host.manager().selectedLanguage() == QStringLiteral("auto"),
              "cold auto retains independent selection");
        check(host.manager().effectiveLanguage() == I18nManager::resolveAutomatic(preferences),
              "cold auto resolves actual host language");
        check(host.manager().setLanguage(QStringLiteral("auto"), &error),
              "auto re-selection succeeds");
        check(QSettings(settingsFile, QSettings::IniFormat)
                      .value(QStringLiteral("i18n/language"))
                      .toString() == QStringLiteral("auto"),
              "auto is persisted as auto");
    }
    if (arguments.contains(QStringLiteral("--failure")))
    {
        const auto previous = host.manager().selectedLanguage();
        const auto revision = host.manager().revision();
        const auto title = hostText("settings.global.title", "Global settings");
        check(!host.manager().setLanguage(QStringLiteral("ja-JP"), &error) && !error.isEmpty(),
              "damaged target is rejected");
        check(host.manager().selectedLanguage() == previous &&
                  host.manager().revision() == revision,
              "failed switch keeps original snapshot");
        check(hostText("settings.global.title", "Global settings") == title,
              "failed switch keeps original display");
        check(QSettings(settingsFile, QSettings::IniFormat)
                      .value(QStringLiteral("i18n/language"), QStringLiteral("auto"))
                      .toString() == previous,
              "failed switch does not persist rejected selection");
        return 0;
    }
    ThemeCore core;
    check(core.isValid(), "real default theme loads offline");
    check(core.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                           QStringLiteral("system"), {}, &error),
          "test defaults to system font through existing Theme Token");
    ThemeManager::installThemeCore(&core);
    ThemeManager themeManager;
    themeManager.watchApplication(&app);
    check(app.font().family() == systemFont.family(), "theme uses the loaded system font");
    if (!child)
        std::printf("SYSTEM_FONT %s\n", systemFont.family().toUtf8().constData());
    ThemeManagementDialog themes(core);
    GlobalSettingsDialog global(themes);
    JackServerSettingsDialog jack(nullptr, true);
    QFileDialog directory;
    configureDirectoryDialog(directory, "jack.chooseDirectory",
                             "Select the directory containing jackd.exe", isolated.path());
    directory.show();
    global.show();
    jack.show();
    app.processEvents();
    auto *directoryInput = directory.findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
    check(directoryInput, "Qt directory chooser has an editable path field");
    directoryInput->setText(QStringLiteral("untouched-directory"));
    directoryInput->setSelection(1, 5);
    for (auto *box : jack.findChildren<QDialogButtonBox *>())
    {
        auto *bar = dynamic_cast<Controls::DialogButtonBar *>(box);
        if (!bar)
            continue;
        QDialogButtonBox native(QDialogButtonBox::Close);
        check(bar->button(QDialogButtonBox::Close)->text() ==
                  native.button(QDialogButtonBox::Close)->text(),
              "first-frame shared Close follows Qt translator");
    }
    auto *language = global.findChild<QComboBox *>(QStringLiteral("application.language"));
    check(language && language->count() == 5, "settings contains all five choices");
    std::unique_ptr<QMenu> menu(host.createLanguageMenu());
    check(menu->actions().size() == 5, "language menu contains five actions");
    check(language->currentData().toString() == host.manager().selectedLanguage(),
          "cold settings selection matches restored state");
    if (child)
    {
        checkBoundText(global);
        checkBoundText(jack);
        checkBoundText(directory);
        const auto state =
            QByteArray("COLD_STATE ") + displayState(global) + "|" + displayState(jack) + "\n";
        std::fwrite(state.constData(), 1, state.size(), stdout);
        std::fflush(stdout);
        return 0;
    }
    const QStringList selections{QStringLiteral("zh-CN"), QStringLiteral("en-US"),
                                 QStringLiteral("ja-JP"), QStringLiteral("ko-KR")};
    for (const auto &preference :
         QStringList{QStringLiteral("zh-Hans"), QStringLiteral("en-GB"), QStringLiteral("ja"),
                     QStringLiteral("ko"), QStringLiteral("fr-FR")})
    {
        QProcess automatic;
        automatic.start(QCoreApplication::applicationFilePath(),
                        {packageRoot, QStringLiteral("--auto"),
                         isolated.filePath(preference + QStringLiteral(".ini")), preference});
        check(automatic.waitForFinished(30000) && automatic.exitCode() == 0,
              "real-window cold auto preference scenario passes");
    }
    for (const auto &selection : selections)
    {
        directoryInput->setText(QStringLiteral("untouched-directory"));
        directoryInput->setSelection(1, 5);
        jack.raise();
        jack.activateWindow();
        app.processEvents();
        auto *field = jack.findChild<QSpinBox *>();
        check(field, "actual numeric field exists");
        field->setKeyboardTracking(false);
        field->setFocus();
        auto *edit = field->findChild<QLineEdit *>();
        edit->setText(QStringLiteral("12345"));
        edit->setSelection(1, 3);
        app.processEvents();
        auto *focus = QApplication::focusWidget();
        const auto selectedText = edit->selectedText();
        const auto numericValue = field->value();
        const auto directoryText = directoryInput->text();
        const auto directorySelection = directoryInput->selectedText();
        auto *action = menu->actions().at(selections.indexOf(selection) + 1);
        check(action->data().toString() == selection,
              "native action uses stable language identity");
        action->trigger();
        check(host.manager().selectedLanguage() == selection,
              "native language menu switches successfully");
        app.processEvents();
        check(edit->text() == QStringLiteral("12345") && edit->selectedText() == selectedText,
              "pending numeric text and selection preserved");
        check(field->value() == numericValue && QApplication::focusWidget() == focus,
              "numeric value and focus preserved");
        checkBoundText(global);
        checkBoundText(jack);
        checkBoundText(directory);
        check(directory.isVisible() && directory.testOption(QFileDialog::DontUseNativeDialog),
              "directory chooser is a real Qt window");
        check(directory.directory().absolutePath() == isolated.path(),
              "directory location preserved across language switch");
        check(directoryInput->text() == directoryText &&
                  directoryInput->selectedText() == directorySelection,
              "directory pending input and selection preserved");
        QFileDialog freshDirectory;
        configureDirectoryDialog(freshDirectory, "jack.chooseDirectory",
                                 "Select the directory containing jackd.exe", isolated.path());
        freshDirectory.show();
        app.processEvents();
        auto *freshInput = freshDirectory.findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        check(freshInput, "fresh directory path field exists");
        freshInput->setText(directoryInput->text());
        app.processEvents();
        for (const auto label : {QFileDialog::Accept, QFileDialog::Reject, QFileDialog::LookIn,
                                 QFileDialog::FileName, QFileDialog::FileType})
        {
            if (directory.labelText(label) != freshDirectory.labelText(label))
                std::fprintf(stderr, "Directory label %d hot=%s fresh=%s\n", int(label),
                             directory.labelText(label).toUtf8().constData(),
                             freshDirectory.labelText(label).toUtf8().constData());
            check(directory.labelText(label) == freshDirectory.labelText(label),
                  "hot directory standard labels match fresh translated dialog");
        }
        freshDirectory.hide();
        check(language->currentData().toString() == selection, "settings selection synchronized");
        QTextLayout glyphs(global.windowTitle(), app.font());
        glyphs.beginLayout();
        auto line = glyphs.createLine();
        line.setLineWidth(4096);
        glyphs.endLayout();
        check(!glyphs.glyphRuns().isEmpty(), "system font produces text glyphs");
        for (const auto &run : glyphs.glyphRuns())
            for (const auto glyph : run.glyphIndexes())
                check(glyph != 0, "system font fallback has no missing glyphs");
        if (arguments.size() > 2)
        {
            const auto output = arguments.at(2);
            check(QDir().mkpath(output), "render output directory");
            check(global.grab().save(output + QStringLiteral("/global-") + selection +
                                     QStringLiteral(".png")),
                  "global settings render saved");
            check(jack.grab().save(output + QStringLiteral("/jack-") + selection +
                                   QStringLiteral(".png")),
                  "JACK settings render saved");
            check(directory.grab().save(output + QStringLiteral("/directory-") + selection +
                                        QStringLiteral(".png")),
                  "directory chooser render saved");
        }
        int checked = 0;
        for (auto *action : menu->actions())
        {
            if (action->isChecked())
            {
                ++checked;
                check(action->data().toString() == selection, "menu stable identity synchronized");
            }
        }
        check(checked == 1, "menu exclusivity");
        for (auto *box : jack.findChildren<QDialogButtonBox *>())
        {
            auto *bar = dynamic_cast<Controls::DialogButtonBar *>(box);
            if (!bar)
                continue;
            QDialogButtonBox native(QDialogButtonBox::Close);
            check(bar->button(QDialogButtonBox::Close)->text() ==
                      native.button(QDialogButtonBox::Close)->text(),
                  "shared Close follows Qt translator");
        }
        const auto state = displayState(global) + "|" + displayState(jack);
        QProcess cold;
        cold.start(QCoreApplication::applicationFilePath(),
                   {packageRoot, QStringLiteral("--cold"), settingsFile});
        check(cold.waitForFinished(30000), "cold restart completes offline");
        check(cold.exitCode() == 0 && cold.exitStatus() == QProcess::NormalExit,
              "cold restart succeeds");
        const auto output = cold.readAllStandardOutput() + cold.readAllStandardError();
        if (!output.contains("COLD_STATE " + state))
        {
            QFile diagnostic(QDir::tempPath() + QStringLiteral("/wjn-host-cold-mismatch.txt"));
            if (diagnostic.open(QIODevice::WriteOnly))
                diagnostic.write("EXPECTED\n" + state + "\nACTUAL\n" + output);
        }
        check(output.contains("COLD_STATE " + state), "first cold display equals runtime switch");
        JackServerSettingsDialog late(nullptr, true);
        check(late.windowTitle() == jack.windowTitle(), "late window uses current snapshot");
        checkBoundText(late);
    }
    language->setCurrentIndex(language->findData(QStringLiteral("auto")));
    check(host.manager().selectedLanguage() == QStringLiteral("auto"),
          "auto remains independent selection");
    check(host.manager().effectiveLanguage() == QStringLiteral("en-US"),
          "auto resolves injected system preference");
    QTemporaryDir fixture;
    check(fixture.isValid(), "package failure fixture");
    check(QDir().mkpath(fixture.filePath(QStringLiteral("locate"))), "fixture locate directory");
    for (const auto &file :
         {QStringLiteral("manifest.json"), QStringLiteral("en-US.json"),
          QStringLiteral("zh-CN.json"), QStringLiteral("ja-JP.json"), QStringLiteral("ko-KR.json")})
        check(QFile::copy(packageRoot + QStringLiteral("/locate/") + file,
                          fixture.filePath(QStringLiteral("locate/") + file)),
              "copy actual catalog fixture");
    check(host.manager().setLanguage(QStringLiteral("ja-JP"), &error),
          "prepare resource-only Qt translation restart");
    QProcess qtFallback;
    qtFallback.start(QCoreApplication::applicationFilePath(),
                     {fixture.path(), QStringLiteral("--cold"), settingsFile});
    check(qtFallback.waitForFinished(30000) && qtFallback.exitCode() == 0,
          "Qt QM missing on disk uses compiled translation");
    check((qtFallback.readAllStandardOutput() + qtFallback.readAllStandardError())
              .contains("COLD_STATE " + displayState(global) + "|" + displayState(jack)),
          "compiled Qt fallback and package restore match hot display");
    QFile damaged(fixture.filePath(QStringLiteral("locate/ja-JP.json")));
    check(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate), "open damaged target fixture");
    damaged.write("{");
    damaged.close();
    QProcess failure;
    failure.start(QCoreApplication::applicationFilePath(),
                  {fixture.path(), QStringLiteral("--failure"),
                   isolated.filePath(QStringLiteral("failure.ini"))});
    check(failure.waitForFinished(30000) && failure.exitCode() == 0,
          "failed switch preserves selection and display in real host");
    QTemporaryDir missing;
    check(host.manager().setLanguage(QStringLiteral("en-US"), &error),
          "prepare complete embedded English fallback");
    QProcess embedded;
    embedded.start(QCoreApplication::applicationFilePath(),
                   {missing.path(), QStringLiteral("--cold"), settingsFile});
    check(embedded.waitForFinished(30000) && embedded.exitCode() == 0,
          "missing locate uses compiled manifest and complete English");
    check((embedded.readAllStandardOutput() + embedded.readAllStandardError())
              .contains("COLD_STATE " + displayState(global) + "|" + displayState(jack)),
          "embedded English equals deployed English display");
    TrayController tray;
    tray.initializeFallback();
    QAction *settings = tray.addMenuAction(hostText("settings.global.title", "Global settings"));
    bindHostText(settings, "text", "settings.global.title", "Global settings");
    QObject::connect(settings, &QAction::triggered, &global, &QWidget::show);
    tray.setQuickAccessSettingsAction(settings);
    global.hide();
    QToolButton *entry = nullptr;
    for (auto *window : QApplication::topLevelWidgets())
        if (auto *button =
                window->findChild<QToolButton *>(QStringLiteral("quickAccessSettingsButton")))
            entry = button;
    check(entry && entry->isEnabled(), "no-tray settings entry reachable");
    entry->click();
    app.processEvents();
    check(global.isVisible(), "no-tray settings action opens actual settings");
    check(tray.contextMenu()->findChild<QMenu *>(QStringLiteral("hostLanguageMenu")),
          "fallback has native language menu");
    for (const auto &selection : selections)
    {
        check(host.manager().setLanguage(selection, &error),
              "existing fallback bar switches language");
        app.processEvents();
        auto *bar = entry->window();
        auto *mixer = bar->findChild<QToolButton *>(QStringLiteral("quickAccessMixerButton"));
        check(mixer && mixer->text() == hostText("view.mixer", "Mixer"),
              "live quickbar label refreshed");
        check(mixer->accessibleName() == mixer->text(),
              "quickbar accessible name preserves full text");
        check(entry->text() == hostText("settings.global.title", "Global settings"),
              "quickbar settings action refreshed like the production host");
        if (arguments.size() > 2)
            check(bar->grab().save(arguments.at(2) + QStringLiteral("/quickbar-") + selection +
                                   QStringLiteral(".png")),
                  "quickbar render saved");
    }
    tray.shutdown();
    std::printf("PASS host i18n assertions %d\n", assertions);
    std::fflush(stdout);
    ThemeManager::installThemeCore(nullptr);
    return 0;
}
