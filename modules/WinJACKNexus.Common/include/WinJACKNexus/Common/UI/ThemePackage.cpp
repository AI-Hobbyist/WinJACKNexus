#include "ThemePackage.h"

namespace wjn::common
{

namespace
{
constexpr int maxEntries = 128;
constexpr juce::int64 maxEntryBytes = 8 * 1024 * 1024;
constexpr juce::int64 maxArchiveBytes = 64 * 1024 * 1024;
constexpr int maxJsonDepth = 16;
constexpr int maxIndexedResources = 128;

bool validateJsonDepth(const juce::var& value, int depth)
{
    if (depth > maxJsonDepth)
        return false;
    if (const auto* object = value.getDynamicObject())
    {
        for (const auto& property : object->getProperties())
            if (! validateJsonDepth(property.value, depth + 1))
                return false;
    }
    else if (const auto* array = value.getArray())
    {
        for (const auto& item : *array)
            if (! validateJsonDepth(item, depth + 1))
                return false;
    }
    return true;
}

bool validateResourceIndex(const juce::var& manifest)
{
    const auto resources = manifest.getProperty("resources", {});
    if (! resources.isVoid())
    {
        const auto* array = resources.getArray();
        if (array == nullptr || array->size() > maxIndexedResources)
            return false;
        for (const auto& item : *array)
            if (! item.isString() || ! ThemePackage::validatePath(item.toString()))
                return false;
    }
    if (const auto* fonts = manifest.getProperty("fonts", {}).getDynamicObject())
        for (const auto& property : fonts->getProperties())
            if (! property.value.isString() || ! ThemePackage::validatePath(property.value.toString()))
                return false;
    return true;
}

bool loadJsonEntry(juce::ZipFile& archive, const juce::String& path, juce::var& result)
{
    const auto* entry = archive.getEntry(path);
    if (entry == nullptr || entry->uncompressedSize > maxEntryBytes)
        return false;

    auto stream = std::unique_ptr<juce::InputStream>(archive.createStreamForEntry(*entry));
    if (stream == nullptr)
        return false;

    result = juce::JSON::parse(stream->readEntireStreamAsString());
    return ! result.isVoid();
}
}

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
        if (archive.getNumEntries() <= 0 || archive.getNumEntries() > maxEntries
            || file.getSize() > maxArchiveBytes)
        {
            error = "主题包为空、过大或无法读取";
            return false;
        }
        for (int index = 0; index < archive.getNumEntries(); ++index)
        {
            const auto* archiveEntry = archive.getEntry(index);
            if (archiveEntry == nullptr || archiveEntry->uncompressedSize > maxEntryBytes
                || ! validatePath (archiveEntry->filename))
            {
                error = "主题包包含非法路径或过大文件";
                return false;
            }
        }

        juce::var manifest;
        if (! loadJsonEntry(archive, "manifest.json", manifest)
            || ! manifest.isObject()
            || manifest.getProperty("schema", {}).toString() != "WinJACKNexus.ThemePackage"
            || static_cast<int>(manifest.getProperty("version", 0)) != 1
            || ! validateJsonDepth(manifest, 0)
            || ! validateResourceIndex(manifest))
        {
            error = "主题包 manifest 缺失或版本无效";
            return false;
        }

        juce::var commonTheme;
        if (! loadJsonEntry(archive, "Common/theme.json", commonTheme)
            && ! loadJsonEntry(archive, "theme.json", commonTheme))
        {
            error = "主题包缺少 Common/theme.json";
            return false;
        }

        if (! validateJsonDepth(commonTheme, 0) || ! context.applyJson(commonTheme))
        {
            error = "主题版本或格式无效";
            return false;
        }

        if (const auto module = manifest.getProperty("defaultModule", {}).toString(); module.isNotEmpty())
        {
            juce::var moduleTheme;
            if (loadJsonEntry(archive, module + "/theme.json", moduleTheme)
                && (! validateJsonDepth(moduleTheme, 0) || ! context.applyJson(moduleTheme)))
            {
                error = "模块主题版本或格式无效";
                return false;
            }
        }
        return true;
    }
    else
    {
        json = juce::JSON::parse(file);
    }

    if (! validateJsonDepth(json, 0) || ! context.applyJson(json))
    {
        error = "主题版本或格式无效";
        return false;
    }
    return true;
}

} // namespace wjn::common
