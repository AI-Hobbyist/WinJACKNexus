// SPDX-License-Identifier: AGPL-3.0-only
#include "../../src/patchbay/mod/src/Library.cpp"
#include <QElapsedTimer>
#include "WindowShowCounter.hpp"
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <QFontDatabase>
#include <QTextLayout>
#include <QGlyphRun>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QHelpEvent>
#include <QToolTip>
#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <DbgHelp.h>
#include <cstdio>
#include <cstdlib>

namespace
{
uint32_t checks{};
LONG crashTrace(EXCEPTION_POINTERS *exception)
{
    const auto process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    auto machine = *exception->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = machine.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = machine.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = machine.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    std::fprintf(stderr, "CRASH code=%lx address=%p checks=%u\n",
                 exception->ExceptionRecord->ExceptionCode,
                 exception->ExceptionRecord->ExceptionAddress, checks);
    for (unsigned index = 0; index < 30; ++index)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &machine,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        std::array<char, sizeof(SYMBOL_INFO) + 1024> storage{};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1024;
        DWORD64 displacement{};
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement{};
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            std::fprintf(stderr, "  %s+%llu", symbol->Name, displacement);
        else
            std::fprintf(stderr, "  0x%llx", frame.AddrPC.Offset);
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
            std::fprintf(stderr, " %s:%lu", line.FileName, line.LineNumber);
        std::fprintf(stderr, "\n");
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
void require(bool condition, const char *description)
{
    ++checks;
    if (std::strcmp(description, "bounded MIDI accepts capacity") != 0 &&
        std::strcmp(description, "system CJK fallback without tofu") != 0)
    {
        std::printf("CHECK %u: %s\n", checks, description);
    }
    if (!condition)
    {
        std::fprintf(stderr, "S3 FAIL: %s\n", description);
        std::exit(2);
    }
}
void settle()
{
    QTest::qWait(180);
    QApplication::processEvents();
}
void capture(QWidget *window, const QString &path)
{
    QPointer<QWidget> visibleWindow(window);
    if (window->windowType() != Qt::ToolTip)
    {
        window->raise();
        window->activateWindow();
        settle();
    }
    require(!visibleWindow.isNull(), "capture window remains alive");
    require(window->isVisible() && IsWindowVisible(reinterpret_cast<HWND>(window->winId())),
            "real HWND visible");
    require(window->devicePixelRatioF() == 1.0, "100 percent DPR");
    require(window->screen()->grabWindow(window->winId()).save(path), "real Windows capture");
}
QTreeWidgetItem *findModule(QTreeWidget *tree, const QString &path)
{
    QTreeWidgetItemIterator iterator(tree);
    while (*iterator)
    {
        if ((*iterator)->data(0, Qt::UserRole).toString() == path)
            return *iterator;
        ++iterator;
    }
    return nullptr;
}
PatchbayCanvas *context(QWidget *window, const std::string &id)
{
    for (auto *canvas : window->findChildren<PatchbayCanvas *>())
        if (canvas->sceneContext().id == id)
            return canvas;
    return nullptr;
}
void addModule(QTreeWidget *tree, const QString &path)
{
    auto *item = findModule(tree, path);
    require(item && !item->isHidden(), "context library exposes permitted node");
    tree->itemDoubleClicked(item, 0);
    settle();
}
std::string moduleClient(PatchbayCanvas *canvas)
{
    for (const auto &client : canvas->snapshot().clients)
        if (client.id.find("/node-") != std::string::npos)
            return client.id;
    require(false, "virtual client appears in child snapshot");
    return {};
}
PatchbayInterfaceInfo interface(PatchbayCanvas *canvas, const char *type)
{
    for (const auto &port : canvas->interfaceNodes())
        if (port.type == QLatin1String(type))
            return port;
    require(false, "interface exists");
    return {};
}
void wire(PatchbayCanvas *canvas, const std::string &source, const std::string &destination)
{
    auto routes = canvas->savedLogicalConnections();
    routes.push_back({source, destination});
    require(canvas->restoreLogicalConnections(routes), "restore route invokes real host gate");
    settle();
    require(canvas->logicalConnections().size() == routes.size(), "all valid routes schedule");
}
bool menuContains(QMenu *menu, const QString &path)
{
    for (auto *action : menu->actions())
    {
        if (action->data().toString() == path)
            return true;
        if (action->menu() && menuContains(action->menu(), path))
            return true;
    }
    return false;
}
void menuMatrix(PatchbayCanvas *canvas, const std::array<QString, 3> &paths, bool child)
{
    bool inspected = false;
    QTimer::singleShot(60, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        require(menu, "native real context menu visible");
        require(menuContains(menu, paths[0]) == !child, "JACK-only add menu matrix");
        require(menuContains(menu, paths[1]) == child, "VIRT-only add menu matrix");
        require(menuContains(menu, paths[2]), "dual add menu matrix");
        inspected = true;
        menu->close();
    });
    const auto point = canvas->viewport()->rect().bottomRight() - QPoint(15, 15);
    QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                            canvas->viewport()->mapToGlobal(point));
    QApplication::sendEvent(canvas->viewport(), &event);
    require(inspected, "add menu was inspected");
}
void graphChecks(const QString &path)
{
    using Graph = WinJACKNexus::Patchbay::VirtualGraph;
    auto node = LoadedModuleNode::create(path, 0, "{\"gain\":1}", WJN_NODE_MODE_VIRT, 48000, 64);
    require(bool(node), "direct VIRT creation");
    node->setContextId("main/root");
    Graph graph;
    auto ports = node->virtualPorts();
    Graph::Topology topology;
    topology.processors = {node->virtualProcessor()};
    topology.ports = ports;
    std::string error;
    require(graph.rebuild(topology, 64, error), "preallocated schedule");
    auto invalid = topology;
    invalid.routes.push_back({ports[1].id, ports[0].id});
    require(!graph.rebuild(invalid, 64, error),
            "zero latency module cycle rejected transactionally");
    invalid = topology;
    invalid.routes.push_back({ports[0].id, ports[1].id});
    require(!graph.rebuild(invalid, 64, error), "reversed port direction rejected");
    invalid = topology;
    invalid.routes.push_back({ports[1].id, ports[2].id});
    require(!graph.rebuild(invalid, 64, error), "audio MIDI mismatch rejected");
    require(graph.begin(64), "old valid state survives failed edits");
    graph.process(64, 99);
    const auto output = graph.audio(graph.slot(ports[1].id));
    require(output && std::all_of(output, output + 64, [](float value) { return value == 0; }),
            "unconnected output silence");
    const uint8_t note[]{0x90, 60, 100};
    const auto midiSlot = graph.slot(ports[2].id);
    for (uint32_t index = 0; index < Graph::midiEvents; ++index)
        require(graph.appendMidi(midiSlot, {3, 3, note}, 64), "bounded MIDI accepts capacity");
    require(!graph.appendMidi(midiSlot, {3, 3, note}, 64) && graph.midi(midiSlot)->dropped == 1,
            "overflow drops newest with counter");
    graph.process(64, 100);
    require(graph.midi(graph.slot(ports[3].id))->eventCount == Graph::midiEvents,
            "bounded MIDI forwarded");
}
}

#include "DeliveryChecks.inc"

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter(crashTrace);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    require(app.platformName() == QStringLiteral("windows"), "windows platform only");
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    WindowShowCounter shows;
    app.installEventFilter(&shows);
    const auto args = app.arguments();
    require(args.size() == 4, "runtime, boundary DLL and evidence supplied");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    WinJACKNexus::UI::HostI18n host({settings.filePath("language.ini"), {"en-US"}, false});
    QString error;
    require(host.initialize(args[1], &error), "host i18n initialized");
    deliveryChecks(args[1]);
    QDir().mkpath(args[3]);
    auto &api = WinJACKNexus::Audio::JackApi::instance();
    require(api.load(reinterpret_cast<const wchar_t *>(args[2].utf16())),
            "load only offline JACK boundary substitute");
    const auto boundary = LoadLibraryW(reinterpret_cast<LPCWSTR>(args[2].utf16()));
    const auto counter =
        reinterpret_cast<uint32_t (*)(uint32_t)>(GetProcAddress(boundary, "wjn_boundary_counter"));
    const auto process =
        reinterpret_cast<void (*)()>(GetProcAddress(boundary, "wjn_boundary_process"));
    const auto setClock = reinterpret_cast<void (*)(jack_nframes_t, jack_nframes_t)>(
        GetProcAddress(boundary, "wjn_boundary_set_clock"));
    require(counter && process && setClock, "boundary observation hooks");
    {
        SubGraphPortSet clockRoot;
        require(clockRoot.openBoundary("clock-regression"), "minimal root boundary opens");
        for (const bool input : {true, false})
        {
            PatchbayInterfaceInfo description;
            description.id = input ? "clock/in" : "clock/out";
            description.portId = description.id + ":port";
            description.kind = WinJACKNexus::Patchbay::Core::PortKind::audio;
            clockRoot.interfaces.push_back(description);
            clockRoot.ports.emplace_back();
            auto &port = clockRoot.ports.back();
            port.id = description.portId;
            port.kind = description.kind;
            port.isInput = input;
            require(port.port.registerPort(clockRoot.client, input ? "in" : "out",
                                           JACK_DEFAULT_AUDIO_TYPE,
                                           input ? JackPortIsInput : JackPortIsOutput),
                    "minimal root audio interface registers");
        }
        require(clockRoot.connect("clock/in:port", "clock/out:port"),
                "minimal root audio passthrough route");
        const auto activations = counter(5);
        QElapsedTimer elapsed;
        elapsed.start();
        for (int index = 0; index < 20; ++index)
            require(clockRoot.synchronizeClock(), "idle clock poll succeeds");
        const auto repeatedActivations = counter(5) - activations;
        auto *input = clockRoot.ports[0].port.audioBuffer(64);
        std::fill_n(input, 64, 0.25f);
        process();
        const auto *output = clockRoot.ports[1].port.audioBuffer(64);
        std::printf("CLOCK REGRESSION: idle polls=20, activations=%u, elapsed=%lld ms, "
                    "output=%g, ready=%d\n",
                    repeatedActivations, elapsed.elapsed(), output[0],
                    int(clockRoot.clockReady.load()));
        require(repeatedActivations == 0, "idle subgraph never deactivates or reactivates JACK");
        require(std::all_of(output, output + 64, [](float value) { return value == 0.25f; }),
                "audio survives activation-time clock notifications");
        setClock(96000, 128);
        require(!clockRoot.clockReady.load(), "actual clock change suspends old schedule");
        process();
        require(output[0] == 0.0f, "unprepared clock change emits silence");
        require(clockRoot.synchronizeClock() && clockRoot.clockReady.load() &&
                    clockRoot.sampleRate == 96000 && clockRoot.maxFrames == 128,
                "actual rate and frame change prepares schedule once");
        const auto changedActivations = counter(5);
        require(clockRoot.synchronizeClock() && counter(5) == changedActivations,
                "new clock remains stable after activation notifications");
        std::fill_n(input, 128, 0.5f);
        process();
        require(std::all_of(output, output + 128, [](float value) { return value == 0.5f; }),
                "changed clock passes the entire new audio block");
        setClock(48000, 64);
    }
    const auto registeredBefore = counter(1);
    std::array<QString, 3> paths;
    std::array<HMODULE, 3> libraries{};
    std::array<uint32_t (*)(uint32_t), 3> counts{};
    const char *kinds[]{"jack", "virt", "dual"};
    const auto fixtureDir = QDir(args[1]).filePath("nodes/prepublic-s3-fixtures");
    require(!QDir(fixtureDir).exists(), "fixture directory must be test-owned and new");
    require(QDir().mkpath(fixtureDir), "temporary discoverable fixtures");
    for (size_t index = 0; index < 3; ++index)
    {
        const auto file = QString("wjn_v4_fixture_%1.dll").arg(kinds[index]);
        paths[index] = QDir(fixtureDir).filePath(file);
        require(QFile::copy(QDir(args[1]).filePath("tests/node-v4/" + file), paths[index]),
                "copy controlled ABI fixture");
        libraries[index] = LoadLibraryW(reinterpret_cast<LPCWSTR>(paths[index].utf16()));
        require(libraries[index], "hold fixture lease for counters");
        counts[index] = reinterpret_cast<uint32_t (*)(uint32_t)>(
            GetProcAddress(libraries[index], "wjn_fixture_counter"));
    }
    require(!LoadedModuleNode::create(paths[0], 0, {}, WJN_NODE_MODE_VIRT),
            "direct child rejects JACK-only before init");
    require(!LoadedModuleNode::create(paths[1], 0, {}, WJN_NODE_MODE_JACK),
            "direct main rejects VIRT-only before init");
    require(counts[0](0) == 0 && counts[1](0) == 0,
            "rejected creation has no factory or JACK side effects");
    graphChecks(paths[1]);
    PatchbayNode node;
    require(node.init() && node.createWindow(nullptr),
            "actual host opens with offline boundary DLL");
    settle();
    auto *window = reinterpret_cast<QWidget *>(
        QWidget::find(reinterpret_cast<WId>(node.createWindow(nullptr))));
    require(window, "host native widget found");
    window->resize(1200, 780);
    window->move(20, 20);
    auto *tree = window->findChild<QTreeWidget *>("patchbay.nodeTree");
    auto *tabs = window->findChild<QTabWidget *>();
    auto *main = context(window, "main");
    require(tree && tabs && main, "actual host controls");
    require(findModule(tree, paths[0]) && !findModule(tree, paths[1]) && findModule(tree, paths[2]),
            "main library matrix");
    applyNodeLibraryFilter(tree, "fixture");
    require(!findModule(tree, paths[0])->isHidden() && !findModule(tree, paths[2])->isHidden(),
            "main search matrix");
    applyNodeLibraryFilter(tree, {});
    menuMatrix(main, paths, false);
    capture(window, QDir(args[3]).filePath("main-library.png"));
    addModule(tree, paths[2]);
    require(counts[2](1) == 1 && counts[2](2) == 1 && counts[2](3) == 0,
            "main dual uses only JACK init path");
    const auto before = counter(0);
    const auto rootId = main->addSubGraph("根图 / 루트", {});
    const auto rootContext = "main/" + rootId;
    auto *root = context(window, rootContext);
    require(root && counter(0) == before + 1, "one root registers one boundary client");
    tabs->setCurrentWidget(root);
    settle();
    const auto idleActivations = counter(5);
    QTest::qWait(500);
    std::printf("REAL WINDOW IDLE: 500 ms, repeated activations=%u\n",
                counter(5) - idleActivations);
    require(counter(5) == idleActivations,
            "visible patchbay refresh does not stop or restart the root audio client");
    require(!findModule(tree, paths[0]) && findModule(tree, paths[1]) && findModule(tree, paths[2]),
            "child library matrix");
    applyNodeLibraryFilter(tree, "fixture");
    require(!findModule(tree, paths[1])->isHidden() && !findModule(tree, paths[2])->isHidden(),
            "child search matrix");
    applyNodeLibraryFilter(tree, {});
    menuMatrix(root, paths, true);
    addModule(tree, paths[2]);
    const auto firstClient = moduleClient(root);
    const auto nestedId = root->addSubGraph("第二层", {});
    auto *nested = context(window, rootContext + "/" + nestedId);
    require(nested && counter(0) == before + 1, "nested graph registers no client");
    tabs->setCurrentWidget(nested);
    settle();
    addModule(tree, paths[2]);
    const auto secondClient = moduleClient(nested);
    const auto deepId = nested->addSubGraph("第三层", {});
    auto *deep = context(window, nested->sceneContext().id + "/" + deepId);
    require(deep && counter(0) == before + 1, "third level registers no client");
    tabs->setCurrentWidget(deep);
    settle();
    addModule(tree, paths[2]);
    const auto thirdClient = moduleClient(deep);
    for (auto *canvas : {root, nested, deep})
        require(canvas->addInterfaceNode("midi_out", {}), "MIDI output interface");
    require(counter(1) == registeredBefore + 6,
            "only six immediate root interfaces register native ports");
    const auto connectChain = [&](const char *inType, const char *outType, uint32_t inputId,
                                  uint32_t outputId) {
        const auto rin = interface(root, inType), rout = interface(root, outType);
        const auto nin = interface(nested, inType), nout = interface(nested, outType);
        const auto din = interface(deep, inType), dout = interface(deep, outType);
        wire(root, rin.portId, firstClient + ":port-" + std::to_string(inputId));
        wire(root, firstClient + ":port-" + std::to_string(outputId), nin.portId);
        wire(root, nout.portId, rout.portId);
        wire(nested, nin.portId, secondClient + ":port-" + std::to_string(inputId));
        wire(nested, secondClient + ":port-" + std::to_string(outputId), din.portId);
        wire(nested, dout.portId, nout.portId);
        wire(deep, din.portId, thirdClient + ":port-" + std::to_string(inputId));
        wire(deep, thirdClient + ":port-" + std::to_string(outputId), dout.portId);
    };
    connectChain("audio_in", "audio_out", 1, 2);
    connectChain("midi_in", "midi_out", 3, 4);
    const auto ain = interface(root, "audio_in"), aout = interface(root, "audio_out");
    auto *inputPort = api.portByName(nullptr, ain.externalPortId.c_str());
    auto *outputPort = api.portByName(nullptr, aout.externalPortId.c_str());
    require(inputPort && outputPort, "root native boundary ports");
    auto *input = static_cast<float *>(api.portGetBuffer(inputPort, 64));
    std::fill_n(input, 64, 0.25f);
    const auto min = interface(root, "midi_in"), mout = interface(root, "midi_out");
    auto *midiInput = api.portGetBuffer(api.portByName(nullptr, min.externalPortId.c_str()), 64);
    const uint8_t note[]{0x90, 60, 100};
    require(api.midiEventWrite(midiInput, 17, note, 3) == 0, "seed native MIDI boundary");
    const auto calls = counts[2](3);
    process();
    auto *output = static_cast<float *>(api.portGetBuffer(outputPort, 64));
    require(std::all_of(output, output + 64, [](float value) { return value == 0.25f; }),
            "three-level audio roundtrip executes actual VIRT modules");
    auto *midiOutput = api.portGetBuffer(api.portByName(nullptr, mout.externalPortId.c_str()), 64);
    jack_midi_event_t event{};
    require(api.midiEventCount(midiOutput) == 1 && api.midiEventGet(&event, midiOutput, 0) == 0 &&
                event.time == 17 && event.size == 3,
            "three-level MIDI roundtrip retains frame offset");
    require(counts[2](3) == calls + 3 && counts[2](1) == 1 && counts[2](2) == 1,
            "three child processors use only VIRT; JACK path unchanged");
    for (const auto &language : {"zh-CN", "en-US", "ja-JP", "ko-KR"})
    {
        require(host.manager().setLanguage(language, &error), "four-language hot switch");
        settle();
        auto *item = findModule(tree, paths[2]);
        require(item, "dual remains available during hot switch");
        const auto tooltip = item->toolTip(0);
        require(tooltip.contains(patchText("capability.jack", "Main graph JACK node")) &&
                    tooltip.contains(patchText("capability.yes", "Yes")) &&
                    tooltip.contains(patchText("capability.no", "No")),
                "tooltip labels and values update through i18n");
        QTextLayout layout(tooltip, app.font());
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
            for (auto glyph : run.glyphIndexes())
                require(glyph != 0, "system CJK fallback without tofu");
        tree->expandAll();
        tree->scrollToItem(item);
        settle();
        const auto point = tree->visualItemRect(item).center();
        window->raise();
        window->activateWindow();
        QCursor::setPos(tree->viewport()->mapToGlobal(point));
        QTest::mouseMove(tree->viewport(), point);
        settle();
        QHelpEvent hover(QEvent::ToolTip, point, tree->viewport()->mapToGlobal(point));
        QApplication::sendEvent(tree->viewport(), &hover);
        QTest::qWait(60);
        QWidget *tip = nullptr;
        for (auto *top : app.topLevelWidgets())
            if (top->windowType() == Qt::ToolTip && top->isVisible())
                tip = top;
        require(tip, "real tooltip near mouse");
        capture(tip, QDir(args[3]).filePath(QString("tooltip-%1.png").arg(language)));
        QToolTip::hideText();
    }
    deep->selectNode(thirdClient);
    settle();
    auto *gui = window->findChild<QPushButton *>("showModuleGuiButton");
    require(gui && gui->isVisible() && !gui->isEnabled(), "GUI-no inspector button disabled");
    QPoint menuPoint;
    for (auto *item : deep->scene()->items())
    {
        if (item->data(0).toString().toStdString() != thirdClient)
            continue;
        deep->ensureVisible(item);
        menuPoint = deep->mapFromScene(item->sceneBoundingRect().topLeft() + QPointF(30, 10));
        break;
    }
    bool disabledMenuChecked = false;
    QTimer::singleShot(60, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        require(menu, "actual node context menu visible");
        QAction *open = nullptr;
        for (auto *action : menu->actions())
            if (action->data().toString() == QStringLiteral("action.showGui"))
                open = action;
        require(open && !open->isEnabled(), "GUI-no native menu action disabled");
        disabledMenuChecked = true;
        menu->close();
    });
    QContextMenuEvent disabledMenu(QContextMenuEvent::Mouse, menuPoint,
                                   deep->viewport()->mapToGlobal(menuPoint));
    QApplication::sendEvent(deep->viewport(), &disabledMenu);
    require(disabledMenuChecked, "disabled GUI menu inspected");
    capture(window, QDir(args[3]).filePath("nested-gui-disabled.png"));
    auto retainedRoutes = deep->savedLogicalConnections();
    retainedRoutes.push_back({"rejected-old:port-2", interface(deep, "audio_out").portId});
    require(deep->restoreLogicalConnections(retainedRoutes) &&
                deep->savedLogicalConnections().size() == retainedRoutes.size(),
            "unloadable legacy node route remains pending and serializable");
    const auto savedFile = settings.filePath("graph.wjn");
    std::vector<PatchbayProjectModule> records{
        {paths[2], 0, thirdClient, "{\"gain\":2}", deep->sceneContext().id},
        {paths[0], 0, "rejected-old", "{\"keep\":true}", deep->sceneContext().id},
        {paths[2], 0, firstClient, "{\"gain\":3}", root->sceneContext().id},
        {paths[2], 0, secondClient, "{\"gain\":4}", nested->sceneContext().id},
        {QDir(args[1]).filePath("tests/node-v4/wjn_v4_fixture_old.dll"), 0, "old-v3",
         "{\"keepVersion3\":true}", "main"},
        {QDir(args[1]).filePath("nodes/missing-s6.dll"), 0, "missing-module",
         "{\"keepMissing\":true}", deep->sceneContext().id}};
    std::vector<PatchbaySubGraphProject> graphs;
    for (auto *canvas : {main, root, nested, deep})
        graphs.push_back({canvas->sceneContext(), canvas->subGraphNodes(), canvas->interfaceNodes(),
                          canvas->savedLogicalConnections()});
    require(main->saveProject(savedFile, records, graphs), "versioned project save");
    std::vector<PatchbayProjectModule> restored;
    std::vector<PatchbaySubGraphProject> restoredGraphs;
    require(main->loadProject(savedFile, restored, restoredGraphs) && restored.size() == 6 &&
                restored[1].settingsJson == records[1].settingsJson &&
                restoredGraphs.back().connections.size() == deep->savedLogicalConnections().size(),
            "project records retain rejected module state and nested routes");
    const auto rootsBefore = counter(0);
    main->addSubGraph("Second root", {});
    require(counter(0) == rootsBefore + 1, "two roots use separate boundary clients");
    const auto closingCalls = counts[2](3);
    node.destroyWindow(nullptr);
    process();
    require(counts[2](3) == closingCalls + 3, "closing editor does not stop child DSP");
    require(counter(3) >= 2, "closing editor retains active root execution");
    require(node.createWindow(nullptr), "reopen editor using retained execution tree");
    settle();
    window = QWidget::find(reinterpret_cast<WId>(node.createWindow(nullptr)));
    root = context(window, rootContext);
    main = context(window, "main");
    require(root && main && context(window, rootContext + "/" + nestedId),
            "reopened root editor present");
    root->selectNode(nestedId);
    QTest::keyClick(root, Qt::Key_Delete);
    settle();
    const auto deletedCalls = counts[2](3);
    process();
    require(counts[2](3) == deletedCalls + 1,
            "nested deletion detaches only its processor subtree");
    main->selectNode(rootId);
    QTest::keyClick(main, Qt::Key_Delete);
    settle();
    require(counter(3) == 2,
            "root deletion releases only that root boundary, leaving observer and second root");
    auto jack = WinJACKNexus::Node::createNodeForMode(libraries[2], WJN_NODE_MODE_JACK);
    require(jack && jack->init(), "offline main DSP instance");
    WinJACKNexus::Node::ModuleContract contract;
    require(WinJACKNexus::Node::inspectNodeModule(libraries[2], contract) ==
                WinJACKNexus::Node::ModuleError::none,
            "dual processing contract");
    const auto jackProcess = reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(
        GetProcAddress(libraries[2], "wjn_fixture_process_jack"));
    const auto virtualBefore = counts[2](3);
    jackProcess(jack);
    WjnNodeProcessV1 block{};
    block.structSize = sizeof(block);
    block.frames = 64;
    contract.processing->process(jack, &block);
    require(counts[2](4) == 1 && counts[2](3) == virtualBefore,
            "main JACK DSP enters only JACK path, virtual path remains zero");
    jack->shutdown();
    reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(
        GetProcAddress(libraries[2], "wjn_destroy_node"))(jack);
    node.shutdown();
    require(counter(3) == 0 && counter(4) == 0, "shutdown cleans only fixture clients and ports");
    QFile saved(savedFile);
    require(saved.open(QIODevice::ReadOnly), "read original restoration project");
    const auto originalBytes = saved.readAll();
    saved.close();
    patchbaySettings().setValue(QStringLiteral("lastProjectPath"), savedFile);
    const auto rejectedCreations = counts[0](0);
    const auto jackInitializations = counts[2](1);
    PatchbayNode reloaded;
    require(reloaded.init() && reloaded.createWindow(nullptr),
            "actual host restores saved project");
    settle();
    window = QWidget::find(reinterpret_cast<WId>(reloaded.createWindow(nullptr)));
    deep = context(window, rootContext + "/" + nestedId + "/" + deepId);
    require(deep && counts[0](0) == rejectedCreations && counts[2](1) == jackInitializations,
            "project restoration rejects child JACK-only before factory and uses VIRT only");
    require(deep->savedLogicalConnections().size() == restoredGraphs.back().connections.size() &&
                deep->logicalConnections().size() + 1 == deep->savedLogicalConnections().size(),
            "actual host restores deepest logical routes");
    const auto restoredCalls = counts[2](3);
    process();
    require(counts[2](3) == restoredCalls + 3, "restored three-level DSP executes");
    require(saved.open(QIODevice::ReadOnly) && saved.readAll() == originalBytes,
            "restoration never silently overwrites source project");
    saved.close();
    QAction *saveProject = nullptr;
    for (auto *action : window->findChildren<QAction *>())
    {
        if (action->data().toString() == QStringLiteral("action.saveProject"))
            saveProject = action;
    }
    require(saveProject, "actual save action found");
    saveProject->trigger();
    main = context(window, "main");
    require(main->loadProject(savedFile, restored, restoredGraphs) && restored.size() == 6,
            "host save retains unloadable module records");
    for (const auto &record : records)
    {
        const auto found = std::find_if(restored.begin(), restored.end(), [&](const auto &value) {
            return value.clientId == record.clientId;
        });
        require(found != restored.end() && found->settingsJson == record.settingsJson,
                "host save preserves independent parameter state");
    }
    reloaded.shutdown();
    patchbaySettings().remove(QStringLiteral("lastProjectPath"));
    require(counter(3) == 0 && counter(4) == 0, "restored host cleans fixture boundaries");
    for (size_t index = 0; index < 3; ++index)
    {
        FreeLibrary(libraries[index]);
        require(QFile::remove(paths[index]), "remove test-owned DLL");
    }
    require(QDir().rmdir(fixtureDir), "remove empty test-only discovery directory");
    FreeLibrary(boundary);
    api.unload();
    require(shows.shows <= 100, "bounded real-window batch");
    std::printf("S3 PASS checks=%u realWindows=%u platform=windows DPR=1\n", checks, shows.shows);
    return 0;
}
