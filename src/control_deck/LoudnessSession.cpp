// SPDX-License-Identifier: AGPL-3.0-only
#include "LoudnessSession.hpp"
#include <QDateTime>
#include <QElapsedTimer>
#include <QSettings>
#include <WinJACKNexus/Common/LoudnessEngine.hpp>
#include <WinJACKNexus/Common/SilenceResetController.hpp>
#include <WinJACKNexus/Meter/MeterCsvWriter.hpp>
#include <algorithm>
#include <chrono>

namespace WinJACKNexus::ControlDeck
{
LoudnessSession::LoudnessSession(std::shared_ptr<Common::AnalysisCapture> source)
    : source_(std::move(source)), worker_([this](std::stop_token stop) { run(stop); })
{
    configure();
}
LoudnessSession::~LoudnessSession()
{
    worker_.request_stop();
    worker_.join();
}
void LoudnessSession::configure()
{
    const std::lock_guard lock(configurationMutex_);
    configuration_ = {source_->csvEnabled, source_->csvUseGlobal, source_->csvDirectory, source_->instanceId,
                      source_->clientName};
}
void LoudnessSession::run(std::stop_token stop)
{
    Common::LoudnessEngine engine;
    Common::SilenceResetController silence;
    const auto layout = source_->channels == 1   ? Common::MeterChannelLayout::mono()
                        : source_->channels == 2 ? Common::MeterChannelLayout::stereo()
                        : source_->channels == 6 ? Common::MeterChannelLayout::fivePointOne()
                                                 : Common::MeterChannelLayout::sevenPointOne();
    unsigned rate = 48000;
    engine.prepare({double(rate), true}, layout);
    std::uint64_t revision = 0, lastCsvFrame = 0;
    std::array<std::array<float, 256>, 8> samples{};
    std::array<const float *, 8> inputs{};
    for (unsigned channel = 0; channel < source_->channels; ++channel)
        inputs[channel] = samples[channel].data();
    unsigned pending = 0;
    bool discontinuity = false;
    double threshold = -70, duration = 3;
    QString globalDirectory, activeDirectory, activeId, status;
    std::unique_ptr<Meter::MeterCsvWriter> csv;
    QElapsedTimer settingsClock;
    Common::MeterSnapshot snapshot;
    while (!stop.stop_requested())
    {
        if (!settingsClock.isValid() || settingsClock.elapsed() >= 250)
        {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                               QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
            const double nextThreshold = settings.value("silenceThresholdDbfs", -70.0).toDouble();
            const double nextDuration = settings.value("silenceDurationSeconds", 3.0).toDouble();
            if (threshold != nextThreshold || duration != nextDuration)
                if (silence.configure(nextThreshold, nextDuration))
                {
                    threshold = nextThreshold;
                    duration = nextDuration;
                }
            globalDirectory = settings.value("logRootDirectory").toString().trimmed();
            if (settingsClock.isValid())
                settingsClock.restart();
            else
                settingsClock.start();
        }
        Configuration config;
        {
            const std::lock_guard lock(configurationMutex_);
            config = configuration_;
        }
        const auto directory = config.useGlobal ? globalDirectory : config.directory.trimmed();
        if (csv && (!config.enabled || directory != activeDirectory || config.instanceId != activeId))
        {
            csv->closeSession(activeId);
            csv.reset();
            lastCsvFrame = 0;
        }
        if (config.enabled && !directory.isEmpty() && !csv)
        {
            csv = std::make_unique<Meter::MeterCsvWriter>();
            activeDirectory = directory;
            activeId = config.instanceId;
        }
        status = config.enabled ? (directory.isEmpty() ? QStringLiteral("CSV destination not configured")
                                                       : QStringLiteral("CSV recording"))
                                : QString();
        const auto currentRevision = source_->revision.load(std::memory_order_acquire);
        if (revision != currentRevision)
        {
            revision = currentRevision;
            source_->samples.clear();
            engine.reset();
            silence.reset();
            pending = 0;
            discontinuity = true;
            lastCsvFrame = 0;
            snapshot = engine.snapshot();
        }
        Common::AnalysisFrame frame;
        unsigned read = 0;
        while (read++ < source_->samples.capacity() && source_->samples.try_pop(frame))
        {
            if (frame.sampleRate != rate)
            {
                rate = frame.sampleRate;
                engine.prepare({double(rate), true}, layout);
                silence.reset();
                pending = 0;
                lastCsvFrame = 0;
                discontinuity = true;
            }
            for (unsigned channel = 0; channel < source_->channels; ++channel)
                samples[channel][pending] = frame.samples[channel];
            if (++pending < 256)
                continue;
            engine.process(inputs.data(), source_->channels, pending);
            snapshot = engine.snapshot();
            if (silence.update(snapshot.samplePeakDbfs, double(pending) / rate))
            {
                engine.reset();
                engine.process(inputs.data(), source_->channels, pending);
                snapshot = engine.snapshot();
                lastCsvFrame = 0;
                discontinuity = true;
            }
            pending = 0;
            if (csv && snapshot.processedFrames - lastCsvFrame >= rate / 10)
            {
                const auto value = [](Common::MeterValue value) -> std::optional<float>
                { return value.valid ? std::optional<float>(float(value.value)) : std::nullopt; };
                Meter::MeterHistoryPoint point;
                point.elapsedMilliseconds = std::int64_t(snapshot.processedFrames * 1000 / rate);
                point.systemTimeMilliseconds = QDateTime::currentMSecsSinceEpoch();
                point.discontinuity = discontinuity;
                point.metrics = {value(snapshot.samplePeakDbfs), value(snapshot.rmsDbfs),
                                 value(snapshot.truePeakDbtp),   value(snapshot.momentaryLufs),
                                 value(snapshot.shortTermLufs),  value(snapshot.integratedLufs),
                                 value(snapshot.loudnessRangeLu)};
                point.lfePeakDbfs = value(snapshot.lfePeakDbfs);
                csv->enqueue({config.instanceId, config.clientName, directory, point});
                lastCsvFrame = snapshot.processedFrames;
                discontinuity = false;
            }
        }
        if (csv)
        {
            const auto state = csv->status(activeId);
            if (!state.statusDetail.isEmpty())
                status = state.statusDetail;
        }
        source_->publishLoudness(snapshot, status);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (csv)
        csv->closeSession(activeId);
}
} // namespace WinJACKNexus::ControlDeck
