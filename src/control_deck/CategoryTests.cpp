// SPDX-License-Identifier: AGPL-3.0-only
#include "../patchbay/mod/src/Library.cpp"
#include <QAccessible>
#include <QApplication>
#include <QFontDatabase>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <chrono>
#include <cstdio>
#include <thread>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.platformName() != "windows")
        return 2;
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    WinJACKNexus::UI::ThemeCore theme;
    WinJACKNexus::UI::ThemeManager::installThemeCore(&theme);
    WinJACKNexus::UI::ThemeManager manager;
    manager.applyToApp(&app);
    WinJACKNexus::UI::HostI18n language({temporary.filePath("language.ini"), {"zh-CN"}, false});
    QString languageError;
    if (!language.initialize(app.applicationDirPath(), &languageError) ||
        !language.manager().setLanguage("zh-CN", &languageError))
        return 2;
    const auto modules = scanNodeModules();
    unsigned count = 0;
    for (const auto &module : modules)
        if (module.category.startsWith("Control Deck / "))
        {
            ++count;
            const auto file = QFileInfo(module.modulePath).fileName();
            const auto legacy = app.applicationDirPath() + "/nodes/control_deck/old/" + file;
            if (controlDeckModulePath(legacy) != module.modulePath)
                return 1;
        }
    if (count != 38)
        return 1;
    WinJACKNexus::UI::Controls::DragTreeWidget standalone;
    std::unique_ptr<PatchbayNode> host;
    QWidget *window = &standalone;
    QTreeWidget *treePointer = &standalone;
    if (app.arguments().contains("--host"))
    {
        host = std::make_unique<PatchbayNode>(true);
        if (!host->init())
            return 1;
        const auto handle = host->createWindow(nullptr);
        window = QWidget::find(reinterpret_cast<WId>(handle));
        treePointer = window ? window->findChild<QTreeWidget *>("patchbay.nodeTree") : nullptr;
        if (!treePointer)
            return 1;
    }
    else
    {
        standalone.setMimeDataFactory(moduleNodeMimeData);
        standalone.setHeaderHidden(true);
        standalone.setIndentation(18);
        standalone.setAnimated(true);
        populateNodeLibrary(&standalone);
        standalone.resize(220, 720);
    }
    auto &tree = *treePointer;
    window->show();
    if (!QTest::qWaitForWindowExposed(window))
        return 1;
    QTest::qWait(150);
    auto roots = tree.findItems("Control Deck", Qt::MatchExactly);
    if (roots.size() != 1 || roots[0]->childCount() != 5)
        return 1;
    bool analysis = false;
    for (int index = 0; index < roots[0]->childCount(); ++index)
        if (roots[0]->child(index)->text(0) == "Analysis")
            analysis = roots[0]->child(index)->childCount() == 5;
    if (!analysis)
        return 1;
    std::jthread watchdog(
        [](std::stop_token stop)
        {
            for (unsigned tick = 0; tick < 100 && !stop.stop_requested(); ++tick)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!stop.stop_requested())
            {
                std::fputs("FAIL category collapse blocked the real Windows event loop for 10 seconds\n",
                           stderr);
                std::fflush(stderr);
                std::_Exit(3);
            }
        });
    unsigned heartbeats = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &tree, [&] { ++heartbeats; });
    heartbeat.start(10);
    QAccessible::setActive(true);
    auto *accessible = QAccessible::queryAccessibleInterface(&tree);
    const auto checkAccessibleRows = [&]
    {
        if (!QAccessible::isActive() || !accessible)
            return false;
        // Prime every cached row and verify the inverse used by Windows UIA
        // NextSibling navigation. A stale inverse can repeatedly return itself.
        for (int index = 0; index < accessible->childCount(); ++index)
        {
            auto *child = accessible->child(index);
            if (!child)
                return false;
            if (accessible->indexOfChild(child) != index)
            {
                std::printf("FAIL accessible row %d maps back to %d (%s)\n", index,
                            accessible->indexOfChild(child),
                            child->text(QAccessible::Name).toUtf8().constData());
                return false;
            }
        }
        return true;
    };
    if (!checkAccessibleRows())
        return 4;
    tree.scrollToItem(roots[0]);
    std::puts("COLLAPSE Control Deck (actual shared tree, theme and animation)");
    std::fflush(stdout);
    const auto rootBounds = tree.visualItemRect(roots[0]);
    QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(rootBounds.left() - tree.indentation() / 2, rootBounds.center().y()));
    QTest::qWait(400);
    if (roots[0]->isExpanded() || heartbeats < 5)
        return 1;
    if (!checkAccessibleRows())
        return 4;
    std::printf("PASS collapse responsive: %u GUI heartbeats\n", heartbeats);
    const auto beforeExpand = heartbeats;
    std::puts("EXPAND Control Deck after collapse");
    std::fflush(stdout);
    QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(rootBounds.left() - tree.indentation() / 2, rootBounds.center().y()));
    QTest::qWait(400);
    if (!roots[0]->isExpanded() || heartbeats < beforeExpand + 5)
        return 1;
    if (!checkAccessibleRows())
        return 4;
    std::printf("PASS re-expand responsive: %u GUI heartbeats\n", heartbeats - beforeExpand);
    QTreeWidgetItem *analysisCategory = nullptr;
    for (int index = 0; index < roots[0]->childCount(); ++index)
        if (libraryItemIdentity(roots[0]->child(index)) == "Analysis")
            analysisCategory = roots[0]->child(index);
    auto *plotCategory = analysisCategory ? analysisCategory->child(0) : nullptr;
    for (auto *category : {roots[0], analysisCategory, plotCategory})
        for (int delay : {0, 16, 100})
        {
            if (!category)
                return 1;
            std::printf("RAPID %s delay=%d\n", libraryItemIdentity(category).toUtf8().constData(), delay);
            std::fflush(stdout);
            const auto previousHeartbeats = heartbeats;
            category->setExpanded(false);
            if (!checkAccessibleRows())
                return 4;
            QTest::qWait(delay);
            category->setExpanded(true);
            if (!checkAccessibleRows())
                return 4;
            QTest::qWait(350);
            if (!category->isExpanded() || heartbeats < previousHeartbeats + 5)
                return 1;
        }
    if (!tree.screen()
             ->grabWindow(tree.winId())
             .save(app.applicationDirPath() + "/../../control-deck-categories.png"))
        return 1;
    std::puts(
        "PASS accessible row indexes remain consistent across collapse, re-expansion and rapid toggles");
    std::puts("PASS 38 categorized modules, five analysis subcategories, legacy alias and old-overlay scan "
              "deduplication");
    if (host)
        host->shutdown();
    return 0;
}
