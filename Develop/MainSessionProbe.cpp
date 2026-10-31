/* SPDX-License-Identifier: AGPL-3.0-only */
// S1 diagnostic only. No JACK, default-device changes, or application UI.
#include <Windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
constexpr double pi = 3.14159265358979323846;
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::printf("ERROR %s HRESULT=0x%08lx\n", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(operation);
    }
}
struct ComScope {
    ComScope() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM MTA"); }
    ~ComScope() { CoUninitialize(); }
};
struct Wave {
    WAVEFORMATEX value{WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 384000, 8, 32, 0};
};
std::wstring id(IMMDevice* device) {
    LPWSTR value = nullptr;
    check(device->GetId(&value), "endpoint ID");
    std::wstring result(value);
    CoTaskMemFree(value);
    return result;
}
std::wstring property(IMMDevice* device, const PROPERTYKEY& key) {
    ComPtr<IPropertyStore> store;
    check(device->OpenPropertyStore(STGM_READ, &store), "property store");
    PROPVARIANT value{};
    check(store->GetValue(key, &value), "property");
    std::wstring result = value.vt == VT_LPWSTR ? value.pwszVal : L"";
    PropVariantClear(&value);
    return result;
}
void topology(IMMDevice* device) {
    ComPtr<IDeviceTopology> graph;
    check(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(graph.GetAddressOf())), "topology");
    UINT count = 0;
    check(graph->GetConnectorCount(&count), "connector count");
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IConnector> connector, peer;
        check(graph->GetConnector(i, &connector), "connector");
        check(connector->GetConnectedTo(&peer), "connected connector");
        LPWSTR adapter = nullptr;
        check(connector->GetDeviceIdConnectedTo(&adapter), "adapter ID");
        ComPtr<IPart> part;
        check(peer.As(&part), "connector part");
        UINT local = 0;
        check(part->GetLocalId(&local), "part local ID");
        std::wprintf(L"  adapter=%ls connectorLocalId=0x%08x\n", adapter, local);
        CoTaskMemFree(adapter);
    }
}
int sessions(IMMDevice* device) {
    ComPtr<IAudioSessionManager2> manager;
    check(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(manager.GetAddressOf())), "session manager");
    ComPtr<IAudioSessionEnumerator> list;
    check(manager->GetSessionEnumerator(&list), "sessions");
    int count = 0;
    int ownActiveSessions = 0;
    check(list->GetCount(&count), "session count");
    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> control;
        ComPtr<IAudioSessionControl2> details;
        check(list->GetSession(i, &control), "session");
        check(control.As(&details), "session details");
        DWORD pid = 0;
        const auto attribution = details->GetProcessId(&pid);
        AudioSessionState state{};
        check(control->GetState(&state), "session state");
        wchar_t path[32768]{};
        DWORD length = 32768;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            QueryFullProcessImageNameW(process, 0, path, &length);
            CloseHandle(process);
        }
        std::wprintf(L"  session pid=%lu attribution=0x%08lx state=%d exe=%ls\n",
            pid, static_cast<unsigned long>(attribution), state, path);
        if (attribution == S_OK && pid == GetCurrentProcessId() && state == AudioSessionStateActive) {
            if (!path[0]) throw std::runtime_error("test process path unavailable");
            ++ownActiveSessions;
        }
    }
    return ownActiveSessions;
}
class Completion final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    ~Completion() { if (done) CloseHandle(done); }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> object;
        HRESULT activation = E_FAIL;
        result = operation->GetActivateResult(&activation, &object);
        if (SUCCEEDED(result)) result = activation;
        if (SUCCEEDED(result)) result = object.As(&client);
        SetEvent(done);
        return S_OK;
    }
};
struct Stream {
    ComPtr<IAudioClient> client;
    ~Stream() { if (client) client->Stop(); }
};
struct Renderer : Stream {
    ComPtr<IAudioRenderClient> render;
    UINT capacity = 0;
    unsigned long long position = 0;
    double frequency;
    Renderer(IMMDevice* device, double hz) : frequency(hz) {
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client.GetAddressOf())), "render activate");
        Wave format;
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            1000000, 0, &format.value, nullptr), "render initialize");
        check(client->GetBufferSize(&capacity), "render buffer size");
        check(client->GetService(IID_PPV_ARGS(&render)), "render service");
        pump();
        check(client->Start(), "render start");
    }
    void pump() {
        UINT padding = 0;
        check(client->GetCurrentPadding(&padding), "padding");
        const UINT frames = capacity - padding;
        if (!frames) return;
        BYTE* bytes = nullptr;
        check(render->GetBuffer(frames, &bytes), "render buffer");
        auto* samples = reinterpret_cast<float*>(bytes);
        for (UINT i = 0; i < frames; ++i, ++position) {
            const float sample = static_cast<float>(0.025 * std::sin(2 * pi * frequency * position / 48000));
            samples[2 * i] = samples[2 * i + 1] = sample;
        }
        check(render->ReleaseBuffer(frames, 0), "render release");
    }
};
struct Capture : Stream {
    ComPtr<IAudioCaptureClient> capture;
    Capture() {
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT value{};
        value.vt = VT_BLOB;
        value.blob.cbSize = sizeof(params);
        value.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
        auto completion = Microsoft::WRL::Make<Completion>();
        if (!completion || !completion->done) throw std::runtime_error("completion event");
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        check(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
            &value, completion.Get(), &operation), "process loopback async activate");
        if (WaitForSingleObject(completion->done, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("activation timeout");
        check(completion->result, "process loopback activation result");
        client = completion->client;
        Wave format;
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
            1000000, 0, &format.value, nullptr), "process capture initialize");
        check(client->GetService(IID_PPV_ARGS(&capture)), "capture service");
        check(client->Start(), "capture start");
        std::printf("capture actual initialized PCM: float32 48000 Hz 2 channels; target PID=%lu\n", GetCurrentProcessId());
    }
    void drain(std::vector<float>& result) {
        UINT available = 0;
        check(capture->GetNextPacketSize(&available), "capture packet size");
        while (available) {
            BYTE* data = nullptr;
            UINT frames = 0;
            DWORD flags = 0;
            check(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "capture buffer");
            for (UINT i = 0; i < frames; ++i)
                result.push_back(flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0 : reinterpret_cast<float*>(data)[i * 2]);
            check(capture->ReleaseBuffer(frames), "capture release");
            check(capture->GetNextPacketSize(&available), "capture next");
        }
    }
};
double amplitude(const std::vector<float>& samples, double hz) {
    double real = 0, imaginary = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        double phase = 2 * pi * hz * i / 48000;
        real += samples[i] * std::cos(phase);
        imaginary += samples[i] * std::sin(phase);
    }
    return samples.empty() ? 0 : 2 * std::hypot(real, imaginary) / samples.size();
}
int wmain(int argc, wchar_t** argv) {
    try {
        ComScope scope;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator)), "device enumerator");
        for (const auto role : {eConsole, eMultimedia, eCommunications}) {
            ComPtr<IMMDevice> device;
            auto hr = enumerator->GetDefaultAudioEndpoint(eRender, role, &device);
            if (SUCCEEDED(hr)) std::wprintf(L"default role=%d id=%ls\n", role, id(device.Get()).c_str());
        }
        if (argc != 2 && argc != 3) {
            std::printf("Usage: wjn_main_session_probe <primary render endpoint ID> [secondary render endpoint ID]\n");
            return 2;
        }
        const bool singleEndpoint = argc == 2;
        std::printf("mode=%s\n", singleEndpoint ? "single application / single endpoint" : "single application / two endpoints");
        if (!singleEndpoint && !_wcsicmp(argv[1], argv[2])) throw std::runtime_error("endpoints must differ");
        ComPtr<IMMDevice> first, second;
        check(enumerator->GetDevice(argv[1], &first), "primary endpoint");
        if (!singleEndpoint) check(enumerator->GetDevice(argv[2], &second), "secondary endpoint");
        for (auto* device : {first.Get(), second.Get()}) {
            if (!device) continue;
            std::wprintf(L"endpoint=%ls name=%ls\n", id(device).c_str(), property(device, PKEY_Device_FriendlyName).c_str());
            ComPtr<IMMEndpoint> endpoint;
            check(device->QueryInterface(IID_PPV_ARGS(&endpoint)), "endpoint direction");
            EDataFlow flow{};
            check(endpoint->GetDataFlow(&flow), "data flow");
            if (flow != eRender) throw std::runtime_error("not a render endpoint");
            topology(device);
        }
        Capture capture;
        Renderer primary(first.Get(), 997);
        std::unique_ptr<Renderer> secondary;
        if (!singleEndpoint) secondary = std::make_unique<Renderer>(second.Get(), 1999);
        const auto primarySessions = sessions(first.Get());
        if (primarySessions != 1) throw std::runtime_error("expected one active test-process session on primary endpoint");
        if (second) sessions(second.Get());
        std::vector<float> samples;
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < 2500) {
            primary.pump();
            if (secondary) secondary->pump();
            capture.drain(samples); Sleep(2);
            if (GetTickCount64() - start < 500) samples.clear();
        }
        const double primaryAmplitude = amplitude(samples, 997);
        const double secondaryAmplitude = amplitude(samples, 1999);
        std::printf("frames=%zu primary997_amplitude=%.8f secondary1999_amplitude=%.8f\n",
            samples.size(), primaryAmplitude, secondaryAmplitude);
        if (samples.size() < 48000 || primaryAmplitude < 0.002)
            throw std::runtime_error("insufficient primary capture; inconclusive");
        if (singleEndpoint) {
            double squareSum = 0;
            for (const auto sample : samples) squareSum += sample * sample;
            const double rms = std::sqrt(squareSum / samples.size());
            std::printf("single_endpoint primary_active_sessions=%d rms=%.8f expected_amplitude=0.02500000\n",
                primarySessions, rms);
            if (std::abs(primaryAmplitude - 0.025) > 0.0025 || secondaryAmplitude > 0.00025
                || std::abs(rms - 0.025 / std::sqrt(2.0)) > 0.0018) {
                std::printf("S1_SINGLE_ENDPOINT=FAIL: captured waveform differs from the single source.\n");
                return 4;
            }
            std::printf("S1_SINGLE_ENDPOINT=PASS: one active attributable session, expected 997 Hz signal captured.\n");
            return 0;
        }
        if (secondaryAmplitude > 0.002 && secondaryAmplitude > primaryAmplitude * 0.1) {
            std::printf("S1_ENDPOINT_ISOLATION=FAIL: process capture includes the other endpoint's tone.\n");
            return 3;
        }
        std::printf("S1_ENDPOINT_ISOLATION=NO_LEAK_OBSERVED (does not prove complete S1 feasibility).\n");
        return 0;
    } catch (const std::exception& error) {
        std::printf("INCONCLUSIVE: %s\n", error.what());
        return 1;
    }
}
