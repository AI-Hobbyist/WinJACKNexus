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
#include <WinJACKNexus/UI/IconManager.hpp>

#include <QPainter>
#include <QFile>
#include <QAbstractButton>
#include <QDynamicPropertyChangeEvent>
#include <QPalette>

namespace WinJACKNexus::UI
{
namespace
{
class IconBinding final : public QObject
{
  public:
    IconBinding(QAbstractButton *button, QString name, QSize size)
        : QObject(button), button_(button), name_(std::move(name)), size_(size)
    {
        button_->installEventFilter(this);
        refresh();
    }
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint || event->type() == QEvent::PaletteChange ||
            (event->type() == QEvent::DynamicPropertyChange &&
             static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "textColor"))
            refresh();
        return false;
    }

  private:
    void refresh()
    {
        const auto color = button_->property("textColor").value<QColor>();
        const auto effective =
            color.isValid() ? color : button_->palette().color(QPalette::ButtonText);
        if (effective == lastColor_)
            return;
        lastColor_ = effective;
        button_->setIcon(QIcon(IconManager::pixmap(name_, size_, effective)));
        button_->setIconSize(size_);
    }
    QAbstractButton *button_;
    QString name_;
    QSize size_;
    QColor lastColor_;
};
}
void IconManager::bindThemedIcon(QAbstractButton *button, const QString &name, const QSize &size)
{
    if (button)
        new IconBinding(button, name, size);
}

QIcon IconManager::icon(const QString &name)
{
    return QIcon(resourcePath(name));
}

QIcon IconManager::icon(const QString &name, const QSize &size)
{
    if (!size.isValid())
        return icon(name);
    return QIcon(pixmap(name, size));
}

QPixmap IconManager::pixmap(const QString &name, const QSize &size, const QColor &tint)
{
    auto result = icon(name).pixmap(size);
    if (result.isNull() || !tint.isValid())
        return result;
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), tint);
    return result;
}

bool IconManager::hasIcon(const QString &name)
{
    return QFile::exists(resourcePath(name));
}

QString IconManager::resourcePath(const QString &name)
{
    const auto normalizedName = name.trimmed();
    if (normalizedName.startsWith(QStringLiteral(":/")))
        return normalizedName;
    return QStringLiteral(":/WinJACKNexus/UI/icons/") + normalizedName + QStringLiteral(".svg");
}

}
