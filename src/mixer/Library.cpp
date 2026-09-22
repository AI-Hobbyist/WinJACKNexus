#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/MixerWindow.hpp>

#include <QPointer>

namespace {

class MixerNode final : public WinJACKNexus::Node::INode {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { "Mixer", "Mixer", "0.1.0", "WinJACKNexus", "Top-level mixer window module." };
    }

    bool init() noexcept override { return true; }

    void shutdown() noexcept override
    {
        destroyWindow(nullptr);
    }

    HWND createWindow(HWND) noexcept override
    {
        if (window_.isNull()) {
            window_ = new WinJACKNexus::UI::MixerWindow;
            window_->setAttribute(Qt::WA_DeleteOnClose);
        }
        window_->show();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        if (!window_.isNull())
            window_->close();
        window_.clear();
    }

private:
    QPointer<WinJACKNexus::UI::MixerWindow> window_;
};

}

const char* wjn_get_node_api_version(void) { return "2"; }
std::uint32_t wjn_get_node_count(void) { return 1; }
const char* wjn_get_node_name(std::uint32_t index) { return index == 0 ? "Mixer" : ""; }
const char* wjn_get_node_category(std::uint32_t index) { return index == 0 ? "Mixer" : ""; }
WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index) { return index == 0 ? new MixerNode : nullptr; }
void wjn_destroy_node(WinJACKNexus::Node::INode* node) { delete node; }
void* wjn_create_settings_view(WinJACKNexus::Node::INode*, void*) { return nullptr; }
void wjn_destroy_settings_view(WinJACKNexus::Node::INode*, void*) {}
QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode*, QWidget*) { return nullptr; }
void wjn_destroy_settings_widget(WinJACKNexus::Node::INode*, QWidget*) {}
const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode*) { return ""; }
int wjn_set_node_settings_json(WinJACKNexus::Node::INode*, const char*) { return 0; }