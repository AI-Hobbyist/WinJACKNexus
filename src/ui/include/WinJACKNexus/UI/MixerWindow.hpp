#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Common/MixerPolarAnalysis.hpp>

#include <QMainWindow>

#include <memory>
#include <functional>

namespace WinJACKNexus::UI {

class WINJACK_UI_API MixerWindow final : public QMainWindow {
public:
    using TopologyCommit = std::function<bool(const Common::DigitalMixerState&, QString*)>;
    using MeterRead = std::function<float(std::uint64_t, bool)>;
    using PolarRead = std::function<bool(std::uint64_t, std::uint64_t,
                                         Common::MixerPolarSnapshot&)>;
    using ChannelMeterRead = std::function<float(std::uint64_t, bool, std::size_t)>;
    using EffectAnalysisRead = std::function<bool(std::uint64_t, std::size_t,
        float[8], float[8], float[8], void*, std::uint32_t)>;
    explicit MixerWindow(QWidget* parent = nullptr);
    MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit, QWidget* parent = nullptr);
    MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit, MeterRead meterRead,
                QWidget* parent = nullptr);
    MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit, MeterRead meterRead,
                PolarRead polarRead, QWidget* parent = nullptr);
    MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit, MeterRead meterRead,
                PolarRead polarRead, ChannelMeterRead channelMeterRead,
                QWidget* parent = nullptr);
    MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit, MeterRead meterRead,
                PolarRead polarRead, ChannelMeterRead channelMeterRead,
                EffectAnalysisRead effectAnalysisRead, QWidget* parent = nullptr);
    ~MixerWindow() override;

    bool channelMixToMain(int index) const;
    double mainInputLevelDb() const;
    double mainOutputLevelDb() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
