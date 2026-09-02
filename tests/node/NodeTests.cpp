#include <WinJACKNexus/Node/INode.hpp>

#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace {

using GetStringFunction = const char* (*)();
using CreateNodeFunction = WinJACKNexus::Node::INode* (*)();
using DestroyNodeFunction = void (*)(WinJACKNexus::Node::INode*);

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

std::filesystem::path executableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

bool testApiStub(const std::filesystem::path& directory)
{
    const auto path = directory / L"node.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "node.dll loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto name = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    passed &= check(version != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr,
                    "node.dll exports the complete Node C API");
    if (version != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr) {
        passed &= check(std::string_view(version()) == "1" && std::string_view(name()) == "WinJACKNexus Node API"
                            && std::string_view(category()) == "Interface",
                        "node.dll reports stable API metadata");
        passed &= check(create() == nullptr, "node.dll remains an interface-only stub");
    }
    FreeLibrary(module);
    return passed;
}

bool testExampleNode(const std::filesystem::path& directory)
{
    const auto path = directory / L"wjn_example_node.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "example node loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto name = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    passed &= check(version != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr,
                    "example node exports the complete Node C API");
    if (version != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr) {
        passed &= check(std::string_view(version()) == "1" && std::string_view(name()) == "Example Node"
                            && std::string_view(category()) == "Tools",
                        "example node reports stable metadata");
        auto* node = create();
        passed &= check(node != nullptr, "example node creates an INode instance");
        if (node != nullptr) {
            const auto info = node->info();
            passed &= check(std::string_view(info.name) == "Example Node" && std::string_view(info.version) == "1.0.0",
                            "example INode exposes NodeInfo metadata");
            passed &= check(node->init(), "example INode initializes");
            passed &= check(node->createWindow(nullptr) == nullptr, "example INode has no window skeleton implementation");
            node->shutdown();
            destroy(node);
        }
    }
    FreeLibrary(module);
    return passed;
}

}

int main()
{
    const auto directory = executableDirectory();
    const auto passed = testApiStub(directory) && testExampleNode(directory);
    if (!passed)
        return 1;
    std::puts("Node tests passed");
    return 0;
}