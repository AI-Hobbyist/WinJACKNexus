// SPDX-License-Identifier: AGPL-3.0-only
// Exercise the private production worker without exporting test hooks in the DLL.
#include "../../src/bc/Service.cpp"
#include <QTextStream>
#include <chrono>
#include <cstdio>
#include <numbers>
#include <thread>

namespace BC = WinJACKNexus::Broadcast;
namespace Model = WinJACKNexus::Common::Broadcast;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 2)
        return 1;
    BC::ConnectionSettings settings;
    settings.serverBaseUrl = QUrl(QString::fromUtf8(argv[1]));
    Model::State state;
    state.addStream(QStringLiteral("Period 256"), Model::Layout::stereo);
    auto audio = std::make_shared<Model::AudioStream>(48000, state.streams[0].controls());
    BC::Worker worker;
    // Model a delayed Windows timer delivery; production's timer stays at 1 ms.
    worker.pump.setInterval(20);
    worker.update({{state.streams[0], {}, audio}});
    bool ready = false;
    worker.report = [&](const BC::ServiceStatus &status)
    {
        if (!status.error.isEmpty())
        {
            std::fprintf(stderr, "%s\n", status.error.toUtf8().constData());
            application.exit(2);
        }
        if (!ready && status.activeStreams)
        {
            ready = true;
            std::puts(QJsonDocument(QJsonObject{{"stream", state.streams[0].id}})
                          .toJson(QJsonDocument::Compact)
                          .constData());
            std::fflush(stdout);
        }
    };
    worker.session.start(settings, QTextStream(stdin).readLine().toUtf8());
    std::jthread producer(
        [&](std::stop_token stop)
        {
            const auto start = std::chrono::steady_clock::now();
            std::uint64_t position = 0;
            std::array<float, 256> samples{};
            const float *inputs[]{samples.data(), samples.data()};
            while (!stop.stop_requested())
            {
                for (unsigned frame = 0; frame < samples.size(); ++frame)
                    samples[frame] =
                        float(0.2 * std::sin(2 * std::numbers::pi * 440 * (position + frame) / 48000));
                audio->render(state.streams[0].controls(), false, inputs, nullptr, 256);
                position += 256;
                std::this_thread::sleep_until(start +
                                              std::chrono::nanoseconds(position * 1000000000 / 48000));
            }
        });
    QTimer::singleShot(2000, &application, &QCoreApplication::quit);
    const int result = application.exec();
    producer.request_stop();
    producer.join();
    const auto dropped = audio->droppedFrames();
    const auto stale = worker.staleFrames;
    worker.report = {};
    worker.stop();
    std::printf("RESULT ready=%d dropped=%llu stale=%llu\n", ready, static_cast<unsigned long long>(dropped),
                static_cast<unsigned long long>(stale));
    return result || !ready || dropped || stale ? 2 : 0;
}
