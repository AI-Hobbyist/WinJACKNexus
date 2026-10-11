/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <QLibrary>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>
#include <QImage>
#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace
{
int checks = 0;
void require(bool ok, const char *what)
{
    ++checks;
    if (!ok)
    {
        std::fprintf(stderr, "S6 VST host FAIL: %s\n", what);
        std::exit(2);
    }
}
void settle()
{
    QEventLoop loop;
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();
}
class ShowBudget final : public QObject
{
  public:
    int shows = 0;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
        {
            auto *widget = qobject_cast<QWidget *>(object);
            if (widget && widget->isWindow())
                require(++shows <= 100, "popup budget");
        }
        return false;
    }
};
QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read evidence");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "write isolated fixture");
    require(file.write(bytes) == bytes.size(), "fixture bytes");
}
void capture(QWidget &window, const QString &path)
{
    const auto handle = reinterpret_cast<HWND>(window.winId());
    const bool wasTopmost = (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    require(
        SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE),
        "protect test window only");
    settle();
    RECT bounds{};
    require(GetClientRect(handle, &bounds), "native client geometry");
    POINT origin{};
    require(ClientToScreen(handle, &origin), "native origin");
    const int width = bounds.right, height = bounds.bottom;
    for (const auto &fraction : {POINT{1, 1}, POINT{9, 1}, POINT{5, 5}, POINT{1, 9}, POINT{9, 9}})
    {
        const POINT point{origin.x + width * fraction.x / 10, origin.y + height * fraction.y / 10};
        require(GetAncestor(WindowFromPoint(point), GA_ROOT) == handle,
                "visible native pixel ownership");
    }
    const auto screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, width, height);
    const auto old = SelectObject(memory, bitmap);
    require(BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT),
            "read visible desktop pixels");
    SelectObject(memory, old);
    QImage pixels(width, height, QImage::Format_RGB32);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    require(GetDIBits(memory, bitmap, 0, height, pixels.bits(), &info, DIB_RGB_COLORS) == height,
            "native capture rows");
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!wasTopmost)
        SetWindowPos(handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    require(pixels.save(path), "save owned desktop capture");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    require(argc == 3, "runtime and capture arguments");
    require(QGuiApplication::platformName() == QStringLiteral("windows"), "Windows platform");
    QApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    ShowBudget budget;
    app.installEventFilter(&budget);
    QTemporaryDir isolated;
    require(isolated.isValid(), "isolated test settings");
    const auto settingsFile = isolated.filePath(QStringLiteral("language.ini"));
    {
        QSettings settings(settingsFile, QSettings::IniFormat);
        settings.setValue(QStringLiteral("i18n/language"), QStringLiteral("ko-KR"));
        settings.sync();
    }
    WinJACKNexus::UI::I18nOptions options;
    options.settingsFile = settingsFile;
    WinJACKNexus::UI::HostI18n language(options);
    QString error;
    const auto runtime = QString::fromLocal8Bit(argv[1]);
    require(language.initialize(runtime, &error), qPrintable(error));
    const auto path = QDir(runtime).filePath(QStringLiteral("nodes/vst/mod_vst_host.dll"));
    QLibrary library(path);
    require(library.load(), qPrintable(library.errorString()));
    auto lease = language.manager().registerLibrary(
        path, WinJACKNexus::UI::I18nManager::Extension::node, 0, &error);
    auto secondLease = language.manager().registerLibrary(
        path, WinJACKNexus::UI::I18nManager::Extension::node, 0, &error);
    require(bool(lease) && bool(secondLease), qPrintable(error));
    const auto create =
        reinterpret_cast<WinJACKNexus::Node::INode *(*)(void)>(library.resolve("wjn_create_node"));
    const auto destroy = reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(
        library.resolve("wjn_destroy_node"));
    const auto widgetCreate =
        reinterpret_cast<QWidget *(*)(WinJACKNexus::Node::INode *, QWidget *)>(
            library.resolve("wjn_create_settings_widget"));
    const auto widgetDestroy = reinterpret_cast<void (*)(WinJACKNexus::Node::INode *, QWidget *)>(
        library.resolve("wjn_destroy_settings_widget"));
    const auto state = reinterpret_cast<const char *(*)(WinJACKNexus::Node::INode *)>(
        library.resolve("wjn_get_node_settings_json"));
    require(create && destroy && widgetCreate && widgetDestroy && state, "actual module exports");
    auto *node = create();
    require(node && node->init(), "offline node with no plugin or JACK process");
    const QByteArray original(state(node));
    QWidget *window = widgetCreate(node, nullptr);
    require(window, "actual DLL settings widget");
    window->setWindowTitle(QStringLiteral("VST i18n verification"));
    window->resize(600, 650);
    window->move(app.primaryScreen()->availableGeometry().topLeft() + QPoint(10, 10));
    window->show();
    window->raise();
    window->activateWindow();
    settle();
    require(window->isVisible() && window->windowHandle()->isExposed(), "visible real DLL window");
    const auto fields = window->findChildren<QLineEdit *>();
    const auto choices = window->findChildren<QComboBox *>();
    require(fields.size() == 3 && choices.size() == 2, "actual settings fields and choices");
    fields.front()->setText(QStringLiteral("draft_日本語_한국어.dll"));
    fields.front()->setSelection(2, 6);
    fields.front()->setFocus();
    settle();
    const auto draft = fields.front()->text(), selected = fields.front()->selectedText();
    const int selectionStart = fields.front()->selectionStart();
    const auto focus = app.focusWidget();
    QVector<QVector<QVariant>> ids;
    for (auto *choice : choices)
    {
        QVector<QVariant> row;
        for (int i = 0; i < choice->count(); ++i)
            row.push_back(choice->itemData(i));
        ids.push_back(row);
    }
    int commits = 0;
    for (auto *choice : choices)
        QObject::connect(choice, &QComboBox::currentIndexChanged, window,
                         [&commits](int) { ++commits; });
    const QStringList languages{QStringLiteral("en-US"), QStringLiteral("zh-CN"),
                                QStringLiteral("ja-JP"), QStringLiteral("ko-KR")};
    const QStringList applyLabels{
        QStringLiteral("Apply and reload"), QStringLiteral("应用并重新加载"),
        QStringLiteral("適用して再ロード"), QStringLiteral("적용 후 다시 로드")};
    for (int lang = 0; lang < languages.size(); ++lang)
    {
        require(language.manager().setLanguage(languages[lang], &error), qPrintable(error));
        settle();
        bool found = false;
        for (auto *button : window->findChildren<QPushButton *>())
            found |= button->text() == applyLabels[lang];
        require(found, "existing action changes language");
        require(fields.front()->text() == draft && fields.front()->selectedText() == selected &&
                    fields.front()->selectionStart() == selectionStart,
                "unsubmitted path draft and selection survive");
        require(app.focusWidget() == focus, "focus survives language notification");
        for (int c = 0; c < choices.size(); ++c)
            for (int i = 0; i < ids[c].size(); ++i)
                require(choices[c]->itemData(i) == ids[c][i],
                        "process and port enum identities preserved");
        require(QByteArray(state(node)) == original,
                "serialized state unchanged by language or draft");
    }
    require(commits == 0, "language selection produces no combo commits");
    bool statusTranslated = false;
    for (auto *label : window->findChildren<QLabel *>())
        statusTranslated |=
            label->text() == QStringLiteral("VST 플러그인을 선택하여 샌드박스를 시작하세요.");
    require(statusTranslated, "stored status retranslates from its key");
    capture(*window, QString::fromLocal8Bit(argv[2]));
    lease.reset();
    require(language.manager().text(QStringLiteral("vst"), QStringLiteral("module.name"),
                                    QStringLiteral("VST Plugin")) == QStringLiteral("VST 플러그인"),
            "second lease keeps actual domain attached");
    secondLease.reset();
    bool fallback = false;
    for (auto *button : window->findChildren<QPushButton *>())
        fallback |= button->text() == QStringLiteral("Apply and reload");
    require(fallback, "detach refreshes existing widget to English");
    require(QByteArray(state(node)) == original, "detach preserves serialized state");
    widgetDestroy(node, window);
    node->shutdown();
    destroy(node);
    require(library.unload(), "actual DLL can unload after owners are released");
    std::printf(
        "S6 VST host PASS: %d checks; platform=windows font=%s; popup shows=%d / 100; single module run, native owned capture, no scale matrix\n",
        checks, qPrintable(app.font().family()), budget.shows);
    return 0;
}
