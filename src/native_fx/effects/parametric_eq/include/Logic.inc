/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
using ParametricEqParameters = WinJACKNexus::Common::ParametricEqParameters;
using ParametricEqProcessor = WinJACKNexus::Common::ParametricEqProcessor;
using ParametricEqWidget = WinJACKNexus::UI::ParametricEqWidget;

struct AtomicParametricEqBand final {
    std::atomic<float> frequencyHz {1000}, q {1}, gainDb {0};
    std::atomic<int> shape {1};
    std::atomic<bool> enabled {true};
    void store(const WinJACKNexus::Common::ParametricEqBand& p) noexcept {
        frequencyHz.store(p.frequencyHz,std::memory_order_relaxed);
        q.store(p.q,std::memory_order_relaxed);
        gainDb.store(p.gainDb,std::memory_order_relaxed);
        shape.store(static_cast<int>(p.shape),std::memory_order_relaxed);
        enabled.store(p.enabled,std::memory_order_relaxed);
    }
    WinJACKNexus::Common::ParametricEqBand load() const noexcept {
        return {frequencyHz.load(std::memory_order_relaxed),q.load(std::memory_order_relaxed),
            gainDb.load(std::memory_order_relaxed),static_cast<WinJACKNexus::Common::ParametricEqShape>(shape.load(std::memory_order_relaxed)),
            enabled.load(std::memory_order_relaxed)};
    }
};
struct AtomicParametricEqParameters final {
    std::array<AtomicParametricEqBand,4> bands;
    std::atomic<float> highPassHz {30}, lowPassHz {20000}, outputGainDb {0};
    std::atomic<int> highPassSlope {12}, lowPassSlope {12};
    std::atomic<bool> highPassEnabled {false}, lowPassEnabled {false}, enabled {true};
    std::atomic<std::uint32_t> revision {1};
    void store(const ParametricEqParameters& p) noexcept {
        for(std::size_t i=0;i<4;++i) bands[i].store(p.bands[i]);
        highPassHz.store(p.highPassHz,std::memory_order_relaxed);
        lowPassHz.store(p.lowPassHz,std::memory_order_relaxed);
        highPassSlope.store(p.highPassSlope,std::memory_order_relaxed);
        lowPassSlope.store(p.lowPassSlope,std::memory_order_relaxed);
        outputGainDb.store(p.outputGainDb,std::memory_order_relaxed);
        highPassEnabled.store(p.highPassEnabled,std::memory_order_relaxed);
        lowPassEnabled.store(p.lowPassEnabled,std::memory_order_relaxed);
        enabled.store(p.enabled,std::memory_order_relaxed);
        revision.fetch_add(1,std::memory_order_release);
    }
    ParametricEqParameters load() const noexcept {
        ParametricEqParameters p;
        for(std::size_t i=0;i<4;++i) p.bands[i]=bands[i].load();
        p.highPassHz=highPassHz.load(std::memory_order_relaxed);
        p.lowPassHz=lowPassHz.load(std::memory_order_relaxed);
        p.highPassSlope=highPassSlope.load(std::memory_order_relaxed);
        p.lowPassSlope=lowPassSlope.load(std::memory_order_relaxed);
        p.outputGainDb=outputGainDb.load(std::memory_order_relaxed);
        p.highPassEnabled=highPassEnabled.load(std::memory_order_relaxed);
        p.lowPassEnabled=lowPassEnabled.load(std::memory_order_relaxed);
        p.enabled=enabled.load(std::memory_order_relaxed);
        return p;
    }
};

QJsonObject toJson(const ParametricEqParameters& p) {
    QJsonArray bands;
    for(const auto& b:p.bands) bands.append(QJsonObject {{"frequencyHz",b.frequencyHz},{"q",b.q},{"gainDb",b.gainDb},
        {"shape",static_cast<int>(b.shape)},{"enabled",b.enabled}});
    return {{"bands",bands},{"highPassHz",p.highPassHz},{"lowPassHz",p.lowPassHz},
        {"highPassSlope",p.highPassSlope},{"lowPassSlope",p.lowPassSlope},{"outputGainDb",p.outputGainDb},
        {"highPassEnabled",p.highPassEnabled},{"lowPassEnabled",p.lowPassEnabled},{"enabled",p.enabled}};
}
bool fromJson(const QJsonObject& object,ParametricEqParameters& p) {
    const auto bands=object.value(QStringLiteral("bands")).toArray();
    if(bands.size()!=4) return false;
    for(int i=0;i<4;++i) {
        if(!bands[i].isObject()) return false;
        const auto b=bands[i].toObject(); auto& target=p.bands[static_cast<std::size_t>(i)];
        if(!readNumber(b,"frequencyHz",20,20000,target.frequencyHz)
            || !readNumber(b,"q",0.1F,10,target.q)
            || !readNumber(b,"gainDb",-18,18,target.gainDb)
            || !b.value(QStringLiteral("shape")).isDouble()
            || b.value(QStringLiteral("shape")).toInt(-1)<0 || b.value(QStringLiteral("shape")).toInt(-1)>4
            || !b.value(QStringLiteral("enabled")).isBool()) return false;
        target.shape=static_cast<WinJACKNexus::Common::ParametricEqShape>(b.value(QStringLiteral("shape")).toInt());
        target.enabled=b.value(QStringLiteral("enabled")).toBool();
    }
    if(!readNumber(object,"highPassHz",20,2000,p.highPassHz)
        || !readNumber(object,"lowPassHz",200,20000,p.lowPassHz)
        || !readNumber(object,"outputGainDb",-18,18,p.outputGainDb)) return false;
    const auto hp=object.value(QStringLiteral("highPassSlope")),lp=object.value(QStringLiteral("lowPassSlope"));
    if(!hp.isDouble() || !lp.isDouble()) return false;
    p.highPassSlope=hp.toInt(); p.lowPassSlope=lp.toInt();
    if((p.highPassSlope!=12 && p.highPassSlope!=24 && p.highPassSlope!=48)
        || (p.lowPassSlope!=12 && p.lowPassSlope!=24 && p.lowPassSlope!=48)) return false;
    const auto hpe=object.value(QStringLiteral("highPassEnabled")),lpe=object.value(QStringLiteral("lowPassEnabled")),en=object.value(QStringLiteral("enabled"));
    if(!hpe.isBool() || !lpe.isBool() || !en.isBool()) return false;
    p.highPassEnabled=hpe.toBool();p.lowPassEnabled=lpe.toBool();p.enabled=en.toBool();return true;
}

class ParametricEqNode final : public WinJACKNexus::Node::INode {
    struct PortConnections final {std::vector<std::string> names;bool failed=false;};
public:
    ParametricEqNode() {for(std::size_t i=0;i<8;++i)atomic_[i].store(settings_[i]);}
    ~ParametricEqNode() override {destroyWindow(nullptr);destroySettingsWidget(clientNameWidget_);shutdown();}
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"Parametric EQ","Native Fx","0.1.0","WinJACKNexus","Eight-channel four-band parametric equalizer."};
    }
    bool init() noexcept override {return client_.isOpen() || openClient();}
    bool openClient() noexcept {
        if(!client_.open({clientName_.c_str(),true,false,nullptr})) return false;
        for(std::size_t i=0;i<8;++i) {
            char inputName[16]{},outputName[16]{};
            std::snprintf(inputName,sizeof(inputName),"in_%s",channelNames[i]);
            std::snprintf(outputName,sizeof(outputName),"out_%s",channelNames[i]);
            if(!inputs_[i].registerPort(client_,inputName,JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput)
                || !outputs_[i].registerPort(client_,outputName,JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput)) {shutdown();return false;}
            processors_[i].prepare(client_.sampleRate());
            processors_[i].setParameters(settings_[i]);
        }
        analyzer_->setSampleRate(client_.sampleRate());
        if(client_.setProcessCallback(&ParametricEqNode::process,this)!=0
            || client_.setSampleRateCallback(&ParametricEqNode::sampleRateChanged,this)!=0
            || !client_.activate()) {shutdown();return false;}
        return true;
    }
    void shutdown() noexcept override {
        client_.deactivate();for(auto& port:inputs_)port.unregisterPort();for(auto& port:outputs_)port.unregisterPort();client_.close();
    }
    HWND createWindow(HWND) noexcept override {
        auto* widget=createEffectWidget();if(widget==nullptr)return nullptr;
        widget->show();widget->raise();widget->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(widget->winId()));
    }
    void destroyWindow(HWND) noexcept override {destroyEffectWidget(effectWidget_);}
    ParametricEqWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {
            if(effectWidget_)return effectWidget_;
            auto* widget=new ParametricEqWidget(parent);
            widget->setAttribute(Qt::WA_DeleteOnClose,false);
            widget->setWindowTitle(QStringLiteral("Parametric EQ 参量均衡器 — %1").arg(QString::fromStdString(clientName_)));
            widget->setParameters(settings_);
            widget->setChangeHandler([this](std::size_t index,const ParametricEqParameters& p){settings_[index]=p;atomic_[index].store(p);});
            widget->setSpectrumFrameReader([this](WinJACKNexus::Common::GraphicEqSpectrumFrame& frame){return analyzer_->tryPopLatest(frame);});
            effectWidget_=widget;return widget;
        }catch(...){return nullptr;}
    }
    void destroyEffectWidget(QWidget* widget) noexcept {if(widget==nullptr || widget!=effectWidget_)return;delete widget;effectWidget_=nullptr;}
    QWidget* createSettingsWidget(QWidget* parent) noexcept {
        try {if(clientNameWidget_)return clientNameWidget_;
            clientNameWidget_=new WinJACKNexus::UI::GateClientNameWidget(QString::fromStdString(clientName_),
                [this](const QString& value){return setClientName(value);},parent);
            return clientNameWidget_;
        }catch(...){return nullptr;}
    }
    void destroySettingsWidget(QWidget* widget) noexcept {if(widget==nullptr || widget!=clientNameWidget_)return;delete widget;clientNameWidget_=nullptr;}
    bool setClientName(const QString& name) noexcept {
        const auto trimmed=name.trimmed();if(trimmed.isEmpty() || trimmed.size()>32)return false;
        try {
            const auto next=trimmed.toStdString();if(next==clientName_)return true;
            if(!client_.isOpen()) {clientName_=next;clientId_=next;}
            else {
                std::array<PortConnections,16> connections;
                const auto collect=[](const char* value,void* data) noexcept {auto* c=static_cast<PortConnections*>(data);try{c->names.emplace_back(value);return true;}catch(...){c->failed=true;return false;}};
                for(std::size_t i=0;i<8;++i){inputs_[i].visitConnections(collect,&connections[i]);outputs_[i].visitConnections(collect,&connections[i+8]);}
                if(std::any_of(connections.begin(),connections.end(),[](const auto& item){return item.failed;}))return false;
                const auto old=clientName_;shutdown();clientName_=next;
                if(!openClient()){clientName_=old;if(openClient())reconnect(connections);return false;}
                if(!reconnect(connections))return false;
            }
            if(effectWidget_)effectWidget_->setWindowTitle(QStringLiteral("Parametric EQ 参量均衡器 — %1").arg(trimmed));
            if(clientNameWidget_)clientNameWidget_->setClientName(trimmed);
            return true;
        }catch(...){return false;}
    }
    bool reconnect(const std::array<PortConnections,16>& connections) noexcept {
        bool success=true;for(std::size_t i=0;i<8;++i){const char* input=inputs_[i].name(),*output=outputs_[i].name();
            if(input==nullptr || output==nullptr)return false;
            for(const auto& peer:connections[i].names)success=WinJACKNexus::Audio::JackGraph::connect(client_,peer.c_str(),input)&&success;
            for(const auto& peer:connections[i+8].names)success=WinJACKNexus::Audio::JackGraph::connect(client_,output,peer.c_str())&&success;
        }return success;
    }
    const char* settingsJson() noexcept {
        try {QJsonArray channels;for(const auto& p:settings_)channels.append(toJson(p));
            serialized_=QJsonDocument(QJsonObject{{QStringLiteral("channels"),channels},{QStringLiteral("clientName"),QString::fromStdString(clientName_)}}).toJson(QJsonDocument::Compact).toStdString();
            return serialized_.c_str();
        }catch(...){return "";}
    }
    int restoreSettingsJson(const char* json) noexcept {
        if(json==nullptr)return 0;
        try {QJsonParseError error;auto document=QJsonDocument::fromJson(QByteArray(json),&error);
            if(error.error!=QJsonParseError::NoError || !document.isObject())return 0;
            const auto root=document.object();
            const auto channels=root.value(QStringLiteral("channels")).toArray();
            if(channels.size()!=8)return 0;
            auto next=settings_;for(int i=0;i<8;++i)if(!channels[i].isObject() || !fromJson(channels[i].toObject(),next[static_cast<std::size_t>(i)]))return 0;
            const auto name=root.value(QStringLiteral("clientName"));if(!name.isUndefined() && (!name.isString() || !setClientName(name.toString())))return 0;
            settings_=next;for(std::size_t i=0;i<8;++i)atomic_[i].store(settings_[i]);if(effectWidget_)effectWidget_->setParameters(settings_);return 1;
        }catch(...){return 0;}
    }
    const char* clientId() const noexcept {return clientId_.c_str();}
private:
    static int process(jack_nframes_t frames,void* context) noexcept {
        auto& self=*static_cast<ParametricEqNode*>(context);
        std::array<const float*,8> inputs{};std::array<float*,8> outputs{};
        for(std::size_t c=0;c<8;++c){
            const auto revision=self.atomic_[c].revision.load(std::memory_order_acquire);
            if(revision!=self.appliedRevisions_[c]){self.processors_[c].setParameters(self.atomic_[c].load());self.appliedRevisions_[c]=revision;}
            inputs[c]=self.inputs_[c].audioBuffer(frames);outputs[c]=self.outputs_[c].audioBuffer(frames);
        }
        for(jack_nframes_t frame=0;frame<frames;++frame){
            std::array<float,8> before{},after{};
            for(std::size_t c=0;c<8;++c){before[c]=inputs[c]?inputs[c][frame]:0;after[c]=self.processors_[c].process(before[c]);if(outputs[c])outputs[c][frame]=after[c];}
            self.analyzer_->pushSamples(before,after);
        }return 0;
    }
    static int sampleRateChanged(jack_nframes_t rate,void* context) noexcept {
        auto& self=*static_cast<ParametricEqNode*>(context);
        for(auto& processor:self.processors_)processor.prepare(rate);
        self.analyzer_->setSampleRate(rate);self.appliedRevisions_.fill(0);return 0;
    }
    WinJACKNexus::Audio::JackClient client_;
    std::array<WinJACKNexus::Audio::JackPort,8> inputs_,outputs_;
    std::array<ParametricEqProcessor,8> processors_;
    std::unique_ptr<WinJACKNexus::Common::GraphicEqSpectrumAnalyzer> analyzer_=std::make_unique<WinJACKNexus::Common::GraphicEqSpectrumAnalyzer>();
    std::array<AtomicParametricEqParameters,8> atomic_;
    std::array<std::uint32_t,8> appliedRevisions_{};
    std::array<ParametricEqParameters,8> settings_{};
    std::string clientName_="ParametricEQ",clientId_="ParametricEQ",serialized_;
    QPointer<ParametricEqWidget> effectWidget_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> clientNameWidget_;
};
