/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#ifndef WJN_ENABLE_VST2
#    define WJN_ENABLE_VST2 0
#endif

#if WJN_ENABLE_VST2
#    include "pluginterfaces/vst2.x/aeffectx.h"
#endif

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::wstring wideFromUtf8(std::string_view value)
{
    if (value.empty())
        return {};
    const auto length = MultiByteToWideChar(CP_UTF8,
                                            MB_ERR_INVALID_CHARS,
                                            value.data(),
                                            static_cast<int>(value.size()),
                                            nullptr,
                                            0);
    if (length <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        result.data(),
                        length);
    return result;
}

std::string utf8FromWide(std::wstring_view value)
{
    if (value.empty())
        return {};
    const auto length = WideCharToMultiByte(CP_UTF8,
                                            WC_ERR_INVALID_CHARS,
                                            value.data(),
                                            static_cast<int>(value.size()),
                                            nullptr,
                                            0,
                                            nullptr,
                                            nullptr);
    if (length <= 0)
        return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8,
                        WC_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        result.data(),
                        length,
                        nullptr,
                        nullptr);
    return result;
}

std::string utf8FromLocal(const char* value)
{
    if (value == nullptr || *value == '\0')
        return {};
    const auto wideLength = MultiByteToWideChar(CP_ACP,
                                                0,
                                                value,
                                                -1,
                                                nullptr,
                                                0);
    if (wideLength <= 1)
        return {};
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value, -1, wide.data(), wideLength);
    wide.pop_back();
    return utf8FromWide(wide);
}

std::string pathString(const fs::path& path)
{
    return utf8FromWide(path.wstring());
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [] (unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string firstSubcategory(const std::string& value)
{
    const auto separator = value.find('|');
    return value.substr(0, separator);
}

std::string jsonString(std::string_view value)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20) {
                result += "\\u00";
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 0x0f]);
            } else {
                result.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

std::string jsonBool(bool value)
{
    return value ? "true" : "false";
}

std::string jsonInteger(std::int64_t value)
{
    return std::to_string(value);
}

std::string jsonObject(std::initializer_list<std::pair<std::string, std::string>> fields)
{
    std::string result = "{";
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first)
            result.push_back(',');
        first = false;
        result += jsonString(key);
        result.push_back(':');
        result += value;
    }
    result.push_back('}');
    return result;
}

std::string jsonArray(const std::vector<std::string>& values)
{
    std::string result = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            result.push_back(',');
        result += values[index];
    }
    result.push_back(']');
    return result;
}

bool writeFileAtomically(const fs::path& outputPath, std::string_view content)
{
    const auto temporaryPath = fs::path(outputPath.wstring()
                                        + L".tmp-"
                                        + std::to_wstring(GetCurrentProcessId()));
    const auto handle = CreateFileW(temporaryPath.wstring().c_str(),
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_TEMPORARY,
                                    nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    bool success = true;
    const auto* data = content.data();
    auto remaining = content.size();
    while (remaining > 0) {
        const auto chunkSize = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(handle, data, chunkSize, &written, nullptr) || written != chunkSize) {
            success = false;
            break;
        }
        data += written;
        remaining -= written;
    }
    if (success)
        success = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    if (!success) {
        DeleteFileW(temporaryPath.wstring().c_str());
        return false;
    }

    if (MoveFileExW(temporaryPath.wstring().c_str(),
                    outputPath.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temporaryPath.wstring().c_str());
    return false;
}

std::string classIdString(const VST3::UID& classId)
{
    return classId.toString(false);
}

bool parseClassId(const std::string& value, VST3::UID& classId)
{
    const auto parsed = VST3::UID::fromString(value, false);
    if (!parsed)
        return false;
    classId = *parsed;
    return true;
}

struct Vst3ClassDescriptor final {
    VST3::UID classId {};
    std::string name;
    std::string vendor;
    std::string version;
    std::string category;
    std::string subcategory;
};

class Vst3Module final {
public:
    bool open(const fs::path& path, std::string& error)
    {
        module = VST3::Hosting::Module::create(pathString(path), error);
        if (module == nullptr)
            return false;
        module->getFactory().setHostContext(&hostApplication);
        return true;
    }

    std::vector<Vst3ClassDescriptor> classes(std::string& error) const
    {
        if (module == nullptr)
            return {};
        std::vector<Vst3ClassDescriptor> result;
        for (const auto& classInfo : module->getFactory().classInfos()) {
            Vst3ClassDescriptor descriptor;
            descriptor.classId = classInfo.ID();
            descriptor.name = classInfo.name();
            descriptor.vendor = classInfo.vendor();
            descriptor.version = classInfo.version();
            descriptor.category = classInfo.category();
            descriptor.subcategory = firstSubcategory(classInfo.subCategoriesString());

            const auto categoryLower = lowerAscii(descriptor.category);
            if (categoryLower.find("controller") != std::string::npos
                || categoryLower.find("service") != std::string::npos
                || categoryLower.find("compatibility") != std::string::npos)
                continue;
            result.push_back(std::move(descriptor));
        }
        if (result.empty())
            error = "VST3 factory 没有返回可用 component class";
        return result;
    }

    VST3::Hosting::Module::Ptr module;
    Steinberg::Vst::HostApplication hostApplication;
};

struct PluginInfo final {
    std::string name;
    std::string vendor;
    std::string version;
    std::string category;
    std::string subcategory;
    std::string internalPlugin;
    std::string classId;
    bool hasUniqueId {};
    std::int64_t uniqueId {};
};

std::string pluginJson(const PluginInfo& plugin)
{
    std::vector<std::pair<std::string, std::string>> fields {
        { "name", jsonString(plugin.name) },
        { "vendor", jsonString(plugin.vendor) },
        { "version", jsonString(plugin.version) },
        { "category", jsonString(plugin.category) },
        { "subcategory", jsonString(plugin.subcategory) },
        { "internal_plugin", jsonString(plugin.internalPlugin) }
    };
    if (!plugin.classId.empty())
        fields.emplace_back("class_id", jsonString(plugin.classId));
    if (plugin.hasUniqueId)
        fields.emplace_back("unique_id", jsonInteger(plugin.uniqueId));

    std::string result = "{";
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            result.push_back(',');
        result += jsonString(fields[index].first);
        result.push_back(':');
        result += fields[index].second;
    }
    result.push_back('}');
    return result;
}

std::string descriptorJson(const Vst3ClassDescriptor& descriptor)
{
    return jsonObject({
        { "class_id", jsonString(classIdString(descriptor.classId)) },
        { "name", jsonString(descriptor.name) },
        { "vendor", jsonString(descriptor.vendor) },
        { "version", jsonString(descriptor.version) },
        { "category", jsonString(descriptor.category) },
        { "subcategory", jsonString(descriptor.subcategory) }
    });
}

std::string probeResult(const char* type,
                        const std::vector<std::string>& classes,
                        bool isShell,
                        std::string_view probeError = {})
{
    std::vector<std::pair<std::string, std::string>> fields {
        { "ok", "true" },
        { "type", jsonString(type) }
    };
    if (isShell)
        fields.emplace_back("is_shell", "true");
    fields.emplace_back("classes", jsonArray(classes));
    if (!probeError.empty())
        fields.emplace_back("probe_error", jsonString(probeError));

    std::string result = "{";
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            result.push_back(',');
        result += jsonString(fields[index].first);
        result.push_back(':');
        result += fields[index].second;
    }
    result.push_back('}');
    return result;
}

PluginInfo makeVst3ClassInfo(const Vst3ClassDescriptor& descriptor, const std::string& classId)
{
    PluginInfo plugin;
    plugin.name = descriptor.name;
    plugin.vendor = descriptor.vendor;
    plugin.version = descriptor.version;
    plugin.category = descriptor.category;
    plugin.subcategory = descriptor.subcategory;
    plugin.classId = classId;
    return plugin;
}

bool probeVst3Factory(const fs::path& path, std::string& output, std::string& error)
{
    Vst3Module module;
    if (!module.open(path, error))
        return false;
    const auto descriptors = module.classes(error);
    std::vector<std::string> classes;
    classes.reserve(descriptors.size());
    for (const auto& descriptor : descriptors)
        classes.push_back(descriptorJson(descriptor));
    if (classes.empty())
        return false;
    output = probeResult("vst3", classes, false);
    return true;
}

bool probeVst3Class(const fs::path& path,
                    const std::string& classIdText,
                    std::string& output,
                    std::string& error)
{
    Vst3Module module;
    if (!module.open(path, error))
        return false;
    VST3::UID requestedId {};
    if (!parseClassId(classIdText, requestedId)) {
        error = "VST3 class ID 无效: " + classIdText;
        return false;
    }

    const auto descriptors = module.classes(error);
    const auto descriptor = std::find_if(descriptors.cbegin(),
                                         descriptors.cend(),
                                         [&] (const Vst3ClassDescriptor& value) {
                                             return value.classId == requestedId;
                                         });
    if (descriptor == descriptors.cend()) {
        error = "VST3 factory 找不到 class ID: " + classIdText;
        return false;
    }

    auto plugin = makeVst3ClassInfo(*descriptor, classIdText);
    auto component = module.module->getFactory().createInstance<Steinberg::Vst::IComponent>(descriptor->classId);
    if (!component) {
        error = "VST3 component class 创建失败: " + descriptor->name;
        output = probeResult("vst3", { pluginJson(plugin) }, false, error);
        return true;
    }

    const auto initialized = component->initialize(&module.hostApplication) == Steinberg::kResultOk;
    if (!initialized) {
        error = "VST3 component initialize 失败: " + descriptor->name;
    } else {
        component->terminate();
    }
    output = probeResult("vst3", { pluginJson(plugin) }, false, error);
    return true;
}

#if WJN_ENABLE_VST2
VstIntPtr currentVst2UniqueId = 0;

VstIntPtr VSTCALLBACK vst2HostCallback(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode == audioMasterCurrentId)
        return currentVst2UniqueId;
    return 0;
}

using Vst2EntryProc = AEffect* (VSTCALLBACK*)(audioMasterCallback);

std::string readVst2String(AEffect* effect, AEffectOpcodes opcode, int maxLength)
{
    if (effect == nullptr || effect->dispatcher == nullptr)
        return {};
    std::vector<char> buffer(static_cast<std::size_t>(maxLength) + 1, '\0');
    effect->dispatcher(effect, opcode, 0, 0, buffer.data(), 0.0F);
    return utf8FromLocal(buffer.data());
}

PluginInfo makeVst2Info(const std::string& name,
                        const std::string& vendor,
                        int version,
                        bool isInstrument,
                        const std::string& internalPlugin,
                        VstIntPtr uniqueId = 0)
{
    PluginInfo result;
    result.name = name;
    result.vendor = vendor;
    result.version = std::to_string(version);
    result.category = isInstrument ? "instrument" : "effect";
    result.internalPlugin = internalPlugin;
    if (uniqueId != 0) {
        result.hasUniqueId = true;
        result.uniqueId = static_cast<std::int64_t>(uniqueId);
    }
    return result;
}

bool probeVst2(const fs::path& path,
               const std::string& internalId,
               std::string& output,
               std::string& error,
               bool& skippable)
{
    skippable = false;
    const auto module = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        error = "LoadLibraryW 失败";
        return false;
    }
    auto entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module, "VSTPluginMain"));
    if (entry == nullptr)
        entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module, "main"));
    if (entry == nullptr) {
        error = "VST2 模块没有 VSTPluginMain/main";
        FreeLibrary(module);
        return false;
    }

    bool idOk = false;
    char* end = nullptr;
    const auto requestedId = std::strtoll(internalId.c_str(), &end, 10);
    idOk = !internalId.empty() && end != internalId.c_str() && *end == '\0';
    currentVst2UniqueId = idOk ? static_cast<VstIntPtr>(requestedId) : 0;
    auto* effect = entry(vst2HostCallback);
    currentVst2UniqueId = 0;
    if (effect == nullptr || effect->magic != kEffectMagic || effect->dispatcher == nullptr) {
        error = "VST2 模块返回无效 AEffect";
        skippable = true;
        FreeLibrary(module);
        return false;
    }
    if (effect->dispatcher != nullptr)
        effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0F);

    const auto product = readVst2String(effect,
                                        static_cast<AEffectOpcodes>(effGetProductString),
                                        kVstMaxProductStrLen);
    const auto productName = product.empty() ? utf8FromWide(path.stem().wstring()) : product;
    const auto vendor = readVst2String(effect,
                                       static_cast<AEffectOpcodes>(effGetVendorString),
                                       kVstMaxVendorStrLen);
    std::vector<std::string> plugins;
    const auto category = effect->dispatcher == nullptr
        ? kPlugCategUnknown
        : effect->dispatcher(effect, static_cast<AEffectOpcodes>(effGetPlugCategory), 0, 0, nullptr, 0.0F);
    const auto isInstrument = (effect->flags & effFlagsIsSynth) != 0
        || category == kPlugCategSynth;
    if (category == kPlugCategShell && effect->dispatcher != nullptr && !idOk) {
        char internalName[kVstMaxProductStrLen + 1] {};
        while (true) {
            std::memset(internalName, 0, sizeof(internalName));
            const auto uniqueId = effect->dispatcher(effect,
                                                     static_cast<AEffectOpcodes>(effShellGetNextPlugin),
                                                     0,
                                                     0,
                                                     internalName,
                                                     0.0F);
            if (uniqueId == 0)
                break;
            const auto name = utf8FromLocal(internalName);
            if (name.empty())
                break;
            plugins.push_back(pluginJson(makeVst2Info(name,
                                                       vendor,
                                                       effect->version,
                                                       isInstrument,
                                                       name,
                                                       uniqueId)));
        }
    }
    if (plugins.empty())
        plugins.push_back(pluginJson(makeVst2Info(productName,
                                                   vendor,
                                                   effect->version,
                                                   isInstrument,
                                                   {},
                                                   idOk ? static_cast<VstIntPtr>(requestedId) : 0)));
    if (effect->dispatcher != nullptr)
        effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0F);
    FreeLibrary(module);
    output = probeResult("vst2",
                         plugins,
                         category == kPlugCategShell && !idOk);
    return true;
}
#endif

std::string failure(std::string_view message, bool skippable)
{
    return jsonObject({
        { "ok", "false" },
        { "error", jsonString(message) },
        { "skippable", jsonBool(skippable) }
    });
}

}

int wmain(int argc, wchar_t* argv[])
{
    const auto comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    const auto validType = argc >= 2
        && (std::wstring_view(argv[1]) == L"--vst3"
            || std::wstring_view(argv[1]) == L"--vst2");
    const auto listMode = argc == 5 && std::wstring_view(argv[3]) == L"--output";
    const auto classMode = argc == 7
        && (std::wstring_view(argv[3]) == L"--class-id"
            || std::wstring_view(argv[3]) == L"--internal-id")
        && std::wstring_view(argv[5]) == L"--output";
    if (!validType || (!listMode && !classMode)) {
        if (comInitialized)
            CoUninitialize();
        return 2;
    }

    const auto format = std::wstring_view(argv[1]);
    const auto path = fs::path(argv[2]);
    const auto selectedId = classMode ? utf8FromWide(argv[4]) : std::string();
    const auto outputPath = fs::path(argv[classMode ? 6 : 4]);
    std::string error;
    std::string output;
    bool skippable = false;
    bool success = false;
    if (format == L"--vst3") {
        success = classMode
            ? probeVst3Class(path, selectedId, output, error)
            : probeVst3Factory(path, output, error);
    } else {
#if WJN_ENABLE_VST2
        success = probeVst2(path,
                    classMode ? selectedId : std::string(),
                    output,
                    error,
                    skippable);
#else
        error = "VST2 探测未启用";
#endif
    }
    if (!success)
        output = failure(error.empty() ? "插件探测失败" : error, skippable);
    const auto outputSuccess = writeFileAtomically(outputPath, output);
    if (comInitialized)
        CoUninitialize();
    return outputSuccess ? (success ? 0 : 1) : 3;
}