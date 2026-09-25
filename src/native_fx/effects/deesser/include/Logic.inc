using DeEsserParameters = WinJACKNexus::Common::DeEsserParameters;
using DeEsserProcessor = WinJACKNexus::Common::DeEsserProcessor;
using DeEsserWidget = WinJACKNexus::UI::DeEsserWidget;
struct AtomicDeEsserParameters final {
    std::atomic<float> centerFrequencyHz {6500}, bandwidthHz {4000}, thresholdDb {-12};
    std::atomic<float> attackMs {0}, releaseMs {100}, outputGainDb {0};
    std::atomic<unsigned char> mode {1}, detector {0}, listen {1};
    std::atomic<bool> enabled {true};
    std::atomic<std::uint32_t> revision {1};
    void store(const DeEsserParameters& p) noexcept {
        centerFrequencyHz.store(p.centerFrequencyHz, std::memory_order_relaxed);
        bandwidthHz.store(p.bandwidthHz, std::memory_order_relaxed);
        thresholdDb.store(p.thresholdDb, std::memory_order_relaxed);
        attackMs.store(p.attackMs, std::memory_order_relaxed);
        releaseMs.store(p.releaseMs, std::memory_order_relaxed);
        outputGainDb.store(p.outputGainDb, std::memory_order_relaxed);
        mode.store(static_cast<unsigned char>(p.mode), std::memory_order_relaxed);
        detector.store(static_cast<unsigned char>(p.detector), std::memory_order_relaxed);
        listen.store(static_cast<unsigned char>(p.listen), std::memory_order_relaxed);
        enabled.store(p.enabled, std::memory_order_relaxed);
        revision.fetch_add(1, std::memory_order_release);
    }
    DeEsserParameters load() const noexcept {
        DeEsserParameters p;
        p.centerFrequencyHz = centerFrequencyHz.load(std::memory_order_relaxed);
        p.bandwidthHz = bandwidthHz.load(std::memory_order_relaxed);
        p.thresholdDb = thresholdDb.load(std::memory_order_relaxed);
        p.attackMs = attackMs.load(std::memory_order_relaxed);
        p.releaseMs = releaseMs.load(std::memory_order_relaxed);
        p.outputGainDb = outputGainDb.load(std::memory_order_relaxed);
        p.mode = static_cast<WinJACKNexus::Common::DeEsserMode>(mode.load(std::memory_order_relaxed));
        p.detector = static_cast<WinJACKNexus::Common::DeEsserDetector>(detector.load(std::memory_order_relaxed));
        p.listen = static_cast<WinJACKNexus::Common::DeEsserListen>(listen.load(std::memory_order_relaxed));
        p.enabled = enabled.load(std::memory_order_relaxed);
        return p;
    }
};

QJsonObject toJson(const DeEsserParameters& p) {
    return {{QStringLiteral("centerFrequencyHz"), p.centerFrequencyHz},
            {QStringLiteral("bandwidthHz"), p.bandwidthHz},
            {QStringLiteral("thresholdDb"), p.thresholdDb},
            {QStringLiteral("attackMs"), p.attackMs},
            {QStringLiteral("releaseMs"), p.releaseMs},
            {QStringLiteral("outputGainDb"), p.outputGainDb},
            {QStringLiteral("mode"), static_cast<int>(p.mode)},
            {QStringLiteral("detector"), static_cast<int>(p.detector)},
            {QStringLiteral("listen"), static_cast<int>(p.listen)},
            {QStringLiteral("enabled"), p.enabled}};
}

bool fromJson(const QJsonObject& object, DeEsserParameters& p) {
    if (!readNumber(object, "centerFrequencyHz", 1000, 12000, p.centerFrequencyHz)
        || !readNumber(object, "bandwidthHz", 250, 12000, p.bandwidthHz)
        || !readNumber(object, "thresholdDb", -60, 0, p.thresholdDb)
        || !readNumber(object, "attackMs", 0, 100, p.attackMs)
        || !readNumber(object, "releaseMs", 5, 1000, p.releaseMs)
        || !readNumber(object, "outputGainDb", -24, 12, p.outputGainDb)) return false;
    const auto mode = object.value(QStringLiteral("mode"));
    const auto detector = object.value(QStringLiteral("detector"));
    const auto listen = object.value(QStringLiteral("listen"));
    const auto enabled = object.value(QStringLiteral("enabled"));
    if (!mode.isDouble() || !detector.isDouble() || !listen.isDouble() || !enabled.isBool()
        || mode.toInt(-1) < 0 || mode.toInt(-1) > 2
        || detector.toInt(-1) < 0 || detector.toInt(-1) > 1
        || listen.toInt(-1) < 0 || listen.toInt(-1) > 2) return false;
    p.mode = static_cast<WinJACKNexus::Common::DeEsserMode>(mode.toInt());
    p.detector = static_cast<WinJACKNexus::Common::DeEsserDetector>(detector.toInt());
    p.listen = static_cast<WinJACKNexus::Common::DeEsserListen>(listen.toInt());
    p.enabled = enabled.toBool();
    return true;
}

class DeEsserNode final : public WinJACKNexus::Node::INode {
    struct PortConnections final { std::vector<std::string> names; bool failed = false; };
public:
    DeEsserNode() {
        for (auto& buffer : spectrumBuffers_)
            buffer = std::make_unique<WinJACKNexus::Common::AudioRingBuffer<
                WinJACKNexus::Common::DeEsserSpectrumSample>>(8192);
    }
    ~DeEsserNode() override { destroyWindow(nullptr); destroySettingsWidget(clientNameWidget_); shutdown(); }
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"De-Esser", "Native Fx", "0.1.0", "WinJACKNexus", "Eight-channel native sibilance suppressor."};
    }
    bool init() noexcept override {
        return client_.isOpen() || openClient();
    }
    bool openClient() noexcept {
        if (!client_.open({clientName_.c_str(), true, false, nullptr})) return false;
        spectrumSampleRate_.store(client_.sampleRate(), std::memory_order_relaxed);
        for (std::size_t i = 0; i < 8; ++i) {
            char inputName[16] {}, outputName[16] {};
            std::snprintf(inputName, sizeof(inputName), "in_%s", channelNames[i]);
            std::snprintf(outputName, sizeof(outputName), "out_%s", channelNames[i]);
            if (!inputs_[i].registerPort(client_, inputName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput)
                || !outputs_[i].registerPort(client_, outputName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput)) {
                shutdown(); return false;
            }
            processors_[i].prepare(client_.sampleRate());
        }
        if (client_.setProcessCallback(&DeEsserNode::process, this) != 0
            || client_.setSampleRateCallback(&DeEsserNode::sampleRateChanged, this) != 0
            || !client_.activate()) { shutdown(); return false; }
        return true;
    }
    void shutdown() noexcept override {
        client_.deactivate();
        for (auto& port : inputs_) port.unregisterPort();
        for (auto& port : outputs_) port.unregisterPort();
        client_.close();
    }
    HWND createWindow(HWND) noexcept override {
        auto* widget = createEffectWidget();
        if (!widget) return nullptr;
        widget->show(); widget->raise(); widget->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(widget->winId()));
    }
    void destroyWindow(HWND) noexcept override {
        if (effectWidget_) { delete effectWidget_; effectWidget_ = nullptr; }
    }
    DeEsserWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {
            if (effectWidget_) return effectWidget_;
            auto* widget = new DeEsserWidget(parent);
            widget->setAttribute(Qt::WA_DeleteOnClose, false);
            widget->setWindowTitle(QStringLiteral("De-Esser 去齿音 — %1").arg(QString::fromStdString(clientName_)));
            widget->setParameters(settings_);
            std::array<WinJACKNexus::Common::AudioRingBuffer<
                WinJACKNexus::Common::DeEsserSpectrumSample>*, 8> spectrumSources {};
            for (std::size_t i = 0; i < spectrumSources.size(); ++i)
                spectrumSources[i] = spectrumBuffers_[i].get();
            widget->setSpectrumSources(spectrumSources, &spectrumSampleRate_);
            widget->setChangeHandler([this](std::size_t index, const DeEsserParameters& p) {
                settings_[index] = p; atomic_[index].store(p);
            });
            effectWidget_ = widget;
            return widget;
        } catch (...) { return nullptr; }
    }
    QWidget* createSettingsWidget(QWidget* parent) noexcept {
        try {
            if (clientNameWidget_) return clientNameWidget_;
            auto* widget = new WinJACKNexus::UI::GateClientNameWidget(
                QString::fromStdString(clientName_), [this](const QString& value) { return setClientName(value); }, parent);
            clientNameWidget_ = widget;
            return widget;
        } catch (...) { return nullptr; }
    }
    void destroySettingsWidget(QWidget* widget) noexcept {
        if (widget && widget == clientNameWidget_) { delete widget; clientNameWidget_ = nullptr; }
    }
    bool setClientName(const QString& name) noexcept {
        const auto trimmed = name.trimmed();
        if (trimmed.isEmpty() || trimmed.size() > 32) return false;
        try {
            const std::string nextName = trimmed.toStdString();
            if (nextName == clientName_) return true;
            if (!client_.isOpen()) {
                clientName_ = nextName; clientId_ = nextName;
            } else {
                std::array<PortConnections, 16> connections;
                const auto collect = [](const char* port, void* context) noexcept {
                    auto* values = static_cast<PortConnections*>(context);
                    try { values->names.emplace_back(port); return true; }
                    catch (...) { values->failed = true; return false; }
                };
                for (std::size_t i = 0; i < 8; ++i) {
                    inputs_[i].visitConnections(collect, &connections[i]);
                    outputs_[i].visitConnections(collect, &connections[i + 8]);
                }
                if (std::any_of(connections.begin(), connections.end(),
                                [](const auto& value) { return value.failed; })) return false;
                const std::string oldName = clientName_;
                shutdown();
                clientName_ = nextName;
                if (!openClient()) {
                    clientName_ = oldName;
                    if (openClient()) reconnect(connections);
                    return false;
                }
                if (!reconnect(connections)) return false;
            }
            if (effectWidget_) effectWidget_->setWindowTitle(QStringLiteral("De-Esser 去齿音 — %1").arg(trimmed));
            if (clientNameWidget_) clientNameWidget_->setClientName(trimmed);
            return true;
        } catch (...) { return false; }
    }
    bool reconnect(const std::array<PortConnections, 16>& connections) noexcept {
        bool success = true;
        for (std::size_t i = 0; i < 8; ++i) {
            const char* input = inputs_[i].name();
            const char* output = outputs_[i].name();
            if (!input || !output) return false;
            for (const auto& peer : connections[i].names)
                success = WinJACKNexus::Audio::JackGraph::connect(client_, peer.c_str(), input) && success;
            for (const auto& peer : connections[i + 8].names)
                success = WinJACKNexus::Audio::JackGraph::connect(client_, output, peer.c_str()) && success;
        }
        return success;
    }
    const char* settingsJson() noexcept {
        try {
            QJsonArray channels;
            for (const auto& p : settings_) channels.append(toJson(p));
            serialized_ = QJsonDocument(QJsonObject {{QStringLiteral("channels"), channels},
                {QStringLiteral("clientName"), QString::fromStdString(clientName_)}})
                .toJson(QJsonDocument::Compact).toStdString();
            return serialized_.c_str();
        } catch (...) { return ""; }
    }
    int restoreSettingsJson(const char* json) noexcept {
        if (!json) return 0;
        try {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) return 0;
            const auto root = document.object();
            const auto channels = root.value(QStringLiteral("channels"));
            if (!channels.isArray() || channels.toArray().size() != 8) return 0;
            auto values = settings_;
            const auto array = channels.toArray();
            for (int i = 0; i < 8; ++i)
                if (!array[i].isObject() || !fromJson(array[i].toObject(), values[static_cast<std::size_t>(i)])) return 0;
            const auto savedName = root.value(QStringLiteral("clientName"));
            if (!savedName.isUndefined() && (!savedName.isString() || !setClientName(savedName.toString()))) return 0;
            settings_ = values;
            for (std::size_t i = 0; i < 8; ++i) atomic_[i].store(settings_[i]);
            if (effectWidget_) effectWidget_->setParameters(settings_);
            return 1;
        } catch (...) { return 0; }
    }
    const char* clientId() const noexcept { return clientId_.c_str(); }
private:
    static int process(jack_nframes_t frames, void* context) noexcept {
        auto& self = *static_cast<DeEsserNode*>(context);
        for (std::size_t c = 0; c < 8; ++c) {
            const auto revision = self.atomic_[c].revision.load(std::memory_order_acquire);
            if (revision != self.appliedRevisions_[c]) {
                self.processors_[c].setParameters(self.atomic_[c].load());
                self.appliedRevisions_[c] = revision;
            }
            const auto* input = self.inputs_[c].audioBuffer(frames);
            auto* output = self.outputs_[c].audioBuffer(frames);
            if (!output) continue;
            std::array<WinJACKNexus::Common::DeEsserSpectrumSample, 512> spectrumSamples {};
            const auto totalFrames = static_cast<std::size_t>(frames);
            for (std::size_t start = 0; start < totalFrames;) {
                const auto count = std::min(spectrumSamples.size(), totalFrames - start);
                for (std::size_t offset = 0; offset < count; ++offset) {
                    const auto inputSample = input != nullptr ? input[start + offset] : 0.0F;
                    const auto processedSample = input != nullptr
                        ? self.processors_[c].process(inputSample) : 0.0F;
                    output[start + offset] = processedSample;
                    spectrumSamples[offset] = {inputSample, processedSample};
                }
                self.spectrumBuffers_[c]->write(spectrumSamples.data(), count);
                start += count;
            }
        }
        return 0;
    }
    static int sampleRateChanged(jack_nframes_t rate, void* context) noexcept {
        auto& self = *static_cast<DeEsserNode*>(context);
        self.spectrumSampleRate_.store(static_cast<double>(rate), std::memory_order_relaxed);
        for (auto& processor : self.processors_) processor.prepare(rate);
        self.appliedRevisions_.fill(0);
        return 0;
    }
    JackClient client_;
    std::array<JackPort, 8> inputs_, outputs_;
    std::array<DeEsserProcessor, 8> processors_;
    std::array<std::unique_ptr<WinJACKNexus::Common::AudioRingBuffer<
        WinJACKNexus::Common::DeEsserSpectrumSample>>, 8> spectrumBuffers_;
    std::atomic<double> spectrumSampleRate_ {48000.0};
    std::array<AtomicDeEsserParameters, 8> atomic_;
    std::array<std::uint32_t, 8> appliedRevisions_ {};
    std::array<DeEsserParameters, 8> settings_ {};
    std::string clientName_ = "DeEsser", clientId_ = "DeEsser", serialized_;
    QPointer<DeEsserWidget> effectWidget_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> clientNameWidget_;
};
