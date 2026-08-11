#include "ThemePackage.h"

namespace wjn::common
{

bool ThemePackage::validatePath(const juce::String& path) noexcept
{
    return path.isNotEmpty() && ! path.contains("..") && ! path.startsWithChar('/')
        && ! path.startsWithChar('\\');
}

bool ThemePackage::load(const juce::File& file, ThemeContext& context, juce::String& error) const
{
    error.clear();
    if (!file.existsAsFile())
    {
        error = "主题文件不存在";
        return false;
    }

    juce::var json;
    if (file.getFileExtension().equalsIgnoreCase(".netheme"))
    {
        juce::ZipFile archive(file);
        if (archive.getNumEntries() <= 0)
        {
            error = "主题包为空或无法读取";
            return false;
        }
        for (int index = 0; index < archive.getNumEntries(); ++index)
        {
            const auto* archiveEntry = archive.getEntry(index);
            if (archiveEntry == nullptr || ! validatePath (archiveEntry->filename))
            {
                error = "主题包包含非法路径";
                return false;
            }
        }
        const auto* entry = archive.getEntry("Common/theme.json");
        if (entry == nullptr)
            entry = archive.getEntry("theme.json");
        if (entry == nullptr)
        {
            error = "主题包缺少 Common/theme.json";
            return false;
        }
        auto stream = std::unique_ptr<juce::InputStream>(archive.createStreamForEntry(*entry));
        if (stream == nullptr)
        {
            error = "无法读取主题配置";
            return false;
        }
        json = juce::JSON::parse(stream->readEntireStreamAsString());
    }
    else
    {
        json = juce::JSON::parse(file);
    }

    if (!context.applyJson(json))
    {
        error = "主题版本或格式无效";
        return false;
    }
    return true;
}

} // namespace wjn::common
