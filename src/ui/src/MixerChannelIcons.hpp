/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

enum class MixerChannelIcon
{
    input,
    aux,
    subMix,
    master,
    monitor
};

// Vector artwork, independent of the installed font or emoji renderer.
QPixmap mixerChannelIcon(MixerChannelIcon kind, const QColor &color, int size, qreal dpr)
{
    QPixmap image(qRound(size * dpr), qRound(size * dpr));
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 32., size / 32.);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (kind == MixerChannelIcon::input)
    {
        painter.drawEllipse(QRectF(3, 3, 26, 26));
        painter.drawEllipse(QRectF(7, 7, 18, 18));
        painter.drawLine(QPointF(16, 4), QPointF(16, 8));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (const auto &pin : {QPointF(12, 14), QPointF(20, 14), QPointF(16, 21)})
            painter.drawEllipse(pin, 2, 2);
    }
    else if (kind == MixerChannelIcon::monitor)
    {
        painter.drawArc(QRectF(5, 4, 22, 24), 0, 180 * 16);
        painter.drawRoundedRect(QRectF(4, 15, 6, 13), 2, 2);
        painter.drawRoundedRect(QRectF(22, 15, 6, 13), 2, 2);
    }
    else if (kind == MixerChannelIcon::master)
    {
        painter.drawPolygon(QPolygonF{QPointF(4, 12), QPointF(10, 12), QPointF(17, 6),
                                      QPointF(17, 26), QPointF(10, 20), QPointF(4, 20)});
        painter.drawArc(QRectF(14, 8, 12, 16), -60 * 16, 120 * 16);
        painter.drawArc(QRectF(13, 3, 18, 26), -60 * 16, 120 * 16);
    }
    else if (kind == MixerChannelIcon::aux)
    {
        painter.drawLine(QPointF(4, 16), QPointF(15, 16));
        painter.drawLine(QPointF(15, 7), QPointF(15, 25));
        for (int y : {7, 25})
        {
            painter.drawLine(QPointF(15, y), QPointF(28, y));
            painter.drawPolyline(QPolygonF{QPointF(24, y - 4), QPointF(28, y), QPointF(24, y + 4)});
        }
    }
    else
    {
        painter.drawPolyline(
            QPolygonF{QPointF(4, 5), QPointF(14, 5), QPointF(20, 16), QPointF(28, 16)});
        painter.drawPolyline(QPolygonF{QPointF(4, 27), QPointF(14, 27), QPointF(20, 16)});
        painter.drawPolyline(QPolygonF{QPointF(24, 12), QPointF(28, 16), QPointF(24, 20)});
    }
    return image;
}

QPixmap mixerPaletteIcon(const QColor &outline, const std::array<QColor, 4> &swatches, int size,
                         qreal dpr)
{
    QPixmap image(qRound(size * dpr), qRound(size * dpr));
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 32.0, size / 32.0);
    painter.setPen(QPen(outline, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(3, 4, 26, 24));
    painter.drawEllipse(QRectF(17, 18, 6, 6));
    const std::array<QPointF, 4> positions{QPointF(10, 12), QPointF(17, 9), QPointF(24, 13),
                                           QPointF(10, 21)};
    painter.setPen(Qt::NoPen);
    for (std::size_t i = 0; i < swatches.size(); ++i)
    {
        painter.setBrush(swatches[i]);
        painter.drawEllipse(positions[i], 2.5, 2.5);
    }
    return image;
}
