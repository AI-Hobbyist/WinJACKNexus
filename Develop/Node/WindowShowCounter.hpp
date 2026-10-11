// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QEvent>
#include <QWidget>

class WindowShowCounter final : public QObject
{
  public:
    unsigned shows{};
    bool eventFilter(QObject *object, QEvent *event) override
    {
        const auto *widget = qobject_cast<QWidget *>(object);
        if (event->type() == QEvent::Show && widget && widget->isWindow())
        {
            ++shows;
        }
        return false;
    }
};
