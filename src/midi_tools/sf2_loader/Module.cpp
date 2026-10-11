// SPDX-License-Identifier: AGPL-3.0-only
#include "../ClientSettings.hpp"
#include "../ModuleI18n.hpp"
#include <QApplication>
#include <WinJACKNexus/Common/MidiToolsSynth.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
namespace MT = WinJACKNexus::MidiTools;
class SynthNode final : public WinJACKNexus::Node::INode, public MT::ClientSettings
{
    MT::Core::SynthCore core_;
    MT::JackBackend jack_{&processThunk, this,
                          [this] {
                              core_.silence();
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
    std::array<MT::Core::EventView, MT::Core::maxEvents> events_{};
    MT::Core::SynthSettings state_;
    MT::Core::SynthStatus lastStatus_;
    uint64_t submitted_ = 1;
    std::atomic<unsigned> stop_{};

  public:
    SynthNode() : ClientSettings(jack_, MT::synthPorts(), QStringLiteral("sf2_loader"))
    {
        jack_.setRateCallback(&rateThunk);
    }
    ~SynthNode()
    {
        shutdown();
    }
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {"sf2_loader", "Midi Tools", "1.0.0", "WinJACKNexus", "SF2 and SFZ synthesizer"};
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
        core_.silence();
    }
    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }
    void destroyWindow(HWND) noexcept override {}
    static int processThunk(jack_nframes_t frames, void *owner) noexcept
    {
        return static_cast<SynthNode *>(owner)->process(frames);
    }
    static int rateThunk(jack_nframes_t rate, void *owner) noexcept
    {
        static_cast<SynthNode *>(owner)->core_.setRequiredRate(rate);
        return 0;
    }
    const char *synthJson()
    {
        core_.status(lastStatus_);
        auto saved = state_;
        saved.clientName = actualName();
        if (lastStatus_.revision >= submitted_)
        {
            for (unsigned c = 0; c < 16; ++c)
            {
                saved.channels[c].msb = lastStatus_.channels[c].msb;
                saved.channels[c].lsb = lastStatus_.channels[c].lsb;
                saved.channels[c].program = lastStatus_.channels[c].program;
            }
        }
        json_ = QByteArray::fromStdString(MT::Core::settingsJson(saved));
        return json_.constData();
    }
    bool restoreSynth(const char *json)
    {
        MT::Core::SynthSettings candidate;
        if (!MT::Core::parseSettings(json, candidate) || !backend_.validName(candidate.clientName))
        {
            return false;
        }
        if (!applyName(QString::fromStdString(candidate.clientName)))
        {
            return false;
        }
        candidate.clientName = actualName();
        if (!core_.submit(candidate))
        {
            return false;
        }
        state_ = std::move(candidate);
        ++submitted_;
        return true;
    }
    int process(jack_nframes_t frames) noexcept
    {
        auto &api = WinJACKNexus::Audio::JackApi::instance();
        auto *left = static_cast<float *>(api.portGetBuffer(jack_.port(1), frames));
        auto *right = static_cast<float *>(api.portGetBuffer(jack_.port(2), frames));
        void *input = api.portGetBuffer(jack_.port(0), frames);
        const auto stop = stop_.load(std::memory_order_acquire);
        if (stop == 1)
        {
            core_.silence();
        }
        const unsigned count = !stop && input ? api.midiEventCount(input) : 0;
        const unsigned limit = (std::min)(count, MT::Core::maxEvents);
        unsigned size = 0;
        for (unsigned i = 0; i < limit; ++i)
        {
            jack_midi_event_t e{};
            if (api.midiEventGet(&e, input, i) == 0)
            {
                events_[size++] = {e.time, {e.buffer, e.size}};
            }
        }
        core_.render(frames, {events_.data(), size}, left, right, count > limit);
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
const char *wjn_get_node_name(uint32_t i)
{
    return i == 0 ? "sf2_loader" : "";
}
const char *wjn_get_node_category(uint32_t i)
{
    return i == 0 ? "Midi Tools" : "";
}
WinJACKNexus::Node::INode *wjn_create_node(uint32_t i)
{
    try
    {
        return i == 0 ? new SynthNode : nullptr;
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
        return n && QApplication::instance() ? static_cast<SynthNode *>(n)->createSettings(parent)
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
        static_cast<SynthNode *>(n)->destroySettings(w);
    }
}
const char *wjn_get_node_settings_json(WinJACKNexus::Node::INode *n)
{
    return n ? static_cast<SynthNode *>(n)->synthJson() : nullptr;
}
int wjn_set_node_settings_json(WinJACKNexus::Node::INode *n, const char *json)
{
    try
    {
        return n && static_cast<SynthNode *>(n)->restoreSynth(json);
    }
    catch (...)
    {
        return 0;
    }
}
extern "C" __declspec(dllexport) const char *wjn_get_node_client_id(WinJACKNexus::Node::INode *n)
{
    return n ? static_cast<SynthNode *>(n)->actualName() : nullptr;
}
uint32_t wjn_get_node_i18n_api_version()
{
    return 1;
}
const char *wjn_get_node_i18n_descriptor_json(uint32_t i)
{
    return i == 0
               ? R"json({"descriptorVersion":1,"i18nApiVersion":1,"moduleId":"midi-tools-sf2_loader","domains":["midi.sf2_loader"],"locatePath":"locate","catalogVersion":1,"displayNameKey":{"domain":"midi.sf2_loader","key":"name"}})json"
               : nullptr;
}
int32_t wjn_i18n_attach_host(const WjnI18nHostV1 *h)
{
    return MT::I18n::attach(h);
}
void wjn_i18n_language_changed(const char *, uint64_t)
{
    MT::I18n::changed();
}
void wjn_i18n_detach_host()
{
    MT::I18n::detach();
}
