#include "FontManager.h"

namespace wjn::common
{

bool FontManager::loadBuiltIns(const juce::File& lcdDirectory, juce::String& error)
{
    error.clear();
    const auto zpixFile = lcdDirectory.getChildFile("zpix.ttf");
    const auto dsDigiFile = lcdDirectory.getChildFile("DS-DIGI.TTF");
    if (zpixFile.existsAsFile())
    {
        juce::MemoryBlock data;
        zpixFile.loadFileAsData(data);
        zpix = juce::Typeface::createSystemTypefaceFor(data.getData(), data.getSize());
    }
    if (dsDigiFile.existsAsFile())
    {
        juce::MemoryBlock data;
        dsDigiFile.loadFileAsData(data);
        dsDigi = juce::Typeface::createSystemTypefaceFor(data.getData(), data.getSize());
    }
    if (zpix == nullptr && dsDigi == nullptr)
    {
        error = "LCD 字体不可用，将回退到系统字体";
        return false;
    }
    return true;
}

bool FontManager::loadOverride(const juce::String& logicalId, const juce::File& file, juce::String& error)
{
    error.clear();
    if (logicalId != "common:lcd-zpix" && logicalId != "common:lcd-ds-digi")
    {
        error = "未知字体逻辑 ID";
        return false;
    }
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 8 * 1024 * 1024)
    {
        error = "字体文件不存在或大小无效";
        return false;
    }
    juce::MemoryBlock data;
    if (! file.loadFileAsData(data))
    {
        error = "无法读取字体文件";
        return false;
    }
    auto typeface = juce::Typeface::createSystemTypefaceFor(data.getData(), data.getSize());
    if (typeface == nullptr)
    {
        error = "字体格式无效";
        return false;
    }
    if (logicalId == "common:lcd-zpix")
        zpix = typeface;
    else
        dsDigi = typeface;
    return true;
}

juce::Typeface::Ptr FontManager::getTypeface(const juce::String& logicalId) const
{
    if (logicalId == "common:lcd-zpix" && zpix != nullptr)
        return zpix;
    if (logicalId == "common:lcd-ds-digi" && dsDigi != nullptr)
        return dsDigi;
    return logicalId == "common:lcd-ds-digi" ? dsDigi : zpix;
}

juce::Font FontManager::getFont(const juce::String& logicalId, float height, int style) const
{
    if (auto typeface = getTypeface(logicalId))
        return juce::Font(juce::FontOptions(typeface).withPointHeight(height).withStyleFlags(style));
    return juce::Font(juce::FontOptions(juce::Font::getSystemUIFontName(), height, style));
}

} // namespace wjn::common
