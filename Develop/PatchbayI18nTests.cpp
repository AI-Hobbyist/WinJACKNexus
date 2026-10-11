/* SPDX-License-Identifier: AGPL-3.0-only */
// Exercise the actual patchbay host code with its JACK observer disabled.
#include "../src/patchbay/mod/src/Library.cpp"
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <QFontDatabase>
#include <QJsonArray>
#include <QProcess>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextLayout>
#include <QSpinBox>
#include <QGlyphRun>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsItem>
#include <QContextMenuEvent>
#include <cstdio>
#include <cstdlib>

using namespace WinJACKNexus::UI;
namespace
{
int checks = 0;
void require(bool condition, const char *message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "S3 assertion failed: %s\n", message);
        std::fflush(stderr);
        std::exit(2);
    }
}
QByteArray bytes(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read evidence file");
    return file.readAll();
}
Snapshot canvasFixture()
{
    Snapshot snapshot;
    WinJACKNexus::Patchbay::Core::ClientInfo user;
    user.id = "user-client";
    user.name = "User name Ω";
    user.alias = QStringLiteral("用户名称 / 名称 / 이름 / A very long user-defined node alias ")
                     .repeated(3)
                     .toStdString();
    user.split = true;
    snapshot.clients.push_back(user);
    WinJACKNexus::Patchbay::Core::ClientInfo plain;
    plain.id = "plain-client";
    plain.name = "Plain user Ω";
    plain.split = true;
    plain.y = 160;
    snapshot.clients.push_back(plain);
    for (const auto &client : snapshot.clients)
        for (const auto direction : {WinJACKNexus::Patchbay::Core::Direction::input,
                                     WinJACKNexus::Patchbay::Core::Direction::output})
        {
            WinJACKNexus::Patchbay::Core::PortInfo port;
            port.id = client.id + (direction == WinJACKNexus::Patchbay::Core::Direction::input
                                       ? ":input"
                                       : ":output");
            port.clientId = client.id;
            port.name = port.id;
            port.kind = WinJACKNexus::Patchbay::Core::PortKind::audio;
            port.direction = direction;
            snapshot.ports.push_back(port);
        }
    return snapshot;
}
bool captureRealWindow(QWidget *window, const QString &path)
{
    window->raise();
    window->activateWindow();
    QApplication::processEvents();
    QEventLoop painted;
    QTimer::singleShot(100, &painted, &QEventLoop::quit);
    painted.exec();
    // QWidget::grab re-renders widget children and omits the accelerated
    // graphics viewport. Capture the actual Windows surface instead.
    const auto pixels = window->screen()->grabWindow(window->winId());
    require(!pixels.isNull(), "native Windows surface capture exists");
    return pixels.save(path);
}
void verifyGlyphs(const QString &text, const QFont &font)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    while (true)
    {
        auto line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(10000);
    }
    layout.endLayout();
    for (const auto &run : layout.glyphRuns())
        for (const auto glyph : run.glyphIndexes())
            require(
                glyph != 0,
                "actual system font fallback supplies translated glyphs without missing-glyph boxes");
}
void verifyBindings(QObject &root)
{
    auto objects = root.findChildren<QObject *>();
    objects.prepend(&root);
    for (auto *object : objects)
        for (const auto &property : object->dynamicPropertyNames())
        {
            if (!property.startsWith("wjn.i18n."))
                continue;
            const auto target = property.mid(9);
            const auto domain =
                object->property((QByteArray("wjn.i18nDomain.") + target).constData())
                    .toByteArray();
            const auto key = object->property(property).toByteArray();
            require(object->property(target).toString() ==
                        localizedText(domain.constData(), key.constData(), "missing"),
                    "live display binding uses its current domain");
            verifyGlyphs(object->property(target).toString(),
                         qobject_cast<QWidget *>(object) ? qobject_cast<QWidget *>(object)->font()
                                                         : QApplication::font());
        }
    if (auto *dialog = qobject_cast<QInputDialog *>(&root))
    {
        const auto expected = dialog->labelText();
        bool visiblePrompt = false;
        for (auto *label : dialog->findChildren<QLabel *>())
            if (label->isVisible() && label->text() == expected)
                visiblePrompt = true;
        require(
            visiblePrompt,
            "actual visible input prompt matches the translated dialog property after Qt LanguageChange");
    }
}
QByteArray displayState(QObject &root)
{
    QStringList rows;
    auto objects = root.findChildren<QObject *>();
    objects.prepend(&root);
    for (auto *object : objects)
        for (const auto &property : object->dynamicPropertyNames())
        {
            if (!property.startsWith("wjn.i18n."))
                continue;
            rows.append(QString::fromLatin1(property) + QLatin1Char('=') +
                        object->property(property.mid(9)).toString());
        }
    if (auto *tree = root.findChild<QTreeWidget *>(QStringLiteral("patchbay.nodeTree")))
    {
        const auto collect = [&](auto &&self, QTreeWidgetItem *item) -> void {
            rows.append(QStringLiteral("library=") + item->text(0));
            for (int i = 0; i < item->childCount(); ++i)
                self(self, item->child(i));
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            collect(collect, tree->topLevelItem(i));
    }
    for (auto *canvas : root.findChildren<PatchbayCanvas *>())
        for (auto *item : canvas->scene()->items())
        {
            if (!item->data(0).isValid())
                continue;
            rows.append(QStringLiteral("canvas=") + item->data(0).toString() + QLatin1Char('=') +
                        item->toolTip());
        }
    rows.sort();
    return QJsonDocument(QJsonArray::fromStringList(rows)).toJson(QJsonDocument::Compact);
}
QAction *action(QObject &root, const QString &identity)
{
    for (auto *item : root.findChildren<QAction *>())
        if (item->data().toString() == identity)
            return item;
    return nullptr;
}
struct AdapterPanels
{
    HMODULE library = nullptr;
    using Create = WinJACKNexus::Node::INode *(*)(std::uint32_t);
    using Destroy = void (*)(WinJACKNexus::Node::INode *);
    using Settings = QWidget *(*)(WinJACKNexus::Node::INode *, QWidget *);
    using DestroySettings = void (*)(WinJACKNexus::Node::INode *, QWidget *);
    using GetSettings = const char *(*)(WinJACKNexus::Node::INode *);
    GetSettings getSettings = nullptr;
    Create create = nullptr;
    Destroy destroy = nullptr;
    Settings settings = nullptr;
    DestroySettings destroySettings = nullptr;
    std::array<WinJACKNexus::Node::INode *, 4> nodes{};
    std::array<QWidget *, 4> panels{};
    std::array<QWidget *, 4> meters{};
    std::array<QByteArray, 4> initialSettings{};
    explicit AdapterPanels(const QString &path, QWidget *parent)
    {
        library = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
        require(library, "load actual adapter DLL");
        create = reinterpret_cast<Create>(GetProcAddress(library, "wjn_create_node"));
        destroy = reinterpret_cast<Destroy>(GetProcAddress(library, "wjn_destroy_node"));
        settings =
            reinterpret_cast<Settings>(GetProcAddress(library, "wjn_create_settings_widget"));
        destroySettings = reinterpret_cast<DestroySettings>(
            GetProcAddress(library, "wjn_destroy_settings_widget"));
        getSettings =
            reinterpret_cast<GetSettings>(GetProcAddress(library, "wjn_get_node_settings_json"));
        require(create && destroy && settings && destroySettings && getSettings,
                "actual adapter widget API is present");
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            nodes[i] = create(i);
            require(nodes[i], "create uninitialized adapter for UI only");
            panels[i] = settings(nodes[i], parent);
            require(panels[i], "create actual adapter settings without init/JACK");
            initialSettings[i] = getSettings(nodes[i]);
        }
    }
    ~AdapterPanels()
    {
        for (int i = 0; i < 4; ++i)
        {
            nodes[i]->destroyWindow(nullptr);
            destroySettings(nodes[i], panels[i]);
            destroy(nodes[i]);
        }
        FreeLibrary(library);
    }
    QWidget *openMeter(int index)
    {
        const auto handle = nodes[index]->createWindow(nullptr);
        require(handle, "uninitialized adapter opens its real meter window without JACK");
        meters[index] = QWidget::find(reinterpret_cast<WId>(handle));
        require(meters[index] && meters[index]->isVisible(), "actual adapter meter is visible");
        return meters[index];
    }
};
void verifyModalWindows(HostI18n &host, QWidget *parent, const QString &language,
                        const QString &render)
{
    QString error;
    const auto alternate =
        language == QStringLiteral("en-US") ? QStringLiteral("ja-JP") : QStringLiteral("en-US");
    const auto switchTwice = [&](QObject &dialog) {
        require(host.manager().setLanguage(alternate, &error), "modal hot switch");
        QApplication::processEvents();
        verifyBindings(dialog);
        require(host.manager().setLanguage(language, &error), "modal language restore");
        QApplication::processEvents();
        verifyBindings(dialog);
    };
    bool accepted = true;
    QTimer::singleShot(0, parent, [&] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        require(dialog && dialog->isVisible(), "rename uses real visible Qt input dialog");
        auto *edit = dialog->findChild<QLineEdit *>();
        require(edit, "rename editor exists");
        dialog->raise();
        dialog->activateWindow();
        edit->setFocus();
        QApplication::processEvents();
        require(QApplication::focusWidget() == edit,
                "rename editor has real focus before the language operation");
        edit->setText(QStringLiteral("unfinished Ω 名称 이름"));
        edit->setSelection(2, 5);
        auto *focus = QApplication::focusWidget();
        switchTwice(*dialog);
        require(edit->text() == QStringLiteral("unfinished Ω 名称 이름") &&
                    edit->selectedText() == QStringLiteral("finis"),
                "rename pending text and selection survive hot switch");
        require(QApplication::focusWidget() == focus, "rename focus survives hot switch");
        require(dialog->grab().save(render + "/rename-" + language + ".png"),
                "real rename capture");
        dialog->reject();
    });
    PatchbayDialogs::text(parent, "action.rename", "Rename node", "node.displayName",
                          "Display name", QStringLiteral("original user name"), &accepted);
    require(!accepted, "cancel rename does not submit pending text");
    QTimer::singleShot(0, parent, [&] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        require(dialog && dialog->isVisible(), "autosave uses real integer dialog");
        dialog->setIntValue(135);
        switchTwice(*dialog);
        require(dialog->intValue() == 135, "pending integer survives hot switch");
        require(dialog->grab().save(render + "/autosave-" + language + ".png"),
                "real integer capture");
        dialog->reject();
    });
    PatchbayDialogs::integer(parent, "project.autosaveTitle", "Autosave", "project.autosavePrompt",
                             "Save interval (seconds; 0 disables autosave)", 60, 0, 3600, 5,
                             &accepted);
    require(!accepted, "cancel autosave does not submit numeric change");
    for (const bool save : {false, true})
    {
        QTimer::singleShot(0, parent, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            require(dialog && dialog->isVisible(), "project chooser uses a visible real window");
            auto *edit = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
            require(edit, "project file editor");
            edit->setText(QStringLiteral("unfinished Ω 名称.wjnproj"));
            edit->setSelection(2, 5);
            switchTwice(*dialog);
            require(edit->text() == QStringLiteral("unfinished Ω 名称.wjnproj") &&
                        edit->selectedText() == QStringLiteral("finis"),
                    "project filename and selection survive translated filters");
            require(dialog->nameFilters() ==
                        QStringList{localizedText("patchbay", "project.fileFilter", "missing")},
                    "project filter uses current language");
            require(dialog->grab().save(render + (save ? "/save-" : "/open-") + language + ".png"),
                    "real project chooser capture");
            dialog->reject();
        });
        require(PatchbayDialogs::projectFile(parent, save, {}).isEmpty(),
                "cancel file dialog does not load or save project");
    }
    QTimer::singleShot(0, parent, [&] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        require(dialog && dialog->isVisible(), "unsaved prompt uses visible real window");
        switchTwice(*dialog);
        require(dialog->standardButtons() ==
                    (QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel),
                "translated standard buttons preserve operation identities");
        require(dialog->grab().save(render + "/unsaved-" + language + ".png"),
                "real message capture");
        dialog->reject();
    });
    PatchbayDialogs::message(
        parent, QMessageBox::Warning, "project.unsavedTitle", "Unsaved project", "project.unsaved",
        "The project has unsaved changes. Save them?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    require(app.platformName() == QStringLiteral("windows"),
            "Windows real windows are mandatory; offscreen forbidden");
    const auto systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    app.setFont(systemFont);
    QTemporaryDir isolated;
    require(isolated.isValid(), "isolated settings directory");
    const auto arguments = app.arguments();
    require(arguments.size() >= 5, "runtime, adapter, example and renders supplied");
    const bool child = arguments.contains(QStringLiteral("--cold"));
    const auto settingsFile =
        child ? arguments.at(6) : isolated.filePath(QStringLiteral("language.ini"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QFileInfo(settingsFile).absolutePath());
    require(patchbaySettings().fileName().startsWith(QFileInfo(settingsFile).absolutePath()),
            "patchbay persistence stays inside test INI root");
    HostI18n host({settingsFile, {QStringLiteral("en-US")}, false});
    QString error;
    require(host.initialize(arguments.at(1), &error), "host installs before S3 windows");
    ThemeCore core;
    require(core.isValid(), "load real default theme");
    require(core.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                             QStringLiteral("system"), {}, &error),
            "system font token");
    ThemeManager::installThemeCore(&core);
    ThemeManager theme;
    theme.watchApplication(&app);
    require(app.font().family() == systemFont.family(), "theme retains system font");
    if (!child)
        std::printf("SYSTEM_FONT %s\n", systemFont.family().toUtf8().constData());
    PatchbayNode node(true);
    require(node.init(), "offline host init does not attach JACK");
    require(node.createWindow(nullptr), "actual offline patchbay window opens");
    app.processEvents();
    QWidget *window = nullptr;
    for (auto *candidate : app.topLevelWidgets())
        if (candidate->findChild<QTreeWidget *>(QStringLiteral("patchbay.nodeTree")))
            window = candidate;
    require(window && window->isVisible(), "actual patchbay window found and visible");
    const auto available = app.primaryScreen()->availableGeometry();
    window->resize(std::min(1240, available.width() - 40), std::min(820, available.height() - 80));
    window->move(20, 20);
    QWidget adapterWindow;
    adapterWindow.setWindowTitle(QStringLiteral("S3 adapter UI verification"));
    auto *adapterLayout = new QVBoxLayout(&adapterWindow);
    auto *adapterTabs = new Controls::TabField(&adapterWindow);
    adapterLayout->addWidget(adapterTabs);
    AdapterPanels adapters(arguments.at(2), &adapterWindow);
    for (std::uint32_t i = 0; i < 4; ++i)
        adapterTabs->addTab(adapters.panels[i], QString::number(i));
    adapterWindow.resize(320, 380);
    adapterWindow.show();
    app.processEvents();
    verifyBindings(*window);
    verifyBindings(adapterWindow);
    auto *initialCanvas = window->findChild<PatchbayCanvas *>();
    require(initialCanvas, "first display canvas");
    initialCanvas->setSnapshot(canvasFixture());
    initialCanvas->selectNode("user-client");
    app.processEvents();
    if (child)
    {
        const auto state = QByteArray("COLD_STATE ") + displayState(*window) + "|" +
                           displayState(adapterWindow) + "\n";
        std::fwrite(state.constData(), 1, state.size(), stdout);
        std::fflush(stdout);
        node.shutdown();
        ThemeManager::installThemeCore(nullptr);
        return 0;
    }
    auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("patchbay.nodeTree"));
    auto *filter = window->findChild<QLineEdit *>(QStringLiteral("patchbay.nodeFilter"));
    auto *canvas = window->findChild<PatchbayCanvas *>();
    require(tree && filter && canvas, "real library/filter/canvas controls");
    require(tree->topLevelItemCount() > 1, "actual node modules scanned offline");
    QTreeWidgetItem *selectedItem = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j)
        {
            auto *item = tree->topLevelItem(i)->child(j);
            if (QFileInfo(item->data(0, Qt::UserRole).toString()).fileName() ==
                    QStringLiteral("mod_adapter.dll") &&
                item->data(0, Qt::UserRole + 1).toUInt() == 0)
                selectedItem = item;
        }
    require(selectedItem, "actual WASAPI library entry exists");
    tree->setCurrentItem(selectedItem);
    filter->setText(QStringLiteral("WASAPI"));
    const auto selectedIdentity = selectedItem->data(0, Qt::UserRole);
    const auto projectBefore = isolated.filePath(QStringLiteral("before.wjnproj"));
    const auto snapshot = canvasFixture();
    const auto client = snapshot.clients.front();
    canvas->setSnapshot(snapshot);
    canvas->selectNode(client.id);
    canvas->zoomFit();
    app.processEvents();
    const auto visibleGraph = canvas->mapToScene(canvas->viewport()->rect()).boundingRect();
    require(visibleGraph.intersects(canvas->scene()->itemsBoundingRect()),
            "fixture node geometry is actually inside the visible canvas");
    require(canvas->saveProject(projectBefore), "serialize actual offline project fixture");
    const auto originalProject = bytes(projectBefore);
    TrayController tray;
    tray.initializeFallback();
    require(QDir().mkpath(arguments.at(4)), "render directory exists before modal captures");
    for (const auto &selection : QStringList{QStringLiteral("zh-CN"), QStringLiteral("en-US"),
                                             QStringLiteral("ja-JP"), QStringLiteral("ko-KR")})
    {
        for (auto *panel : adapters.panels)
        {
            auto *input = panel->findChild<QLineEdit *>(QStringLiteral("clientNameEditor"));
            require(input, "adapter name editor");
            input->setText(QStringLiteral("unfinished user name Ω"));
            input->setSelection(2, 5);
            auto *devices = panel->findChild<QComboBox *>(QStringLiteral("deviceCombo"));
            devices->setEditText(QStringLiteral("unfinished device search Ω"));
            if (auto *channels = panel->findChild<QSpinBox *>(QStringLiteral("channelsSpinBox")))
                channels->setValue(7);
            panel->findChild<QPushButton *>(QStringLiteral("applyButton"))->click();
            require(panel->findChild<QLabel *>(QStringLiteral("statusLabel"))->text() ==
                        localizedText("adapter", "status.selectDevice", "Select a listed device"),
                    "invalid editable device stops before audio apply");
        }
        window->raise();
        window->activateWindow();
        filter->setFocus();
        app.processEvents();
        auto *focus = QApplication::focusWidget();
        const auto expanded = captureExpandedCategories(tree);
        const auto treeVertical = tree->verticalScrollBar()->value();
        const auto treeHorizontal = tree->horizontalScrollBar()->value();
        for (int i = 0; i < 4; ++i)
        {
            auto *input =
                adapters.panels[i]->findChild<QLineEdit *>(QStringLiteral("clientNameEditor"));
            // Establish selection after deliberate window/focus changes. Qt's
            // normal focus-out may clear it before the language operation.
            input->setSelection(2, 5);
            require(input->selectedText() == QStringLiteral("finis"),
                    "adapter selection exists immediately before language switch");
        }
        require(host.manager().setLanguage(selection, &error), "four-language switch succeeds");
        app.processEvents();
        verifyBindings(*window);
        verifyBindings(adapterWindow);
        QSet<QString> probeNames;
        for (int category = 0; category < tree->topLevelItemCount(); ++category)
        {
            auto *group = tree->topLevelItem(category);
            for (int index = 0; index < group->childCount(); ++index)
            {
                auto *item = group->child(index);
                if (QFileInfo(item->data(0, Qt::UserRole).toString()).fileName() !=
                    QStringLiteral("mod_meterbridge_probe.dll"))
                    continue;
                const auto type = item->data(0, Qt::UserRole + 1).toUInt();
                const char *keys[] = {"node.meterMono", "node.meterStereo", "node.meter51",
                                      "node.meter71", "node.meterCustom"};
                require(type < 5 &&
                            item->text(0) == localizedText("patchbay", keys[type], "missing"),
                        "each actual probe node retains its own translated type");
                require(item->toolTip(0).startsWith(item->text(0) + QLatin1Char('\n')),
                        "each probe has its translated description tooltip");
                probeNames.insert(item->text(0));
            }
        }
        require(probeNames.size() == 5, "five probe choices stay distinct in every language");
        require(QApplication::focusWidget() == focus, "language switch retains focus");
        require(filter->text() == QStringLiteral("WASAPI"), "library search text preserved");
        require(tree->currentItem() == selectedItem &&
                    selectedItem->data(0, Qt::UserRole) == selectedIdentity,
                "library selection and module identity preserved without rebuild");
        require(captureExpandedCategories(tree) == expanded, "library expansion preserved");
        const auto fullName = localizedText("adapter", "node.wasapiIn", "WASAPI input");
        require(selectedItem->text(0) == fullName &&
                    selectedItem->toolTip(0).startsWith(fullName + QLatin1Char('\n')),
                "library full translated name and description remain available through tooltip");
        verifyGlyphs(selectedItem->toolTip(0), tree->font());
        require(tree->textElideMode() == Qt::ElideRight,
                "native library draws an ellipsis when the sidebar cannot fit its full model text");
        bool longAliasVisible = false;
        for (auto *item : canvas->scene()->items())
            if (item->data(0).toString().toStdString() == client.id)
            {
                require(item->toolTip().contains(QString::fromStdString(client.alias)),
                        "canvas long alias tooltip retains complete user content");
                longAliasVisible = true;
            }
        require(longAliasVisible, "actual long alias graphics items exist");
        require(tree->verticalScrollBar()->value() == treeVertical &&
                    tree->horizontalScrollBar()->value() == treeHorizontal,
                "library scroll preserved");
        require(canvas->selectedNodeId() == client.id, "canvas selected identity preserved");
        require(canvas->snapshot().clients.front().name == client.name &&
                    canvas->snapshot().clients.front().alias == client.alias,
                "user names and aliases preserved");
        const auto after = isolated.filePath(QStringLiteral("after.wjnproj"));
        require(canvas->saveProject(after), "serialize after switch");
        require(bytes(after) == originalProject,
                "project JSON and routing identity unchanged byte for byte");
        for (auto *panel : adapters.panels)
        {
            auto *input = panel->findChild<QLineEdit *>(QStringLiteral("clientNameEditor"));
            require(input->text() == QStringLiteral("unfinished user name Ω") &&
                        input->selectedText() == QStringLiteral("finis"),
                    "adapter pending name and selection preserved");
            require(
                panel->findChild<QComboBox *>(QStringLiteral("deviceCombo"))->currentText() ==
                    QStringLiteral("unfinished device search Ω"),
                "editable device search preserved; language does not enumerate/reapply devices");
            if (auto *channels = panel->findChild<QSpinBox *>(QStringLiteral("channelsSpinBox")))
                require(channels->value() == 7, "pending channel count survives language switch");
            require(panel->findChild<QLabel *>(QStringLiteral("statusLabel"))->text() ==
                        localizedText("adapter", "status.selectDevice", "missing"),
                    "adapter status retranslates without reapplying settings");
        }
        for (int i = 0; i < 4; ++i)
            require(QByteArray(adapters.getSettings(adapters.nodes[i])) ==
                        adapters.initialSettings[i],
                    "adapter persistent settings unchanged by pending edits and language");
        const auto *saveAction = action(*window, QStringLiteral("action.saveProject"));
        require(saveAction, "menu action has stable key");
        require(saveAction->text() == patchText("action.saveProject", "Save project"),
                "menu action display translated independently of key");
        filter->clear();
        app.processEvents();
        const auto state = displayState(*window) + "|" + displayState(adapterWindow);
        QProcess cold;
        cold.start(app.applicationFilePath(),
                   {arguments.at(1), arguments.at(2), arguments.at(3), arguments.at(4),
                    QStringLiteral("--cold"), settingsFile});
        require(cold.waitForFinished(30000) && cold.exitCode() == 0 &&
                    cold.exitStatus() == QProcess::NormalExit,
                "real-window cold process succeeds without JACK");
        const auto output = cold.readAllStandardOutput() + cold.readAllStandardError();
        if (!output.contains("COLD_STATE " + state))
        {
            QFile diagnostic(arguments.at(4) + QStringLiteral("/cold-mismatch.txt"));
            if (diagnostic.open(QIODevice::WriteOnly))
                diagnostic.write("EXPECTED\n" + state + "\nACTUAL\n" + output);
        }
        require(
            output.contains("COLD_STATE " + state),
            "cold first display equals language switch for library, sidebar and four adapter types");
        const auto render = arguments.at(4);
        require(QDir().mkpath(render), "render directory");
        require(captureRealWindow(window, render + QStringLiteral("/patchbay-") + selection +
                                              QStringLiteral(".png")),
                "actual patchbay render");
        for (int i = 0; i < 4; ++i)
        {
            adapterTabs->setCurrentIndex(i);
            app.processEvents();
            require(adapters.panels[i]->isVisible(),
                    "each adapter panel is visible in a real window before capture");
            require(adapterWindow.grab().save(render + QStringLiteral("/adapter-") +
                                              QString::number(i) + QLatin1Char('-') + selection +
                                              QStringLiteral(".png")),
                    "actual adapter render");
            auto *meter = adapters.openMeter(i);
            app.processEvents();
            verifyBindings(*meter);
            const auto title = meter->windowTitle();
            require(host.manager().setLanguage(selection == QStringLiteral("en-US")
                                                   ? QStringLiteral("ja-JP")
                                                   : QStringLiteral("en-US"),
                                               &error),
                    "existing meter hot switch");
            app.processEvents();
            verifyBindings(*meter);
            require(meter->windowTitle() == title, "meter client identity is not translated");
            require(host.manager().setLanguage(selection, &error),
                    "restore meter capture language");
            app.processEvents();
            verifyBindings(*meter);
            require(meter->grab().save(render + "/meter-" + QString::number(i) + "-" + selection +
                                       ".png"),
                    "real adapter meter capture");
            adapters.nodes[i]->destroyWindow(nullptr);
            adapters.meters[i] = nullptr;
            meter = adapters.openMeter(i);
            app.processEvents();
            verifyBindings(*meter);
            adapters.nodes[i]->destroyWindow(nullptr);
            adapters.meters[i] = nullptr;
        }
        verifyModalWindows(host, window, selection, render);
        auto firstLease = moduleLanguageLease(arguments.at(3), 0);
        auto secondLease = moduleLanguageLease(arguments.at(3), 0);
        require(firstLease && firstLease == secondLease,
                "library and instance consumers share the actual community DLL domain lease");
        auto example = LoadedModuleNode::create(arguments.at(3), 0);
        require(example && !example->hasClient(),
                "actual community loader initializes nonaudio example without JACK");
        require(example->name() == QStringLiteral("Example Node"),
                "community raw metadata identity is retained");
        require(
            example->displayName() == localizedText("example.node", "name", "Example Node"),
            "late project-loaded community node resolves validated displayNameKey outside the scan directory");
        const auto *exampleIdentity = example.get();
        require(host.manager().setLanguage(selection == QStringLiteral("en-US")
                                               ? QStringLiteral("ja-JP")
                                               : QStringLiteral("en-US"),
                                           &error),
                "live community node language switch");
        require(example.get() == exampleIdentity &&
                    example->displayName() == localizedText("example.node", "name", "Example Node"),
                "community display updates without recreating the instance");
        require(host.manager().setLanguage(selection, &error), "restore community test language");
        example.reset();
        secondLease.reset();
        require(firstLease != nullptr, "one remaining consumer retains domain lifetime");
        firstLease.reset();
        auto *dock = window->findChild<QDockWidget *>(QStringLiteral("patchbay.nodeLibrary"));
        require(dock, "real node sidebar exists");
        dock->hide();
        dock->show();
        app.processEvents();
        verifyBindings(*dock);
        require(dock->isVisible() && tree->currentItem() == selectedItem,
                "reopened sidebar retains language and selection");
        QWidget *quickbar = nullptr;
        for (auto *candidate : app.topLevelWidgets())
            if (candidate->objectName() == QStringLiteral("quickAccessBar"))
                quickbar = candidate;
        require(quickbar && quickbar->isVisible(), "fallback quickbar is a real visible window");
        int shown = 0;
        const std::vector<TrayNodeGuiEntry> entries{
            {QStringLiteral("s3-adapter"),
             QStringLiteral("Windows Native Driver"),
             {},
             QStringLiteral("WASAPI In"),
             [&] {
                 ++shown;
                 return true;
             }},
            {QStringLiteral("s3-user"), QStringLiteral("Windows Native Driver"),
             QStringLiteral("User client Ω 名称"), QStringLiteral("System MIDI Out"), [&] {
                 ++shown;
                 return true;
             }}};
        const auto translated = localizedText("adapter", "node.wasapiIn", "WASAPI input");
        TrayController::setNodeGuiEntries(
            entries, {{QStringLiteral("s3-adapter"), translated},
                      {QStringLiteral("s3-user"), QStringLiteral("unused display")}});
        app.processEvents();
        auto *adapterAction = action(*quickbar, QStringLiteral("s3-adapter"));
        auto *userAction = action(*quickbar, QStringLiteral("s3-user"));
        require(adapterAction && adapterAction->text() == translated,
                "new tray overload uses translated display map with stable callback ID");
        require(userAction && userAction->text() == QStringLiteral("User client Ω 名称"),
                "tray keeps explicit client names unchanged");
        auto *directionMenu = qobject_cast<QMenu *>(adapterAction->parent());
        auto *protocolMenu =
            directionMenu ? qobject_cast<QMenu *>(directionMenu->parent()) : nullptr;
        require(protocolMenu && protocolMenu->title() == QStringLiteral("WASAPI"),
                "translated adapter display retains original WASAPI classification");
        directionMenu->popup(window->mapToGlobal(QPoint(80, 80)));
        app.processEvents();
        require(directionMenu->isVisible(), "native device menu visibly opens");
        require(directionMenu->grab().save(render + "/devices-" + selection + ".png"),
                "real native device menu capture");
        directionMenu->hide();
        adapterAction->trigger();
        userAction->trigger();
        require(shown == 2,
                "translated and user-named tray entries invoke their original callbacks");
        TrayController::setNodeGuiEntries(entries);
        app.processEvents();
        require(action(*quickbar, QStringLiteral("s3-adapter"))->text() ==
                    QStringLiteral("WASAPI In"),
                "old tray overload preserves original metadata display");
        TrayController::clearNodeGuiEntries();
        filter->setText(QStringLiteral("WASAPI"));
    }
    // Open a genuine host-created child tab, then replace only its JACK port
    // allocator with a deterministic offline model fixture.
    auto *mainWindow = qobject_cast<QMainWindow *>(window);
    require(mainWindow, "patchbay is a main window");
    auto *graphTabs = qobject_cast<QTabWidget *>(mainWindow->centralWidget());
    require(graphTabs, "actual graph tabs");
    const auto subgraphId =
        canvas->addSubGraph(QStringLiteral("User subgraph Ω 名称"), QPointF(100, 100));
    require(!subgraphId.empty(), "create actual subgraph model");
    app.processEvents();
    QGraphicsItem *subgraphItem = nullptr;
    for (auto *item : canvas->scene()->items())
        if (item->data(0).toString().toStdString() == subgraphId)
            subgraphItem = item;
    require(subgraphItem, "actual subgraph graphics item exists");
    QGraphicsSceneMouseEvent doubleClick(QEvent::GraphicsSceneMouseDoubleClick);
    doubleClick.setButton(Qt::LeftButton);
    doubleClick.setPos(subgraphItem->boundingRect().center());
    doubleClick.setScenePos(subgraphItem->sceneBoundingRect().center());
    canvas->scene()->sendEvent(subgraphItem, &doubleClick);
    app.processEvents();
    auto *childCanvas = dynamic_cast<PatchbayCanvas *>(graphTabs->currentWidget());
    require(childCanvas && childCanvas != canvas && childCanvas->sceneContext().isSubGraph,
            "double click opens actual host-created child canvas");
    childCanvas->setInterfaceNodeAddedHandler([](PatchbayInterfaceInfo &item) {
        item.portId = "offline-fixture:" + item.type.toStdString() + ":" + item.id;
        item.externalPortId = "offline-external:" + item.type.toStdString() + ":" + item.id;
        return true;
    });
    for (const auto &type : QStringList{QStringLiteral("audio_in"), QStringLiteral("audio_out"),
                                        QStringLiteral("midi_in"), QStringLiteral("midi_out")})
        require(childCanvas->addInterfaceNode(type, {}),
                "create actual interface UI using offline identity allocator");
    const auto interfaces = childCanvas->interfaceNodes();
    require(interfaces.size() == 4, "four audio/MIDI interface types exist");
    filter->clear();
    childCanvas->zoomFit();
    app.processEvents();
    require(childCanvas->mapToScene(childCanvas->viewport()->rect())
                .boundingRect()
                .intersects(childCanvas->scene()->itemsBoundingRect()),
            "interface graphics are inside the real visible child canvas");
    const auto childProject = isolated.filePath(QStringLiteral("subgraph.wjnproj"));
    require(childCanvas->saveProject(childProject), "save actual interface project fixture");
    const auto childBytes = bytes(childProject);
    for (const auto &selection : QStringList{QStringLiteral("zh-CN"), QStringLiteral("en-US"),
                                             QStringLiteral("ja-JP"), QStringLiteral("ko-KR")})
    {
        require(host.manager().setLanguage(selection, &error), "subgraph language switch");
        app.processEvents();
        verifyBindings(*window);
        require(graphTabs->tabText(graphTabs->indexOf(childCanvas)) ==
                    QStringLiteral("User subgraph Ω 名称"),
                "existing user subgraph tab name preserved");
        require(graphTabs->tabText(graphTabs->indexOf(canvas)) ==
                    patchText("graph.main", "Main graph"),
                "main graph tab retranslates");
        for (std::size_t i = 0; i < interfaces.size(); ++i)
        {
            const auto &current = childCanvas->interfaceNodes()[i];
            const auto &original = interfaces[i];
            require(current.id == original.id && current.type == original.type &&
                        current.name == original.name && current.portId == original.portId &&
                        current.externalPortId == original.externalPortId,
                    "actual interface model and routing names preserved");
        }
        require(childCanvas->saveProject(childProject) && bytes(childProject) == childBytes,
                "subgraph project bytes unchanged by language");
        require(captureRealWindow(window, arguments.at(4) + "/subgraph-" + selection + ".png"),
                "real subgraph and interface library capture");
        childCanvas->scene()->clearSelection();
        const QPoint menuPoint(4, 4);
        QTimer::singleShot(0, childCanvas, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            require(menu && menu->isVisible(), "actual canvas native context menu opens");
            verifyBindings(*menu);
            require(!menu->actions().isEmpty(), "canvas context actions exist");
            require(host.manager().setLanguage(selection == QStringLiteral("en-US")
                                                   ? QStringLiteral("ja-JP")
                                                   : QStringLiteral("en-US"),
                                               &error),
                    "open canvas menu hot switch");
            app.processEvents();
            verifyBindings(*menu);
            require(host.manager().setLanguage(selection, &error), "canvas menu language restored");
            app.processEvents();
            verifyBindings(*menu);
            require(menu->grab().save(arguments.at(4) + "/context-" + selection + ".png"),
                    "real canvas context menu capture");
            menu->close();
        });
        QContextMenuEvent context(QContextMenuEvent::Mouse, menuPoint,
                                  childCanvas->viewport()->mapToGlobal(menuPoint));
        QApplication::sendEvent(childCanvas->viewport(), &context);
        app.processEvents();
    }
    node.shutdown();
    tray.shutdown();
    ThemeManager::installThemeCore(nullptr);
    std::printf("S3 PASS: %d real-window checks\n", checks);
    std::fflush(stdout);
    return 0;
}
