// SPDX-License-Identifier: AGPL-3.0-only
#include "../ClientSettings.hpp"
#include "../ModuleI18n.hpp"
#include "Theme.hpp"
#include <QApplication>
#include <WinJACKNexus/Common/MidiToolsSplit.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
namespace MT = WinJACKNexus::MidiTools;
class SplitNode final : public WinJACKNexus::Node::INode, public MT::ClientSettings
{
    MT::JackBackend jack_{&processThunk, this,
                          [this] {
                              processor_.reset();
                              stop_ = 0;
                          },
                          [this] {
                              stop_.store(1, std::memory_order_release);
                              for (unsigned i = 0;
                                   i < 100 && stop_.load(std::memory_order_acquire) != 2; ++i)
                              {
                                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                              }
                          }};
    MT::Core::SplitProcessor processor_;
    std::atomic<unsigned> stop_{};

  public:
    SplitNode() : ClientSettings(jack_, MT::splitPorts(), QStringLiteral("midi_spilit")) {}
    ~SplitNode()
    {
        shutdown();
    }
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {"midi_spilit", "Midi Tools", "1.0.0", "WinJACKNexus", "16-channel MIDI splitter"};
    }
    bool init() noexcept override
    {
        try
        {
            return jack_.active() || jack_.replace(name_.toUtf8().toStdString(), ports_);
        }
        catch (...)
        {
            return false;
        }
    }
    void shutdown() noexcept override
    {
        jack_.close();
        processor_.reset();
    }
    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }
    void destroyWindow(HWND) noexcept override {}
    static int processThunk(jack_nframes_t frames, void *owner) noexcept
    {
        return static_cast<SplitNode *>(owner)->process(frames);
    }
    int process(jack_nframes_t frames) noexcept
    {
        auto &api = WinJACKNexus::Audio::JackApi::instance();
        std::array<void *, 16> output{};
        for (unsigned c = 0; c < 16; ++c)
        {
            output[c] = api.portGetBuffer(jack_.port(c + 1), frames);
            if (output[c])
            {
                api.midiClearBuffer(output[c]);
            }
        }
        void *input = api.portGetBuffer(jack_.port(0), frames);
        const auto stop = stop_.load(std::memory_order_acquire);
        if (stop == 1)
        {
            processor_.requestRelease();
        }
        processor_.process(
            frames, !stop && input ? api.midiEventCount(input) : 0,
            [&](unsigned i) {
                jack_midi_event_t e{};
                if (api.midiEventGet(&e, input, i))
                {
                    return MT::Core::EventView{};
                }
                return MT::Core::EventView{e.time, {e.buffer, e.size}};
            },
            [&](unsigned c, MT::Core::EventView e) {
                return output[c] &&
                       api.midiEventWrite(output[c], e.frame, e.bytes.data(), e.bytes.size()) == 0;
            });
        if (stop == 1)
        {
            stop_.store(2, std::memory_order_release);
        }
        return 0;
    }
};
const char *wjn_get_node_api_version()
{
    return "2";
}
uint32_t wjn_get_node_count()
{
    return 1;
}
uint32_t wjn_get_node_theme_api_version()
{
    return WJN_NODE_THEME_API_VERSION;
}
const char *wjn_get_node_theme_namespace_json(uint32_t index)
{
    return index == 0 ? midiToolsThemeDescriptor : nullptr;
}
const char *wjn_get_node_name(uint32_t i)
{
    return i == 0 ? "midi_spilit" : "";
}
const char *wjn_get_node_category(uint32_t i)
{
    return i == 0 ? "Midi Tools" : "";
}
WinJACKNexus::Node::INode *wjn_create_node(uint32_t i)
{
    try
    {
        return i == 0 ? new SplitNode : nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}
void wjn_destroy_node(WinJACKNexus::Node::INode *n)
{
    delete n;
}
QWidget *wjn_create_settings_widget(WinJACKNexus::Node::INode *n, QWidget *parent)
{
    try
    {
        return n && QApplication::instance() ? static_cast<SplitNode *>(n)->createSettings(parent)
                                             : nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}
void wjn_destroy_settings_widget(WinJACKNexus::Node::INode *n, QWidget *w)
{
    if (n)
    {
        static_cast<SplitNode *>(n)->destroySettings(w);
    }
}
const char *wjn_get_node_settings_json(WinJACKNexus::Node::INode *n)
{
    return n ? static_cast<SplitNode *>(n)->settingsJson() : nullptr;
}
int wjn_set_node_settings_json(WinJACKNexus::Node::INode *n, const char *json)
{
    try
    {
        return n && static_cast<SplitNode *>(n)->restore(json);
    }
    catch (...)
    {
        return 0;
    }
}
extern "C" __declspec(dllexport) const char *wjn_get_node_client_id(WinJACKNexus::Node::INode *n)
{
    return n ? static_cast<SplitNode *>(n)->actualName() : nullptr;
}
uint32_t wjn_get_node_i18n_api_version()
{
    return 1;
}
const char *wjn_get_node_i18n_descriptor_json(uint32_t i)
{
    return i == 0
               ? R"json({"descriptorVersion":1,"i18nApiVersion":1,"moduleId":"midi-tools-midi_spilit","domains":["midi.midi_spilit"],"locatePath":"locate","catalogVersion":1,"displayNameKey":{"domain":"midi.midi_spilit","key":"name"}})json"
               : nullptr;
}
int32_t wjn_i18n_attach_host(const WjnI18nHostV1 *h)
{
    return MT::I18n::attach(h);
}
void wjn_i18n_language_changed(const char *, uint64_t) {}
void wjn_i18n_detach_host()
{
    MT::I18n::detach();
}
