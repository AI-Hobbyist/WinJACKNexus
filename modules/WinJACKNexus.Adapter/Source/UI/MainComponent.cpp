#include "MainComponent.h"

namespace wjn::adapter
{

MainComponent::MainComponent()
{
    // 骨架阶段空组件；M1.1 起在此挂载 TabbedComponent。
}

MainComponent::~MainComponent() = default;

void MainComponent::paint (juce::Graphics& g)
{
    // 骨架占位背景（深色）。M1.1 起统一使用 Theme.h 色板，禁止硬编码。
    g.fillAll (juce::Colour (0xff121316));
}

void MainComponent::resized()
{
}

} // namespace wjn::adapter
