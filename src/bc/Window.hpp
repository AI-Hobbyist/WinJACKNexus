// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QWidget>
#include <memory>

namespace WinJACKNexus::Broadcast
{
class BroadcastNode;
class BroadcastWindow final : public QWidget
{
  public:
    explicit BroadcastWindow(BroadcastNode &node);
    ~BroadcastWindow() override;

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace WinJACKNexus::Broadcast
