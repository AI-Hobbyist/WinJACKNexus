// SPDX-License-Identifier: AGPL-3.0-only
#include "UdpTransport.hpp"

namespace WinJACKNexus::Broadcast
{
UdpTransport::UdpTransport(PublishSession &session) : session_(session)
{
}

bool UdpTransport::send(const AudioBlock &block)
{
    error_.clear();
    if (session_.state() != QStringLiteral("online"))
    {
        reset();
        error_ = QStringLiteral("Publishing session is offline");
        return false;
    }
    if (context_.session != session_.sessionId())
    {
        reset();
        context_.session = session_.sessionId();
        context_.key = session_.key();
    }
    const auto packets = datagrams(context_, block, &error_);
    if (packets.empty())
        return false;
    // Use the authenticated control peer itself, not a remotely supplied host
    // or a fresh DNS lookup that could redirect audio to an unrelated endpoint.
    const auto address = session_.serverAddress();
    for (const auto &packet : packets)
    {
        if (socket_.writeDatagram(packet, address, session_.udpPort()) != packet.size())
        {
            error_ = QStringLiteral("UDP audio write failed (%1)").arg(static_cast<int>(socket_.error()));
            return false;
        }
        ++datagrams_;
        bytes_ += static_cast<std::uint64_t>(packet.size());
    }
    return true;
}

void UdpTransport::reset()
{
    socket_.abort();
    context_.session.clear();
    context_.key.fill('\0');
    context_.key.clear();
    context_.nextSequence = 1;
}

QString UdpTransport::error() const
{
    return error_;
}
std::uint64_t UdpTransport::sentDatagrams() const noexcept
{
    return datagrams_;
}
std::uint64_t UdpTransport::sentBytes() const noexcept
{
    return bytes_;
}
} // namespace WinJACKNexus::Broadcast
