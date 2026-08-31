#include <WinJACKNexus/Audio/JackGraph.hpp>

namespace WinJACKNexus::Audio {

std::size_t JackGraph::visitPortNames(JackClient& client,
                                      const char* portNamePattern,
                                      const char* typeNamePattern,
                                      unsigned long flags,
                                      JackPortNameVisitor visitor,
                                      void* userData) noexcept
{
    if (!client.isOpen())
        return 0;
    const auto** ports = JackApi::instance().getPorts(client.nativeHandle(), portNamePattern, typeNamePattern, flags);
    if (ports == nullptr)
        return 0;

    std::size_t count = 0;
    for (const auto** port = ports; *port != nullptr; ++port) {
        ++count;
        if (visitor != nullptr && !visitor(*port, userData))
            break;
    }
    JackApi::instance().freeMemory(const_cast<void*>(static_cast<const void*>(ports)));
    return count;
}

bool JackGraph::connect(JackClient& client,
                        const char* sourcePort,
                        const char* destinationPort) noexcept
{
    return client.isOpen() && sourcePort != nullptr && destinationPort != nullptr
        && JackApi::instance().connect(client.nativeHandle(), sourcePort, destinationPort) == 0;
}

bool JackGraph::disconnect(JackClient& client,
                           const char* sourcePort,
                           const char* destinationPort) noexcept
{
    return client.isOpen() && sourcePort != nullptr && destinationPort != nullptr
        && JackApi::instance().disconnect(client.nativeHandle(), sourcePort, destinationPort) == 0;
}

bool JackGraph::portExists(JackClient& client, const char* portName) noexcept
{
    return client.isOpen() && portName != nullptr && JackApi::instance().portByName(client.nativeHandle(), portName) != nullptr;
}

}