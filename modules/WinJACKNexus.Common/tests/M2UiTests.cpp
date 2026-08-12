#include <WinJACKNexus/Common/Localization/LocaleManager.h>
#include <WinJACKNexus/Common/UI/ChannelCard.h>
#include <WinJACKNexus/Common/UI/FontManager.h>
#include <WinJACKNexus/Common/UI/MeterComponent.h>
#include <WinJACKNexus/Common/UI/MixerChannelStripComponent.h>
#include <WinJACKNexus/Common/UI/SpatialPannerComponent.h>
#include <WinJACKNexus/Common/UI/ThemeContext.h>
#include <WinJACKNexus/Common/UI/ThemePackage.h>

#include <cstdlib>
#include <iostream>

namespace
{
juce::File makeThemePackage()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("WinJACKNexus_M2_theme");
    directory.deleteRecursively();
    directory.createDirectory();
    directory.getChildFile("Common").createDirectory();
    directory.getChildFile("Adapter").createDirectory();
    directory.getChildFile("manifest.json").replaceWithText(R"({
        "schema":"WinJACKNexus.ThemePackage", "version":1,
        "defaultModule":"Adapter"
    })");
    directory.getChildFile("Common/theme.json").replaceWithText(R"({
        "schema":"WinJACKNexus.Theme", "version":1,
        "colors":{"accent":"#ff123456"}
    })");
    directory.getChildFile("Adapter/theme.json").replaceWithText(R"({
        "schema":"WinJACKNexus.Theme", "version":1,
        "metrics":{"panelRadius":3}
    })");

    const auto packageFile = directory.getSiblingFile("WinJACKNexus_M2_theme.netheme");
    packageFile.deleteFile();
    juce::ZipFile::Builder builder;
    builder.addFile(directory.getChildFile("manifest.json"), 0, "manifest.json");
    builder.addFile(directory.getChildFile("Common/theme.json"), 0, "Common/theme.json");
    builder.addFile(directory.getChildFile("Adapter/theme.json"), 0, "Adapter/theme.json");
    std::unique_ptr<juce::FileOutputStream> output(packageFile.createOutputStream());
    if (output == nullptr || ! builder.writeToStream(*output, nullptr))
        return {};
    output->flush();
    directory.deleteRecursively();
    return packageFile;
}
}

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "M2 UI test failure: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    wjn::common::ThemeContext theme;
    const auto json = juce::JSON::parse(R"({
        "schema":"WinJACKNexus.Theme", "version":1,
        "colors":{"accent":"#ff123456"},
        "metrics":{"panelRadius":3},
        "controls":{"defaultStyle":"flat","meter":{"style":"flat-segmented"}}
    })");
    require(theme.applyJson(json), "Theme JSON must load");
    require(theme.colour("accent") == juce::Colour(0xff123456), "Theme colour override must apply");
    require(theme.metric("panelRadius", 0.0f) == 3.0f, "Theme metric override must apply");
    require(theme.controlStyle("meter", "fallback") == "flat-segmented", "Meter style must apply");

        const auto packageFile = makeThemePackage();
        require(packageFile.existsAsFile(), "Theme package fixture must be created");
        wjn::common::ThemeContext packagedTheme;
        wjn::common::ThemePackage themePackage;
        juce::String themeError;
        require(themePackage.load(packageFile, packagedTheme, themeError), "Theme package must load");
        require(packagedTheme.colour("accent") == juce::Colour(0xff123456),
            "Common theme must load from package");
        require(packagedTheme.metric("panelRadius", 0.0f) == 3.0f,
            "Module theme must override Common theme");
        packageFile.deleteFile();

        const auto invalidTheme = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("WinJACKNexus_M2_invalid.lang");
        invalidTheme.replaceWithText(R"({
            "schema":"WinJACKNexus.Language", "version":1, "locale":"zh-CN",
            "strings":{}, "templates":{"broken":"{name"}
        })");
        wjn::common::TextCatalog invalidCatalog;
        require(! invalidCatalog.load(invalidTheme, themeError),
                "Invalid language placeholder must be rejected");
        invalidTheme.deleteFile();

    wjn::common::MeterComponent meter("PEAK", wjn::common::MeterComponent::MeterType::decibels);
    meter.setTheme(theme);
    meter.setValue(-3.0f);
    meter.setPreset(-23.0f, 1.0f, -1.0f);
    meter.setSize(64, 180);
    require(meter.getWidth() == 64, "Meter must accept a stable size");

    wjn::common::ChannelCard card(0);
    card.setTheme(theme);
    card.setPreset(-23.0f, 1.0f, -1.0f);
    card.setSize(560, 180);
    require(card.getChannelName().isNotEmpty(), "Channel card must have a default name");

    wjn::common::MixerChannelStripComponent strip("通道 1");
    strip.setTheme(theme);
    strip.setGain(0.75f);
    strip.setMeter(0.8f, 0.5f, false);
    strip.setSize(96, 320);

    wjn::common::SpatialPannerComponent panner(true);
    panner.setTheme(theme);
    panner.setPosition(0.25f, 0.75f);
    require(panner.getPosition().x == 0.25f && panner.getPosition().y == 0.75f,
            "Spatial panner must clamp and expose position");

    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("WinJACKNexus_M2_zh-CN.lang");
    temp.replaceWithText(R"({
        "schema":"WinJACKNexus.Language", "version":1,
        "locale":"zh-CN", "displayName":"\u7b80\u4f53\u4e2d\u6587", "module":"Common",
        "strings":{"common.action.close":"\u5173\u95ed"},
        "templates":{"common.status.connectedTo":"\u5df2\u8fde\u63a5\u5230 {name}"}
    })");
    wjn::common::LocaleManager locale;
    juce::String error;
    require(locale.load(temp, {}, error), "Chinese language catalog must load");
    require(locale.text("common.action.close", "关闭") == "关闭", "Chinese text must resolve");
    temp.deleteFile();

        const auto projectRoot = juce::File(WINJACKNEXUS_SOURCE_DIR);
        wjn::common::FontManager fonts;
        require(fonts.loadBuiltIns(projectRoot.getChildFile("LCD"), error),
            "Project LCD fonts must load");
        require(fonts.getTypeface("common:lcd-zpix") != nullptr,
            "zpix logical font must resolve");
        require(fonts.getTypeface("common:lcd-ds-digi") != nullptr,
            "DS-DIGI logical font must resolve");

        wjn::common::LocaleManager fallbackLocale;
        require(fallbackLocale.load(projectRoot.getChildFile("locales/zh-CN.lang"),
                    projectRoot.getChildFile("locales/Adapter/zh-CN.lang"), error),
            "Project language catalogs must load");
        require(fallbackLocale.text("common.action.close", "") == "关闭",
            "Common language catalog must resolve");
        require(fallbackLocale.text("adapter.action.addDevice", "") == "添加设备",
            "Adapter language catalog must resolve");
            require(fallbackLocale.catalog().hasKey("common.action.close"),
                "Locale catalog must report known keys");
            bool changed = false;
            fallbackLocale.setChangeCallback([&] { changed = true; });
            require(fallbackLocale.load(projectRoot.getChildFile("locales/zh-CN.lang"), {}, error),
                "Locale reload must succeed");
            require(changed, "Locale reload must notify listeners");

    std::cout << "M2 UI tests passed\n";
    return 0;
}
