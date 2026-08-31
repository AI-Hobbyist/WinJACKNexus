#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QVariant>
#include <QWidget>

namespace WinJACKNexus::UI {

namespace {

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

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , basePalette_(ThemePalette::darkRack())
    , palette_(basePalette_)
    , themeName_(QStringLiteral("dark-rack")) {
    resetVariables();
    loadBuiltInTheme(themeName_);
}

ThemeManager::~ThemeManager() = default;

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
    return true;
}

bool ThemeManager::applyToWidget(QWidget* widget) const {
    if (widget == nullptr)
        return false;
    widget->setStyleSheet(styleSheet_);
    return true;
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
            variables_.insert(QString::fromLatin1(key), colorValue.name(QColor::HexRgb));
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