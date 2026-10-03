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

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>
class QAbstractButton;

namespace WinJACKNexus::UI {

class WINJACK_UI_API IconManager final {
public:
    static QIcon icon(const QString& name);
    static QIcon icon(const QString& name, const QSize& size);
    static QPixmap pixmap(const QString& name, const QSize& size, const QColor& tint = {});
    static bool hasIcon(const QString& name);
    static QString resourcePath(const QString& name);
    // Reuses the button's shared textColor/palette, including live theme changes.
    static void bindThemedIcon(QAbstractButton* button, const QString& name,
        const QSize& size = QSize(20, 20));
};

}
