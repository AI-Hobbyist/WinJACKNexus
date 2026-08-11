#include <WinJACKNexus/Common/Audio/JackAudioInput.h>
#include <WinJACKNexus/Common/Audio/JackAudioOutput.h>
#include <WinJACKNexus/Common/DSP/LevelMeterProbe.h>
#include <WinJACKNexus/Common/MIDI/JackMidiInput.h>
#include <WinJACKNexus/Common/MIDI/JackMidiOutput.h>
#include <WinJACKNexus/Common/Metering/LoudnessPresetLibrary.h>
#include <WinJACKNexus/Common/IO/CsvLogWriter.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "M1 integration boundary test failure: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    juce::AudioBuffer<float> buffer(2, 4);
    buffer.copyFrom(0, 0, std::array<float, 4> { 0.25f, -0.5f, 0.25f, -0.5f }.data(), 4);
    buffer.copyFrom(1, 0, std::array<float, 4> { 1.0f, 0.0f, -1.0f, 0.0f }.data(), 4);
    wjn::common::LevelMeterProbe probe;
    probe.prepare(2);
    probe.process(buffer);
    const auto& frame = probe.getLatestFrame();
    require(frame.channelCount == 2 && frame.overload, "Meter frame must report channels and overload");
    require(std::abs(frame.peak[0] - 0.5f) < 0.0001f && std::abs(frame.rms[0] - 0.3952847f) < 0.0001f,
            "Meter frame must calculate peak and RMS");
    probe.process(buffer);
    require(frame.peakHold[0] >= frame.peak[0], "Peak hold must not decrease");

    wjn::common::LoudnessPresetLibrary presets;
    require(presets.getPresets().size() >= 9, "Built-in loudness presets must be available");
    require(std::abs(presets.get("ebu_r128").targetLufs + 23.0f) < 0.001f,
            "EBU R128 preset must be available");

    const auto temporaryFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("WinJACKNexus_M1_CsvLogWriterTests.csv");
    temporaryFile.deleteFile();
    {
        wjn::common::CsvLogWriter writer;
        require(writer.enqueue(temporaryFile, "0,-20,-23,-20,-23,-23,-23,0\r\n"),
                "CSV writer must accept a pending row");
    }
    require(temporaryFile.existsAsFile(), "CSV writer must flush before destruction");
    require(temporaryFile.loadFileAsString().contains("timestamp,peak_dbfs"),
            "CSV writer must emit the header");
    temporaryFile.deleteFile();

    wjn::common::JackAudioInput audioInput;
    wjn::common::JackAudioOutput audioOutput;
    wjn::common::JackMidiInput midiInput;
    wjn::common::JackMidiOutput midiOutput;
    const auto audioInputOpened = audioInput.open("WinJACKNexus.M1.AudioInputTest", 2, 256);
    if (audioInputOpened)
    {
        require(audioInput.isOpen(), "Opened audio input must report connected");
        audioInput.close();
    }
    else
        require(!audioInput.getLastError().isEmpty(), "Audio input failures must expose an error message");

    const auto audioOutputOpened = audioOutput.open("WinJACKNexus.M1.AudioOutputTest", 2, 256);
    if (audioOutputOpened)
    {
        require(audioOutput.isOpen(), "Opened audio output must report connected");
        audioOutput.close();
    }
    else
        require(!audioOutput.getLastError().isEmpty(), "Audio output failures must expose an error message");

    const auto midiInputOpened = midiInput.open("WinJACKNexus.M1.MidiInputTest", "midi_in");
    if (midiInputOpened)
    {
        require(midiInput.isOpen(), "Opened MIDI input must report connected");
        midiInput.close();
    }
    else
        require(!midiInput.getLastError().isEmpty(), "MIDI input failures must expose an error message");

    const auto midiOutputOpened = midiOutput.open("WinJACKNexus.M1.MidiOutputTest", "midi_out");
    if (midiOutputOpened)
    {
        require(midiOutput.isOpen(), "Opened MIDI output must report connected");
        midiOutput.close();
    }
    else
        require(!midiOutput.getLastError().isEmpty(), "MIDI output failures must expose an error message");
    std::cout << "M1 integration boundary tests passed\n";
    return 0;
}