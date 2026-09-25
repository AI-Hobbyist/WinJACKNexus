/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "Dsp.hpp"
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common {
namespace {
constexpr float floorDb=-120.0F;
float db(float amplitude) noexcept {
    return amplitude>1.0e-6F?20.0F*std::log10(amplitude):floorDb;
}
float alpha(float ms,double rate) noexcept {
    return static_cast<float>(std::exp(-1.0/(std::max(0.1F,ms)*0.001*rate)));
}
}

void LimiterProcessor::prepare(double sampleRate) noexcept {
    sampleRate_=std::isfinite(sampleRate)&&sampleRate>=8000.0?sampleRate:48000.0;
    reset();updateCoefficients();
}
void LimiterProcessor::reset() noexcept {
    gain_=1.0F;inputEnergy_=0.0F;outputEnergy_=0.0F;
}
void LimiterProcessor::setParameters(const LimiterParameters& p) noexcept {
    parameters_=p;
    parameters_.thresholdDb=std::clamp(std::isfinite(p.thresholdDb)?p.thresholdDb:-1.0F,-24.0F,6.0F);
    parameters_.ceilingDb=std::clamp(std::isfinite(p.ceilingDb)?p.ceilingDb:-1.0F,-12.0F,12.0F);
    parameters_.releaseMs=std::clamp(std::isfinite(p.releaseMs)?p.releaseMs:100.0F,10.0F,500.0F);
    parameters_.mode=std::clamp(p.mode,0,2);
    ceilingLinear_=std::pow(10.0F,parameters_.ceilingDb/20.0F);
    updateCoefficients();
}
void LimiterProcessor::updateCoefficients() noexcept {
    releaseAlpha_=alpha(parameters_.releaseMs,sampleRate_);
    meterAttackAlpha_=alpha(10.0F,sampleRate_);
    meterReleaseAlpha_=alpha(180.0F,sampleRate_);
}
float LimiterProcessor::process(float sample) noexcept {
    if(!std::isfinite(sample))sample=0.0F;
    const float magnitude=std::abs(sample);
    const float inputSquare=magnitude*magnitude;
    const float inputAlpha=inputSquare>inputEnergy_?meterAttackAlpha_:meterReleaseAlpha_;
    inputEnergy_=inputAlpha*inputEnergy_+(1.0F-inputAlpha)*inputSquare;

    float output=sample;
    if(parameters_.enabled){
        const float inputDb=db(magnitude);
        const float ratio=parameters_.mode==0?2.0F:parameters_.mode==1?4.0F:12.0F;
        const float knee=parameters_.mode==0?6.0F:parameters_.mode==1?3.0F:0.0F;
        const float delta=inputDb-parameters_.thresholdDb;
        float desiredDb=inputDb;
        if(knee>0.0F&&delta>-knee*0.5F&&delta<knee*0.5F){
            const float t=delta+knee*0.5F;
            desiredDb=inputDb+(1.0F/ratio-1.0F)*t*t/(2.0F*knee);
        }else if(delta>=knee*0.5F){
            desiredDb=parameters_.thresholdDb+delta/ratio;
        }
        desiredDb=std::min(desiredDb,parameters_.ceilingDb);
        const float targetGain=magnitude>1.0e-12F
            ?std::clamp(std::pow(10.0F,(desiredDb-inputDb)/20.0F),0.0F,1.0F):1.0F;
        // Instant attack prevents peaks crossing the target; release is smoothed.
        gain_=targetGain<gain_?targetGain:releaseAlpha_*gain_+(1.0F-releaseAlpha_)*targetGain;
        output=sample*gain_;
        output=std::clamp(output,-ceilingLinear_,ceilingLinear_);
    }else{
        gain_=1.0F;
    }
    const float outputSquare=output*output;
    const float outputAlpha=outputSquare>outputEnergy_?meterAttackAlpha_:meterReleaseAlpha_;
    outputEnergy_=outputAlpha*outputEnergy_+(1.0F-outputAlpha)*outputSquare;
    return output;
}
LimiterMeter LimiterProcessor::meter() const noexcept {
    LimiterMeter result;
    result.inputDb=db(std::sqrt(std::max(0.0F,inputEnergy_)));
    result.outputDb=db(std::sqrt(std::max(0.0F,outputEnergy_)));
    result.gainReductionDb=parameters_.enabled&&gain_<1.0F?20.0F*std::log10(std::max(gain_,1.0e-6F)):0.0F;
    result.active=result.inputDb>-100.0F;
    return result;
}
}
