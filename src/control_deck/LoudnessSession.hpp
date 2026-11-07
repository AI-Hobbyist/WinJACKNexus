// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Common/ControlDeckAnalysis.hpp>
#include <memory>
#include <mutex>
#include <thread>
namespace WinJACKNexus::ControlDeck
{
class LoudnessSession final
{
  public:
    explicit LoudnessSession(std::shared_ptr<Common::AnalysisCapture> source);
    ~LoudnessSession();
    void configure();

  private:
    struct Configuration
    {
        bool enabled{}, useGlobal{true};
        QString directory, instanceId, clientName;
    };
    std::shared_ptr<Common::AnalysisCapture> source_;
    std::mutex configurationMutex_;
    Configuration configuration_;
    std::jthread worker_;
    void run(std::stop_token stop);
};
} // namespace WinJACKNexus::ControlDeck
