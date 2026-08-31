#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QHash>
#include <QObject>
#include <QString>

class QApplication;
class QWidget;

namespace WinJACKNexus::UI {

class WINJACK_UI_API ThemeManager final : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override;

    bool setTheme(const QString& name);
    bool loadBuiltInTheme(const QString& name);
    bool loadTheme(const QString& path);
    bool reload();

    bool applyToApp(QApplication* application = nullptr) const;
    bool applyToWidget(QWidget* widget) const;

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
    void resetVariables();
    void rebuildStyleSheet();
    void notifyThemeChanged();

    ThemePalette basePalette_;
    ThemePalette palette_;
    QString themeName_;
    QString sourcePath_;
    QString sourceText_;
    QString styleSheet_;
    QHash<QString, QString> variables_;
};

}