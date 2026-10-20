/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include "Service.hpp"
#include <memory>

namespace WinJACKNexus::SignalSwitch
{
class WJN_SWITCH_API Sound
{
  public:
    Sound();
    ~Sound();
    bool load(const QString &path, bool restoring = false);
    bool poll();
    void trigger();
    void stop();
    bool ready() const;
    bool loading() const;
    bool playing() const;
    QString path() const;
    QString error() const;
    void render(float *left, float *right, unsigned frames, double rate) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace WinJACKNexus::SignalSwitch
