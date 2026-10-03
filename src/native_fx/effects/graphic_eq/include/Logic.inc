struct AtomicGraphicEqParameters final {
    std::array<std::atomic<float>, WinJACKNexus::Common::graphicEqBandCount> bands {};
    std::atomic<float> outputGainDb {0.0F};
    std::atomic<bool> enabled {true};
    std::atomic<std::uint32_t> revision {1};
    void store(const WinJACKNexus::Common::GraphicEqParameters& p) noexcept {
        for (std::size_t i = 0; i < bands.size(); ++i)
            bands[i].store(p.bandGainsDb[i], std::memory_order_relaxed);
        outputGainDb.store(p.outputGainDb, std::memory_order_relaxed);
        enabled.store(p.enabled, std::memory_order_relaxed);
        revision.fetch_add(1, std::memory_order_release);
    }
    WinJACKNexus::Common::GraphicEqParameters load() const noexcept {
        WinJACKNexus::Common::GraphicEqParameters p;
        for (std::size_t i = 0; i < bands.size(); ++i)
            p.bandGainsDb[i] = bands[i].load(std::memory_order_relaxed);
        p.outputGainDb = outputGainDb.load(std::memory_order_relaxed);
        p.enabled = enabled.load(std::memory_order_relaxed);
        return p;
    }
};

QJsonObject toJson(const WinJACKNexus::Common::GraphicEqParameters& p) {
    QJsonArray bands;
    for (const auto gain : p.bandGainsDb) bands.append(gain);
    return {{QStringLiteral("bands"), bands}, {QStringLiteral("outputGainDb"), p.outputGainDb},
            {QStringLiteral("enabled"), p.enabled}};
}

bool fromJson(const QJsonObject& object, WinJACKNexus::Common::GraphicEqParameters& p) {
    const auto bands = object.value(QStringLiteral("bands"));
    const auto output = object.value(QStringLiteral("outputGainDb"));
    const auto enabled = object.value(QStringLiteral("enabled"));
    if (!bands.isArray() || bands.toArray().size() != static_cast<int>(WinJACKNexus::Common::graphicEqBandCount)
        || !output.isDouble() || !std::isfinite(output.toDouble()) || output.toDouble() < -24 || output.toDouble() > 12
        || !enabled.isBool())
        return false;
    const auto values = bands.toArray();
    for (int i = 0; i < values.size(); ++i) {
        if (!values[i].isDouble() || !std::isfinite(values[i].toDouble())
            || values[i].toDouble() < -12 || values[i].toDouble() > 12)
            return false;
        p.bandGainsDb[static_cast<std::size_t>(i)] = static_cast<float>(values[i].toDouble());
    }
    p.outputGainDb = static_cast<float>(output.toDouble());
    p.enabled = enabled.toBool();
    return true;
}

class GraphicEqNode final : public WinJACKNexus::Node::INode {
    struct PortConnections final { std::vector<std::string> names; bool failed = false; };
public:
    ~GraphicEqNode() override { destroyWindow(nullptr); destroySettingsWidget(clientNameWidget_); shutdown(); }
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"Graphic EQ", "Native Fx", "0.1.0", "WinJACKNexus", "Eight-channel 31-band graphic equalizer."};
    }
    bool init() noexcept override { return client_.isOpen() || openClient(); }
    bool openClient() noexcept {
        if (!client_.open({clientName_.c_str(), true, false, nullptr})) return false;
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
        spectrumAnalyzer_->setSampleRate(client_.sampleRate());
        if (client_.setProcessCallback(&GraphicEqNode::process, this) != 0
            || client_.setSampleRateCallback(&GraphicEqNode::sampleRateChanged, this) != 0
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
        if (widget == nullptr) return nullptr;
        widget->show(); widget->raise(); widget->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(widget->winId()));
    }
    void destroyWindow(HWND) noexcept override { destroyEffectWidget(effectWidget_); }
    WinJACKNexus::UI::GraphicEqWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {
            if (effectWidget_) return effectWidget_;
            auto* widget = new WinJACKNexus::UI::GraphicEqWidget(parent);
            widget->setAttribute(Qt::WA_DeleteOnClose, false);
            WinJACKNexus::NativeFx::I18n::bind(widget, [this, widget] {
                widget->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Graphic EQ — %1").arg(QString::fromStdString(clientName_)));
                widget->update();
            });
            widget->setParameters(settings_);
            widget->setChangeHandler([this](std::size_t index, const WinJACKNexus::Common::GraphicEqParameters& p) {
                settings_[index] = p; atomic_[index].store(p);
            });
            widget->setSpectrumFrameReader([this](WinJACKNexus::Common::GraphicEqSpectrumFrame& frame) {
                return spectrumAnalyzer_->tryPopLatest(frame);
            });
            effectWidget_ = widget;
            return widget;
        } catch (...) { return nullptr; }
    }
    void destroyEffectWidget(QWidget* widget) noexcept {
        if (widget == nullptr || widget != effectWidget_) return;
        delete widget; effectWidget_ = nullptr;
    }
    QWidget* createSettingsWidget(QWidget* parent) noexcept {
        try {
            if (clientNameWidget_)
                return clientNameWidget_;
            auto* widget = new WinJACKNexus::UI::GateClientNameWidget(
                QString::fromStdString(clientName_),
                [this](const QString& value) { return setClientName(value); }, parent);
            clientNameWidget_ = widget;
            return widget;
        } catch (...) {
            return nullptr;
        }
    }
    void destroySettingsWidget(QWidget* widget) noexcept {
        if (widget == nullptr || widget != clientNameWidget_)
            return;
        delete widget;
        clientNameWidget_ = nullptr;
    }
    bool setClientName(const QString& requestedName) noexcept {
        const auto trimmed = requestedName.trimmed();
        if (trimmed.isEmpty() || trimmed.size() > 32)
            return false;
        try {
            const std::string nextName = trimmed.toStdString();
            if (nextName == clientName_)
                return true;
            if (!client_.isOpen()) {
                clientName_ = nextName;
                clientId_ = nextName;
            } else {
                std::array<PortConnections, 16> connections;
                const auto collect = [](const char* name, void* context) noexcept {
                    auto* values = static_cast<PortConnections*>(context);
                    try {
                        values->names.emplace_back(name);
                        return true;
                    } catch (...) {
                        values->failed = true;
                        return false;
                    }
                };
                for (std::size_t i = 0; i < 8; ++i) {
                    inputs_[i].visitConnections(collect, &connections[i]);
                    outputs_[i].visitConnections(collect, &connections[i + 8]);
                }
                if (std::any_of(connections.begin(), connections.end(),
                                [](const auto& item) { return item.failed; }))
                    return false;
                const auto oldName = clientName_;
                shutdown();
                clientName_ = nextName;
                if (!openClient()) {
                    clientName_ = oldName;
                    if (openClient())
                        reconnect(connections);
                    return false;
                }
                if (!reconnect(connections))
                    return false;
            }
            if (effectWidget_)
                effectWidget_->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Graphic EQ — %1").arg(trimmed));
            if (clientNameWidget_)
                clientNameWidget_->setClientName(trimmed);
            return true;
        } catch (...) {
            return false;
        }
    }
    bool reconnect(const std::array<PortConnections, 16>& connections) noexcept {
        bool success = true;
        for (std::size_t i = 0; i < 8; ++i) {
            const char* input = inputs_[i].name();
            const char* output = outputs_[i].name();
            if (input == nullptr || output == nullptr)
                return false;
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
        if (json == nullptr) return 0;
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
            if (!savedName.isUndefined() && (!savedName.isString() || !setClientName(savedName.toString())))
                return 0;
            settings_ = values;
            for (std::size_t i = 0; i < 8; ++i) atomic_[i].store(settings_[i]);
            if (effectWidget_) effectWidget_->setParameters(settings_);
            return 1;
        } catch (...) { return 0; }
    }
    const char* clientId() const noexcept { return clientId_.c_str(); }
    const char* clientName() const noexcept { return clientName_.c_str(); }
    bool hasFixedPortLayout() const noexcept { return fixedNodePorts(inputs_, outputs_); }
private:
    static int process(jack_nframes_t frames, void* context) noexcept {
        auto& self = *static_cast<GraphicEqNode*>(context);
        std::array<const float*, 8> inputBuffers {};
        std::array<float*, 8> outputBuffers {};
        for (std::size_t c = 0; c < 8; ++c) {
            const auto revision = self.atomic_[c].revision.load(std::memory_order_acquire);
            if (revision != self.appliedRevisions_[c]) {
                self.processors_[c].setParameters(self.atomic_[c].load());
                self.appliedRevisions_[c] = revision;
            }
            inputBuffers[c] = self.inputs_[c].audioBuffer(frames);
            outputBuffers[c] = self.outputs_[c].audioBuffer(frames);
        }
        for (jack_nframes_t frame = 0; frame < frames; ++frame) {
            std::array<float, WinJACKNexus::Common::graphicEqSpectrumChannelCount> inputSamples {};
            std::array<float, WinJACKNexus::Common::graphicEqSpectrumChannelCount> outputSamples {};
            for (std::size_t c = 0; c < 8; ++c) {
                const auto* input = inputBuffers[c];
                const auto inputSample = input != nullptr ? input[frame] : 0.0F;
                const auto outputSample = self.processors_[c].process(inputSample);
                if (auto* output = outputBuffers[c])
                    output[frame] = outputSample;
                inputSamples[c] = inputSample;
                outputSamples[c] = outputSample;
            }
            self.spectrumAnalyzer_->pushSamples(inputSamples, outputSamples);
        }
        return 0;
    }
    static int sampleRateChanged(jack_nframes_t rate, void* context) noexcept {
        auto& self = *static_cast<GraphicEqNode*>(context);
        for (auto& processor : self.processors_) processor.prepare(rate);
        self.spectrumAnalyzer_->setSampleRate(rate);
        self.appliedRevisions_.fill(0);
        return 0;
    }
    WinJACKNexus::Audio::JackClient client_;
    std::array<WinJACKNexus::Audio::JackPort, 8> inputs_, outputs_;
    std::array<WinJACKNexus::Common::GraphicEqProcessor, 8> processors_;
    std::unique_ptr<WinJACKNexus::Common::GraphicEqSpectrumAnalyzer> spectrumAnalyzer_
        = std::make_unique<WinJACKNexus::Common::GraphicEqSpectrumAnalyzer>();
    std::array<AtomicGraphicEqParameters, 8> atomic_;
    std::array<std::uint32_t, 8> appliedRevisions_ {};
    std::array<WinJACKNexus::Common::GraphicEqParameters, 8> settings_ {};
    std::string clientName_ = "GraphicEQ", clientId_ = "GraphicEQ", serialized_;
    QPointer<WinJACKNexus::UI::GraphicEqWidget> effectWidget_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> clientNameWidget_;
};
