// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QString>
#include <array>
#include <cstdint>
#include <vector>

namespace WinJACKNexus::Common::ClassicMixer
{
constexpr int maximumInputs = 20;
constexpr int maximumAudioInputs = 24;
constexpr int outputChannels = 8;
constexpr double silenceDb = -96.0;

struct Equalizer
{
    bool enabled = false;
    double low = 0;
    double mid = 0;
    double high = 0;
    double frequency = 1000;
};

enum class Listen
{
    off,
    afl,
    pfl
};

struct Input
{
    QString name;
    bool stereo = false;
    double gain = 0;
    Equalizer eq;
    std::array<double, 2> sends{silenceDb, silenceDb};
    std::array<bool, 2> pre{false, false};
    std::array<bool, 2> enabled{false, false};
    double pan = 0;
    double fader = 0;
    bool mix = true;
    bool mute = false;
    bool solo = false;
    Listen listen = Listen::off;
};

struct Preset
{
    QString id;
    QString name;
    QByteArray payload;
};

struct State
{
    int layout = 16;
    std::vector<Input> inputs;
    Equalizer masterEq;
    std::array<double, 2> auxMaster{0, 0};
    double masterFader = 0;
    double monitorFader = 0;
    bool masterMute = false;
    bool postMeter = true;
    bool layoutChosen = false;
    std::vector<Preset> presets;
    QString selectedPreset;

    static State create(int layout);
    bool validate(QString *error = nullptr) const;
    int audioInputCount() const noexcept;
    QJsonObject toJson(bool includePresets = true) const;
    static bool fromJson(const QJsonObject &json, State &result, QString *error = nullptr);
    bool savePreset(const QString &name, const QString &overwriteId = {});
    bool applyPreset(const QString &id, State &result, QString *error = nullptr) const;
};

struct Port
{
    std::uint32_t id;
    bool input;
    QString name;
};
std::vector<Port> ports(const State &state);
double gainForDb(double db) noexcept;
double faderDb(double position) noexcept;
double faderPosition(double db) noexcept;

struct Coefficients
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};
struct EqConfiguration
{
    std::array<Coefficients, 3> bands;
    bool enabled = false;
};
struct InputConfiguration
{
    EqConfiguration eq;
    double gain = 1;
    double fader = 1;
    std::array<double, 2> pan{1, 1};
    std::array<double, 2> sends{};
    std::array<bool, 2> pre{};
    bool stereo = false;
    bool mix = true;
    bool mute = false;
    bool solo = false;
    Listen listen = Listen::off;
};
// Fixed-size control-thread prepared data. No strings or ownership in the
// callback.
struct Configuration
{
    std::array<InputConfiguration, maximumInputs> inputs;
    EqConfiguration masterEq;
    std::array<double, 2> auxMaster{1, 1};
    int inputCount = 0;
    double masterFader = 1;
    double monitorFader = 1;
    bool masterMute = false;
};
Configuration configuration(const State &state, double sampleRate);

struct Levels
{
    // Input strips followed by Master and Monitor; values are linear sample
    // peaks.
    std::array<std::array<float, 2>, maximumInputs + 2> input{};
    std::array<std::array<float, 2>, maximumInputs + 2> post{};
};

class Processor
{
  public:
    bool prepare(double sampleRate, std::uint32_t maximumFrames,
                 const Configuration &configuration) noexcept;
    void setConfiguration(const Configuration &configuration) noexcept;
    void reset() noexcept;
    void process(const float *const *inputs, float *const *outputs, std::uint32_t frames) noexcept;
    const Levels &levels() const noexcept
    {
        return levels_;
    }

  private:
    struct Filter
    {
        double z1 = 0, z2 = 0;
        double process(double sample, const Coefficients &coefficients) noexcept;
    };
    using EqHistory = std::array<std::array<Filter, 3>, 2>;
    std::array<EqHistory, maximumInputs + 1> history_{};
    Configuration target_{};
    Configuration current_{};
    Levels levels_{};
    double smoothing_ = 1;
    std::uint32_t maximumFrames_ = 0;
    static double equalize(double value, int side, EqHistory &history,
                           const EqConfiguration &configuration) noexcept;
};
} // namespace WinJACKNexus::Common::ClassicMixer
