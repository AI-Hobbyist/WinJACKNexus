#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <Windows.h>

#include <QtGlobal>

#include <QSize>

class QWidget;

namespace WinJACKNexus::UI::NativeWindowUtils {

WINJACK_UI_API QWidget* embedNativeWindow(HWND nativeWindow, QWidget* parent = nullptr);
WINJACK_UI_API bool detachNativeWindow(QWidget* container) noexcept;
WINJACK_UI_API HWND obtainNativeHandle(const QWidget* widget) noexcept;
WINJACK_UI_API qreal scaleFactor(HWND nativeWindow) noexcept;
WINJACK_UI_API qreal devicePixelRatio(const QWidget* widget) noexcept;
WINJACK_UI_API QSize logicalToNativeSize(const QWidget* widget, const QSize& logicalSize) noexcept;
WINJACK_UI_API bool enablePerMonitorDpiAwareness() noexcept;

}