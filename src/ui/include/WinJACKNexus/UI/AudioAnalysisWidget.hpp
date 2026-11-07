// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QWidget>
#include <WinJACKNexus/Common/ControlDeckAnalysis.hpp>
#include <WinJACKNexus/Common/MeterSnapshot.hpp>
#include <WinJACKNexus/UI/Export.hpp>
#include <memory>

namespace WinJACKNexus::UI
{
// One widget consumes one capture. The shared capture keeps pending GUI events
// safe when a graph node is removed. No analysis runs in the audio callback.
class WINJACK_UI_API AudioAnalysisWidget final : public QWidget
{
  public:
    explicit AudioAnalysisWidget(std::shared_ptr<Common::AnalysisCapture> capture, QWidget *parent = nullptr,
                                 const QString &domain = QStringLiteral("ui"));
    ~AudioAnalysisWidget() override;
    std::array<double, 8> rmsLevels() const;
    double dominantFrequency(unsigned channel) const;
    double overloadDb() const;
    Common::MeterSnapshot loudnessSnapshot() const;

  protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace WinJACKNexus::UI
