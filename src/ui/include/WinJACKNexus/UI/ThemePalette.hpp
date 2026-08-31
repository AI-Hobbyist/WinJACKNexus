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