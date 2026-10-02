/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/StandardChannelNames.hpp>
#include <WinJACKNexus/Common/ChannelLayout.hpp>
#include <QStringList>
namespace WinJACKNexus::Common {
inline QStringList standardChannelLabels(std::size_t count) {
 QStringList result;
 for(std::size_t i=0;i<count;++i) result.append(QString::fromStdString(standardChannelName(i,count)));
 return result;
}
inline QStringList standardChannelChoices() {
 auto result=standardChannelLabels(8);result.prepend(QStringLiteral("All"));return result;
}
inline const char* standardRoleName(MeterChannelRole role) noexcept {
 switch(role) {
 case MeterChannelRole::mono:return "M";
 case MeterChannelRole::frontLeft:return standardChannelNames[0];
 case MeterChannelRole::frontCenter:return standardChannelNames[1];
 case MeterChannelRole::frontRight:return standardChannelNames[2];
 case MeterChannelRole::sideLeft:return standardChannelNames[5];
 case MeterChannelRole::sideRight:return standardChannelNames[6];
 case MeterChannelRole::rearLeft:return standardChannelNames[7];
 case MeterChannelRole::rearRight:return standardChannelNames[8];
 case MeterChannelRole::lfe:return standardChannelNames[9];
 case MeterChannelRole::topFrontLeft:return standardChannelNames[10];
 case MeterChannelRole::topFrontRight:return standardChannelNames[11];
 case MeterChannelRole::topRearLeft:return standardChannelNames[14];
 case MeterChannelRole::topRearRight:return standardChannelNames[15];
 default:return "";
 }
}
}
