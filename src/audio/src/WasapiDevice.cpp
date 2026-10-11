/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Audio/WasapiDevice.hpp>

#include <Windows.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

namespace WinJACKNexus::Audio
{

namespace
{

using Microsoft::WRL::ComPtr;

class ComScope final
{
  public:
    ComScope() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
          ownsInitialization_(result_ == S_OK || result_ == S_FALSE)
    {
    }

    ~ComScope() noexcept
    {
        if (ownsInitialization_)
            CoUninitialize();
    }

    bool isUsable() const noexcept
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT result() const noexcept
    {
        return result_;
    }

  private:
    HRESULT result_ = E_FAIL;
    bool ownsInitialization_ = false;
};

void copyString(wchar_t *destination, std::size_t capacity, const wchar_t *source) noexcept
{
    if (destination == nullptr || capacity == 0)
        return;
    destination[0] = L'\0';
    if (source != nullptr)
        wcsncpy_s(destination, capacity, source, _TRUNCATE);
}

EDataFlow dataFlow(WasapiStreamDirection direction) noexcept
{
    return direction == WasapiStreamDirection::capture ? eCapture : eRender;
}

bool makeDeviceInfo(IMMDevice *device, std::uint32_t index, bool isDefault,
                    WasapiStreamDirection direction, WasapiDeviceInfo &result) noexcept
{
    if (device == nullptr)
        return false;

    LPWSTR deviceId = nullptr;
    if (FAILED(device->GetId(&deviceId)))
        return false;
    result = {};
    result.index = index;
    result.isDefault = isDefault;
    result.isInput = direction == WasapiStreamDirection::capture;
    result.isOutput = direction == WasapiStreamDirection::render;
    copyString(result.id, std::size(result.id), deviceId);
    CoTaskMemFree(deviceId);

    ComPtr<IPropertyStore> propertyStore;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &propertyStore)))
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(propertyStore->GetValue(PKEY_Device_FriendlyName, &value)))
        {
            if (value.vt == VT_LPWSTR)
                copyString(result.name, std::size(result.name), value.pwszVal);
            else if (value.vt == VT_BSTR)
                copyString(result.name, std::size(result.name), value.bstrVal);
        }
        PropVariantClear(&value);
    }
    return result.id[0] != L'\0';
}

struct DefaultDeviceContext final
{
    WasapiDeviceInfo *result = nullptr;
    bool found = false;
};

bool selectDefault(const WasapiDeviceInfo &device, void *userData) noexcept
{
    auto *context = static_cast<DefaultDeviceContext *>(userData);
    if (context == nullptr || context->result == nullptr)
        return false;
    if (!device.isDefault)
        return true;
    *context->result = device;
    context->found = true;
    return false;
}

}

std::size_t WasapiDeviceEnumerator::enumerate(WasapiStreamDirection direction,
                                              WasapiDeviceVisitor visitor, void *userData) noexcept
{
    lastError_ = S_OK;
    try
    {
        ComScope com;
        if (!com.isUsable())
        {
            lastError_ = static_cast<std::int32_t>(com.result());
            return 0;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(&enumerator));
        if (FAILED(result))
        {
            lastError_ = static_cast<std::int32_t>(result);
            return 0;
        }

        ComPtr<IMMDevice> defaultDevice;
        std::wstring defaultId;
        if (SUCCEEDED(
                enumerator->GetDefaultAudioEndpoint(dataFlow(direction), eConsole, &defaultDevice)))
        {
            LPWSTR defaultIdBuffer = nullptr;
            if (SUCCEEDED(defaultDevice->GetId(&defaultIdBuffer)))
            {
                defaultId = defaultIdBuffer;
                CoTaskMemFree(defaultIdBuffer);
            }
        }

        ComPtr<IMMDeviceCollection> collection;
        result =
            enumerator->EnumAudioEndpoints(dataFlow(direction), DEVICE_STATE_ACTIVE, &collection);
        if (FAILED(result))
        {
            lastError_ = static_cast<std::int32_t>(result);
            return 0;
        }

        UINT count = 0;
        result = collection->GetCount(&count);
        if (FAILED(result))
        {
            lastError_ = static_cast<std::int32_t>(result);
            return 0;
        }

        std::size_t visited = 0;
        for (UINT index = 0; index < count; ++index)
        {
            ComPtr<IMMDevice> device;
            if (FAILED(collection->Item(index, &device)))
                continue;
            WasapiDeviceInfo info;
            if (!makeDeviceInfo(device.Get(), index, false, direction, info))
                continue;
            info.isDefault = !defaultId.empty() && defaultId == info.id;
            ++visited;
            if (visitor != nullptr && !visitor(info, userData))
                break;
        }
        return visited;
    }
    catch (...)
    {
        lastError_ = static_cast<std::int32_t>(E_OUTOFMEMORY);
        return 0;
    }
}

bool WasapiDeviceEnumerator::defaultDevice(WasapiStreamDirection direction,
                                           WasapiDeviceInfo &result) noexcept
{
    result = {};
    DefaultDeviceContext context{&result, false};
    enumerate(direction, selectDefault, &context);
    return context.found;
}

std::int32_t WasapiDeviceEnumerator::lastError() const noexcept
{
    return lastError_;
}

}