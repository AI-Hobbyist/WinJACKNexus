/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
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