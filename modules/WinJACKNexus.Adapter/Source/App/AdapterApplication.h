#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::adapter
{

class AdapterMainWindow;

/** Adapter 应用本体（JUCEApplication 子类）。
 *
 *  骨架阶段：仅创建/销毁主窗口。
 *  M1.1 将补充：
 *    - 单实例锁（Named Mutex "WinJACK_Nexus_Adapter_Lock"，唤醒既有窗口）
 *    - 系统托盘挂载
 */
class AdapterApplication final : public juce::JUCEApplication
{
public:
    AdapterApplication() = default;
    ~AdapterApplication() override;

    // JUCEApplication
    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;
    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String& commandLine) override;

private:
    std::unique_ptr<AdapterMainWindow> mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdapterApplication)
};

} // namespace wjn::adapter
