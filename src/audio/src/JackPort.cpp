#include <WinJACKNexus/Audio/JackPort.hpp>

namespace WinJACKNexus::Audio {

JackPort::~JackPort() noexcept
{
    unregisterPort();
}

JackPort::JackPort(JackPort&& other) noexcept
    : client_(other.client_)
    , port_(other.port_)
{
    other.client_ = nullptr;
    other.port_ = nullptr;
}

JackPort& JackPort::operator=(JackPort&& other) noexcept
{
    if (this == &other)
        return *this;
    unregisterPort();
    client_ = other.client_;
    port_ = other.port_;
    other.client_ = nullptr;
    other.port_ = nullptr;
    return *this;
}

bool JackPort::registerPort(JackClient& client,
                            const char* portName,
                            const char* portType,
                            unsigned long portFlags,
                            unsigned long bufferSize) noexcept
{
    unregisterPort();
    if (!client.isOpen() || portName == nullptr || portType == nullptr)
        return false;

    port_ = JackApi::instance().portRegister(client.nativeHandle(), portName, portType, portFlags, bufferSize);
    if (port_ == nullptr)
        return false;
    client_ = &client;
    return true;
}

bool JackPort::unregisterPort() noexcept
{
    if (port_ == nullptr)
        return true;

    bool success = true;
    if (client_ != nullptr && client_->isOpen())
        success = JackApi::instance().portUnregister(client_->nativeHandle(), port_) == 0;
    if (success) {
        client_ = nullptr;
        port_ = nullptr;
    }
    return success;
}

bool JackPort::isValid() const noexcept
{
    return port_ != nullptr;
}

jack_port_t* JackPort::nativeHandle() const noexcept
{
    return port_;
}

void* JackPort::buffer(jack_nframes_t frames) const noexcept
{
    return port_ != nullptr ? JackApi::instance().portGetBuffer(port_, frames) : nullptr;
}

jack_default_audio_sample_t* JackPort::audioBuffer(jack_nframes_t frames) const noexcept
{
    return static_cast<jack_default_audio_sample_t*>(buffer(frames));
}

void* JackPort::midiBuffer(jack_nframes_t frames) const noexcept
{
    return buffer(frames);
}

const char* JackPort::name() const noexcept
{
    return port_ != nullptr ? JackApi::instance().portName(port_) : nullptr;
}

const char* JackPort::shortName() const noexcept
{
    return port_ != nullptr ? JackApi::instance().portShortName(port_) : nullptr;
}

const char* JackPort::type() const noexcept
{
    return port_ != nullptr ? JackApi::instance().portType(port_) : nullptr;
}

int JackPort::flags() const noexcept
{
    return port_ != nullptr ? JackApi::instance().portFlags(port_) : 0;
}

bool JackPort::isInput() const noexcept
{
    return (flags() & JackPortIsInput) != 0;
}

bool JackPort::isOutput() const noexcept
{
    return (flags() & JackPortIsOutput) != 0;
}

std::size_t JackPort::visitConnections(JackPortNameVisitor visitor, void* userData) const noexcept
{
    if (port_ == nullptr)
        return 0;
    const auto** connections = JackApi::instance().portGetConnections(port_);
    if (connections == nullptr)
        return 0;

    std::size_t count = 0;
    for (const auto** connection = connections; *connection != nullptr; ++connection) {
        ++count;
        if (visitor != nullptr && !visitor(*connection, userData))
            break;
    }
    JackApi::instance().freeMemory(const_cast<void*>(static_cast<const void*>(connections)));
    return count;
}

}