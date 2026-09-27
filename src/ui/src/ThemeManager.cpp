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
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>

#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QSettings>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace WinJACKNexus::UI {

namespace {

QPointer<ThemeCore> processThemeCore;

constexpr const char* paletteKeys[] {
    "window-bg",
    "panel-bg",
    "panel-raised",
    "text",
    "text-muted",
    "accent",
    "accent-alt",
    "border",
    "danger",
    "success"
};

struct PaletteToken { const char* oldKey; const char* token; };
constexpr PaletteToken paletteTokens[] {
    {"window-bg", "--wjn-theme-global-surface-window"},
    {"panel-bg", "--wjn-theme-global-surface-panel"},
    {"panel-raised", "--wjn-theme-global-surface-raised"},
    {"text", "--wjn-theme-global-text-primary"},
    {"text-muted", "--wjn-theme-global-text-muted"},
    {"accent", "--wjn-theme-global-accent-primary"},
    {"accent-alt", "--wjn-theme-global-accent-secondary"},
    {"border", "--wjn-theme-global-border-normal"},
    {"danger", "--wjn-theme-global-state-danger"},
    {"success", "--wjn-theme-global-state-success"}
};

QString tokenForLegacyKey(const QString& key) {
    for (const auto& mapping : paletteTokens)
        if (key == QString::fromLatin1(mapping.oldKey))
            return QString::fromLatin1(mapping.token);
    return {};
}

QColor colorFromThemeValue(const QString& value) {
    if (value == QStringLiteral("transparent")) return QColor(0, 0, 0, 0);
    if (value.startsWith(QStringLiteral("rgb(")) || value.startsWith(QStringLiteral("rgba("))) {
        const bool alpha = value.startsWith(QStringLiteral("rgba("));
        const auto fields = value.mid(alpha ? 5 : 4, value.size() - (alpha ? 6 : 5)).split(',');
        if (fields.size() == (alpha ? 4 : 3)) {
            const auto opacity = alpha ? fields[3].trimmed().toDouble() : 1.0;
            return QColor(fields[0].trimmed().toInt(), fields[1].trimmed().toInt(),
                          fields[2].trimmed().toInt(), qRound(opacity * 255.0));
        }
    }
    return QColor(value);
}

QString colorForStyleSheet(const QColor& color) {
    if (color.alpha() == 255) return color.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red()).arg(color.green()).arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

QString builtInResourcePath(const QString& name) {
    return QStringLiteral(":/WinJACKNexus/UI/themes/") + name + QStringLiteral(".qss");
}

QSettings settings() {
    return QSettings(QSettings::IniFormat,
                     QSettings::UserScope,
                     QStringLiteral("WinJACKNexus"),
                     QStringLiteral("WinJACKNexus"));
}

}

struct ThemeManager::FontState {
    struct LoadedFont { QByteArray hash; int id = -1; QString family; };
    ~FontState() {
        for (const auto& font : loaded) QFontDatabase::removeApplicationFont(font.id);
    }

    void releaseUnused(const QByteArray& pinnedHash) {
        for (qsizetype index = loaded.size(); index > 0; --index) {
            const auto& font = loaded[index - 1];
            if (font.hash == currentHash || font.hash == pinnedHash) continue;
            QFontDatabase::removeApplicationFont(font.id);
            loaded.removeAt(index - 1);
        }
    }

    void apply(QApplication* application, const ThemeSnapshot& snapshot,
               const QByteArray& pinnedHash) {
        if (!application) return;
        if (!capturedSystemFont) {
            systemFont = application->font();
            currentFamily = systemFont.family();
            capturedSystemFont = true;
        }
        const auto source = snapshot.value(QStringLiteral("--wjn-theme-global-font-source"));
        QString desiredFamily = systemFont.family();
        QByteArray activeHash;
        diagnostic.clear();
        if (source != QStringLiteral("system")) {
            const auto resource = snapshot.resource(QStringLiteral("--wjn-theme-global-font-source"));
            const auto bytes = resource.bytes();
            if (bytes.isEmpty()) {
                diagnostic = QStringLiteral("Theme font resource is missing; using system font");
            } else {
                activeHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
                const auto found = std::find_if(loaded.cbegin(), loaded.cend(),
                    [&](const LoadedFont& font) { return font.hash == activeHash; });
                if (found != loaded.cend()) {
                    desiredFamily = found->family;
                } else {
                    const int id = QFontDatabase::addApplicationFontFromData(bytes);
                    const auto families = id < 0 ? QStringList {} : QFontDatabase::applicationFontFamilies(id);
                    if (families.isEmpty() || families.first().isEmpty()) {
                        if (id >= 0) QFontDatabase::removeApplicationFont(id);
                        diagnostic = QStringLiteral("Theme font registration failed; using system font");
                        activeHash.clear();
                    } else {
                        desiredFamily = families.first();
                        loaded.append({activeHash, id, desiredFamily});
                        if (families.size() > 1)
                            diagnostic = QStringLiteral("Theme font contains multiple families; using the first");
                    }
                }
            }
        }
        QFont applicationFont = systemFont;
        if (desiredFamily != systemFont.family())
            applicationFont.setFamilies({desiredFamily, systemFont.family()});
        application->setFont(applicationFont);
        for (auto* widget : QApplication::allWidgets()) {
            if (widget->property("wjnPreserveFontFamily").toBool()) continue;
            auto widgetFont = widget->font();
            const auto family = widgetFont.family();
            if (family != currentFamily && family != systemFont.family()
                && family != desiredFamily) continue;
            if (desiredFamily != systemFont.family())
                widgetFont.setFamilies({desiredFamily, systemFont.family()});
            else widgetFont.setFamily(systemFont.family());
            widget->setFont(widgetFont);
            widget->updateGeometry();
            widget->update();
        }
        currentFamily = desiredFamily;
        currentHash = activeHash;
        releaseUnused(pinnedHash);
    }

    bool capturedSystemFont = false;
    QFont systemFont;
    QString currentFamily;
    QString diagnostic;
    QByteArray currentHash;
    QList<LoadedFont> loaded;
    QPointer<QApplication> application;
};

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , basePalette_(ThemePalette::darkRack())
    , palette_(basePalette_)
    , themeName_(QStringLiteral("dark-rack"))
    , fontState_([] {
        static std::weak_ptr<FontState> shared;
        auto active = shared.lock();
        if (!active) {
            active = std::make_shared<FontState>();
            shared = active;
        }
        return active;
    }()) {
    resetVariables();
    loadBuiltInTheme(themeName_);
    if (processThemeCore) bindThemeCore(processThemeCore);
}

ThemeManager::~ThemeManager() = default;

void ThemeManager::installThemeCore(ThemeCore* core) {
    processThemeCore = core;
    if (core) core->refreshRegisteredNamespaces();
}
ThemeCore* ThemeManager::installedThemeCore() { return processThemeCore; }

bool ThemeManager::setTheme(const QString& name) {
    return loadBuiltInTheme(name);
}

bool ThemeManager::loadBuiltInTheme(const QString& name) {
    const auto normalizedName = name.trimmed();
    if (normalizedName != QStringLiteral("dark-rack") && normalizedName != QStringLiteral("light-console"))
        return false;

    QFile file(builtInResourcePath(normalizedName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    sourceText_ = QString::fromUtf8(file.readAll());
    sourcePath_.clear();
    themeName_ = normalizedName;
    basePalette_ = normalizedName == QStringLiteral("light-console")
        ? ThemePalette::lightConsole()
        : ThemePalette::darkRack();
    palette_ = basePalette_;
    resetVariables();
    rebuildStyleSheet();
    notifyThemeChanged();
    return true;
}

bool ThemeManager::loadTheme(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    sourceText_ = QString::fromUtf8(file.readAll());
    sourcePath_ = QFileInfo(file).absoluteFilePath();
    themeName_ = QFileInfo(file).baseName();
    rebuildStyleSheet();
    notifyThemeChanged();
    return true;
}

bool ThemeManager::reload() {
    return sourcePath_.isEmpty() ? loadBuiltInTheme(themeName_) : loadTheme(sourcePath_);
}

bool ThemeManager::applyToApp(QApplication* application) const {
    if (application == nullptr)
        application = qApp;
    if (application == nullptr)
        return false;
    application->setStyleSheet(styleSheet_);
    fontState_->application = application;
    if (core_ && core_->snapshot()) {
        const auto base = core_->previewBaseSnapshot();
        const auto pinned = base ? base->resource(QStringLiteral("--wjn-theme-global-font-source")).bytes()
            : QByteArray {};
        fontState_->apply(application, *core_->snapshot(),
            pinned.isEmpty() ? QByteArray {} : QCryptographicHash::hash(pinned, QCryptographicHash::Sha256));
    }
    return true;
}

bool ThemeManager::applyToWidget(QWidget* widget) const {
    if (widget == nullptr)
        return false;
    widget->setStyleSheet(styleSheet_);
    if (core_ && fontState_->application
        && !widget->property("wjnPreserveFontFamily").toBool()) {
        auto widgetFont = widget->font();
        widgetFont.setFamilies(fontState_->application->font().families());
        widget->setFont(widgetFont);
        widget->updateGeometry();
    }
    return true;
}

void ThemeManager::watchApplication(QApplication* application) {
    fontState_->application = application;
    applicationOwner_ = application != nullptr;
}

void ThemeManager::bindThemeCore(ThemeCore* core) {
    QObject::disconnect(coreConnection_);
    QObject::disconnect(previewConnection_);
    core_ = core;
    if (core_) {
        coreConnection_ = QObject::connect(core_, &ThemeCore::themeChanged,
                                           this, [this] { syncSnapshot(true); });
        previewConnection_ = QObject::connect(core_, &ThemeCore::previewCompleted,
                                              this, [this] { fontState_->releaseUnused({}); });
        syncSnapshot(false);
    }
}

QString ThemeManager::fontDiagnostic() const { return fontState_->diagnostic; }

void ThemeManager::syncSnapshot(bool applyApplication) {
    if (!core_ || !core_->snapshot()) return;
    const auto snapshot = core_->snapshot();
    ThemePalette mapped = ThemePalette::darkRack();
    for (const auto& mapping : paletteTokens) {
        const QColor color = colorFromThemeValue(snapshot->value(QString::fromLatin1(mapping.token)));
        if (color.isValid()) mapped.setColor(QString::fromLatin1(mapping.oldKey), color);
    }
    basePalette_ = mapped;
    palette_ = mapped;
    themeName_ = snapshot->themeId();
    resetVariables();
    rebuildStyleSheet();
    if (applyApplication && applicationOwner_ && fontState_->application)
        applyToApp(fontState_->application);
    notifyThemeChanged();
}

QString ThemeManager::themeName() const {
    return themeName_;
}

QString ThemeManager::styleSheet() const {
    return styleSheet_;
}

ThemePalette ThemeManager::palette() const {
    return palette_;
}

bool ThemeManager::setVariable(const QString& key, const QString& value) {
    const auto normalizedKey = key.trimmed();
    if (normalizedKey.isEmpty() || value.isEmpty())
        return false;
    if (core_) {
        const auto token = tokenForLegacyKey(normalizedKey);
        if (!token.isEmpty()) return core_->setOverride(token, value);
    }
    variables_.insert(normalizedKey, value);
    const QColor colorValue(value);
    if (colorValue.isValid())
        palette_.setColor(normalizedKey, colorValue);
    rebuildStyleSheet();
    notifyThemeChanged();
    return true;
}

bool ThemeManager::removeVariable(const QString& key) {
    const auto normalizedKey = key.trimmed();
    if (core_) {
        const auto token = tokenForLegacyKey(normalizedKey);
        if (!token.isEmpty()) return core_->removeOverride(token);
    }
    if (!variables_.contains(normalizedKey))
        return false;
    variables_.remove(normalizedKey);
    const auto baseColor = basePalette_.color(normalizedKey);
    if (baseColor.isValid())
        palette_.setColor(normalizedKey, baseColor);
    rebuildStyleSheet();
    notifyThemeChanged();
    return true;
}

bool ThemeManager::saveSettings() const {
    auto store = settings();
    store.setValue(QStringLiteral("theme/name"), themeName_);
    store.setValue(QStringLiteral("theme/source"), sourcePath_);
    store.beginWriteArray(QStringLiteral("variables"));
    int index = 0;
    for (auto iterator = variables_.cbegin(); iterator != variables_.cend(); ++iterator) {
        store.setArrayIndex(index++);
        store.setValue(QStringLiteral("key"), iterator.key());
        store.setValue(QStringLiteral("value"), iterator.value());
    }
    store.endArray();
    store.sync();
    return store.status() == QSettings::NoError;
}

bool ThemeManager::loadSettings() {
    auto store = settings();
    const auto storedName = store.value(QStringLiteral("theme/name"), themeName_).toString();
    const auto storedSource = store.value(QStringLiteral("theme/source")).toString();
    bool loaded = false;
    if (!storedSource.isEmpty() && QFileInfo::exists(storedSource))
        loaded = loadTheme(storedSource);
    if (!loaded)
        loaded = setTheme(storedName);
    if (!loaded)
        return false;

    const auto count = store.beginReadArray(QStringLiteral("variables"));
    for (int index = 0; index < count; ++index) {
        store.setArrayIndex(index);
        setVariable(store.value(QStringLiteral("key")).toString(),
                    store.value(QStringLiteral("value")).toString());
    }
    store.endArray();
    return true;
}

void ThemeManager::resetVariables() {
    variables_.clear();
    for (const auto* key : paletteKeys) {
        const auto colorValue = palette_.color(QString::fromLatin1(key));
        if (colorValue.isValid())
            variables_.insert(QString::fromLatin1(key), colorForStyleSheet(colorValue));
    }
}

void ThemeManager::rebuildStyleSheet() {
    QString resolved = sourceText_;
    for (auto iterator = variables_.cbegin(); iterator != variables_.cend(); ++iterator)
        resolved.replace(QStringLiteral("${") + iterator.key() + QStringLiteral("}"), iterator.value());
    styleSheet_ = palette_.resolveVariables(resolved);
}

void ThemeManager::notifyThemeChanged() {
    emit themeChanged(palette_);
    emit onThemeChanged(palette_);
}

}
