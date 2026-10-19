// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/UI/LocalizedText.hpp>
namespace WinJACKNexus::UI
{
inline QString classicMixerDiagnostic(const QString &message, const char *domain = "classicMixer")
{
    struct Entry
    {
        const char *source;
        const char *key;
    };
    static constexpr Entry entries[]{
        {"Invalid layout", "diagnostic.invalidLayout"},
        {"Invalid strip count", "diagnostic.stripCount"},
        {"Invalid or duplicate channel name", "diagnostic.channelName"},
        {"Duplicate audio port name", "diagnostic.portName"},
        {"Invalid channel parameters", "diagnostic.channelParameters"},
        {"Invalid AUX send", "diagnostic.auxSend"},
        {"Invalid bus parameters", "diagnostic.busParameters"},
        {"Invalid AUX master", "diagnostic.auxMaster"},
        {"Too many presets", "diagnostic.presetLimit"},
        {"Invalid preset record", "diagnostic.presetRecord"},
        {"Unknown selected preset", "diagnostic.selectedPreset"},
        {"Unsupported state schema", "diagnostic.schema"},
        {"Malformed input state", "diagnostic.inputState"},
        {"Malformed AUX state", "diagnostic.auxState"},
        {"Malformed AUX values", "diagnostic.auxValues"},
        {"Malformed bus state", "diagnostic.busState"},
        {"Malformed preset library", "diagnostic.presetLibrary"},
        {"Malformed preset record", "diagnostic.presetMalformed"},
        {"Invalid preset Base64", "diagnostic.base64"},
        {"Preset not found", "diagnostic.notFound"},
        {"Invalid preset payload", "diagnostic.payload"},
        {"Preset layout mismatch", "diagnostic.layoutMismatch"},
        {"Layout is fixed after preparation", "diagnostic.fixedLayout"},
        {"Parameter queue is busy", "diagnostic.busy"},
        {"Port topology changed", "diagnostic.topology"},
        {"Port identity changed", "diagnostic.identity"},
        {"Connection snapshot failed", "diagnostic.snapshot"},
        {"Port rename or reconnection failed", "diagnostic.rename"},
        {"rollback incomplete", "diagnostic.rollback"},
        {"Enter a valid preset name.", "diagnostic.presetName"},
    };
    // Endpoint names on subsequent lines are user data, never translation keys.
    const auto newline = message.indexOf('\n');
    const auto first = newline < 0 ? message : message.left(newline);
    for (const auto &entry : entries)
    {
        if (first == QLatin1String(entry.source))
            return localizedText(domain, entry.key, entry.source) +
                   (newline < 0 ? QString() : message.mid(newline));
    }
    return message;
}
} // namespace WinJACKNexus::UI
