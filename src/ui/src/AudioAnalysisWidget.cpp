// SPDX-License-Identifier: AGPL-3.0-only
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QTimer>
#include <WinJACKNexus/Common/LoudnessEngine.hpp>
#include <WinJACKNexus/Common/MeterCustomLoudnessPresets.hpp>
#include <WinJACKNexus/Common/MeterLoudnessPresetCatalog.hpp>
#include <WinJACKNexus/Common/MixerPolarCurve.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/AudioAnalysisWidget.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/UI/MixerPolarPlot.hpp>
#include <algorithm>
#include <bit>
#include <cstring>
#include <numbers>
#include <vector>

namespace WinJACKNexus::UI
{
namespace
{
using Kind = Common::AnalysisKind;
const char *roles[]{"MONO", "L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"};
QString role(unsigned channel, unsigned count)
{
    return QString::fromLatin1(roles[count == 1 ? 0 : channel + 1]);
}
QColor color(const QWidget &widget, const QString &token, QPalette::ColorRole fallback)
{
    return Controls::themeTokenColor("global", token, widget.palette().color(fallback));
}
QColor channelColor(const QWidget &widget, unsigned channel)
{
    return color(widget, QStringLiteral("analysis-channel-%1").arg(channel + 1), QPalette::Highlight);
}
QColor blend(const QColor &first, const QColor &second, double position)
{
    return QColor::fromRgbF(first.redF() + (second.redF() - first.redF()) * position,
                            first.greenF() + (second.greenF() - first.greenF()) * position,
                            first.blueF() + (second.blueF() - first.blueF()) * position);
}
} // namespace
class AudioAnalysisWidget::Impl
{
  public:
    AudioAnalysisWidget &owner;
    std::shared_ptr<Common::AnalysisCapture> source;
    QTimer timer;
    QString domain;
    std::array<std::array<float, 1024>, 8> wave{};
    std::array<std::array<double, 512>, 8> powers{};
    std::array<double, 8> rms{};
    std::array<QImage, 8> history;
    std::array<MetricReadout *, 8> readouts{};
    unsigned position{}, pending{}, rate{48000};
    std::uint64_t revision{};
    bool lastSeparate{}, lastLog{};

    Impl(AudioAnalysisWidget &widget, std::shared_ptr<Common::AnalysisCapture> capture, QString domain)
        : owner(widget), source(std::move(capture)), domain(std::move(domain))
    {
        timer.setParent(&owner);
        bindRefreshTimer(timer, &owner);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] { refresh(); });
        if (source->kind == Kind::spectrogram)
            for (unsigned channel = 0; channel < source->channels; ++channel)
            {
                history[channel] = QImage(256, 128, QImage::Format_RGB32);
                history[channel].fill(color(owner, "analysis-spectrum-floor", QPalette::Base));
            }
        owner.setMinimumSize(200, 120);
        if (source->kind == Kind::loudness)
        {
            const char *names[]{"M", "S", "I", "LRA", "dBTP", "Sample Peak", "RMS", "LFE Peak"};
            const char *units[]{"LUFS", "LUFS", "LUFS", "LU", "dBTP", "dBFS", "dBFS", "dBFS"};
            for (unsigned index = 0; index < readouts.size(); ++index)
            {
                auto *readout = new MetricReadout(&owner);
                readout->setObjectName(QStringLiteral("controlDeck.loudnessReadout.%1").arg(index));
                readout->setLabel(QString::fromLatin1(names[index]));
                readout->setUnit(QString::fromLatin1(units[index]));
                readout->setPrecision(1);
                readout->setShowSurface(true);
                readout->setMinimumSize(0, 0);
                readout->setAttribute(Qt::WA_TransparentForMouseEvents);
                readout->hide();
                readouts[index] = readout;
            }
        }
    }
    std::pair<unsigned, unsigned> grid(unsigned count) const
    {
        const unsigned customColumns = source->layoutColumns.load();
        const unsigned customRows = source->layoutRows.load();
        const unsigned columns = customColumns && customRows          ? customColumns
                                 : source->verticalArrangement.load() ? 1
                                                                      : std::max(1u, count);
        const unsigned rows = std::max(1u, (count + columns - 1) / columns);
        return {columns, customColumns && customRows ? std::max(customRows, rows) : rows};
    }
    void refreshLoudnessReadouts()
    {
        const auto snapshot = source->loudnessSnapshot();
        const std::array values{snapshot.momentaryLufs,   snapshot.shortTermLufs, snapshot.integratedLufs,
                                snapshot.loudnessRangeLu, snapshot.truePeakDbtp,  snapshot.samplePeakDbfs,
                                snapshot.rmsDbfs,         snapshot.lfePeakDbfs};
        const char *accents[]{"chart-series-momentary-stroke",  "chart-series-short-term-stroke",
                              "chart-series-integrated-stroke", "chart-series-lra-stroke",
                              "chart-series-true-peak-stroke",  "chart-series-peak-stroke",
                              "chart-series-rms-stroke",        "chart-series-peak-stroke"};
        auto palette = ThemePalette::darkRack();
        palette.setColor("panel-bg", color(owner, "surface-panel", QPalette::Base));
        palette.setColor("panel-raised", color(owner, "surface-raised", QPalette::Window));
        palette.setColor("border", color(owner, "border-normal", QPalette::Mid));
        palette.setColor("text", color(owner, "text-primary", QPalette::WindowText));
        palette.setColor("text-muted", color(owner, "text-muted", QPalette::WindowText));
        const int margin = owner.style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
        const QRect area = owner.rect().adjusted(margin, margin, -margin, -margin);
        const auto mask = source->metricMask.load();
        const unsigned count = std::popcount(mask & 0xffu);
        const auto [columns, rows] = grid(count);
        unsigned cell = 0;
        for (unsigned index = 0; index < readouts.size(); ++index)
        {
            auto *readout = readouts[index];
            const bool visible = source->numeric.load() && (mask & (1u << index));
            readout->setVisible(visible);
            if (!visible)
                continue;
            const int width = area.width() / int(columns);
            const int height = area.height() / int(rows);
            readout->setGeometry(QRect(area.left() + int(cell % columns) * width,
                                       area.top() + int(cell / columns) * height, width, height)
                                     .adjusted(margin / 2, margin / 2, -margin / 2, -margin / 2));
            readout->setThemePalette(palette);
            readout->setAccentColor(Controls::themeTokenColor(
                "meter-bridge", accents[index], color(owner, "accent-primary", QPalette::Highlight)));
            readout->setValue(values[index].valid ? std::optional<double>(values[index].value)
                                                  : std::nullopt);
            ++cell;
        }
    }
    void clear()
    {
        wave = {};
        powers = {};
        rms = {};
        position = pending = 0;
        for (auto &image : history)
            image.fill(color(owner, "analysis-spectrum-floor", QPalette::Base));
    }
    void refresh()
    {
        if (source->kind == Kind::loudness)
        {
            refreshLoudnessReadouts();
            owner.setToolTip(source->recordingStatus());
            owner.update();
            return;
        }
        const auto currentRevision = source->revision.load(std::memory_order_acquire);
        if (revision != currentRevision)
        {
            revision = currentRevision;
            source->samples.clear();
            clear();
        }
        const bool separate = source->separate.load();
        const bool logarithmic = source->logarithmic.load();
        if (lastSeparate != separate || lastLog != logarithmic)
        {
            for (auto &image : history)
                image.fill(color(owner, "analysis-spectrum-floor", QPalette::Base));
            lastSeparate = separate;
            lastLog = logarithmic;
        }
        Common::AnalysisFrame frame;
        unsigned read = 0;
        while (read++ < source->samples.capacity() && source->samples.try_pop(frame))
        {
            if (frame.sampleRate != rate)
            {
                rate = frame.sampleRate;
                clear();
            }
            for (unsigned channel = 0; channel < source->channels; ++channel)
                wave[channel][position] = frame.samples[channel];
            position = (position + 1) % 1024;
            ++pending;
        }
        if (pending >= 512)
        {
            for (unsigned channel = 0; channel < source->channels; ++channel)
            {
                std::array<float, 1024> ordered{};
                for (unsigned sample = 0; sample < 1024; ++sample)
                {
                    ordered[sample] = wave[channel][(position + sample) % 1024];
                }
                rms[channel] = Common::analysisRms(ordered);
                if (source->kind != Kind::oscilloscope && source->kind != Kind::polar &&
                    source->kind != Kind::loudness)
                    powers[channel] = Common::analysisSpectrum(ordered);
            }
            if (source->kind == Kind::spectrogram)
                addSpectrum();
            pending = 0;
        }
        owner.update();
    }
    double power(unsigned panel, unsigned bin) const
    {
        if (source->separate.load())
            return powers[panel][bin];
        return Common::analysisMeanPower(powers, source->channels, bin);
    }
    unsigned binFor(double fraction) const
    {
        const double frequency = source->logarithmic.load()
                                     ? 20 * std::pow(std::max(1.0, rate / 40.0), fraction)
                                     : fraction * rate / 2;
        return std::clamp(unsigned(frequency * 1024 / rate), 0u, 511u);
    }
    QColor spectrumColor(double power) const
    {
        const double db = power > 0 ? 10 * std::log10(power) : -90;
        const double amount = std::clamp((db + 90) / 90, 0.0, 1.0);
        const auto low = color(owner, "analysis-spectrum-floor", QPalette::Base);
        const auto middle = color(owner, "analysis-spectrum-mid", QPalette::Highlight);
        const auto high = color(owner, "analysis-spectrum-hot", QPalette::BrightText);
        return amount < 0.5 ? blend(low, middle, amount * 2) : blend(middle, high, amount * 2 - 1);
    }
    void addSpectrum()
    {
        const unsigned panels = source->separate.load() ? source->channels : 1;
        for (unsigned panel = 0; panel < panels; ++panel)
            for (int row = 0; row < 128; ++row)
            {
                auto *pixels = reinterpret_cast<QRgb *>(history[panel].scanLine(row));
                std::memmove(pixels, pixels + 1, 255 * sizeof(QRgb));
                pixels[255] = spectrumColor(power(panel, binFor(1 - row / 127.0))).rgb();
            }
    }
    void paint(QPainter &painter)
    {
        painter.fillRect(owner.rect(), color(owner, "surface-panel", QPalette::Base));
        painter.setRenderHint(QPainter::Antialiasing);
        const int margin = owner.style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
        const QRectF area = QRectF(owner.rect()).adjusted(margin, margin, -margin, -margin);
        if (source->kind == Kind::loudness)
        {
            paintLoudness(painter, area);
            return;
        }
        if (source->kind == Kind::polar)
        {
            polar(painter, area);
            return;
        }
        const bool separate = source->separate.load();
        const unsigned panels = separate ? source->channels : 1;
        const bool overlay = !separate && source->kind == Kind::spectrumCurve && source->overlay.load();
        const auto [columns, rows] = panels == 1 ? std::pair{1u, 1u} : grid(panels);
        for (unsigned panel = 0; panel < panels; ++panel)
        {
            QRectF bounds(area.left() + (panel % columns) * area.width() / columns,
                          area.top() + (panel / columns) * area.height() / rows, area.width() / columns,
                          area.height() / rows);
            const QString caption = separate ? role(panel, source->channels) : QStringLiteral("Σ/N");
            painter.setPen(color(owner, "text-primary", QPalette::WindowText));
            if (!separate && (source->kind == Kind::oscilloscope || overlay))
            {
                qreal left = bounds.left();
                for (unsigned channel = 0; channel < source->channels; ++channel)
                {
                    const auto name = role(channel, source->channels);
                    const auto width = owner.fontMetrics().horizontalAdvance(name) + margin;
                    painter.setPen(channelColor(owner, channel));
                    painter.drawText(QRectF(left, bounds.top(), width, owner.fontMetrics().height()),
                                     Qt::AlignLeft | Qt::AlignTop, name);
                    left += width;
                }
            }
            else
                painter.drawText(bounds, Qt::AlignLeft | Qt::AlignTop, caption);
            bounds.adjust(0, owner.fontMetrics().height(), 0, -margin);
            painter.setPen(color(owner, "border-normal", QPalette::Mid));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bounds);
            if (source->kind == Kind::spectrogram)
            {
                painter.drawImage(bounds, history[panel]);
                painter.setPen(color(owner, "text-primary", QPalette::WindowText));
                painter.drawText(bounds, Qt::AlignRight | Qt::AlignTop, QString::number(rate / 2) + " Hz");
                continue;
            }
            painter.save();
            painter.setClipRect(bounds);
            if (source->kind == Kind::oscilloscope)
            {
                painter.setPen(color(owner, "border-normal", QPalette::Mid));
                painter.drawLine(QPointF(bounds.left(), bounds.center().y()),
                                 QPointF(bounds.right(), bounds.center().y()));
                const unsigned first = separate ? panel : 0;
                const unsigned end = separate ? panel + 1 : source->channels;
                for (unsigned channel = first; channel < end; ++channel)
                {
                    QPainterPath path;
                    for (unsigned sample = 0; sample < 1024; ++sample)
                    {
                        const double value = wave[channel][(position + sample) % 1024];
                        const QPointF point(bounds.left() + sample * bounds.width() / 1023,
                                            bounds.center().y() - value * bounds.height() / 2);
                        if (sample == 0)
                            path.moveTo(point);
                        else
                            path.lineTo(point);
                    }
                    painter.setPen(channelColor(owner, channel));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPath(path);
                }
            }
            else if (source->kind == Kind::spectrumBars)
            {
                std::array<double, 512> spectrum{};
                for (unsigned bin = 0; bin < spectrum.size(); ++bin)
                    spectrum[bin] = power(panel, bin);
                const auto bands = Common::analysisSpectrumBands(spectrum);
                const double width = bounds.width() / bands.size();
                for (unsigned band = 0; band < bands.size(); ++band)
                {
                    const double value = bands[band];
                    const double db = value > 0 ? 10 * std::log10(value) : -90;
                    const double height = std::clamp((db + 90) / 90, 0.0, 1.0) * bounds.height();
                    painter.fillRect(QRectF(bounds.left() + (band + 0.1) * width, bounds.bottom() - height,
                                            width * 0.8, height),
                                     spectrumColor(value));
                }
            }
            else
            {
                const unsigned curves = overlay ? source->channels : 1;
                for (unsigned curve = 0; curve < curves; ++curve)
                {
                    QPainterPath path;
                    for (unsigned bin = 1; bin < 256; ++bin)
                    {
                        const double value = overlay ? powers[curve][bin * 2] : power(panel, bin * 2);
                        const double db = value > 0 ? 10 * std::log10(value) : -90;
                        const QPointF point(bounds.left() + bin * bounds.width() / 255,
                                            bounds.bottom() -
                                                std::clamp((db + 90) / 90, 0.0, 1.0) * bounds.height());
                        if (bin == 1)
                            path.moveTo(point);
                        else
                            path.lineTo(point);
                    }
                    painter.setPen(channelColor(owner, overlay ? curve : panel));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPath(path);
                }
            }
            painter.restore();
        }
    }
    void paintLoudness(QPainter &painter, const QRectF &area)
    {
        if (source->numeric.load())
            return;
        const auto snapshot = source->loudnessSnapshot();
        const std::array values{snapshot.momentaryLufs,   snapshot.shortTermLufs, snapshot.integratedLufs,
                                snapshot.loudnessRangeLu, snapshot.truePeakDbtp,  snapshot.samplePeakDbfs,
                                snapshot.rmsDbfs,         snapshot.lfePeakDbfs};
        const char *names[]{"M", "S", "I", "LRA", "dBTP", "SP", "RMS", "LFE"};
        const char *units[]{"LUFS", "LUFS", "LUFS", "LU", "dBTP", "dBFS", "dBFS", "dBFS"};
        std::optional<Common::MeterCustomLoudnessPreset> reference =
            Common::findCustomMeterLoudnessPreset(source->loudnessPreset);
        if (!reference)
            if (const auto *preset = Common::findMeterLoudnessPreset(source->loudnessPreset.toStdString()))
                reference = Common::MeterCustomLoudnessPreset{
                    QString::fromUtf8(preset->id), QString::fromUtf8(preset->displayName),
                    preset->targetIntegratedLufs, preset->alternateTargetHighLufs,
                    preset->maximumTruePeakDbtp};
        const bool numeric = source->numeric.load();
        const int margin = owner.style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
        const auto height = owner.fontMetrics().height();
        std::vector<unsigned> visible;
        for (unsigned index = 0; index < values.size(); ++index)
            if (source->metricMask.load() & (1u << index))
                visible.push_back(index);
        if (visible.empty())
            return;
        const unsigned columns = unsigned(visible.size());
        for (unsigned cellIndex = 0; cellIndex < visible.size(); ++cellIndex)
        {
            const unsigned index = visible[cellIndex];
            QRectF cell(area.left() + cellIndex * area.width() / columns, area.top(), area.width() / columns,
                        area.height());
            cell.adjust(margin / 2, margin / 2, -margin / 2, -margin / 2);
            painter.setPen(color(owner, "text-primary", QPalette::WindowText));
            painter.drawText(cell, Qt::AlignTop | Qt::AlignHCenter, QString::fromLatin1(names[index]));
            const auto value = values[index];
            const auto text = !value.valid                  ? QStringLiteral("—")
                              : !std::isfinite(value.value) ? QStringLiteral("−∞")
                                                            : QString::number(value.value, 'f', 1) + ' ' +
                                                                  QString::fromLatin1(units[index]);
            painter.drawText(cell, numeric ? Qt::AlignCenter : Qt::AlignBottom | Qt::AlignHCenter, text);
            if (numeric)
                continue;
            QRectF bar = cell.adjusted(0, height + margin, -cell.width() / 2, -height - margin);
            Controls::MeterStyle style;
            style.vertical = true;
            style.minimumDb = index == 3 ? 0 : -72;
            style.maximumDb = index == 3 ? 40 : 12;
            style.zones = {{style.maximumDb, color(owner, "accent-primary", QPalette::Highlight),
                            color(owner, "surface-raised", QPalette::Base)}};
            style.border = color(owner, "border-normal", QPalette::Mid);
            Controls::drawMeter(painter, bar, value.valid ? value.value : style.minimumDb, std::nullopt,
                                style);
            const auto line = [&](double target, const QString &label, const QString &token,
                                  Qt::PenStyle penStyle, bool below = false)
            {
                const double position = (target - style.minimumDb) / (style.maximumDb - style.minimumDb);
                const double y = bar.bottom() - std::clamp(position, 0.0, 1.0) * bar.height();
                QPen pen(color(owner, token, QPalette::Highlight));
                pen.setStyle(penStyle);
                painter.setPen(pen);
                painter.drawLine(QPointF(bar.left(), y), QPointF(cell.right(), y));
                painter.drawText(
                    QRectF(bar.right(), below ? y : y - height, cell.right() - bar.right(), height),
                    Qt::AlignRight | Qt::AlignVCenter, label + ' ' + QString::number(target, 'f', 0));
            };
            if (reference && index < 3)
            {
                line(reference->targetIntegratedLufs,
                     UI::localizedText(domain.toUtf8().constData(), "analysisReference", "Ref"),
                     "analysis-reference-target", Qt::DashLine,
                     reference->alternateTargetHighLufs.has_value());
                if (reference->alternateTargetHighLufs)
                    line(*reference->alternateTargetHighLufs,
                         UI::localizedText(domain.toUtf8().constData(), "analysisReferenceHigh", "High"),
                         "analysis-reference-upper", Qt::DotLine);
            }
            if (reference && index == 4)
                line(reference->maximumTruePeakDbtp,
                     UI::localizedText(domain.toUtf8().constData(), "analysisReferencePeak", "Max"),
                     "analysis-reference-peak", Qt::DashDotLine);
        }
    }
    void polar(QPainter &painter, const QRectF &area)
    {
        const QPointF center = area.center();
        const double radius =
            std::max(1.0, std::min(area.width(), area.height()) / 2 - owner.fontMetrics().height());
        drawMixerPolarGrid(painter, center, radius, "sel-main");
        const std::array<double, 8> angles{-30, 30, 0, 0, -110, 110, -150, 150};
        for (unsigned channel = 0; channel < source->channels; ++channel)
        {
            const bool lfe = source->channels >= 6 && channel == 3;
            const double angle = (source->channels == 1 ? 0 : angles[channel]) * std::numbers::pi / 180;
            const double length =
                Common::MixerPolarCurve::radiusForPower(rms[channel] * rms[channel]) * radius;
            const QPointF end(center.x() + std::sin(angle) * length, center.y() - std::cos(angle) * length);
            const QPointF label(center.x() + std::sin(angle) * radius, center.y() - std::cos(angle) * radius);
            painter.setPen(channelColor(owner, channel));
            painter.setBrush(Qt::NoBrush);
            if (lfe)
            {
                painter.drawEllipse(center, length / 5, length / 5);
                painter.drawText(QRectF(center, QSizeF(radius, owner.fontMetrics().height())),
                                 Qt::AlignCenter, "LFE");
            }
            else
            {
                painter.drawLine(center, end);
                painter.drawText(QRectF(label.x() - 25, label.y() - owner.fontMetrics().height(), 50,
                                        owner.fontMetrics().height()),
                                 Qt::AlignCenter, role(channel, source->channels));
            }
        }
    }
};
AudioAnalysisWidget::AudioAnalysisWidget(std::shared_ptr<Common::AnalysisCapture> source, QWidget *parent,
                                         const QString &domain)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this, std::move(source), domain))
{
}
AudioAnalysisWidget::~AudioAnalysisWidget() = default;
std::array<double, 8> AudioAnalysisWidget::rmsLevels() const
{
    return impl_->rms;
}
double AudioAnalysisWidget::dominantFrequency(unsigned channel) const
{
    if (channel >= impl_->source->channels)
        return 0;
    const auto &powers = impl_->powers[channel];
    const auto peak = std::max_element(powers.begin() + 1, powers.end());
    return std::distance(powers.begin(), peak) * double(impl_->rate) / 1024;
}
Common::MeterSnapshot AudioAnalysisWidget::loudnessSnapshot() const
{
    return impl_->source->loudnessSnapshot();
}
void AudioAnalysisWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    impl_->paint(painter);
}
void AudioAnalysisWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    impl_->timer.start();
}
void AudioAnalysisWidget::hideEvent(QHideEvent *event)
{
    impl_->timer.stop();
    QWidget::hideEvent(event);
}
} // namespace WinJACKNexus::UI
