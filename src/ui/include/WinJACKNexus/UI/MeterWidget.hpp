#pragma once

#include <WinJACKNexus/UI/AnimatedWidget.hpp>

#include <array>
#include <cstdint>
#include <QString>
#include <QWidget>

#include <vector>

class QHBoxLayout;

namespace WinJACKNexus::UI {

class WINJACK_UI_API MeterWidget : public AnimatedWidget {
    Q_OBJECT

public:
    explicit MeterWidget(QWidget* parent = nullptr);
    ~MeterWidget() override = default;

    void setRange(float minimum, float maximum);
    void setThresholds(float warning, float danger);
    void setScaleMarks(const std::vector<float>& marks);
    void setValue(float value);
    void clearValue();
    void setChannelLabel(const QString& label);
    void setShowScaleLabels(bool show);

    float value() const noexcept;
    float peakValue() const noexcept;
    QSize sizeHint() const override;

protected:
    void paintContent(QPainter& painter) override;
    void advanceFrame() override;

private:
    float normalize(float value) const noexcept;
    float denormalize(float value) const noexcept;
    QString formatValue(float value) const;

    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    float warning_ = 0.7f;
    float danger_ = 0.9f;
    float level_ = 0.0f;
    float peak_ = 0.0f;
    int peakHoldFrames_ = 0;
    std::vector<float> scaleMarks_;
    QString channelLabel_;
    bool showScaleLabels_ = false;
};

class WINJACK_UI_API PeakMeterWidget final : public MeterWidget {
    Q_OBJECT

public:
    explicit PeakMeterWidget(QWidget* parent = nullptr);
    ~PeakMeterWidget() override = default;
};

class WINJACK_UI_API MeterArrayWidget : public QWidget {
public:
    explicit MeterArrayWidget(QWidget* parent = nullptr);
    ~MeterArrayWidget() override = default;

    void setChannelCount(std::uint16_t count);
    std::uint16_t channelCount() const noexcept;
    void setRange(float minimum, float maximum);
    void setThresholds(float warning, float danger);
    void setScaleMarks(const std::vector<float>& marks);
    void setShowScaleLabels(bool show);
    void setValues(const std::array<float, 16>& values, std::uint16_t count);
    void setValue(std::uint16_t channel, float value);
    void clear();
    QSize sizeHint() const override;

private:
    void configureMeter(MeterWidget& meter, std::size_t index);

    QHBoxLayout* layout_ = nullptr;
    std::vector<MeterWidget*> meters_;
    std::uint16_t channelCount_ = 0;
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    float warning_ = 0.7f;
    float danger_ = 0.9f;
    std::vector<float> scaleMarks_;
    bool showScaleLabels_ = false;
    QWidget* scaleWidget_ = nullptr;
};

class WINJACK_UI_API MidiVelocityWidget final : public MeterArrayWidget {
    Q_OBJECT

public:
    explicit MidiVelocityWidget(QWidget* parent = nullptr);
    ~MidiVelocityWidget() override = default;
};

}