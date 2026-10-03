/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QApplication>
#include <QEventLoop>
#include <QImage>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ThemeTest {
inline void settle() {
    QEventLoop loop;
    QTimer::singleShot(180, &loop, &QEventLoop::quit);
    loop.exec();
}
inline bool capture(QWidget& window, const QString& path, QJsonObject* evidence) {
    if (!window.isVisible() || !window.windowHandle() || !window.windowHandle()->isExposed()) return false;
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    const bool wasTopmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    struct Restore {
        HWND hwnd; bool wasTopmost;
        ~Restore() { if (!wasTopmost) SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
    } restore {hwnd, wasTopmost};
    if (!SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) return false;
    window.repaint(); settle();
    RECT client {}; POINT origin {};
    if (!GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &origin)) return false;
    const int width = client.right, height = client.bottom;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (width <= 0 || height <= 0 || origin.x < left || origin.y < top
        || origin.x + width > left + GetSystemMetrics(SM_CXVIRTUALSCREEN)
        || origin.y + height > top + GetSystemMetrics(SM_CYVIRTUALSCREEN)) return false;
    for (const auto point : {POINT{1,1}, POINT{9,1}, POINT{5,5}, POINT{1,9}, POINT{9,9}}) {
        const POINT sample {origin.x + width * point.x / 10, origin.y + height * point.y / 10};
        if (GetAncestor(WindowFromPoint(sample), GA_ROOT) != hwnd) return false;
    }
    const auto desktop = GetDC(nullptr);
    if (!desktop) return false;
    const auto memory = CreateCompatibleDC(desktop);
    const auto bitmap = CreateCompatibleBitmap(desktop, width, height);
    if (!memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, desktop); return false;
    }
    const auto previous = SelectObject(memory, bitmap);
    const bool copied = BitBlt(memory, 0, 0, width, height, desktop, origin.x, origin.y, SRCCOPY | CAPTUREBLT);
    SelectObject(memory, previous);
    QImage image(width, height, QImage::Format_RGB32);
    BITMAPINFO info {}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    const auto rows = GetDIBits(memory, bitmap, 0, height, image.bits(), &info, DIB_RGB_COLORS);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, desktop);
    if (!copied || rows != height || !image.save(path)) return false;
    *evidence = {{"path", path}, {"protocol", "native-window-ownership-v2"},
        {"ownership_samples", 5}, {"client_width", width}, {"client_height", height},
        {"desktop_x", static_cast<int>(origin.x)}, {"desktop_y", static_cast<int>(origin.y)},
        {"device_pixel_ratio", window.devicePixelRatioF()}, {"title", window.windowTitle()}};
    return true;
}
}
