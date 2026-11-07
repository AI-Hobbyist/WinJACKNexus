// SPDX-License-Identifier: AGPL-3.0-only
#include "../../src/patchbay/mod/src/Library.cpp"
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QJsonArray>
#include <QTest>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    if (app.platformName() != "windows")
        return 2;
    const QString fixture = QString::fromUtf8(WJN_BC_BOUNDARY);
    if (!WinJACKNexus::Audio::JackApi::instance().load(reinterpret_cast<const wchar_t *>(fixture.utf16())))
        return 2;
    WinJACKNexus::Audio::JackClient observer;
    if (!observer.open({"BC-Host-Test", false, false, nullptr}))
        return 2;
    std::vector<std::unique_ptr<LoadedModuleNode>> nodes;
    auto node =
        LoadedModuleNode::create(app.applicationDirPath() + "/nodes/lan_remote_audio/mod_broadcast.dll", 0);
    if (!node)
    {
        std::puts("FAIL default node initialization");
        return 1;
    }
    const auto id = node->clientId();
    nodes.push_back(std::move(node));
    auto snapshot = Snapshot::capture(observer);
    const auto settings =
        QJsonDocument::fromJson(QByteArray::fromStdString(nodes[0]->settingsJson())).object();
    const auto streams = settings.value("streams").toArray();
    if (streams.size() != 1 || snapshot.ports.size() != 4)
    {
        std::puts("FAIL new broadcast node must reserve one stereo stream and four JACK ports");
        return 1;
    }
    preserveLoadedModuleClients({}, snapshot, nodes);
    const auto found = std::find_if(snapshot.clients.begin(), snapshot.clients.end(),
                                    [&](const auto &client) { return client.id == id; });
    if (found == snapshot.clients.end())
    {
        std::puts("FAIL newly dropped broadcast node missing from canvas snapshot");
        return 1;
    }
    std::puts("PASS default stereo broadcast node present in canvas snapshot with four ports");
    auto &api = WinJACKNexus::Audio::JackApi::instance();
    auto *original = api.portByName(observer.nativeHandle(), (id + ":Stream 1-in-L").c_str());
    WinJACKNexus::Common::Broadcast::State state;
    if (!WinJACKNexus::Common::Broadcast::State::fromJson(settings, state) || !original)
        return 1;
    const auto parent = state.addCategory(QStringLiteral("节目"));
    const auto child = state.addCategory(QStringLiteral("新闻"), parent);
    state.streams[0].category = child;
    state.streams[0].name = QStringLiteral("主输出");
    if (!nodes[0]->restoreSettingsJson(
            QJsonDocument(state.toJson()).toJson(QJsonDocument::Compact).toStdString()))
        return 1;
    const auto renamed = (QString::fromStdString(id) + QStringLiteral(":节目-新闻-主输出-in-L")).toUtf8();
    if (api.portByName(observer.nativeHandle(), renamed.constData()) != original)
        return 1;
    std::puts("PASS category/name edit renames the existing JACK port without replacing its handle");
    snapshot = Snapshot::capture(observer);
    PatchbayCanvas canvas;
    canvas.resize(640, 480);
    canvas.setSnapshot(snapshot);
    canvas.show();
    if (!QTest::qWaitForWindowExposed(&canvas))
        return 1;
    QTest::qWait(180);
    const auto items = canvas.scene()->items();
    const bool visibleNode = std::any_of(items.cbegin(), items.cend(), [&](const auto *item)
                                         { return item->data(0).toString() == QString::fromStdString(id); });
    if (!visibleNode || !nodes[0]->showGui(nullptr))
        return 1;
    QTest::qWait(180);
    canvas.close();
    std::puts("PASS default broadcast stream, real canvas item and standalone GUI");
    return 0;
}
