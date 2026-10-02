/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Common/AuxMixerState.hpp>
#include <string_view>
template<class Check> void verifyStandardChannelNames(Check check)
{
    using namespace WinJACKNexus::Common;
    const std::array<std::string_view,16> expected {"L","C","R","Lw","Rw","Ls","Rs","Lrb","Rrb","LFE","Ltf","Rtf","Ltm","Rtm","Ltr","Rtr"};
    for (std::size_t i=0;i<expected.size();++i)
        check(standardChannelNames[i]==expected[i], "all sixteen shared speaker names");
    for (const auto count : {1,2,6,8})
        check(mixerChannelLabels(static_cast<MixerLayout>(count))==standardChannelLabels(count),
            "mixer ports and controls retain shared sample order");
    check(auxLayoutChannelLabels(AuxChannelLayout::surround71)==standardChannelLabels(8),
        "AUX and spatial panner labels share mixer names");
    check(standardChannelChoices().last()==QStringLiteral("Rrb")
        && standardChannelChoices().first()==QStringLiteral("All"), "FX choices use shared names");
    check(std::string_view(standardRoleName(MeterChannelRole::rearLeft))=="Lrb"
        && std::string_view(standardRoleName(MeterChannelRole::topFrontRight))=="Rtf", "probe role labels");
    check(standardChannelName(0,1)=="M" && standardChannelName(2,4)=="Lrb"
        && standardChannelName(3,4)=="Rrb", "adapter and VST mono and quad names");
    std::array<std::string,16> names;
    for (std::size_t i=0;i<names.size();++i) names[i]=standardChannelName(i,16);
    for (std::size_t i=0;i<names.size();++i)
        for (std::size_t j=i+1;j<names.size();++j)
            check(names[i]!=names[j], "sixteen standard port names remain unique");
    check(standardChannelName(16,17)=="Ch_17", "extended unnamed channels remain unique");
}
