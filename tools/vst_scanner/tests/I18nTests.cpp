/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/VstScanner/Language.hpp>
#include <WinJACKNexus/VstScanner/MainWindow.hpp>
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
        std::fprintf(stderr, "S6 scanner FAIL: %s\n", what);
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
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
    const auto settingsFile = isolated.filePath(QStringLiteral("language.ini"));
    {
        QSettings settings(settingsFile, QSettings::IniFormat);
        settings.setValue(QStringLiteral("i18n/language"), QStringLiteral("ko-KR"));
        settings.sync();
    }
    const auto savedSelection = read(settingsFile);
    WinJACKNexus::UI::I18nOptions options;
    options.settingsFile = settingsFile;
    WinJACKNexus::UI::HostI18n language(options);
    QString error;
    auto lease = WinJACKNexus::VstScanner::initializeLanguage(
        language, QString::fromLocal8Bit(argv[1]), &error);
    require(bool(lease), qPrintable(error));
    require(language.manager().effectiveLanguage() == QStringLiteral("ko-KR"),
            "cold saved selection");
    require(read(settingsFile) == savedSelection, "startup preserves language settings bytes");
    const auto lookup = [&language](const char *key, const char *english) {
        return language.manager().text(QStringLiteral("vstscanner"), QString::fromUtf8(key),
                                       QString::fromUtf8(english));
    };
    const auto output = isolated.filePath(QStringLiteral("catalog 日本語 한국어"));
    require(QDir().mkpath(output), "isolated output directory");
    const auto infoPath = output + QStringLiteral("/identity.vstinfo");
    const QByteArray original =
        R"({"name":"Identity_日本語_한국어","vendor":"Vendor_ID","version":"1.0","type":"vst2","bits":64,"path":"D:/identity/plugin.dll","category":"instrument","isInstrument":true,"internal_plugin":"stable-id"})";
    write(infoPath, original);
    const auto missing = isolated.filePath(QStringLiteral("missing 日本語 한국어"));
    {
        WinJACKNexus::VstScanner::MainWindow window({missing}, output);
        const auto available = app.primaryScreen()->availableGeometry();
        window.resize(qMin(1240, available.width() - 40), qMin(780, available.height() - 60));
        window.move(available.topLeft() + QPoint(10, 10));
        window.show();
        window.raise();
        window.activateWindow();
        settle();
        require(window.isVisible() && window.windowHandle()->isExposed(),
                "real visible scanner window");
        require(window.windowTitle() == QStringLiteral("VST 스캐너"), "first frame Korean title");
        auto *table = window.findChild<QTableWidget *>();
        require(table && table->rowCount() == 1, "loaded existing catalog");
        require(table->item(0, 0)->text() == QStringLiteral("Identity_日本語_한국어"),
                "plug-in name identity");
        require(table->item(0, 0)->data(Qt::UserRole).toString() == infoPath,
                "info file stable identity");
        require(table->item(0, 4)->text() == QStringLiteral("악기"), "category display translated");
        require(table->horizontalHeaderItem(0)->text() == QStringLiteral("이름"),
                "first frame table header");
        bool stableFormat = false, stableCategory = false, translatedScan = false;
        for (auto *choice : window.findChildren<QComboBox *>())
        {
            stableFormat |= choice->findData(QStringLiteral("vst2")) >= 0;
            stableCategory |= choice->findData(QStringLiteral("instrument")) >= 0;
        }
        for (auto *button : window.findChildren<QPushButton *>())
            translatedScan |= button->text() == QStringLiteral("플러그인 스캔");
        require(stableFormat && stableCategory, "filter data never translated");
        require(translatedScan, "first frame scan action");
        capture(window, QString::fromLocal8Bit(argv[2]));
        WinJACKNexus::VstScanner::ScanOptions scan;
        scan.searchPaths = {missing};
        scan.outputDirectory = output;
        scan.textCallback = lookup;
        const auto report = WinJACKNexus::VstScanner::Scanner().scan(scan);
        require(report.messages.size() == 1 && report.messages.front().error,
                "missing path report");
        require(report.messages.front().text == QStringLiteral("스캔 경로가 존재하지 않습니다"),
                "translated scan error");
        require(read(infoPath) == original, "scan preserves serialized plug-in identity bytes");
    }
    require(read(settingsFile) == savedSelection, "closing scanner preserves language settings");
    // Core remains independently usable after request-local translation ends.
    WinJACKNexus::VstScanner::ScanOptions plain;
    plain.searchPaths = {missing};
    plain.outputDirectory = output;
    const auto plainReport = WinJACKNexus::VstScanner::Scanner().scan(plain);
    require(plainReport.messages.front().text == QStringLiteral("Scan path does not exist"),
            "no leaked translation callback");
    std::printf(
        "S6 scanner PASS: %d checks; platform=windows font=%s; popup shows=%d / 100; native owned capture; no scale matrix\n",
        checks, qPrintable(app.font().family()), budget.shows);
    return 0;
}
