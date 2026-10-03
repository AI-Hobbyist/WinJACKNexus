using CompressorParameters = WinJACKNexus::Common::CompressorParameters;
using CompressorProcessor = WinJACKNexus::Common::CompressorProcessor;

struct AtomicCompressorParameters final {
    std::atomic<float> thresholdDb {-18}, ratio {3}, kneeDb {3};
    std::atomic<float> attackMs {10}, releaseMs {100}, makeupDb {0};
    std::atomic<float> highPassHz {80}, lowPassHz {8000};
    std::atomic<bool> enabled {true}, rms {true}, sidechainFilter {true}, autoMakeup {false};
    std::atomic<std::uint32_t> revision {1};
    void store(const CompressorParameters& p) noexcept {
        thresholdDb.store(p.thresholdDb,std::memory_order_relaxed);
        ratio.store(p.ratio,std::memory_order_relaxed);
        kneeDb.store(p.kneeDb,std::memory_order_relaxed);
        attackMs.store(p.attackMs,std::memory_order_relaxed);
        releaseMs.store(p.releaseMs,std::memory_order_relaxed);
        makeupDb.store(p.makeupDb,std::memory_order_relaxed);
        highPassHz.store(p.highPassHz,std::memory_order_relaxed);
        lowPassHz.store(p.lowPassHz,std::memory_order_relaxed);
        enabled.store(p.enabled,std::memory_order_relaxed);
        rms.store(p.rms,std::memory_order_relaxed);
        sidechainFilter.store(p.sidechainFilter,std::memory_order_relaxed);
        autoMakeup.store(p.autoMakeup,std::memory_order_relaxed);
        revision.fetch_add(1,std::memory_order_release);
    }
    CompressorParameters load() const noexcept {
        CompressorParameters p;
        p.thresholdDb=thresholdDb.load(std::memory_order_relaxed);
        p.ratio=ratio.load(std::memory_order_relaxed);
        p.kneeDb=kneeDb.load(std::memory_order_relaxed);
        p.attackMs=attackMs.load(std::memory_order_relaxed);
        p.releaseMs=releaseMs.load(std::memory_order_relaxed);
        p.makeupDb=makeupDb.load(std::memory_order_relaxed);
        p.highPassHz=highPassHz.load(std::memory_order_relaxed);
        p.lowPassHz=lowPassHz.load(std::memory_order_relaxed);
        p.enabled=enabled.load(std::memory_order_relaxed);
        p.rms=rms.load(std::memory_order_relaxed);
        p.sidechainFilter=sidechainFilter.load(std::memory_order_relaxed);
        p.autoMakeup=autoMakeup.load(std::memory_order_relaxed);
        return p;
    }
};

class CompressorNode final : public WinJACKNexus::Node::INode {
    struct PortConnections final { std::vector<std::string> names; bool failed = false; };
public:
    ~CompressorNode() override { destroyWindow(nullptr); destroySettingsWidget(clientNameWidget_); shutdown(); }
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"Compressor", "Native Fx", "0.1.0", "WinJACKNexus", "Eight-channel native compressor control UI."};
    }
    bool init() noexcept override {
        if (client_.isOpen()) return true;
        return openClient();
    }
    bool openClient() noexcept {
        if (!client_.open({clientName_.c_str(), true, false, nullptr})) return false;
        try { clientId_ = client_.name() == nullptr ? clientName_ : client_.name(); }
        catch (...) { shutdown(); return false; }
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
        if (client_.setProcessCallback(&CompressorNode::process, this) != 0
            || client_.setSampleRateCallback(&CompressorNode::sampleRateChanged, this) != 0
            || !client_.activate()) {
            shutdown(); return false;
        }
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
    WinJACKNexus::UI::CompressorWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {
            if (!effectWidget_) {
                auto* widget = new WinJACKNexus::UI::CompressorWidget(parent);
                widget->setAttribute(Qt::WA_DeleteOnClose, false);
                WinJACKNexus::NativeFx::I18n::bind(widget, [this, widget] {
                    widget->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Compressor — %1").arg(QString::fromStdString(clientName_)));
                    widget->update();
                });
                widget->setParameters(settings_);
                widget->setChangeHandler([this](std::size_t index, const CompressorParameters& values) {
                    settings_[index] = values;
                    atomic_[index].store(values);
                });
                widget->setMeterReader([this] {
                    std::array<WinJACKNexus::UI::CompressorMeter, 8> values {};
                    const auto activeMask=meterActiveMask_.load(std::memory_order_acquire);
                    for(std::size_t channel=0;channel<values.size();++channel) {
                        values[channel].inputDb=meterInputDb_[channel].load(std::memory_order_relaxed);
                        values[channel].outputDb=meterOutputDb_[channel].load(std::memory_order_relaxed);
                        values[channel].active=(activeMask&(1u<<channel))!=0;
                        values[channel].gainReductionDb=meterGainReductionDb_[channel].load(std::memory_order_relaxed);
                    }
                    return values;
                });
                effectWidget_ = widget;
            }
            return effectWidget_;
        } catch (...) { return nullptr; }
    }
    void destroyWindow(HWND) noexcept override { delete effectWidget_; effectWidget_ = nullptr; }
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
        if (!widget || widget != clientNameWidget_) return;
        delete widget; clientNameWidget_ = nullptr;
    }
    bool setClientName(const QString& requestedName) noexcept {
        const auto trimmed = requestedName.trimmed();
        if (trimmed.isEmpty() || trimmed.size() > 32) return false;
        try {
            const std::string nextName = trimmed.toStdString();
            if (nextName == clientName_) return true;
            if (!client_.isOpen()) {
                clientName_ = nextName;
                clientId_ = nextName;
            } else {
                std::array<PortConnections,16> connections;
                const auto collect = [](const char* name, void* context) noexcept {
                    auto* values = static_cast<PortConnections*>(context);
                    try { values->names.emplace_back(name); return true; }
                    catch (...) { values->failed = true; return false; }
                };
                for (std::size_t i=0;i<8;++i) {
                    inputs_[i].visitConnections(collect,&connections[i]);
                    outputs_[i].visitConnections(collect,&connections[i+8]);
                }
                if (std::any_of(connections.begin(),connections.end(),[](const auto& item){return item.failed;})) return false;
                const std::string oldName = clientName_;
                shutdown();
                clientName_ = nextName;
                if (!openClient()) {
                    clientName_ = oldName;
                    if (openClient()) reconnect(connections);
                    return false;
                }
                if (effectWidget_)
                    effectWidget_->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Compressor — %1").arg(trimmed));
                if (clientNameWidget_) clientNameWidget_->setClientName(trimmed);
                if (!reconnect(connections)) return false;
            }
            return true;
        } catch (...) { return false; }
    }
    bool reconnect(const std::array<PortConnections,16>& connections) noexcept {
        bool success = true;
        for (std::size_t i=0;i<8;++i) {
            const char* input=inputs_[i].name(); const char* output=outputs_[i].name();
            if (!input || !output) return false;
            for (const auto& peer:connections[i].names)
                success=WinJACKNexus::Audio::JackGraph::connect(client_,peer.c_str(),input)&&success;
            for (const auto& peer:connections[i+8].names)
                success=WinJACKNexus::Audio::JackGraph::connect(client_,output,peer.c_str())&&success;
        }
        return success;
    }
    const char* settingsJson() noexcept {
        try {
            QJsonArray channels;
            for (const auto& p : settings_) channels.append(QJsonObject{
                {"thresholdDb",p.thresholdDb},{"ratio",p.ratio},{"kneeDb",p.kneeDb},
                {"attackMs",p.attackMs},{"releaseMs",p.releaseMs},{"makeupDb",p.makeupDb},
                {"highPassHz",p.highPassHz},{"lowPassHz",p.lowPassHz},{"enabled",p.enabled},
                {"rms",p.rms},{"sidechainFilter",p.sidechainFilter},{"autoMakeup",p.autoMakeup}});
            serialized_ = QJsonDocument(QJsonObject{{"channels",channels},{"clientName",QString::fromStdString(clientName_)}})
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
            const auto arrayValue = root.value("channels");
            if (!arrayValue.isArray() || arrayValue.toArray().size() != 8) return 0;
            auto values = settings_;
            const auto array = arrayValue.toArray();
            for (int i = 0; i < 8; ++i) {
                if (!array[i].isObject()) return 0;
                const auto obj = array[i].toObject();
                auto& p = values[i];
                if (!readNumber(obj,"thresholdDb",-60,0,p.thresholdDb)
                    || !readNumber(obj,"ratio",1,20,p.ratio)
                    || !readNumber(obj,"kneeDb",0,24,p.kneeDb)
                    || !readNumber(obj,"attackMs",0.1F,200,p.attackMs)
                    || !readNumber(obj,"releaseMs",1,2000,p.releaseMs)
                    || !readNumber(obj,"makeupDb",-12,24,p.makeupDb)
                    || !readNumber(obj,"highPassHz",20,2000,p.highPassHz)
                    || !readNumber(obj,"lowPassHz",500,20000,p.lowPassHz)) return 0;
                const auto enabled=obj.value("enabled"),rms=obj.value("rms"),filter=obj.value("sidechainFilter"),makeup=obj.value("autoMakeup");
                if (!enabled.isBool() || !rms.isBool() || !filter.isBool() || !makeup.isBool()) return 0;
                p.enabled=enabled.toBool();p.rms=rms.toBool();p.sidechainFilter=filter.toBool();p.autoMakeup=makeup.toBool();
            }
            const auto name = root.value("clientName");
            const QString requestedName = name.isUndefined() ? QString::fromStdString(clientName_)
                                                              : name.isString() ? name.toString() : QString();
            if (requestedName.trimmed().isEmpty() || requestedName.trimmed().size() > 32
                || !setClientName(requestedName)) return 0;
            settings_ = values;
            for(std::size_t channel=0;channel<settings_.size();++channel)
                atomic_[channel].store(settings_[channel]);
            if (effectWidget_) effectWidget_->setParameters(settings_);
            return 1;
        } catch (...) { return 0; }
    }
    const char* clientId() const noexcept { return clientId_.c_str(); }
    const char* clientName() const noexcept { return clientName_.c_str(); }
    bool hasFixedPortLayout() const noexcept { return fixedNodePorts(inputs_, outputs_); }
private:
    static int process(jack_nframes_t frames, void* context) noexcept {
        auto& self = *static_cast<CompressorNode*>(context);
        std::uint8_t activeMask=0;
        for (std::size_t c=0;c<8;++c) {
            const auto revision=self.atomic_[c].revision.load(std::memory_order_acquire);
            if(revision!=self.appliedRevisions_[c]) {
                self.processors_[c].setParameters(self.atomic_[c].load());
                self.appliedRevisions_[c]=revision;
            }
            const auto* input=self.inputs_[c].audioBuffer(frames);
            auto* output=self.outputs_[c].audioBuffer(frames);
            if(output==nullptr) {
                self.processors_[c].reset();
                self.meterInputDb_[c].store(-120.0F,std::memory_order_relaxed);
                self.meterOutputDb_[c].store(-120.0F,std::memory_order_relaxed);
                self.meterGainReductionDb_[c].store(0.0F,std::memory_order_relaxed);
                continue;
            }
            if(input==nullptr) {
                std::fill_n(output,frames,0.0F);
                self.processors_[c].reset();
                self.meterInputDb_[c].store(-120.0F,std::memory_order_relaxed);
                self.meterOutputDb_[c].store(-120.0F,std::memory_order_relaxed);
                self.meterGainReductionDb_[c].store(0.0F,std::memory_order_relaxed);
                continue;
            }
            for(jack_nframes_t frame=0;frame<frames;++frame)
                output[frame]=self.processors_[c].process(input[frame]);
            const float inputDb=self.processors_[c].inputLevelDb();
            self.meterInputDb_[c].store(inputDb,std::memory_order_relaxed);
            self.meterOutputDb_[c].store(self.processors_[c].outputLevelDb(),std::memory_order_relaxed);
            self.meterGainReductionDb_[c].store(self.processors_[c].gainReductionDb(),std::memory_order_relaxed);
            if(inputDb>-100.0F)activeMask|=static_cast<std::uint8_t>(1u<<c);
        }
        self.meterActiveMask_.store(activeMask,std::memory_order_release);
        return 0;
    }
    static int sampleRateChanged(jack_nframes_t rate,void* context) noexcept {
        auto& self=*static_cast<CompressorNode*>(context);
        for(auto& processor:self.processors_)processor.prepare(static_cast<double>(rate));
        self.appliedRevisions_.fill(0);
        return 0;
    }
    JackClient client_;
    std::array<JackPort,8> inputs_,outputs_;
    std::array<CompressorProcessor,8> processors_ {};
    std::array<AtomicCompressorParameters,8> atomic_ {};
    std::array<std::uint32_t,8> appliedRevisions_ {};
    std::array<std::atomic<float>,8> meterInputDb_ {};
    std::array<std::atomic<float>,8> meterOutputDb_ {};
    std::array<std::atomic<float>,8> meterGainReductionDb_ {};
    std::atomic<std::uint8_t> meterActiveMask_ {0};
    std::array<CompressorParameters,8> settings_ {};
    std::string clientName_="Compressor",clientId_="Compressor",serialized_;
    QPointer<WinJACKNexus::UI::CompressorWidget> effectWidget_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> clientNameWidget_;
};
