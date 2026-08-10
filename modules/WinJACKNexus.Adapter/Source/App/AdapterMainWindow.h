#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::adapter
{

/** 主窗口（DocumentWindow）。
 *  M1.1 将扩展：系统托盘图标、最小化挂载、关闭 → 托盘而非退出等行为。
 */
class AdapterMainWindow final : public juce::DocumentWindow
{
public:
    explicit AdapterMainWindow (const juce::String& name);
    ~AdapterMainWindow() override;

    // DocumentWindow
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdapterMainWindow)
};

} // namespace wjn::adapter
