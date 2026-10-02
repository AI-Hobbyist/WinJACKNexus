/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

enum class MixerChannelIcon { input, aux, subMix, master, monitor };

// Vector artwork, independent of the installed font or emoji renderer.
QPixmap mixerChannelIcon(MixerChannelIcon kind, const QColor& color, int size, qreal dpr)
{
    QPixmap image(qRound(size*dpr),qRound(size*dpr));image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.scale(size/32.,size/32.);
    painter.setPen(QPen(color,2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.setBrush(Qt::NoBrush);
    if(kind==MixerChannelIcon::input) {
        painter.drawRoundedRect(QRectF(11,3,10,17),5,5);
        painter.drawArc(QRectF(7,9,18,16),180*16,180*16);
        painter.drawLine(QPointF(16,25),QPointF(16,29));painter.drawLine(QPointF(11,29),QPointF(21,29));
        for(int y:{8,12,16})painter.drawLine(QPointF(12,y),QPointF(20,y));
    } else if(kind==MixerChannelIcon::monitor) {
        painter.drawArc(QRectF(5,4,22,24),0,180*16);
        painter.drawRoundedRect(QRectF(4,15,6,13),2,2);painter.drawRoundedRect(QRectF(22,15,6,13),2,2);
    } else if(kind==MixerChannelIcon::master) {
        painter.drawPolygon(QPolygonF{QPointF(4,12),QPointF(10,12),QPointF(17,6),QPointF(17,26),QPointF(10,20),QPointF(4,20)});
        painter.drawArc(QRectF(14,8,12,16),-60*16,120*16);painter.drawArc(QRectF(13,3,18,26),-60*16,120*16);
    } else if(kind==MixerChannelIcon::aux) {
        painter.drawLine(QPointF(4,16),QPointF(15,16));painter.drawLine(QPointF(15,7),QPointF(15,25));
        for(int y:{7,25}) {
            painter.drawLine(QPointF(15,y),QPointF(28,y));
            painter.drawPolyline(QPolygonF{QPointF(24,y-4),QPointF(28,y),QPointF(24,y+4)});
        }
    } else {
        painter.drawPolyline(QPolygonF{QPointF(4,5),QPointF(14,5),QPointF(20,16),QPointF(28,16)});
        painter.drawPolyline(QPolygonF{QPointF(4,27),QPointF(14,27),QPointF(20,16)});
        painter.drawPolyline(QPolygonF{QPointF(24,12),QPointF(28,16),QPointF(24,20)});
    }
    return image;
}
