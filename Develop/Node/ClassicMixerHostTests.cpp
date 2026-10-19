// SPDX-License-Identifier: AGPL-3.0-only
#include "../../src/patchbay/mod/src/Library.cpp"
#include "WindowShowCounter.hpp"
#include <QFontDatabase>
#include <QJsonArray>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <WinJACKNexus/UI/ClassicMixerWindow.hpp>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <iostream>
#include <mod_patchbay/ProjectStateCodec.hpp>
#include <stdexcept>

namespace CM = WinJACKNexus::Common::ClassicMixer;
namespace UI = WinJACKNexus::UI;
namespace C = UI::Controls;
QString mixerModule(int layout)
{
    switch (layout)
    {
    case 8:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_8);
    case 16:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_16);
    default:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_24);
    }
}
int checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    std::cout << "CHECK " << checks << ": " << message << std::endl;
    if (!value)
        throw std::runtime_error(message);
}
void settle()
{
    QTest::qWait(180);
    QApplication::processEvents();
}
PatchbayCanvas *graphContext(QWidget *window, const std::string &id)
{
    for (auto *canvas : window->findChildren<PatchbayCanvas *>())
        if (canvas->sceneContext().id == id)
            return canvas;
    throw std::runtime_error("graph context missing");
}
PatchbayInterfaceInfo boundary(PatchbayCanvas *canvas, const QString &type)
{
    for (const auto &interface : canvas->interfaceNodes())
        if (interface.type == type)
            return interface;
    throw std::runtime_error("interface missing");
}
std::string mixerSettings(int layout, const QString &name)
{
    auto state = CM::State::create(layout);
    state.layoutChosen = true;
    state.inputs[0].pan = -1;
    state.inputs[0].enabled = {true, true};
    state.inputs[0].pre = {true, false};
    state.inputs[0].sends = {-3, -6};
    state.inputs[0].name = QString::fromUtf8("话筒 보컬");
    state.masterFader = -3;
    state.monitorFader = -6;
    check(state.savePreset(QString::fromUtf8("内嵌 프리셋")),
          "create embedded preset for host save");
    auto json = state.toJson();
    json.insert("clientName", name);
    return QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
}
std::vector<PatchbayProjectModule> readModuleRecords(const QString &path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read project without mutating active canvas");
    const auto document = QJsonDocument::fromJson(file.readAll()).object();
    std::vector<PatchbayProjectModule> records;
    for (const auto &value : document.value("modules").toArray())
    {
        const auto record = value.toObject();
        QJsonObject state;
        check(WinJACKNexus::Patchbay::ProjectStateCodec::restoreModuleSettings(record, state),
              "production settings codec decodes Base64");
        records.push_back({record.value("path").toString(), 0,
                           record.value("client").toString().toStdString(),
                           QJsonDocument(state).toJson(QJsonDocument::Compact).toStdString(),
                           record.value("context").toString().toStdString()});
    }
    return records;
}
UI::ClassicMixerWindow *visibleMixer()
{
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *window = dynamic_cast<UI::ClassicMixerWindow *>(widget);
            window && window->isVisible())
            return window;
    throw std::runtime_error("standalone mixer missing");
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try
    {
        check(app.platformName() == "windows", "Windows real-window platform");
        app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        WindowShowCounter shows;
        app.installEventFilter(&shows);
        QTemporaryDir temporary;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        UI::HostI18n host({temporary.filePath("language.ini"), {"en-US"}, false});
        QString error;
        check(host.initialize(QCoreApplication::applicationDirPath(), &error),
              "isolated host language session");
        auto &api = WinJACKNexus::Audio::JackApi::instance();
        const QString fixturePath = QString::fromUtf8(CLASSIC_MIXER_BOUNDARY);
        check(api.load(reinterpret_cast<const wchar_t *>(fixturePath.utf16())),
              "load explicit offline JACK fixture only");
        const auto fixture = LoadLibraryW(reinterpret_cast<LPCWSTR>(fixturePath.utf16()));
        const auto process =
            reinterpret_cast<void (*)()>(GetProcAddress(fixture, "wjn_boundary_process"));
        const auto count = reinterpret_cast<uint32_t (*)(uint32_t)>(
            GetProcAddress(fixture, "wjn_boundary_counter"));
        const auto failRename =
            reinterpret_cast<void (*)(int)>(GetProcAddress(fixture, "wjn_classic_fail_rename"));
        check(process && count && failRename, "offline observation/failure hooks");
        for (int layout : {8, 16, 24})
        {
            auto newlyCreated =
                LoadedModuleNode::create(mixerModule(layout), 0, {}, WJN_NODE_MODE_VIRT);
            check(
                newlyCreated &&
                    QJsonDocument::fromJson(QByteArray::fromStdString(newlyCreated->settingsJson()))
                            .object()
                            .value("layoutId")
                            .toInt() == layout &&
                    QApplication::activeModalWidget() == nullptr,
                "fixed DLL creates correct layout without modal chooser");
            const auto legacy = QFileInfo(mixerModule(layout))
                                    .dir()
                                    .absoluteFilePath("../classic_mixer/mod_classic_mixer.dll");
            auto migrated = LoadedModuleNode::create(legacy, 0, mixerSettings(layout, "Migrated"),
                                                     WJN_NODE_MODE_VIRT);
            check(migrated && migrated->modulePath() == mixerModule(layout),
                  "legacy module path migrates to matching fixed DLL");
        }
        check(!LoadedModuleNode::create(mixerModule(8), 0, mixerSettings(24, "Mismatch"),
                                        WJN_NODE_MODE_VIRT),
              "host refuses mismatched fixed module state");
        const auto available = scanNodeModules();
        for (int layout : {8, 16, 24})
            check(std::count_if(available.begin(), available.end(),
                                [&](const auto &entry) {
                                    return entry.modulePath == mixerModule(layout) &&
                                           entry.category == "Classic Mixer";
                                }) == 1,
                  "each fixed model appears once under Classic Mixer");
        check(std::none_of(available.begin(), available.end(),
                           [](const auto &entry) {
                               return QFileInfo(entry.modulePath).fileName() ==
                                      "mod_classic_mixer.dll";
                           }),
              "obsolete chooser DLL excluded from node menu");
        const QString project = temporary.filePath("classic.wjn");
        std::string rootContext, nestedContext;
        std::vector<PatchbayProjectModule> expected;
        {
            PatchbayNode scaffold;
            check(scaffold.init() && scaffold.createWindow(nullptr),
                  "actual host creates visible graph editor");
            auto *window = QWidget::find(reinterpret_cast<WId>(scaffold.createWindow(nullptr)));
            check(QTest::qWaitForWindowExposed(window) && window->devicePixelRatioF() == 1,
                  "host exposed at 100 percent");
            auto *main = graphContext(window, "main");
            const auto rootId = main->addSubGraph("Root", {});
            rootContext = "main/" + rootId;
            auto *root = graphContext(window, rootContext);
            const auto nestedId = root->addSubGraph("Nested", {});
            nestedContext = rootContext + '/' + nestedId;
            auto *nested = graphContext(window, nestedContext);
            for (auto *canvas : {root, nested})
            {
                check(canvas->addInterfaceNode("audio_in", {}),
                      "host creates audio input junction");
                check(canvas->addInterfaceNode("audio_out", {}),
                      "host creates audio output junction");
            }
            const auto rootClient = rootContext + "/node-classic-root";
            const auto nestedClient = nestedContext + "/node-classic-nested";
            const auto rin = boundary(root, "audio_in"), rout = boundary(root, "audio_out");
            const auto nin = boundary(nested, "audio_in"), nout = boundary(nested, "audio_out");
            expected = {
                {mixerModule(8), 0, "ClassicMain", mixerSettings(8, "ClassicMain"), "main"},
                {mixerModule(16), 0, rootClient, mixerSettings(16, "Root mixer"), rootContext},
                {mixerModule(24), 0, nestedClient, mixerSettings(24, "Nested mixer"),
                 nestedContext}};
            std::vector<PatchbaySubGraphProject> graphs;
            for (auto *canvas : {main, root, nested})
                graphs.push_back({canvas->sceneContext(),
                                  canvas->subGraphNodes(),
                                  canvas->interfaceNodes(),
                                  {}});
            graphs[1].connections = {{rin.portId, rootClient + ":port-1"},
                                     {rootClient + ":port-101", nin.portId},
                                     {nout.portId, rout.portId}};
            graphs[2].connections = {{nin.portId, nestedClient + ":port-1"},
                                     {nestedClient + ":port-101", nout.portId}};
            auto legacyRecords = expected;
            for (auto &record : legacyRecords)
                record.modulePath = QFileInfo(record.modulePath)
                                        .dir()
                                        .absoluteFilePath("../classic_mixer/mod_classic_mixer.dll");
            check(main->saveProject(project, legacyRecords, graphs),
                  "production project writer saves three layouts and nested routes");
            scaffold.shutdown();
        }
        QFile saved(project);
        check(saved.open(QIODevice::ReadOnly), "read saved project");
        const auto original = saved.readAll();
        saved.close();
        const auto rootJson = QJsonDocument::fromJson(original).object();
        const auto serialized = original;
        check(!serialized.contains("payloadBase64") && serialized.contains("settingsBase64"),
              "all module state is inside project Base64");
        patchbaySettings().setValue("lastProjectPath", project);
        patchbaySettings().setValue("autoSaveIntervalSeconds", 0);
        PatchbayNode restored;
        check(restored.init() && restored.createWindow(nullptr),
              "actual host restores full main project");
        settle();
        auto *window = QWidget::find(reinterpret_cast<WId>(restored.createWindow(nullptr)));
        auto *main = graphContext(window, "main");
        auto *root = graphContext(window, rootContext);
        auto *nested = graphContext(window, nestedContext);
        check(root->logicalConnections().size() == 3 && nested->logicalConnections().size() == 2,
              "all nested module routes restored");
        check(count(3) == 3, "only observer root boundary and main mixer own JACK clients");
        const auto rootDisplay =
            std::find_if(root->snapshot().clients.begin(), root->snapshot().clients.end(),
                         [&](const auto &client) { return client.id == expected[1].clientId; });
        check(rootDisplay != root->snapshot().clients.end() && rootDisplay->name == "Root mixer",
              "virtual canvas displays user node name separately from stable ID");
        auto loaded = readModuleRecords(project);
        check(loaded.size() == 3, "project contains all three module records");
        for (std::size_t index = 0; index < loaded.size(); ++index)
            check(loaded[index].settingsJson == expected[index].settingsJson,
                  "Base64 settings and embedded preset exact round trip");
        const auto rin = boundary(root, "audio_in"), rout = boundary(root, "audio_out");
        auto *inputPort = api.portByName(nullptr, rin.externalPortId.c_str());
        auto *outputPort = api.portByName(nullptr, rout.externalPortId.c_str());
        check(inputPort && outputPort, "actual root native ports supplied by fixture");
        auto *input = static_cast<float *>(api.portGetBuffer(inputPort, 64));
        std::fill_n(input, 64, 0.25f);
        process();
        auto *output = static_cast<float *>(api.portGetBuffer(outputPort, 64));
        const auto expectedSample = 0.25 * CM::gainForDb(-3) * CM::gainForDb(-3);
        check(std::abs(output[20] - expectedSample) < 1e-6,
              "restored two-level Classic Mixer DSP produces expected real samples");
        // Main graph exercises the real module's JACK adapter against the fixture.
        const auto monoName = QString::fromUtf8("ClassicMain:话筒 보컬_M").toUtf8();
        auto *mainInput = api.portByName(nullptr, monoName.constData());
        auto *mainOutput = api.portByName(nullptr, "ClassicMain:Master_L");
        check(mainInput && mainOutput, "main graph correct named ports");
        std::fill_n(static_cast<float *>(api.portGetBuffer(mainInput, 64)), 64, 0.25f);
        process();
        check(std::abs(static_cast<float *>(api.portGetBuffer(mainOutput, 64))[20] -
                       0.25 * CM::gainForDb(-3)) < 1e-6,
              "JACK adapter shares DSP results");
        check(api.connect(nullptr, "ClassicMain:Master_R", "ClassicMain:Stereo 1_L") == 0 &&
                  api.connect(nullptr, "ClassicMain:AUX1_R", "ClassicMain:Stereo 1_R") == 0,
              "fixture owns stereo self-connections");
        auto *tabs = window->findChild<QTabWidget *>();
        tabs->setCurrentWidget(main);
        settle();
        main->selectNode("ClassicMain");
        settle();
        auto *gui = window->findChild<QPushButton *>("showModuleGuiButton");
        check(gui && gui->isEnabled(), "actual inspector enables independent GUI");
        for (auto *checkbox : window->findChildren<QCheckBox *>())
        {
            if (checkbox->property("wjn.i18n.text").toString() == "inspector.split")
                check(!checkbox->isVisible(), "Classic Mixer hides generic sidebar split editor");
        }
        gui->click();
        settle();
        auto *console = visibleMixer();
        const auto renameStereo = [&](const QString &name) {
            auto *editor = dynamic_cast<C::TextField *>(
                console->findChild<QWidget *>("classicMixer.strip.6.name"));
            check(editor != nullptr, "stereo uses shared name editor");
            editor->setFocus();
            editor->setText(name);
            QTest::keyClick(editor, Qt::Key_Return);
            settle();
        };
        failRename(4);
        renameStereo("Rejected");
        check(api.portByName(nullptr, "ClassicMain:Stereo 1_L") &&
                  api.portByName(nullptr, "ClassicMain:Stereo 1_R"),
              "real module rolls back failed second stereo rename");
        failRename(-1);
        renameStereo("Renamed stereo");
        auto *left = api.portByName(nullptr, "ClassicMain:Renamed stereo_L");
        auto *right = api.portByName(nullptr, "ClassicMain:Renamed stereo_R");
        check(left && right && !api.portByName(nullptr, "ClassicMain:Stereo 1_L"),
              "both actual module ports renamed");
        const auto **peers = api.portGetConnections(left);
        check(peers && std::string(peers[0]) == "ClassicMain:Master_R",
              "left connection automatically retained");
        api.freeMemory(peers);
        peers = api.portGetConnections(right);
        check(peers && std::string(peers[0]) == "ClassicMain:AUX1_R",
              "right connection automatically retained");
        api.freeMemory(peers);
        console->close();
        tabs->setCurrentWidget(nested);
        nested->selectNode(expected[2].clientId);
        settle();
        gui->click();
        settle();
        console = visibleMixer();
        auto *nameEditor = dynamic_cast<C::TextField *>(
            console->findChild<QWidget *>("classicMixer.strip.0.name"));
        check(nameEditor != nullptr, "nested GUI available");
        C::ClientNameWidget *nodeName = nullptr;
        for (auto *widget : window->findChildren<QWidget *>())
            if (auto *candidate = dynamic_cast<C::ClientNameWidget *>(widget))
                nodeName = candidate;
        check(nodeName != nullptr, "nested sidebar has public node-name control");
        auto *nodeNameEditor = nodeName->findChild<QLineEdit *>();
        auto *applyName = nodeName->findChild<QPushButton *>();
        check(nodeNameEditor && applyName, "node-name edit and apply available");
        nodeNameEditor->setText("Nested renamed");
        applyName->click();
        settle();
        const auto renamedClient =
            std::find_if(nested->snapshot().clients.begin(), nested->snapshot().clients.end(),
                         [&](const auto &client) { return client.id == expected[2].clientId; });
        check(renamedClient != nested->snapshot().clients.end() &&
                  renamedClient->name == "Nested renamed",
              "virtual node rename updates caption without changing graph identity");
        nameEditor->setFocus();
        nameEditor->setText(QString::fromUtf8("已改名 리드"));
        QTest::keyClick(nameEditor, Qt::Key_Return);
        settle();
        process();
        check(std::abs(output[20] - expectedSample) < 1e-6 &&
                  nested->logicalConnections().size() == 2,
              "nested live port rename retains audio routes");
        auto *presetName =
            dynamic_cast<C::TextField *>(console->findChild<QWidget *>("classicMixer.presetName"));
        auto *save = dynamic_cast<C::ActionButton *>(
            console->findChild<QWidget *>("classicMixer.newPreset"));
        presetName->setText("Host embedded");
        save->click();
        settle();
        console->close();
        QAction *saveAction = nullptr;
        for (auto *action : window->findChildren<QAction *>())
            if (action->data().toString() == "action.saveProject")
                saveAction = action;
        check(saveAction != nullptr, "production save action exists");
        saveAction->trigger();
        settle();
        loaded = readModuleRecords(project);
        for (const auto &record : loaded)
            check(QFileInfo(record.modulePath).fileName() != "mod_classic_mixer.dll",
                  "explicit save persists migrated module paths");
        const auto nestedRecord =
            std::find_if(loaded.begin(), loaded.end(), [&](const auto &record) {
                return record.clientId == expected[2].clientId;
            });
        check(nestedRecord != loaded.end(), "nested stable instance remains saved");
        const auto nestedState =
            QJsonDocument::fromJson(QByteArray::fromStdString(nestedRecord->settingsJson)).object();
        check(nestedState.value("presets").toArray().size() == 2 &&
                  nestedState.value("inputs").toArray()[0].toObject().value("name").toString() ==
                      QString::fromUtf8("已改名 리드"),
              "host saves live changed names and embedded presets");
        check(saved.open(QIODevice::ReadOnly), "read updated project");
        const auto finalBytes = saved.readAll();
        saved.close();
        restored.shutdown();
        check(count(3) == 0 && count(4) == 0, "shutdown releases test-owned clients and ports");
        PatchbayNode reopened;
        check(reopened.init() && reopened.createWindow(nullptr),
              "actual project reopens after all instances destroyed");
        settle();
        window = QWidget::find(reinterpret_cast<WId>(reopened.createWindow(nullptr)));
        main = graphContext(window, "main");
        nested = graphContext(window, nestedContext);
        check(nested->logicalConnections().size() == 2 &&
                  api.portByName(nullptr, "ClassicMain:Renamed stereo_L"),
              "reopen retains VIRT routes and new JACK names");
        check(saved.open(QIODevice::ReadOnly) && saved.readAll() == finalBytes,
              "restoration never overwrites source project");
        saved.close();
        QDir().mkpath("build/classic-mixer-split-evidence");
        window->resize(1200, 800);
        window->findChild<QTabWidget *>()->setCurrentWidget(main);
        main->zoomFit();
        main->selectNode("ClassicMain");
        window->raise();
        settle();
        check(window->screen()
                  ->grabWindow(window->winId())
                  .save("build/classic-mixer-split-evidence/restored-host.png"),
              "capture actual restored host window");
        reopened.shutdown();
        check(shows.shows <= 100, "bounded 100-window batch");
        std::cout << "PASS " << checks
                  << " offline host integration checks; window shows=" << shows.shows << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
