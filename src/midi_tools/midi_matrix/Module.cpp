// S1 foundation; processing is enabled in its ordered implementation stage.
// SPDX-License-Identifier: AGPL-3.0-only
#include <WinJACKNexus/Node/NodeApi.h>
#include <new>
namespace {
class FoundationNode final : public WinJACKNexus::Node::INode {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override { return {"midi matrix", "Midi Tools", "0.1.0", "WinJACKNexus", "MIDI Tools foundation"}; }
    bool init() noexcept override { return false; } // No pretend audio client before implementation.
    void shutdown() noexcept override {}
    HWND createWindow(HWND) noexcept override { return nullptr; }
    void destroyWindow(HWND) noexcept override {}
};
}
const char* wjn_get_node_api_version() { return "2"; }
uint32_t wjn_get_node_count() { return 1; }
const char* wjn_get_node_name(uint32_t i) { return i == 0 ? "midi matrix" : ""; }
const char* wjn_get_node_category(uint32_t i) { return i == 0 ? "Midi Tools" : ""; }
WinJACKNexus::Node::INode* wjn_create_node(uint32_t i) { return i == 0 ? new(std::nothrow) FoundationNode : nullptr; }
void wjn_destroy_node(WinJACKNexus::Node::INode* n) { delete n; }
uint32_t wjn_get_node_i18n_api_version() { return 1; }
const char* wjn_get_node_i18n_descriptor_json(uint32_t i) {
    return i == 0 ? R"json({"descriptorVersion":1,"i18nApiVersion":1,"moduleId":"midi-tools-midi_matrix","domains":["midi.midi_matrix"],"locatePath":"locate","catalogVersion":1,"displayNameKey":{"domain":"midi.midi_matrix","key":"name"}})json" : nullptr;
}
