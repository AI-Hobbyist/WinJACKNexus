/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeBackground.hpp>

#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QImageReader>
#include <QEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <vector>

namespace WinJACKNexus::UI
{
namespace
{
constexpr int kCacheBytes = 64 * 1024 * 1024;
constexpr qint64 kMaxDecodedPixels = 64 * 1024 * 1024;

struct ImageCaches final
{
    QCache<QString, QImage> decoded{kCacheBytes};
    QCache<QString, QImage> prepared{kCacheBytes};
    QPointer<ThemeCore> core;
};

ImageCaches &caches()
{
    static ImageCaches value;
    return value;
}

void useCore(ThemeCore *core)
{
    auto &cache = caches();
    if (cache.core == core)
        return;
    cache.decoded.clear();
    cache.prepared.clear();
    cache.core = core;
    if (core)
        QObject::connect(core, &ThemeCore::themeChanged, core, [] {
            caches().decoded.clear();
            caches().prepared.clear();
        });
}

QString name(const QString &scope, const QString &suffix)
{
    return QStringLiteral("--wjn-theme-%1-background-%2").arg(scope, suffix);
}

qreal position(const QString &value, qreal space)
{
    bool valid = false;
    if (value.endsWith('%'))
    {
        const auto number = value.chopped(1).toDouble(&valid);
        return valid ? space * number / 100.0 : space / 2.0;
    }
    if (value.endsWith("px"))
    {
        const auto number = value.chopped(2).toDouble(&valid);
        return valid ? number : 0.0;
    }
    return space / 2.0;
}

QSizeF imageSize(const QSize &source, const QSizeF &viewport, const QString &mode)
{
    if (mode == "original")
        return source;
    if (mode == "stretch")
        return viewport;
    if (source.isEmpty() || viewport.isEmpty())
        return {};
    const auto x = viewport.width() / source.width();
    const auto y = viewport.height() / source.height();
    const auto scale = mode == "contain" ? std::min(x, y) : std::max(x, y);
    return QSizeF(source.width() * scale, source.height() * scale);
}

QImage decoded(const QByteArray &bytes, QString *diagnostic)
{
    auto &cache = caches().decoded;
    const auto key =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (const auto *found = cache.object(key))
        return *found;
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.isEmpty() || qint64(size.width()) * size.height() > kMaxDecodedPixels)
    {
        if (diagnostic)
            *diagnostic =
                QStringLiteral("Image dimensions are invalid or exceed 64 million pixels");
        return {};
    }
    auto image = reader.read();
    if (image.isNull())
    {
        if (diagnostic)
            *diagnostic = reader.errorString();
        return {};
    }
    const auto cost = qint64(image.sizeInBytes());
    if (cost <= kCacheBytes)
        cache.insert(key, new QImage(image), int(std::max<qint64>(1, cost)));
    return image;
}

QImage gaussianBlur(const QImage &source, int radius)
{
    if (radius <= 0)
        return source;
    const auto input = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage horizontal(input.size(), QImage::Format_ARGB32_Premultiplied);
    QImage output(input.size(), QImage::Format_ARGB32_Premultiplied);
    const auto sigma = std::max(0.5, radius / 2.0);
    std::vector<double> weights(std::size_t(radius * 2 + 1));
    double sum = 0;
    for (int i = -radius; i <= radius; ++i)
    {
        const auto weight = std::exp(-double(i * i) / (2.0 * sigma * sigma));
        weights[std::size_t(i + radius)] = weight;
        sum += weight;
    }
    for (auto &weight : weights)
        weight /= sum;
    const auto pass = [&](const QImage &from, QImage &to, bool vertical) {
        for (int y = 0; y < from.height(); ++y)
        {
            auto *destination = reinterpret_cast<QRgb *>(to.scanLine(y));
            for (int x = 0; x < from.width(); ++x)
            {
                double red = 0, green = 0, blue = 0, alpha = 0;
                for (int i = -radius; i <= radius; ++i)
                {
                    const auto px = vertical ? x : std::clamp(x + i, 0, from.width() - 1);
                    const auto py = vertical ? std::clamp(y + i, 0, from.height() - 1) : y;
                    const auto value = reinterpret_cast<const QRgb *>(from.constScanLine(py))[px];
                    const auto weight = weights[std::size_t(i + radius)];
                    red += qRed(value) * weight;
                    green += qGreen(value) * weight;
                    blue += qBlue(value) * weight;
                    alpha += qAlpha(value) * weight;
                }
                destination[x] = qRgba(qRound(red), qRound(green), qRound(blue), qRound(alpha));
            }
        }
    };
    pass(input, horizontal, false);
    pass(horizontal, output, true);
    return output;
}

QImage prepared(const QImage &source, const QByteArray &bytes, const QSize &pixelSize,
                int blurRadius, QString *diagnostic)
{
    auto &cache = caches().prepared;
    if (pixelSize.isEmpty() || qint64(pixelSize.width()) * pixelSize.height() > kMaxDecodedPixels)
    {
        if (diagnostic)
            *diagnostic = QStringLiteral("Scaled image exceeds 64 million pixels");
        return {};
    }
    const auto key =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) +
        QStringLiteral(":%1:%2:%3").arg(pixelSize.width()).arg(pixelSize.height()).arg(blurRadius);
    if (const auto *found = cache.object(key))
        return *found;
    auto image = source.scaled(pixelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image = gaussianBlur(image, blurRadius);
    const auto cost = qint64(image.sizeInBytes());
    if (cost <= kCacheBytes)
        cache.insert(key, new QImage(image), int(std::max<qint64>(1, cost)));
    return image;
}
} // namespace

bool ThemeBackground::paint(QPainter &painter, const QRectF &viewport, const QString &scope,
                            bool overlay, QString *diagnostic)
{
    if (diagnostic)
        diagnostic->clear();
    auto *core = ThemeManager::installedThemeCore();
    const auto snapshot = core ? core->snapshot() : nullptr;
    if (!snapshot || viewport.isEmpty())
        return false;
    useCore(core);
    const auto imageToken = name(scope, QStringLiteral("image"));
    const auto imageValue = snapshot->value(imageToken);
    if (imageValue.isEmpty() || imageValue == "none")
        return false;
    const auto overlayEnabled =
        snapshot->value(name(scope, QStringLiteral("overlay-enabled"))) == "true";
    if (overlay != overlayEnabled)
        return false;
    const auto resource = snapshot->resource(imageToken);
    if (!resource.isValid())
    {
        if (diagnostic)
            *diagnostic = imageToken + QStringLiteral(": missing image resource");
        return false;
    }
    auto image = decoded(resource.bytes(), diagnostic);
    if (image.isNull())
    {
        if (diagnostic)
            *diagnostic = imageToken + QStringLiteral(": ") + *diagnostic;
        return false;
    }
    auto size = imageSize(image.size(), viewport.size(),
                          snapshot->value(name(scope, QStringLiteral("size"))));
    const auto scaleValue = snapshot->value(name(scope, QStringLiteral("scale"))).toDouble();
    if (scaleValue > 0.0)
        size *= scaleValue;
    if (size.isEmpty() || !std::isfinite(size.width()) || !std::isfinite(size.height()))
        return false;
    const auto x =
        viewport.left() + position(snapshot->value(name(scope, QStringLiteral("position-x"))),
                                   viewport.width() - size.width());
    const auto y =
        viewport.top() + position(snapshot->value(name(scope, QStringLiteral("position-y"))),
                                  viewport.height() - size.height());
    const auto dpr = std::max(1.0, painter.device()->devicePixelRatioF());
    const auto blur =
        overlayEnabled
            ? 0.0
            : snapshot->value(name(scope, QStringLiteral("blur-radius"))).chopped(2).toDouble();
    const auto blurPixels = qRound(blur * dpr);
    const auto repeat = snapshot->value(name(scope, QStringLiteral("repeat")));
    if (blurPixels > 0 || repeat != "no-repeat")
    {
        const QSize scaled(qMax(1, qRound(size.width() * dpr)),
                           qMax(1, qRound(size.height() * dpr)));
        image = prepared(image, resource.bytes(), scaled, blurPixels, diagnostic);
        if (image.isNull())
            return false;
        image.setDevicePixelRatio(dpr);
    }
    const auto opacityToken =
        overlay ? QStringLiteral("overlay-opacity") : QStringLiteral("opacity");
    const auto opacity = snapshot->value(name(scope, opacityToken)).toDouble();
    if (opacity <= 0.0)
        return false;
    painter.save();
    painter.setClipRect(viewport);
    painter.setOpacity(painter.opacity() * std::clamp(opacity, 0.0, 1.0));
    if (repeat == "no-repeat")
    {
        painter.drawImage(QRectF(QPointF(x, y), size), image);
    }
    else
    {
        // Qt's tiled pixmap repeats in logical pixels after the DPR is set.
        auto tile = QPixmap::fromImage(image);
        tile.setDevicePixelRatio(dpr);
        QRectF area = viewport;
        if (repeat == "repeat-x")
            area.setTop(y), area.setHeight(size.height());
        if (repeat == "repeat-y")
            area.setLeft(x), area.setWidth(size.width());
        area = area.intersected(viewport);
        if (!area.isEmpty())
            painter.drawTiledPixmap(area, tile, QPointF(area.left() - x, area.top() - y));
    }
    painter.restore();
    return true;
}

qreal ThemeBackground::componentOpacity(const QString &scope)
{
    const auto *core = ThemeManager::installedThemeCore();
    const auto snapshot = core ? core->snapshot() : nullptr;
    if (!snapshot || snapshot->value(name(scope, QStringLiteral("overlay-enabled"))) == "true")
        return 1.0;
    const auto value = snapshot->value(name(scope, QStringLiteral("component-opacity")));
    return value.isEmpty() ? 1.0 : std::clamp(value.toDouble(), 0.0, 1.0);
}

ThemeBackgroundOverlay::ThemeBackgroundOverlay(QString scope, QWidget *parent)
    : QWidget(parent), scope_(std::move(scope))
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setGeometry(parent->rect());
    parent->installEventFilter(this);
    if (auto *core = ThemeManager::installedThemeCore())
        connect(core, &ThemeCore::themeChanged, this, [this] { update(); });
    show();
    raise();
}

bool ThemeBackgroundOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}

void ThemeBackgroundOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    ThemeBackground::paint(painter, rect(), scope_, true);
}

}
