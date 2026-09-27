/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeCatalog.hpp>

#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemePackage.hpp>

#include <QCoreApplication>
#include <QActionGroup>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMenu>
#include <QSettings>

#include <algorithm>

namespace WinJACKNexus::UI {
namespace {
const QString defaultId = QStringLiteral("org.winjacknexus-default");
const QString legacyDefaultId = QStringLiteral("builtin-default");
QString overrideKey(const QString& id) { return QStringLiteral("theme/overrides/") + id; }
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
}

ThemeCatalog::ThemeCatalog(ThemeCore& core, QSettings& settings, QString directory, QObject* parent)
    : QObject(parent), core_(core), settings_(settings),
      directory_(directory.isEmpty()
          ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("themes"))
          : std::move(directory)),
      selectedId_(defaultId) {
    connect(&core_, &ThemeCore::themeChanged, this, [this] { synchronizeExternalSelection(); });
    connect(&core_, &ThemeCore::previewCompleted, this, [this] { synchronizeExternalSelection(); });
}

bool ThemeCatalog::refresh(QString* error) {
    if (!QDir().mkpath(directory_)) return fail(error, QStringLiteral("无法创建主题目录：") + directory_);
    QDir folder(directory_);
    auto files = folder.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                                      | QDir::NoSymLinks, QDir::NoSort);
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        if (a.isDir() != b.isDir()) return a.isDir();
        const auto foldedA = a.fileName().toCaseFolded();
        const auto foldedB = b.fileName().toCaseFolded();
        return foldedA == foldedB ? a.fileName() < b.fileName() : foldedA < foldedB;
    });
    QVector<ThemeCatalogEntry> found;
    QStringList diagnostics;
    QHash<QString, QString> seenIds;
    for (const auto& file : files) {
        const bool dev = file.isDir();
        if (dev) {
            if (!QFileInfo::exists(QDir(file.absoluteFilePath()).filePath(QStringLiteral("meta.json")))
                || !QFileInfo::exists(QDir(file.absoluteFilePath()).filePath(QStringLiteral("style.css")))) continue;
        } else if (file.suffix().compare(QStringLiteral("wjnskin"), Qt::CaseInsensitive) != 0) continue;
        ThemePackage package;
        QString issue;
        if (!(dev ? ThemePackage::loadFromDirectory(file.absoluteFilePath(), &package, &issue)
                  : ThemePackage::loadFromFile(file.absoluteFilePath(), &package, &issue))) {
            diagnostics.append(file.fileName() + QStringLiteral(": ") + issue);
            continue;
        }
        if (package.metadata().value("kind") != QStringLiteral("full")) continue;
        if (package.id() == defaultId) {
            diagnostics.append(file.fileName() + QStringLiteral(": 默认主题由必需 DLL 提供"));
            continue;
        }
        ThemeCore probe;
        if (!probe.setFullTheme(package, &issue)) {
            diagnostics.append(file.fileName() + QStringLiteral(": ") + issue);
            continue;
        }
        if (seenIds.contains(package.id())) {
            diagnostics.append(file.fileName() + QStringLiteral(": 重复主题 ID ") + package.id()
                               + QStringLiteral("（先发现 ") + seenIds.value(package.id()) + QLatin1Char('）'));
            continue;
        }
        seenIds.insert(package.id(), file.fileName());
        const auto meta = package.metadata();
        found.append({package.id(), meta.value("name").toString(),
                      meta.value("author").toString(), {}, file.absoluteFilePath(), dev});
    }
    QHash<QString, int> nameCounts;
    QHash<QString, int> nameAuthorCounts;
    for (const auto& entry : found) {
        ++nameCounts[entry.name];
        ++nameAuthorCounts[entry.name + QChar(0) + entry.author];
    }
    for (auto& entry : found) {
        entry.displayName = entry.name;
        if (nameCounts.value(entry.name) > 1)
            entry.displayName += QStringLiteral(" · ") + entry.author;
        if (nameAuthorCounts.value(entry.name + QChar(0) + entry.author) > 1)
            entry.displayName += QStringLiteral(" [") + entry.id + QLatin1Char(']');
        if (entry.dev) entry.displayName += QStringLiteral(" (Dev)");
    }
    entries_ = std::move(found);
    diagnostics_ = std::move(diagnostics);
    if (error) error->clear();
    return true;
}

void ThemeCatalog::saveCurrentOverrides() {
    if (core_.hasUserOverrides()) {
        ThemePackage package;
        QByteArray bytes;
        if (core_.exportUserOverrides(&package) && package.exportToBytes(&bytes))
            settings_.setValue(overrideKey(selectedId_), bytes);
    } else settings_.remove(overrideKey(selectedId_));
}

void ThemeCatalog::synchronizeExternalSelection() {
    if (switching_ || core_.previewBaseSnapshot()) return;
    const auto actual = core_.snapshot()->themeId();
    if (actual != selectedId_) {
        selectedId_ = actual;
        settings_.setValue(QStringLiteral("theme/selectedId"), selectedId_);
        if (actual == defaultId) settings_.remove(overrideKey(defaultId));
        else {
            const auto saved = settings_.value(overrideKey(actual)).toByteArray();
            if (!saved.isEmpty()) {
                ThemePackage overrides;
                QString issue;
                switching_ = true;
                const bool restored = ThemePackage::loadFromBytes(saved, &overrides, &issue)
                    && core_.restoreUserOverrides(overrides, &issue);
                switching_ = false;
                if (!restored)
                    diagnostics_.append(QStringLiteral("无法恢复主题覆盖 ") + actual + QStringLiteral(": ") + issue);
                else return;
            }
        }
    }
    saveCurrentOverrides();
}

bool ThemeCatalog::activate(const QString& id, QString* error) {
    if (id == selectedId_ && id != defaultId) {
        const auto selected = std::find_if(entries_.cbegin(), entries_.cend(),
                                           [&id](const auto& item) { return item.id == id; });
        if (selected != entries_.cend() && !selected->dev) {
            if (error) error->clear();
            return true;
        }
    }
    ThemePackage package;
    if (id != defaultId) {
        const auto found = std::find_if(entries_.cbegin(), entries_.cend(),
                                        [&id](const auto& item) { return item.id == id; });
        if (found == entries_.cend()) return fail(error, QStringLiteral("主题不在安装目录中：") + id);
        if (!(found->dev ? ThemePackage::loadFromDirectory(found->filePath, &package, error)
                         : ThemePackage::loadFromFile(found->filePath, &package, error))) return false;
        if (package.id() != id || package.metadata().value("kind") != QStringLiteral("full"))
            return fail(error, QStringLiteral("主题文件已改变，请刷新列表"));
    }
    if (initialized_) saveCurrentOverrides();
    switching_ = true;
    QString issue;
    bool succeeded = true;
    if (id == defaultId) {
        settings_.remove(overrideKey(defaultId));
        core_.restoreDefault();
    } else {
        succeeded = core_.beginPreview(package, &issue);
        if (succeeded) {
            const auto saved = settings_.value(overrideKey(id)).toByteArray();
            if (!saved.isEmpty()) {
                ThemePackage overrides;
                if (!ThemePackage::loadFromBytes(saved, &overrides, &issue)
                    || !core_.restoreUserOverrides(overrides, &issue)) succeeded = false;
            }
            if (succeeded) core_.commitPreview();
            else core_.cancelPreview();
        }
    }
    switching_ = false;
    if (!succeeded) return fail(error, issue);
    selectedId_ = id;
    initialized_ = true;
    settings_.setValue(QStringLiteral("theme/selectedId"), id);
    settings_.sync();
    if (settings_.status() != QSettings::NoError)
        diagnostics_.append(QStringLiteral("主题已切换，但无法保存选择"));
    if (error) error->clear();
    return true;
}

bool ThemeCatalog::restoreSaved(QString* error) {
    if (!refresh(error)) return false;
    const auto stored = settings_.value(QStringLiteral("theme/selectedId"), defaultId).toString();
    const auto saved = stored == legacyDefaultId ? defaultId : stored;
    if (saved == defaultId) {
        const auto bytes = settings_.value(overrideKey(defaultId)).toByteArray();
        if (!bytes.isEmpty()) {
            ThemePackage overrides;
            if (!ThemePackage::loadFromBytes(bytes, &overrides, error)
                || !core_.restoreUserOverrides(overrides, error)) return false;
        }
        selectedId_ = defaultId;
        initialized_ = true;
        if (stored != defaultId) settings_.setValue(QStringLiteral("theme/selectedId"), defaultId);
        if (error) error->clear();
        return true;
    }
    if (activate(saved, error)) return true;
    const auto reason = error ? *error : QString {};
    core_.restoreDefault();
    selectedId_ = defaultId;
    initialized_ = true;
    settings_.setValue(QStringLiteral("theme/selectedId"), defaultId);
    diagnostics_.append(QStringLiteral("无法恢复上次主题 ") + saved + QStringLiteral(": ") + reason);
    return false;
}

QVector<ThemeCatalogEntry> ThemeCatalog::entries() const { return entries_; }
QStringList ThemeCatalog::diagnostics() const { return diagnostics_; }
QString ThemeCatalog::selectedId() const { return selectedId_; }
QString ThemeCatalog::directory() const { return directory_; }

QMenu* ThemeCatalog::createMenu(QWidget* parent, std::function<void()> openManagement,
                                std::function<void(const QString&)> reportError) {
    auto* menu = new QMenu(QStringLiteral("主题"), parent);
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    connect(menu, &QMenu::aboutToShow, menu,
            [this, menu, group, openManagement, reportError] {
        QString issue;
        if (!refresh(&issue) && reportError) reportError(issue);
        for (auto* action : group->actions()) group->removeAction(action);
        menu->clear();
        const auto addTheme = [this, menu, group, reportError](const QString& id,
                                                                const QString& label) {
            auto* action = menu->addAction(label);
            action->setObjectName(QStringLiteral("theme.menu.") + id);
            action->setData(id);
            action->setCheckable(true);
            action->setChecked(id == selectedId_);
            group->addAction(action);
            connect(action, &QAction::triggered, menu, [this, id, group, reportError] {
                QString issue;
                if (!activate(id, &issue) && reportError) reportError(issue);
                for (auto* item : group->actions())
                    item->setChecked(item->data().toString() == selectedId_);
            });
        };
        addTheme(defaultId, core_.defaultTheme().metadata().value(QStringLiteral("name")).toString());
        for (const auto& entry : entries_) addTheme(entry.id, entry.displayName);
        const auto selected = std::find_if(entries_.cbegin(), entries_.cend(),
                                           [this](const auto& entry) { return entry.id == selectedId_; });
        if (selected != entries_.cend() && selected->dev) {
            menu->addSeparator();
            auto* reload = menu->addAction(QStringLiteral("刷新当前 Dev 主题"));
            reload->setObjectName(QStringLiteral("theme.menu.refresh-dev"));
            connect(reload, &QAction::triggered, menu, [this, reportError] {
                QString issue;
                if (!activate(selectedId_, &issue) && reportError) reportError(issue);
            });
        }
        menu->addSeparator();
        auto* manage = menu->addAction(QStringLiteral("主题管理…"));
        manage->setObjectName(QStringLiteral("theme.menu.management"));
        connect(manage, &QAction::triggered, menu, openManagement);
    });
    return menu;
}
}
