// Offline-capable core: no Qt widget/JACK dependencies. Control API is single-threaded.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Common/MidiToolsCore.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>
namespace WinJACKNexus::Common::MidiTools
{
struct ResourceSpec
{
    std::string id, path, format;
    uint8_t msb{}, lsb{}, program{}; // SFZ mapping; SF2 uses native preset banks.
};
struct ChannelSettings
{
    uint8_t msb{}, lsb{}, program{};
    double gainDb{};
    bool mute{}, solo{};
};
struct SynthSettings
{
    std::string clientName = "sf2_loader", preferred;
    std::vector<ResourceSpec> resources; // Catalog only; preferred is the explicit active ID.
    std::array<ChannelSettings, 16> channels{};
    double masterDb{};
    bool masterMute{};
};
enum class ResourceState : uint8_t
{
    pending,
    ready,
    missing,
    failed,
    budget,
    unloaded
};
struct ResourceInfo
{
    ResourceSpec spec;
    ResourceState state{};
    std::string detail;
    std::vector<std::string> presets;
};
struct ChannelStatus
{
    std::array<char, 64> resource{};
    std::array<char, 128> preset{};
    uint8_t msb{}, lsb{}, program{};
    bool matched{};
    float peak{};
};
struct SynthStatus
{
    std::array<ChannelStatus, 16> channels{};
    float leftPeak{}, rightPeak{};
    bool preparing{};
    uint64_t dropped{}, overflow{}, evicted{}, unsupported{}, revision{};
};
bool validSettings(const SynthSettings &) noexcept;
std::string settingsJson(const SynthSettings &);
bool parseSettings(const char *, SynthSettings &);
class SynthCore final
{
  public:
    explicit SynthCore(double rate = 48000);
    ~SynthCore();
    SynthCore(const SynthCore &) = delete;
    SynthCore &operator=(const SynthCore &) = delete;
    // Latest-request-wins loader. false rejects malformed settings without mutation.
    // The legacy preserveOnFailure argument is ignored: failed selections stay silent.
    bool submit(const SynthSettings &, bool preserveOnFailure = false);
    void setRequiredRate(double) noexcept; // Safe from JACK rate callback: atomic only.
    void render(uint32_t frames, std::span<const EventView>, float *left, float *right,
                bool inputOverflow = false) noexcept;
    void silence() noexcept;                     // Audio thread, or after the callback has stopped.
    bool status(SynthStatus &) noexcept;         // Single GUI/control consumer, bounded drain.
    std::vector<ResourceInfo> resources() const; // Control thread only.
    bool loading() const noexcept;
    uint64_t preparedBytes() const noexcept;
    uint64_t activeResources() const noexcept;
    uint64_t destroyedResources() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace WinJACKNexus::Common::MidiTools
