// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "PublishSession.hpp"
#include "Wire.hpp"
#include <QUdpSocket>

namespace WinJACKNexus::Broadcast
{
// Construct/use/destroy on the publishing worker. This owns no audio queue;
// the realtime producer never calls Qt networking or packetization.
class UdpTransport final
{
  public:
    explicit UdpTransport(PublishSession &session);
    bool send(const AudioBlock &block);
    void reset();
    QString error() const;
    std::uint64_t sentDatagrams() const noexcept;
    std::uint64_t sentBytes() const noexcept;

  private:
    PublishSession &session_;
    QUdpSocket socket_;
    DatagramContext context_;
    QString error_;
    std::uint64_t datagrams_ = 0, bytes_ = 0;
};
} // namespace WinJACKNexus::Broadcast
