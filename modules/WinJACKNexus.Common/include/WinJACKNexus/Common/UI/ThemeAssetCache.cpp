#include "ThemeAssetCache.h"

namespace wjn::common
{

juce::Image ThemeAssetCache::loadImage(const juce::File& file)
{
    const auto key = file.getFullPathName();
    if (images.contains(key))
        return images[key];
    auto image = file.existsAsFile() ? juce::ImageFileFormat::loadFrom(file) : juce::Image();
    if (image.isValid() && image.getWidth() <= 4096 && image.getHeight() <= 4096)
        images.set(key, image);
    return image;
}

void ThemeAssetCache::clear() { images.clear(); }

} // namespace wjn::common
