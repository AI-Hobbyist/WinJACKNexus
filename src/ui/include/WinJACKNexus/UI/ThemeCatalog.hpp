/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

class QSettings;
class QMenu;
class QWidget;

namespace WinJACKNexus::UI
{
class ThemeCore;

struct WINJACK_UI_API ThemeCatalogEntry final
{
    QString id;
    QString name;
    QString author;
    QString displayName;
    QString filePath;
    bool dev = false;
};

// Scans only EXE-adjacent themes/*.wjnskin; keeps per-theme user overrides in settings.
class WINJACK_UI_API ThemeCatalog final : public QObject
{
  public:
    ThemeCatalog(ThemeCore &core, QSettings &settings, QString directory = {},
                 QObject *parent = nullptr);

    bool refresh(QString *error = nullptr);
    bool restoreSaved(QString *error = nullptr);
    bool activate(const QString &id, QString *error = nullptr);
    QVector<ThemeCatalogEntry> entries() const;
    QStringList diagnostics() const;
    QString selectedId() const;
    QString directory() const;
    QMenu *createMenu(QWidget *parent, std::function<void()> openManagement,
                      std::function<void(const QString &)> reportError);

  private:
    void saveCurrentOverrides();
    void synchronizeExternalSelection();

    ThemeCore &core_;
    QSettings &settings_;
    QString directory_;
    QVector<ThemeCatalogEntry> entries_;
    QStringList diagnostics_;
    QString selectedId_;
    bool switching_ = false;
    bool initialized_ = false;
};
}
