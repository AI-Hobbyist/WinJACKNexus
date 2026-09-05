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
#include <QString>

namespace WinJACKNexus::UI {

class WINJACK_UI_API ThemePalette final {
public:
    ThemePalette();
    explicit ThemePalette(const QString& name);
    ~ThemePalette();

    ThemePalette(const ThemePalette& other);
    ThemePalette& operator=(const ThemePalette& other);
    ThemePalette(ThemePalette&& other) noexcept;
    ThemePalette& operator=(ThemePalette&& other) noexcept;

    static ThemePalette darkRack();
    static ThemePalette lightConsole();

    QString name() const;
    void setName(const QString& name);
    QColor color(const QString& key) const;
    bool setColor(const QString& key, const QColor& color);
    QString resolveVariables(const QString& text) const;
    bool isValid() const noexcept;

private:
    struct Impl;
    static void configurePalette(Impl& impl, const QString& name, bool light);
    Impl* impl_ = nullptr;
};

}