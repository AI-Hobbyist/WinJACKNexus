#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace WinJACKNexus::UI {

class WINJACK_UI_API IconManager final {
public:
    static QIcon icon(const QString& name);
    static QIcon icon(const QString& name, const QSize& size);
    static QPixmap pixmap(const QString& name, const QSize& size, const QColor& tint = {});
    static bool hasIcon(const QString& name);
    static QString resourcePath(const QString& name);
};

}