/* Offline i18n contract and real DLL lifecycle tests. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/Common/I18nApi.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace WinJACKNexus::UI;
extern "C" int wjn_test_i18n_c_abi(void);
namespace
{
int checks = 0;
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
    ++checks;
}
void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "open fixture");
    require(file.write(bytes) == bytes.size(), "write fixture");
}
QByteArray bytes(const QJsonObject &value)
{
    return QJsonDocument(value).toJson();
}
QJsonObject descriptor(const QString &id = "fixture", const QString &domain = "fixture.ui")
{
    return {{"descriptorVersion", 1},        {"i18nApiVersion", 1},    {"moduleId", id},
            {"domains", QJsonArray{domain}}, {"locatePath", "locate"}, {"catalogVersion", 1}};
}
struct Package
{
    QTemporaryDir directory;
    QString id = "fixture", domain = "fixture.ui";
    QJsonObject english{
        {"title", "English"},
        {"greeting", "Hello {name}"},
        {"items", QJsonObject{{"one", "{count} item"}, {"other", "{count} items"}}}};
    QJsonObject manifest;
    explicit Package(QString identity = "fixture", QString d = "fixture.ui")
        : id(identity), domain(d)
    {
        require(directory.isValid(), "temporary package root");
        QJsonArray keys;
        for (auto it = english.begin(); it != english.end(); ++it)
            keys.append(it.key());
        manifest = {{"formatVersion", 1},
                    {"moduleId", id},
                    {"domains", QJsonArray{domain}},
                    {"defaultLanguage", "en-US"},
                    {"catalogVersion", 1},
                    {"languages", QJsonObject{{"en-US", "en-US.json"},
                                              {"zh-CN", "zh-CN.json"},
                                              {"ja-JP", "ja-JP.json"},
                                              {"ko-KR", "ko-KR.json"}}},
                    {"requiredKeys", QJsonObject{{domain, keys}}}};
        saveManifest();
        save("en-US", english);
        save("zh-CN", {{"title", QString::fromUtf8("中文")},
                       {"items", QJsonObject{{"other", QString::fromUtf8("{count} 项")}}}});
        save("ja-JP", {{"title", QString::fromUtf8("日本語")}});
        save("ko-KR", {{"title", QString::fromUtf8("한국어")}});
    }
    QString root() const
    {
        return directory.path();
    }
    QString path(const QString &name) const
    {
        return root() + "/locate/" + name;
    }
    QJsonObject document(const QString &language, const QJsonObject &entries) const
    {
        return {{"formatVersion", 1},
                {"catalogVersion", 1},
                {"moduleId", id},
                {"language", language},
                {"domains", QJsonObject{{domain, entries}}}};
    }
    void save(const QString &language, const QJsonObject &entries)
    {
        write(path(language + ".json"), bytes(document(language, entries)));
    }
    void saveManifest()
    {
        write(path("manifest.json"), bytes(manifest));
    }
};
void core()
{
    require(I18nManager::resolveAutomatic({"fr-FR", "ZH_hAnt_TW"}) == "zh-CN",
            "Chinese variants and preference order");
    for (const auto &value : QStringList{"zh-CN", "zh-Hans", "zh-Hant", "zh_TW"})
        require(I18nManager::resolveAutomatic({value}) == "zh-CN", "Chinese auto");
    require(I18nManager::resolveAutomatic({"en-GB"}) == "en-US", "English region");
    require(I18nManager::resolveAutomatic({"ja"}) == "ja-JP", "Japanese auto");
    require(I18nManager::resolveAutomatic({"ko_KR"}) == "ko-KR", "Korean auto");
    require(I18nManager::resolveAutomatic({"unknown", "fr"}) == "en-US",
            "unsupported auto fallback");
    require(I18nManager::resolveAutomatic({"en invalid", "ja"}) == "ja-JP",
            "invalid preference skipped");
    Package package;
    const auto settings = package.root() + "/settings.ini";
    I18nManager manager({settings, {"ja"}, false});
    require(manager.install(), "install manager");
    I18nManager other({package.root() + "/other.ini", {"ko"}, false});
    require(!other.install(), "single installed manager");
    require(manager.selectedLanguage() == "auto" && manager.effectiveLanguage() == "ja-JP",
            "initial auto restored before UI");
    QString error;
    auto lease = manager.registerModule(descriptor(), package.root(), {}, &error);
    require(bool(lease), qPrintable(error));
    QObject binding;
    QString rendered;
    int refreshes = 0;
    manager.bind(&binding, [&] {
        rendered = manager.text("fixture.ui", "title", "Fallback");
        ++refreshes;
    });
    require(rendered == QString::fromUtf8("日本語") && refreshes == 1,
            "binding applies current language immediately");
    require(manager.setLanguage("zh-CN", &error), "switch Chinese");
    require(rendered == QString::fromUtf8("中文") && refreshes == 2, "live binding updates");
    require(manager.text("fixture.ui", "greeting", "fallback", {{"name", "User <name>"}}) ==
                "Hello User <name>",
            "entry fallback same-module English and no implicit HTML escaping");
    require(manager.text("fixture.ui", "items", "fallback", {}, 2) == QString::fromUtf8("2 项"),
            "other-only plural");
    require(manager.setLanguage("en-US"), "switch English");
    require(manager.text("fixture.ui", "items", "fallback", {}, 1) == "1 item", "English one");
    require(manager.text("fixture.ui", "items", "fallback", {}, 0) == "0 items", "English other");
    require(manager.text("unknown", "key", "Fallback") == "Fallback", "call-site English fallback");
    require(manager.text("unknown", "key", {}) == "Unknown", "release missing key");
    I18nManager debug({package.root() + "/debug.ini", {"en"}, true});
    require(debug.text("unknown", "key", {}) == "unknown/key", "development missing key");
    QString formatted;
    require(I18nManager::format("{{{name}}} {name}", {{"name", "{injected}"}}, -1, &formatted) &&
                formatted == "{{injected}} {injected}",
            "escaping repeated params no recursive substitution");
    require(!I18nManager::format("{name}", {}, -1, &formatted), "missing argument rejected");
    require(!I18nManager::format("{bad-name}", {}, -1, &formatted), "bad placeholder rejected");
    require(!I18nManager::format("}", {}, -1, &formatted), "unmatched brace rejected");
    require(manager.setLanguage("ko-KR"), "switch Korean");
    auto second = manager.registerModule(descriptor(), package.root());
    require(bool(second), "duplicate registration creates lease");
    lease.reset();
    require(manager.text("fixture.ui", "title", "Fallback") == QString::fromUtf8("한국어"),
            "first lease does not remove shared module");
    auto conflicting = descriptor("other", "fixture.ui");
    require(!manager.registerModule(conflicting, package.root()), "duplicate domain rejected");
    require(!manager.registerModule(descriptor(), "relative/path"), "relative root rejected");
    Package secondPackage("second", "second.ui");
    auto independent =
        manager.registerModule(descriptor("second", "second.ui"), secondPackage.root());
    require(bool(independent), "register second independent module");
    const auto multiRevision = manager.revision();
    write(secondPackage.path("ja-JP.json"), "broken");
    require(!manager.setLanguage("ja-JP") && manager.revision() == multiRevision &&
                manager.text("fixture.ui", "title", "fallback") == QString::fromUtf8("한국어") &&
                manager.text("second.ui", "title", "fallback") == QString::fromUtf8("한국어"),
            "second module validation failure retains entire old snapshot");
    independent.reset();
    const auto revision = manager.revision();
    write(package.path("ja-JP.json"), "broken");
    require(!manager.setLanguage("ja-JP", &error) && manager.revision() == revision &&
                manager.selectedLanguage() == "ko-KR" && rendered == QString::fromUtf8("한국어"),
            "corrupt target leaves snapshot/UI/setting unchanged");
    require(QSettings(settings, QSettings::IniFormat).value("i18n/language").toString() == "ko-KR",
            "failed switch not persisted");
    package.save("ja-JP", {{"title", QString::fromUtf8("日本語")}});
    require(manager.setLanguage("auto") && manager.selectedLanguage() == "auto" &&
                manager.effectiveLanguage() == "ja-JP",
            "auto is separate persisted selection");
    require(QSettings(settings, QSettings::IniFormat).value("i18n/language").toString() == "auto",
            "auto persisted");
    bool wrongThreadSucceeded = true;
    QString readerText;
    std::thread reader([&] {
        readerText = manager.text("fixture.ui", "title", "fallback");
        wrongThreadSucceeded = manager.setLanguage("en-US");
    });
    reader.join();
    require(!wrongThreadSucceeded && readerText == QString::fromUtf8("日本語"),
            "read-only snapshot thread-safe mutations GUI-only");
    second.reset();
    require(manager.text("fixture.ui", "title", "Fallback") == "Fallback",
            "last lease removes domain");
    for (const auto &selection : QStringList{"en-US", "zh-CN", "ja-JP", "ko-KR", "auto"})
    {
        I18nManager writer({settings, {"ko"}, false});
        require(writer.setLanguage(selection), "persist language choice");
        I18nManager restarted({settings, {"ko"}, false});
        require(restarted.selectedLanguage() == selection &&
                    restarted.effectiveLanguage() ==
                        (selection == "auto" ? QString("ko-KR") : selection),
                "restart restores effective and selected language");
        auto loaded = restarted.registerModule(descriptor(), package.root());
        QObject window;
        QString first;
        restarted.bind(&window, [&] { first = restarted.text("fixture.ui", "title", "fallback"); });
        const auto before = first;
        require(restarted.setLanguage(selection) && first == before,
                "cold binding equals switching same language");
    }
    QSettings invalid(settings, QSettings::IniFormat);
    invalid.setValue("i18n/language", "removed-language");
    invalid.sync();
    I18nManager recovered({settings, {"zh-Hant"}, false});
    require(recovered.selectedLanguage() == "auto" && recovered.effectiveLanguage() == "zh-CN",
            "invalid saved preference returns to auto");
    // Manager deletion invalidates leases without use-after-free.
    auto *shortLived = new I18nManager({package.root() + "/short.ini", {"en"}, false});
    auto lateLease = shortLived->registerModule(descriptor(), package.root());
    delete shortLived;
    require(!lateLease, "lease invalid after manager deletion");
    lateLease.reset();
}
void validation()
{
    Package package;
    auto english = bytes(package.document("en-US", package.english));
    I18nManager manager({package.root() + "/settings.ini", {"en"}, false});
    QString error;
    auto reject = [&](const QByteArray &data, const char *message) {
        write(package.path("en-US.json"), data);
        auto lease = manager.registerModule(descriptor(), package.root(), {}, &error);
        require(!lease, message);
    };
    reject("{\"formatVersion\":1,\"formatVersion\":1}", "duplicate JSON keys rejected");
    auto duplicate = english;
    duplicate.replace("\"title\":", "\"t\\u0069tle\": \"shadow\", \"title\":");
    reject(duplicate, "escaped duplicate keys rejected");
    reject(QByteArray("\xff") + english, "invalid UTF8 rejected");
    reject(QByteArray(2 * 1024 * 1024 + 1, ' '), "package size limit");
    auto empty = package.english;
    empty["title"] = "";
    reject(bytes(package.document("en-US", empty)), "empty translation rejected");
    auto missing = package.english;
    missing.remove("title");
    reject(bytes(package.document("en-US", missing)), "incomplete English rejected");
    auto extra = package.english;
    extra["unknown"] = "Unknown";
    reject(bytes(package.document("en-US", extra)), "unknown key rejected");
    auto malformed = package.english;
    malformed["greeting"] = "Hello {name";
    reject(bytes(package.document("en-US", malformed)), "malformed placeholder rejected");
    auto plurals = package.english;
    plurals["items"] = QJsonObject{{"one", "one"}, {"other", "{count} items"}};
    reject(bytes(package.document("en-US", plurals)),
           "plural placeholder mismatch including empty set");
    auto wrongVersion = package.document("en-US", package.english);
    wrongVersion["catalogVersion"] = 2;
    reject(bytes(wrongVersion), "catalog version mismatch");
    reject("damaged", "damaged English with no embedded copy rejected");
    auto embedded = manager.registerModule(descriptor(), package.root(), english, &error);
    require(bool(embedded) && manager.text("fixture.ui", "title", "fallback") == "English",
            "trusted complete embedded English fallback");
    embedded.reset();
    package.save("en-US", package.english);
    auto lease = manager.registerModule(descriptor(), package.root());
    require(bool(lease), "restore valid baseline");
    package.save("zh-CN", {{"greeting", "wrong {path}"}});
    require(!manager.setLanguage("zh-CN"), "cross-language placeholder mismatch rejected");
    package.save("zh-CN", {{"items", QJsonObject{{"one", "{count}"}, {"other", "{count}"}}}});
    require(!manager.setLanguage("zh-CN"), "non-English one plural rejected");
    QFile::remove(package.path("zh-CN.json"));
    require(manager.setLanguage("zh-CN") &&
                manager.text("fixture.ui", "title", "fallback") == "English",
            "missing target file falls back English");
    lease.reset();
    auto mappings = package.manifest["languages"].toObject();
    mappings["en-US"] = "../escape.json";
    package.manifest["languages"] = mappings;
    package.saveManifest();
    require(!manager.registerModule(descriptor(), package.root()),
            "path traversal mapping rejected");
}
using Query = int32_t (*)(const char *, const char *, const char *, const char *, int64_t, char *,
                          uint32_t, uint32_t *);
using Events = uint32_t (*)(uint32_t);
void modules(const QString &node, const QString &fx, const QString &old)
{
    Package settings;
    I18nManager manager({settings.root() + "/settings.ini", {"ja"}, false});
    QString error;
    require(!manager.registerLibrary(old, I18nManager::Extension::node, 0, &error),
            "old module no extension continues original API");
    require(!manager.registerLibrary(node, I18nManager::Extension::node, 99, &error),
            "invalid descriptor index rejected");
    auto nodeLease = manager.registerLibrary(node, I18nManager::Extension::node, 0, &error);
    require(bool(nodeLease), qPrintable(error));
    QLibrary nodeProbe(node);
    require(nodeProbe.load(), "load node probe");
    auto query = reinterpret_cast<Query>(nodeProbe.resolve("wjn_example_i18n_query"));
    auto events = reinterpret_cast<Events>(nodeProbe.resolve("wjn_example_i18n_events"));
    require(query && events, "resolve real C probes");
    require(events(0) == 1 && events(1) == 1, "attach + first current-language notification");
    I18nManager foreign({settings.root() + "/foreign.ini", {"en"}, false});
    require(!foreign.registerLibrary(node, I18nManager::Extension::node) && events(0) == 1 &&
                events(2) == 0,
            "second host cannot detach or overwrite live DLL services");
    uint32_t size = 0;
    require(query("example.node", "name", "Example Node", nullptr, -1, nullptr, 0, &size) ==
                    WJN_I18N_OK &&
                size > 1,
            "C size query includes NUL");
    QByteArray buffer(size, 'x');
    require(query("example.node", "name", "Example Node", nullptr, -1, buffer.data(), size - 1,
                  &size) == WJN_I18N_BUFFER_TOO_SMALL &&
                buffer[0] == 'x',
            "short buffer status no partial write");
    require(query("example.node", "name", "Example Node", nullptr, -1, buffer.data(), size,
                  &size) == WJN_I18N_OK &&
                QString::fromUtf8(buffer.constData()) == QString::fromUtf8("サンプルノード"),
            "C UTF8 output actual language");
    require(query("host", "name", "Forbidden", nullptr, -1, nullptr, 0, &size) ==
                WJN_I18N_FORBIDDEN_DOMAIN,
            "C cannot access another module domain");
    require(query("example.node", "greeting", "{count} nodes for {name}", "{\"name\":\"User\"}", 2,
                  nullptr, 0, &size) == WJN_I18N_OK,
            "C argument JSON English plural fallback");
    buffer = QByteArray(size, '\0');
    require(query("example.node", "greeting", "{count} nodes for {name}", "{\"name\":\"User\"}", 2,
                  buffer.data(), size, &size) == WJN_I18N_OK &&
                buffer.constData() == QByteArray("2 nodes for User"),
            "C named argument output");
    require(query("example.node", "greeting", "{name}", "{\"name\":{},\"other\":1}", -1, nullptr, 0,
                  &size) == WJN_I18N_INVALID_ARGUMENT,
            "C non-scalar args rejected");
    require(query("example.node", "name", "Name", "{\"a\":1,\"a\":2}", -1, nullptr, 0, &size) ==
                WJN_I18N_INVALID_ARGUMENT,
            "C duplicate args rejected");
    require(query("example.node", "name", "Name", nullptr, -2, nullptr, 0, &size) ==
                WJN_I18N_INVALID_ARGUMENT,
            "C invalid count rejected");
    int32_t wrong = 0;
    std::thread caller([&] {
        uint32_t n;
        wrong = query("example.node", "name", "Name", nullptr, -1, nullptr, 0, &n);
    });
    caller.join();
    require(wrong == WJN_I18N_WRONG_THREAD, "C GUI-only service");
    auto nodeSecond = manager.registerLibrary(node, I18nManager::Extension::node);
    require(bool(nodeSecond) && events(0) == 1, "DLL attaches once for multiple instances");
    nodeLease.reset();
    require(events(2) == 0, "first release keeps services");
    require(manager.setLanguage("ko-KR") && events(1) == 2, "language notification once per DLL");
    nodeSecond.reset();
    require(events(2) == 1, "last release detaches exactly once");
    require(query("example.node", "name", "Name", nullptr, -1, nullptr, 0, &size) ==
                WJN_I18N_INVALID_ARGUMENT,
            "module clears borrowed services on detach");
    require(nodeProbe.unload(), "real node DLL unload");
    require(GetModuleHandleW(reinterpret_cast<LPCWSTR>(node.utf16())) == nullptr,
            "node DLL no outstanding loader reference");
    require(manager.setLanguage("ja-JP"), "set language before FX late load");
    auto fxLease = manager.registerLibrary(fx, I18nManager::Extension::nativeFx, 0, &error);
    require(bool(fxLease), qPrintable(error));
    auto dual = manager.registerLibrary(fx, I18nManager::Extension::node);
    require(bool(dual), "same DLL dual-host lease");
    QLibrary fxProbe(fx);
    require(fxProbe.load(), "FX probe load");
    auto fxEvents = reinterpret_cast<Events>(fxProbe.resolve("wjn_example_i18n_events"));
    require(fxEvents && fxEvents(0) == 1 && fxEvents(1) == 1, "dual-host attaches/notifies once");
    using Create = void *(*)(uint32_t);
    using Editor = QWidget *(*)(void *, QWidget *);
    using Destroy = void (*)(void *);
    auto create = reinterpret_cast<Create>(fxProbe.resolve("wjn_fx_create_mixer"));
    auto editor = reinterpret_cast<Editor>(fxProbe.resolve("wjn_fx_create_mixer_editor"));
    auto destroy = reinterpret_cast<Destroy>(fxProbe.resolve("wjn_fx_destroy_mixer"));
    require(create && editor && destroy, "FX UI call sites resolved");
    auto *instance = create(0);
    require(instance, "create offline FX instance no JACK");
    QWidget shell;
    auto *view = editor(instance, &shell);
    require(view, "create actual example UI");
    auto *group = view->findChild<QGroupBox *>();
    require(group && group->title() == QString::fromUtf8("ゲインのサンプル"),
            "late editor first frame current language");
    auto *field = view->findChild<Controls::DecimalField *>();
    require(field, "shared numeric control");
    field->setValue(1.25);
    auto *secondInstance = create(0);
    require(secondInstance, "create independent second FX instance");
    auto *secondView = editor(secondInstance, &shell);
    require(secondView, "late second editor");
    auto *secondGroup = secondView->findChild<QGroupBox *>();
    auto *secondField = secondView->findChild<Controls::DecimalField *>();
    require(secondGroup && secondField, "second shared-control editor");
    secondField->setValue(0.75);
    require(manager.setLanguage("ko-KR") && group->title() == QString::fromUtf8("게인 예제") &&
                field->value() == 1.25 && secondGroup->title() == QString::fromUtf8("게인 예제") &&
                secondField->value() == 0.75,
            "multiple live editors refresh together and retain independent numeric state");
    view->resize(480, 240);
    require(!view->grab().isNull(), "offline UI rendering");
    destroy(instance); // close UI before releasing lease, then unload
    destroy(secondInstance);
    fxLease.reset();
    require(fxEvents(2) == 0, "dual-host first release no detach");
    dual.reset();
    require(fxEvents(2) == 1, "dual-host final release detaches");
    require(fxProbe.unload(), "FX DLL unload");
    require(GetModuleHandleW(reinterpret_cast<LPCWSTR>(fx.utf16())) == nullptr,
            "FX DLL no dangling loader reference");
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        require(argc == 4, "expected node / FX / old DLL paths");
        require(wjn_test_i18n_c_abi() == 1, "service header compiles as C");
        core();
        validation();
        modules(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]),
                QString::fromLocal8Bit(argv[3]));
        std::cout << "S1 i18n PASS: " << checks << " offline checks\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "S1 i18n FAIL: " << error.what() << '\n';
        return 1;
    }
}
