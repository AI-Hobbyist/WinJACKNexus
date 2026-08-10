#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::adapter
{

/** 顶层内容组件。
 *
 *  骨架阶段：仅填充暗色背景的空窗口。
 *  M1.1 将实现：TabbedComponent（Physical Audio / Virtual Playback / System MIDI），
 *  每页内 In | Out 上下分割；并接入 WinJACKNexus.Common 的 Theme 色板。
 */
class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    // Component
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace wjn::adapter
