// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QWidget>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <WinJACKNexus/UI/Export.hpp>
#include <functional>
#include <memory>

namespace WinJACKNexus::UI
{
class WINJACK_UI_API ClassicMixerWindow final : public QWidget
{
  public:
    using State = Common::ClassicMixer::State;
    using Commit = std::function<bool(const State &, QString *)>;
    using MeterReader = std::function<Common::ClassicMixer::Levels()>;
    ClassicMixerWindow(const State &state, Commit commit, MeterReader meterReader);
    ~ClassicMixerWindow() override;
    void setState(const State &state);
    void setClientName(const QString &name);
    static bool chooseLayout(State &state, QWidget *parent = nullptr);

  protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace WinJACKNexus::UI
