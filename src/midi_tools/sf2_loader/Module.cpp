// SPDX-License-Identifier: AGPL-3.0-only
#include "../ClientSettings.hpp"
#include "../ModuleI18n.hpp"
#include "Theme.hpp"
#include <QApplication>
#include <WinJACKNexus/Common/MidiToolsSynth.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/MidiSynthWindow.hpp>
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
    QPointer<WinJACKNexus::UI::MidiSynthWindow> window_;
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
        delete window_;
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
        try
        {
            if (!QApplication::instance())
            {
                return nullptr;
            }
            if (!window_)
            {
                WinJACKNexus::UI::MidiSynthBindings bindings;
                bindings.edit = [this](const auto &value, bool) {
                    auto candidate = value;
                    core_.status(lastStatus_);
                    if (lastStatus_.revision >= submitted_)
                    {
                        for (unsigned channel = 0; channel < 16; ++channel)
                        {
                            candidate.channels[channel].msb = lastStatus_.channels[channel].msb;
                            candidate.channels[channel].lsb = lastStatus_.channels[channel].lsb;
                            candidate.channels[channel].program =
                                lastStatus_.channels[channel].program;
                        }
                    }
                    candidate.clientName = actualName();
                    if (!core_.submit(candidate))
                    {
                        return false;
                    }
                    state_ = std::move(candidate);
                    ++submitted_;
                    return true;
                };
                bindings.status = [this] {
                    core_.status(lastStatus_);
                    return lastStatus_;
                };
                bindings.resources = [this] { return core_.resources(); };
                bindings.loading = [this] { return core_.loading(); };
                bindings.text = [](const char *key, const char *fallback) {
                    return MT::I18n::text("midi.sf2_loader", key, fallback);
                };
                window_ = new WinJACKNexus::UI::MidiSynthWindow(state_, std::move(bindings));
                const QPointer<WinJACKNexus::UI::MidiSynthWindow> weak = window_;
                MT::I18n::bind(window_, [weak] {
                    if (weak)
                    {
                        weak->language();
                    }
                });
            }
            window_->show();
            window_->raise();
            window_->activateWindow();
            return reinterpret_cast<HWND>(window_->winId());
        }
        catch (...)
        {
            return nullptr;
        }
    }
    void destroyWindow(HWND) noexcept override
    {
        if (window_)
        {
            window_->close();
        }
    }
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
        if (window_)
        {
            window_->setSettings(state_);
        }
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
    return "3";
}

const char *wjn_get_node_name(void)
{
    return "sf2_loader";
}
const char *wjn_get_node_category(void)
{
    return "Midi Tools";
}
WinJACKNexus::Node::INode *wjn_create_node(void)
{
    try
    {
        return new SynthNode;
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
extern "C" __declspec(dllexport) int wjn_show_node_gui(WinJACKNexus::Node::INode *node)
{
    return node && node->createWindow(nullptr) ? 1 : 0;
}
uint32_t wjn_get_node_theme_api_version()
{
    return WJN_NODE_THEME_API_VERSION;
}
const char *wjn_get_node_theme_namespace_json(void)
{
    return midiToolsThemeDescriptor;
}
uint32_t wjn_get_node_i18n_api_version()
{
    return WJN_NODE_I18N_API_VERSION;
}
const char *wjn_get_node_i18n_descriptor_json(void)
{
    return R"json({"descriptorVersion":1,"i18nApiVersion":1,"moduleId":"midi-tools-sf2_loader","domains":["midi.sf2_loader"],"locatePath":"locate","catalogVersion":1,"displayNameKey":{"domain":"midi.sf2_loader","key":"name"}})json";
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
