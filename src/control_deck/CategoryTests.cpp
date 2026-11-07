// SPDX-License-Identifier: AGPL-3.0-only
#include "../patchbay/mod/src/Library.cpp"
#include <QApplication>
#include <QFontDatabase>
#include <QScreen>
#include <QTest>
#include <cstdio>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.platformName() != "windows")
        return 2;
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
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
    QTreeWidget tree;
    populateNodeLibrary(&tree);
    tree.resize(440, 720);
    tree.show();
    if (!QTest::qWaitForWindowExposed(&tree))
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
    if (!tree.screen()
             ->grabWindow(tree.winId())
             .save(app.applicationDirPath() + "/../../control-deck-categories.png"))
        return 1;
    std::puts("PASS 38 categorized modules, five analysis subcategories, legacy alias and old-overlay scan "
              "deduplication");
    return 0;
}
