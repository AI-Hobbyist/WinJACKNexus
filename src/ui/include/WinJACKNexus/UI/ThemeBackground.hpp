/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QRectF>
#include <QString>
#include <QWidget>

class QPainter;

namespace WinJACKNexus::UI
{

// Paints a theme package image in a module's logical-pixel content viewport.
// Call from a GUI-thread paint event before controls, or after them for the
// explicitly enabled input-transparent overlay layer.
class WINJACK_UI_API ThemeBackground final
{
  public:
    static bool paint(QPainter &painter, const QRectF &viewport, const QString &scope,
                      bool overlay = false, QString *diagnostic = nullptr);
    static qreal componentOpacity(const QString &scope);
};

// A transparent, input-pass-through child placed above a module's QWidget
// controls. Construct after the content widgets so it remains topmost.
class WINJACK_UI_API ThemeBackgroundOverlay final : public QWidget
{
  public:
    explicit ThemeBackgroundOverlay(QString scope, QWidget *parent);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

  private:
    QString scope_;
};

}
