// SPDX-License-Identifier: AGPL-3.0-only
#include "../ClientSettings.hpp"
#include "../ModuleI18n.hpp"
#include <QApplication>
#include <QJsonArray>
#include <WinJACKNexus/Common/MidiToolsMatrix.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/MidiMatrixWindow.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
namespace MT = WinJACKNexus::MidiTools;
class MatrixNode final : public WinJACKNexus::Node::INode, public MT::ClientSettings
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
    MT::Core::MatrixProcessor processor_;
    MT::Core::MatrixSnapshot snapshot_;
    MT::Core::MatrixRoutes routes_ = MT::Core::diagonalRoutes(), audioRoutes_ = routes_;
    QPointer<WinJACKNexus::UI::MidiMatrixWindow> window_;
    std::atomic<unsigned> stop_{};

  public:
    MatrixNode() : ClientSettings(jack_, MT::matrixPorts(), QStringLiteral("midi_matrix")) {}
    ~MatrixNode()
    {
        shutdown();
        delete window_;
    }
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {"midi matrix", "Midi Tools", "1.0.0", "WinJACKNexus", "16 by 16 MIDI port matrix"};
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
        try
        {
            if (!QApplication::instance())
            {
                return nullptr;
            }
            if (!window_)
            {
                window_ = new WinJACKNexus::UI::MidiMatrixWindow(
                    routes_, [this](unsigned r, unsigned c, bool on) {
                        if (on)
                        {
                            routes_[r] |= uint16_t(1u << c);
                        }
                        else
                        {
                            routes_[r] &= uint16_t(~(1u << c));
                        }
                        snapshot_.publish(routes_);
                    });
                const QPointer<WinJACKNexus::UI::MidiMatrixWindow> weak = window_;
                MT::I18n::bind(window_, [weak] {
                    if (weak)
                    {
                        weak->language(
                            MT::I18n::text("midi.midi_matrix", "name", "MIDI matrix"),
                            MT::I18n::text("midi.midi_matrix", "help",
                                           "Rows are input ports; columns are output ports. "
                                           "Original MIDI channels are preserved."),
                            MT::I18n::text("midi.midi_matrix", "input", "Input"),
                            MT::I18n::text("midi.midi_matrix", "output", "Output"));
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
    const char *matrixJson()
    {
        QJsonArray routes;
        for (unsigned r = 0; r < 16; ++r)
        {
            for (unsigned c = 0; c < 16; ++c)
            {
                routes.append(bool(routes_[r] & (1u << c)));
            }
        }
        json_ = QJsonDocument(QJsonObject{{"version", 1},
                                          {"clientName", QString::fromUtf8(actualName())},
                                          {"routes", routes}})
                    .toJson(QJsonDocument::Compact);
        return json_.constData();
    }
    bool restoreMatrix(const char *json)
    {
        if (!json || strnlen_s(json, 65537) > 65536)
        {
            return false;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(json, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }
        const auto o = doc.object();
        if (o.size() != 3 || o["version"] != QJsonValue(1) || !o["clientName"].isString() ||
            !o["routes"].isArray())
        {
            return false;
        }
        const auto list = o["routes"].toArray();
        if (list.size() != 256)
        {
            return false;
        }
        MT::Core::MatrixRoutes candidate{};
        for (unsigned i = 0; i < 256; ++i)
        {
            if (!list[i].isBool())
            {
                return false;
            }
            if (list[i].toBool())
            {
                candidate[i / 16] |= uint16_t(1u << (i % 16));
            }
        }
        if (!applyName(o["clientName"].toString()))
        {
            return false;
        }
        routes_ = candidate;
        snapshot_.publish(routes_);
        if (window_)
        {
            window_->routes(routes_);
        }
        return true;
    }
    static int processThunk(jack_nframes_t frames, void *owner) noexcept
    {
        return static_cast<MatrixNode *>(owner)->process(frames);
    }
    int process(jack_nframes_t frames) noexcept
    {
        auto &api = WinJACKNexus::Audio::JackApi::instance();
        std::array<void *, 16> input{}, output{};
        for (unsigned c = 0; c < 16; ++c)
        {
            input[c] = api.portGetBuffer(jack_.port(c), frames);
            output[c] = api.portGetBuffer(jack_.port(c + 16), frames);
            if (output[c])
            {
                api.midiClearBuffer(output[c]);
            }
        }
        const auto stop = stop_.load(std::memory_order_acquire);
        if (stop == 1)
        {
            processor_.requestRelease();
        }
        snapshot_.read(audioRoutes_);
        processor_.process(
            frames, audioRoutes_,
            [&](unsigned c) { return !stop && input[c] ? api.midiEventCount(input[c]) : 0; },
            [&](unsigned c, unsigned i) {
                jack_midi_event_t e{};
                if (api.midiEventGet(&e, input[c], i))
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
const char *wjn_get_node_name(uint32_t i)
{
    return i == 0 ? "midi matrix" : "";
}
const char *wjn_get_node_category(uint32_t i)
{
    return i == 0 ? "Midi Tools" : "";
}
WinJACKNexus::Node::INode *wjn_create_node(uint32_t i)
{
    try
    {
        return i == 0 ? new MatrixNode : nullptr;
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
        return n && QApplication::instance() ? static_cast<MatrixNode *>(n)->createSettings(parent)
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
        static_cast<MatrixNode *>(n)->destroySettings(w);
    }
}
const char *wjn_get_node_settings_json(WinJACKNexus::Node::INode *n)
{
    return n ? static_cast<MatrixNode *>(n)->matrixJson() : nullptr;
}
int wjn_set_node_settings_json(WinJACKNexus::Node::INode *n, const char *json)
{
    try
    {
        return n && static_cast<MatrixNode *>(n)->restoreMatrix(json);
    }
    catch (...)
    {
        return 0;
    }
}
extern "C" __declspec(dllexport) const char *wjn_get_node_client_id(WinJACKNexus::Node::INode *n)
{
    return n ? static_cast<MatrixNode *>(n)->actualName() : nullptr;
}
uint32_t wjn_get_node_i18n_api_version()
{
    return 1;
}
const char *wjn_get_node_i18n_descriptor_json(uint32_t i)
{
    return i == 0
               ? R"json({"descriptorVersion":1,"i18nApiVersion":1,"moduleId":"midi-tools-midi_matrix","domains":["midi.midi_matrix"],"locatePath":"locate","catalogVersion":1,"displayNameKey":{"domain":"midi.midi_matrix","key":"name"}})json"
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

extern "C" WINJACK_NODE_API int wjn_show_node_gui(WinJACKNexus::Node::INode *n)
{
    return n && n->createWindow(nullptr) ? 1 : 0;
}
