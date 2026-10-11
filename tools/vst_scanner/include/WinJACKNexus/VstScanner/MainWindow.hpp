/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <WinJACKNexus/VstScanner/Scanner.hpp>

#include <QMainWindow>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace WinJACKNexus::VstScanner
{

class MainWindow final : public QMainWindow
{
  public:
    explicit MainWindow(const QStringList &initialPaths = {},
                        const QString &initialOutputDirectory = {});
    ~MainWindow() override;

    void scanNow();

  private:
    void createUi();
    void loadSettings(const QStringList &initialPaths, const QString &initialOutputDirectory);
    void saveSettings() const;
    void loadCatalog();
    void refreshPluginTable();
    void addSearchPath();
    void removeSearchPath();
    void removeSelectedPlugin();
    void removeMissingPlugins();
    void openSelectedPluginLocation();
    void openOutputDirectory();
    void appendMessage(const ScanMessage &message);
    QStringList searchPaths() const;
    QString selectedInfoFile() const;

    QListWidget *searchPathList_{};
    QLineEdit *outputDirectoryEdit_{};
    QLineEdit *filterEdit_{};
    QComboBox *typeFilter_{};
    QComboBox *categoryFilter_{};
    QCheckBox *rescanVerifiedPlugins_{};
    QTableWidget *pluginTable_{};
    QPlainTextEdit *logView_{};
    QProgressBar *progressBar_{};
    QPushButton *scanButton_{};
    QLabel *progressLabel_{};
    QLabel *statusLabel_{};
    QVector<PluginInfo> plugins_;
};

}
