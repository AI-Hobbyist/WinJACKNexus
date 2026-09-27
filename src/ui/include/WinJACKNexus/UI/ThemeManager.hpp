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
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QApplication;
class QWidget;

namespace WinJACKNexus::UI {

class ThemeCore;

class WINJACK_UI_API ThemeManager final : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override;

    // New legacy adapters created by other modules follow this process theme core.
    static void installThemeCore(ThemeCore* core);
    static ThemeCore* installedThemeCore();

    bool setTheme(const QString& name);
    bool loadBuiltInTheme(const QString& name);
    bool loadTheme(const QString& path);
    bool reload();

    bool applyToApp(QApplication* application = nullptr) const;
    bool applyToWidget(QWidget* widget) const;
    void watchApplication(QApplication* application);

    // Bridge the legacy palette/QSS API to the active v1 theme snapshot.
    void bindThemeCore(ThemeCore* core);
    QString fontDiagnostic() const;

    QString themeName() const;
    QString styleSheet() const;
    ThemePalette palette() const;

    bool setVariable(const QString& key, const QString& value);
    bool removeVariable(const QString& key);

    bool saveSettings() const;
    bool loadSettings();

Q_SIGNALS:
    void themeChanged(const ThemePalette& palette);
    void onThemeChanged(const ThemePalette& palette);

private:
    struct FontState;
    void resetVariables();
    void rebuildStyleSheet();
    void notifyThemeChanged();
    void syncSnapshot(bool applyApplication);

    ThemePalette basePalette_;
    ThemePalette palette_;
    QString themeName_;
    QString sourcePath_;
    QString sourceText_;
    QString styleSheet_;
    QHash<QString, QString> variables_;
    QPointer<ThemeCore> core_;
    QMetaObject::Connection coreConnection_;
    QMetaObject::Connection previewConnection_;
    std::shared_ptr<FontState> fontState_;
    bool applicationOwner_ = false;
};

}
