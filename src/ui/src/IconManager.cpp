#include <WinJACKNexus/UI/IconManager.hpp>

#include <QPainter>
#include <QFile>

namespace WinJACKNexus::UI {

QIcon IconManager::icon(const QString& name)
{
    return QIcon(resourcePath(name));
}

QIcon IconManager::icon(const QString& name, const QSize& size)
{
    if (!size.isValid())
        return icon(name);
    return QIcon(pixmap(name, size));
}

QPixmap IconManager::pixmap(const QString& name, const QSize& size, const QColor& tint)
{
    auto result = icon(name).pixmap(size);
    if (result.isNull() || !tint.isValid())
        return result;
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), tint);
    return result;
}

bool IconManager::hasIcon(const QString& name)
{
    return QFile::exists(resourcePath(name));
}

QString IconManager::resourcePath(const QString& name)
{
    const auto normalizedName = name.trimmed();
    if (normalizedName.startsWith(QStringLiteral(":/")))
        return normalizedName;
    return QStringLiteral(":/WinJACKNexus/UI/icons/") + normalizedName + QStringLiteral(".svg");
}

}