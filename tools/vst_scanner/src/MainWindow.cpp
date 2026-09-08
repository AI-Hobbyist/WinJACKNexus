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
#include <WinJACKNexus/VstScanner/MainWindow.hpp>

#include <WinJACKNexus/Common/PathUtils.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QUrl>
#include <QDesktopServices>
#include <QVBoxLayout>
#include <QWidget>
#include <QStyle>

#include <algorithm>
#include <filesystem>

namespace WinJACKNexus::VstScanner {
namespace {

QString defaultOutputDirectory()
{
    const auto path = Common::PathUtils::moduleDirectory() / L"nodes" / L"VSTPlugins";
    return QString::fromStdWString(path.wstring());
}

QStringList defaultSearchPaths()
{
    QStringList candidates {
        qEnvironmentVariable("CommonProgramW6432") + QStringLiteral("/VST3"),
        qEnvironmentVariable("CommonProgramFiles") + QStringLiteral("/VST3"),
        qEnvironmentVariable("ProgramW6432") + QStringLiteral("/VstPlugins"),
        qEnvironmentVariable("ProgramFiles") + QStringLiteral("/VstPlugins"),
        qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/VstPlugins")
    };
    candidates.removeAll(QString());
    candidates.removeDuplicates();
    candidates.erase(std::remove_if(candidates.begin(),
                                    candidates.end(),
                                    [] (const QString& path) { return !QFileInfo::exists(path); }),
                     candidates.end());
    return candidates;
}

QTableWidgetItem* tableItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString pluginFormat(const PluginInfo& plugin)
{
    return plugin.type.compare(QStringLiteral("vst3"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("VST3")
        : QStringLiteral("VST2");
}

QString pluginCategory(const PluginInfo& plugin)
{
    return plugin.isInstrument ? QStringLiteral("Instrument") : QStringLiteral("Effect");
}

}

MainWindow::MainWindow(const QStringList& initialPaths, const QString& initialOutputDirectory)
{
    createUi();
    loadSettings(initialPaths, initialOutputDirectory);
    loadCatalog();
    refreshPluginTable();
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::createUi()
{
    setWindowTitle(QStringLiteral("Vst Scanner"));
    resize(1240, 780);
    setMinimumSize(960, 620);

    auto* centralWidget = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* actionLayout = new QHBoxLayout;
    scanButton_ = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload),
                                  QStringLiteral("扫描插件"),
                                  centralWidget);
    scanButton_->setDefault(true);
    connect(scanButton_, &QPushButton::clicked, this, &MainWindow::scanNow);
    actionLayout->addWidget(scanButton_);

    rescanVerifiedPlugins_ = new QCheckBox(QStringLiteral("重新扫描已验证插件"), centralWidget);
    rescanVerifiedPlugins_->setToolTip(QStringLiteral("默认仅增量扫描未验证或新增的插件"));
    actionLayout->addWidget(rescanVerifiedPlugins_);

    auto* openOutputButton = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon),
                                             QStringLiteral("打开输出目录"),
                                             centralWidget);
    connect(openOutputButton, &QPushButton::clicked, this, &MainWindow::openOutputDirectory);
    actionLayout->addWidget(openOutputButton);
    actionLayout->addStretch();

    auto* statusHint = new QLabel(QStringLiteral("双击插件可打开所在位置"), centralWidget);
    statusHint->setStyleSheet(QStringLiteral("color: #68727d;"));
    actionLayout->addWidget(statusHint);
    rootLayout->addLayout(actionLayout);

    auto* progressLayout = new QHBoxLayout;
    progressLabel_ = new QLabel(QStringLiteral("就绪"), centralWidget);
    progressLabel_->setMinimumWidth(260);
    progressLayout->addWidget(progressLabel_);
    progressBar_ = new QProgressBar(centralWidget);
    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
    progressBar_->setFormat(QStringLiteral("%v / %m"));
    progressLayout->addWidget(progressBar_, 1);
    rootLayout->addLayout(progressLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, centralWidget);
    splitter->setChildrenCollapsible(false);

    auto* pathGroup = new QGroupBox(QStringLiteral("扫描路径"), splitter);
    pathGroup->setMinimumWidth(260);
    auto* pathLayout = new QVBoxLayout(pathGroup);
    searchPathList_ = new QListWidget(pathGroup);
    searchPathList_->setSelectionMode(QAbstractItemView::SingleSelection);
    searchPathList_->setAlternatingRowColors(true);
    pathLayout->addWidget(searchPathList_);

    auto* pathButtonLayout = new QHBoxLayout;
    auto* addPathButton = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon),
                                          QStringLiteral("添加"),
                                          pathGroup);
    connect(addPathButton, &QPushButton::clicked, this, &MainWindow::addSearchPath);
    pathButtonLayout->addWidget(addPathButton);
    auto* removePathButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogCloseButton),
                                             QStringLiteral("移除"),
                                             pathGroup);
    connect(removePathButton, &QPushButton::clicked, this, &MainWindow::removeSearchPath);
    pathButtonLayout->addWidget(removePathButton);
    pathLayout->addLayout(pathButtonLayout);
    splitter->addWidget(pathGroup);

    auto* catalogWidget = new QWidget(splitter);
    auto* catalogLayout = new QVBoxLayout(catalogWidget);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    catalogLayout->setSpacing(8);

    auto* outputLayout = new QHBoxLayout;
    outputLayout->addWidget(new QLabel(QStringLiteral("输出目录"), catalogWidget));
    outputDirectoryEdit_ = new QLineEdit(catalogWidget);
    outputDirectoryEdit_->setClearButtonEnabled(true);
    outputLayout->addWidget(outputDirectoryEdit_, 1);
    auto* chooseOutputButton = new QToolButton(catalogWidget);
    chooseOutputButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    chooseOutputButton->setToolTip(QStringLiteral("选择输出目录"));
    connect(chooseOutputButton, &QToolButton::clicked, this, [this] {
        const auto directory = QFileDialog::getExistingDirectory(this,
                                                                 QStringLiteral("选择输出目录"),
                                                                 outputDirectoryEdit_->text());
        if (!directory.isEmpty())
            outputDirectoryEdit_->setText(QDir::cleanPath(directory));
    });
    outputLayout->addWidget(chooseOutputButton);
    catalogLayout->addLayout(outputLayout);

    auto* filterLayout = new QHBoxLayout;
    filterLayout->addWidget(new QLabel(QStringLiteral("格式"), catalogWidget));
    typeFilter_ = new QComboBox(catalogWidget);
    typeFilter_->addItem(QStringLiteral("全部"), QString());
    typeFilter_->addItem(QStringLiteral("VST2"), QStringLiteral("vst2"));
    typeFilter_->addItem(QStringLiteral("VST3"), QStringLiteral("vst3"));
    filterLayout->addWidget(typeFilter_);
    filterLayout->addWidget(new QLabel(QStringLiteral("类别"), catalogWidget));
    categoryFilter_ = new QComboBox(catalogWidget);
    categoryFilter_->addItem(QStringLiteral("全部"), QString());
    categoryFilter_->addItem(QStringLiteral("Effect"), QStringLiteral("effect"));
    categoryFilter_->addItem(QStringLiteral("Instrument"), QStringLiteral("instrument"));
    filterLayout->addWidget(categoryFilter_);
    filterEdit_ = new QLineEdit(catalogWidget);
    filterEdit_->setPlaceholderText(QStringLiteral("搜索名称、厂商、路径"));
    filterEdit_->setClearButtonEnabled(true);
    filterLayout->addWidget(filterEdit_, 1);
    catalogLayout->addLayout(filterLayout);

    pluginTable_ = new QTableWidget(catalogWidget);
    pluginTable_->setColumnCount(6);
    pluginTable_->setHorizontalHeaderLabels({ QStringLiteral("名称"),
                                              QStringLiteral("厂商"),
                                              QStringLiteral("格式"),
                                              QStringLiteral("位宽"),
                                              QStringLiteral("类别"),
                                              QStringLiteral("版本") });
    pluginTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pluginTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    pluginTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pluginTable_->setAlternatingRowColors(true);
    pluginTable_->setSortingEnabled(true);
    pluginTable_->verticalHeader()->setVisible(false);
    pluginTable_->horizontalHeader()->setStretchLastSection(true);
    pluginTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    pluginTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pluginTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    connect(pluginTable_, &QTableWidget::cellDoubleClicked, this, [this] {
        openSelectedPluginLocation();
    });
    catalogLayout->addWidget(pluginTable_, 1);

    auto* catalogActions = new QHBoxLayout;
    auto* openLocationButton = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon),
                                               QStringLiteral("打开插件位置"),
                                               catalogWidget);
    connect(openLocationButton, &QPushButton::clicked, this, &MainWindow::openSelectedPluginLocation);
    catalogActions->addWidget(openLocationButton);
    auto* removePluginButton = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon),
                                               QStringLiteral("删除条目"),
                                               catalogWidget);
    connect(removePluginButton, &QPushButton::clicked, this, &MainWindow::removeSelectedPlugin);
    catalogActions->addWidget(removePluginButton);
    auto* removeMissingButton = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon),
                                                QStringLiteral("清理失效条目"),
                                                catalogWidget);
    connect(removeMissingButton, &QPushButton::clicked, this, &MainWindow::removeMissingPlugins);
    catalogActions->addWidget(removeMissingButton);
    catalogActions->addStretch();
    catalogLayout->addLayout(catalogActions);

    auto* logGroup = new QGroupBox(QStringLiteral("扫描日志"), catalogWidget);
    auto* logLayout = new QVBoxLayout(logGroup);
    logView_ = new QPlainTextEdit(logGroup);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(2000);
    logLayout->addWidget(logView_);
    catalogLayout->addWidget(logGroup);

    splitter->addWidget(catalogWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(centralWidget);
    statusLabel_ = new QLabel(statusBar());
    statusBar()->addPermanentWidget(statusLabel_, 1);

    connect(filterEdit_, &QLineEdit::textChanged, this, &MainWindow::refreshPluginTable);
    connect(typeFilter_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshPluginTable);
    connect(categoryFilter_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshPluginTable);
}

void MainWindow::loadSettings(const QStringList& initialPaths, const QString& initialOutputDirectory)
{
    QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Vst Scanner"));
    auto paths = initialPaths;
    if (paths.isEmpty())
        paths = settings.value(QStringLiteral("searchPaths")).toStringList();
    if (paths.isEmpty())
        paths = defaultSearchPaths();

    for (const auto& path : paths) {
        const auto normalized = QDir::cleanPath(path);
        if (!normalized.isEmpty() && !searchPaths().contains(normalized, Qt::CaseInsensitive))
            searchPathList_->addItem(normalized);
    }

    auto output = initialOutputDirectory;
    if (output.isEmpty())
        output = settings.value(QStringLiteral("outputDirectory")).toString();
    if (output.isEmpty())
        output = defaultOutputDirectory();
    outputDirectoryEdit_->setText(QDir::cleanPath(output));
    rescanVerifiedPlugins_->setChecked(settings.value(QStringLiteral("rescanVerifiedPlugins"), false).toBool());
}

void MainWindow::saveSettings() const
{
    QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Vst Scanner"));
    settings.setValue(QStringLiteral("searchPaths"), searchPaths());
    settings.setValue(QStringLiteral("outputDirectory"), outputDirectoryEdit_->text().trimmed());
    settings.setValue(QStringLiteral("rescanVerifiedPlugins"), rescanVerifiedPlugins_->isChecked());
}

void MainWindow::loadCatalog()
{
    plugins_.clear();
    const auto outputDirectory = outputDirectoryEdit_->text().trimmed();
    if (outputDirectory.isEmpty())
        return;

    QSet<QString> loadedFiles;
    QDirIterator iterator(outputDirectory,
                          { QStringLiteral("*.vstinfo"), QStringLiteral("*.vst3info") },
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto infoFile = iterator.next();
        const auto key = QDir::cleanPath(infoFile).toLower();
        if (loadedFiles.contains(key))
            continue;
        loadedFiles.insert(key);
        if (const auto info = Scanner::loadInfoFile(infoFile))
            plugins_.push_back(*info);
    }
}

void MainWindow::refreshPluginTable()
{
    if (pluginTable_ == nullptr)
        return;

    const auto text = filterEdit_->text().trimmed();
    const auto type = typeFilter_->currentData().toString();
    const auto category = categoryFilter_->currentData().toString();
    pluginTable_->setSortingEnabled(false);
    pluginTable_->setRowCount(0);
    for (const auto& plugin : plugins_) {
        const auto searchable = QStringList { plugin.name,
                                              plugin.vendor,
                                              plugin.path,
                                              plugin.subcategory,
                                              plugin.internalPlugin }
                                   .join(QLatin1Char(' '));
        if (!text.isEmpty() && !searchable.contains(text, Qt::CaseInsensitive))
            continue;
        if (!type.isEmpty() && plugin.type.compare(type, Qt::CaseInsensitive) != 0)
            continue;
        if (!category.isEmpty() && plugin.category.compare(category, Qt::CaseInsensitive) != 0)
            continue;

        const auto row = pluginTable_->rowCount();
        pluginTable_->insertRow(row);
        pluginTable_->setItem(row, 0, tableItem(plugin.name));
        pluginTable_->setItem(row, 1, tableItem(plugin.vendor));
        pluginTable_->setItem(row, 2, tableItem(pluginFormat(plugin)));
        pluginTable_->setItem(row, 3, tableItem(QStringLiteral("%1-bit").arg(plugin.bits)));
        pluginTable_->setItem(row, 4, tableItem(pluginCategory(plugin)));
        pluginTable_->setItem(row, 5, tableItem(plugin.version));
        pluginTable_->item(row, 0)->setData(Qt::UserRole, plugin.infoFile);
        pluginTable_->item(row, 0)->setToolTip(plugin.path);
    }
    pluginTable_->setSortingEnabled(true);
    statusLabel_->setText(QStringLiteral("插件 %1 个").arg(pluginTable_->rowCount()));
}

void MainWindow::addSearchPath()
{
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 VST 扫描路径"));
    if (directory.isEmpty())
        return;
    const auto normalized = QDir::cleanPath(directory);
    if (searchPaths().contains(normalized, Qt::CaseInsensitive))
        return;
    searchPathList_->addItem(normalized);
    saveSettings();
}

void MainWindow::removeSearchPath()
{
    delete searchPathList_->takeItem(searchPathList_->currentRow());
    saveSettings();
}

void MainWindow::removeSelectedPlugin()
{
    const auto infoFile = selectedInfoFile();
    if (infoFile.isEmpty())
        return;
    const auto answer = QMessageBox::question(this,
                                              QStringLiteral("删除插件条目"),
                                              QStringLiteral("确定删除当前插件的扫描条目吗？\n插件文件本身不会被删除。"));
    if (answer != QMessageBox::Yes)
        return;
    if (!QFile::remove(infoFile)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"), QStringLiteral("无法删除 info 文件。"));
        return;
    }
    loadCatalog();
    refreshPluginTable();
}

void MainWindow::removeMissingPlugins()
{
    int removed = 0;
    const auto currentPlugins = plugins_;
    for (const auto& plugin : currentPlugins) {
        if (QFileInfo::exists(plugin.path))
            continue;
        if (QFile::remove(plugin.infoFile))
            ++removed;
    }
    loadCatalog();
    refreshPluginTable();
    logView_->appendPlainText(QStringLiteral("清理失效条目：%1 个").arg(removed));
}

void MainWindow::openSelectedPluginLocation()
{
    const auto row = pluginTable_->currentRow();
    if (row < 0 || pluginTable_->item(row, 0) == nullptr)
        return;
    const auto infoFile = pluginTable_->item(row, 0)->data(Qt::UserRole).toString();
    const auto info = Scanner::loadInfoFile(infoFile);
    if (!info)
        return;
    const auto path = QFileInfo(info->path);
    const auto location = path.isDir() ? path.absoluteFilePath() : path.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(location));
}

void MainWindow::openOutputDirectory()
{
    const auto outputDirectory = outputDirectoryEdit_->text().trimmed();
    if (outputDirectory.isEmpty())
        return;
    QDir().mkpath(outputDirectory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputDirectory).absoluteFilePath()));
}

void MainWindow::appendMessage(const ScanMessage& message)
{
    const auto prefix = message.error ? QStringLiteral("[错误]") : QStringLiteral("[信息]");
    logView_->appendPlainText(QStringLiteral("%1 %2: %3").arg(prefix, message.path, message.text));
}

QStringList MainWindow::searchPaths() const
{
    QStringList paths;
    for (int index = 0; index < searchPathList_->count(); ++index)
        paths.push_back(searchPathList_->item(index)->text());
    return paths;
}

QString MainWindow::selectedInfoFile() const
{
    const auto row = pluginTable_->currentRow();
    if (row < 0 || pluginTable_->item(row, 0) == nullptr)
        return {};
    return pluginTable_->item(row, 0)->data(Qt::UserRole).toString();
}

void MainWindow::scanNow()
{
    if (scanButton_ == nullptr || !scanButton_->isEnabled())
        return;

    saveSettings();
    logView_->appendPlainText(QStringLiteral("开始扫描..."));
    scanButton_->setEnabled(false);
    rescanVerifiedPlugins_->setEnabled(false);
    progressLabel_->setText(QStringLiteral("准备扫描..."));
    progressLabel_->setToolTip({});
    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    ScanOptions options;
    options.searchPaths = searchPaths();
    options.outputDirectory = outputDirectoryEdit_->text().trimmed();
    options.rescanVerifiedPlugins = rescanVerifiedPlugins_->isChecked();
    options.infoWrittenCallback = [this] (const PluginInfo&) {
        loadCatalog();
        refreshPluginTable();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    };
    options.progressCallback = [this] (int completed, int total, const QString& currentPath) {
        const auto range = std::max(1, total);
        progressBar_->setRange(0, range);
        progressBar_->setValue(std::min(completed, range));
        if (currentPath.isEmpty()) {
            progressLabel_->setText(total == 0
                                        ? QStringLiteral("未发现插件候选")
                                        : completed >= total ? QStringLiteral("扫描完成")
                                                             : QStringLiteral("准备扫描..."));
            progressLabel_->setToolTip({});
        } else {
            const auto fileName = QFileInfo(currentPath).fileName();
            progressLabel_->setText(QStringLiteral("正在扫描：%1  (%2/%3)")
                                        .arg(fileName.isEmpty() ? currentPath : fileName)
                                        .arg(std::min(completed + 1, total))
                                        .arg(total));
            progressLabel_->setToolTip(currentPath);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    };

    const auto report = Scanner().scan(options);
    for (const auto& message : report.messages)
        appendMessage(message);
    loadCatalog();
    refreshPluginTable();
    logView_->appendPlainText(QStringLiteral("扫描完成：写入 %1 个条目，当前目录共 %2 个条目。")
                                  .arg(report.plugins.size())
                                  .arg(plugins_.size()));
    progressBar_->setRange(0, std::max(1, progressBar_->maximum()));
    progressBar_->setValue(progressBar_->maximum());
    progressLabel_->setText(report.plugins.isEmpty() && report.messages.isEmpty()
                                ? QStringLiteral("未发现插件")
                                : QStringLiteral("扫描完成：写入 %1 个条目").arg(report.plugins.size()));
    scanButton_->setEnabled(true);
    rescanVerifiedPlugins_->setEnabled(true);
    QApplication::restoreOverrideCursor();
}

}
