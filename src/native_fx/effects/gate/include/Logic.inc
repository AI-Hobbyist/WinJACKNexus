struct AtomicParameters final {
    std::atomic<float> thresholdDb {-32}, ratio {4}, attackMs {10}, releaseMs {150};
    std::atomic<float> floorDb {-60}, holdDb {3}, highPassHz {80}, lowPassHz {8000};
    std::atomic<bool> sidechainFilter {true}, enabled {true};
    std::atomic<std::uint32_t> revision {1};
    void store(const GateParameters& p) noexcept {
        thresholdDb.store(p.thresholdDb,std::memory_order_relaxed);
        ratio.store(p.ratio,std::memory_order_relaxed);
        attackMs.store(p.attackMs,std::memory_order_relaxed);
        releaseMs.store(p.releaseMs,std::memory_order_relaxed);
        floorDb.store(p.floorDb,std::memory_order_relaxed);
        holdDb.store(p.holdDb,std::memory_order_relaxed);
        highPassHz.store(p.highPassHz,std::memory_order_relaxed);
        lowPassHz.store(p.lowPassHz,std::memory_order_relaxed);
        sidechainFilter.store(p.sidechainFilter,std::memory_order_relaxed);
        enabled.store(p.enabled,std::memory_order_relaxed);
        revision.fetch_add(1,std::memory_order_release);
    }
    GateParameters load() const noexcept {
        GateParameters p;
        p.thresholdDb=thresholdDb.load(std::memory_order_relaxed);
        p.ratio=ratio.load(std::memory_order_relaxed);
        p.attackMs=attackMs.load(std::memory_order_relaxed);
        p.releaseMs=releaseMs.load(std::memory_order_relaxed);
        p.floorDb=floorDb.load(std::memory_order_relaxed);
        p.holdDb=holdDb.load(std::memory_order_relaxed);
        p.highPassHz=highPassHz.load(std::memory_order_relaxed);
        p.lowPassHz=lowPassHz.load(std::memory_order_relaxed);
        p.sidechainFilter=sidechainFilter.load(std::memory_order_relaxed);
        p.enabled=enabled.load(std::memory_order_relaxed);
        return p;
    }
};

QJsonObject toJson(const GateParameters& p) {
    return {{QStringLiteral("thresholdDb"),p.thresholdDb},{QStringLiteral("ratio"),p.ratio},
            {QStringLiteral("attackMs"),p.attackMs},{QStringLiteral("releaseMs"),p.releaseMs},
            {QStringLiteral("floorDb"),p.floorDb},{QStringLiteral("holdDb"),p.holdDb},
            {QStringLiteral("highPassHz"),p.highPassHz},{QStringLiteral("lowPassHz"),p.lowPassHz},
            {QStringLiteral("sidechainFilter"),p.sidechainFilter},{QStringLiteral("enabled"),p.enabled}};
}

bool fromJson(const QJsonObject& object,GateParameters& p) {
    if(!readNumber(object,"thresholdDb",-60,0,p.thresholdDb)
       || !readNumber(object,"ratio",1,20,p.ratio)
       || !readNumber(object,"attackMs",0.1F,200,p.attackMs)
       || !readNumber(object,"releaseMs",1,2000,p.releaseMs)
       || !readNumber(object,"floorDb",-90,0,p.floorDb)
       || !readNumber(object,"holdDb",0,24,p.holdDb)
       || !readNumber(object,"highPassHz",20,2000,p.highPassHz)
       || !readNumber(object,"lowPassHz",500,20000,p.lowPassHz)) return false;
    auto filter=object.value(QStringLiteral("sidechainFilter"));
    auto enabled=object.value(QStringLiteral("enabled"));
    if(!filter.isBool() || !enabled.isBool()) return false;
    p.sidechainFilter=filter.toBool(); p.enabled=enabled.toBool(); return true;
}

class GateNode final : public WinJACKNexus::Node::INode {
    struct PortConnections final { std::vector<std::string> names; bool failed=false; };
public:
    ~GateNode() override { destroyWindow(nullptr); destroySettingsWidget(clientNameWidget_); shutdown(); }
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"Gate","Native Fx","0.1.0","WinJACKNexus","Eight-channel native downward gate."};
    }
    bool init() noexcept override {
        if(client_.isOpen()) return true;
        return openClient();
    }
    bool openClient() noexcept {
        if(!client_.open({clientName_.c_str(),true,false,nullptr})) return false;
        try { clientId_=client_.name()==nullptr?clientName_:client_.name(); }
        catch(...) { shutdown(); return false; }
        for(std::size_t i=0;i<8;++i) {
            char inputName[16] {},outputName[16] {};
            std::snprintf(inputName,sizeof(inputName),"in_%s",channelNames[i]);
            std::snprintf(outputName,sizeof(outputName),"out_%s",channelNames[i]);
            if(!inputs_[i].registerPort(client_,inputName,JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput)
               || !outputs_[i].registerPort(client_,outputName,JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput)) {
                shutdown(); return false;
            }
            processors_[i].prepare(client_.sampleRate());
        }
        if(client_.setProcessCallback(&GateNode::process,this)!=0
           || client_.setSampleRateCallback(&GateNode::sampleRateChanged,this)!=0
           || !client_.activate()) { shutdown(); return false; }
        return true;
    }
    void shutdown() noexcept override {
        client_.deactivate();
        for(auto& port:inputs_) port.unregisterPort();
        for(auto& port:outputs_) port.unregisterPort();
        client_.close();
    }
    HWND createWindow(HWND) noexcept override {
        auto* widget=createEffectWidget();
        if(widget==nullptr) return nullptr;
        widget->show(); widget->raise(); widget->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(widget->winId()));
    }
    void destroyWindow(HWND) noexcept override { destroyEffectWidget(effectWidget_); }
    GateEffectWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {
            if(effectWidget_) return effectWidget_;
            auto* widget=new GateEffectWidget(parent);
            widget->setAttribute(Qt::WA_DeleteOnClose,false);
            WinJACKNexus::NativeFx::I18n::bind(widget, [this, widget] {
                widget->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Gate — %1")
                    .arg(QString::fromStdString(clientName_)));
                widget->update();
            });
            widget->setParameters(settings_);
            widget->setChangeHandler([this](std::size_t index,const GateParameters& p) {
                settings_[index]=p; atomic_[index].store(p);
            });
            effectWidget_=widget;
            return widget;
        } catch(...) { return nullptr; }
    }
    void destroyEffectWidget(QWidget* widget) noexcept {
        if(widget==nullptr || widget!=effectWidget_) return;
        delete widget; effectWidget_=nullptr;
    }
    QWidget* createSettingsWidget(QWidget* parent) noexcept {
        try {
            if(clientNameWidget_) return clientNameWidget_;
            auto* widget=new WinJACKNexus::UI::GateClientNameWidget(
                QString::fromStdString(clientName_),[this](const QString& value) { return setClientName(value); },parent);
            clientNameWidget_=widget;
            return widget;
        } catch(...) { return nullptr; }
    }
    void destroySettingsWidget(QWidget* widget) noexcept {
        if(widget==nullptr || widget!=clientNameWidget_) return;
        delete widget; clientNameWidget_=nullptr;
    }
    bool setClientName(const QString& requestedName) noexcept {
        const auto trimmed=requestedName.trimmed();
        if(trimmed.isEmpty() || trimmed.size()>32) return false;
        try {
            const std::string nextName=trimmed.toStdString();
            if(nextName==clientName_) return true;
            if(!client_.isOpen()) {
                clientName_=nextName;
                clientId_=nextName;
                return true;
            }
            std::array<PortConnections,16> connections;
            const auto collect=[](const char* name,void* context) noexcept {
                auto* values=static_cast<PortConnections*>(context);
                try { values->names.emplace_back(name); return true; }
                catch(...) { values->failed=true; return false; }
            };
            for(std::size_t i=0;i<8;++i) {
                inputs_[i].visitConnections(collect,&connections[i]);
                outputs_[i].visitConnections(collect,&connections[i+8]);
            }
            if(std::any_of(connections.begin(),connections.end(),[](const auto& item){return item.failed;}))
                return false;
            const std::string oldName=clientName_;
            shutdown();
            clientName_=nextName;
            if(!openClient()) {
                clientName_=oldName;
                if(openClient()) reconnect(connections);
                return false;
            }
            if(effectWidget_)
                effectWidget_->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Gate — %1").arg(trimmed));
            if(clientNameWidget_) clientNameWidget_->setClientName(trimmed);
            return reconnect(connections);
        } catch(...) { return false; }
    }
    bool reconnect(const std::array<PortConnections,16>& connections) noexcept {
        bool success=true;
        for(std::size_t i=0;i<8;++i) {
            const char* input=inputs_[i].name();
            const char* output=outputs_[i].name();
            if(input==nullptr || output==nullptr) return false;
            for(const auto& peer:connections[i].names)
                success=WinJACKNexus::Audio::JackGraph::connect(client_,peer.c_str(),input) && success;
            for(const auto& peer:connections[i+8].names)
                success=WinJACKNexus::Audio::JackGraph::connect(client_,output,peer.c_str()) && success;
        }
        return success;
    }
    const char* settingsJson() noexcept {
        try {
            QJsonArray channels;
            for(const auto& p:settings_) channels.append(toJson(p));
            serialized_=QJsonDocument(QJsonObject{{QStringLiteral("channels"),channels},
                                                   {QStringLiteral("clientName"),QString::fromStdString(clientName_)}})
                           .toJson(QJsonDocument::Compact).toStdString();
            return serialized_.c_str();
        } catch(...) { return ""; }
    }
    int restoreSettingsJson(const char* json) noexcept {
        if(json==nullptr) return 0;
        try {
            QJsonParseError error;
            const auto document=QJsonDocument::fromJson(QByteArray(json),&error);
            if(error.error!=QJsonParseError::NoError || !document.isObject()) return 0;
            const auto root=document.object();
            const auto channels=root.value(QStringLiteral("channels"));
            if(!channels.isArray() || channels.toArray().size()!=8) return 0;
            const auto array=channels.toArray();
            auto values=settings_;
            for(int i=0;i<8;++i) {
                auto item=array.at(i);
                if(!item.isObject() || !fromJson(item.toObject(),values[i])) return 0;
            }
            const auto nameValue=root.value(QStringLiteral("clientName"));
            const QString requestedName=nameValue.isUndefined()
                ? QString::fromStdString(clientName_)
                : nameValue.isString()?nameValue.toString():QString();
            if(requestedName.trimmed().isEmpty() || requestedName.trimmed().size()>32
               || !setClientName(requestedName)) return 0;
            settings_=values;
            for(int i=0;i<8;++i) atomic_[i].store(settings_[i]);
            if(effectWidget_) effectWidget_->setParameters(settings_);
            return 1;
        } catch(...) { return 0; }
    }
    const char* clientId() const noexcept { return clientId_.c_str(); }
    const char* clientName() const noexcept { return clientName_.c_str(); }
    bool hasFixedPortLayout() const noexcept { return fixedNodePorts(inputs_, outputs_); }
private:
    static int process(jack_nframes_t frames,void* context) noexcept {
        auto& self=*static_cast<GateNode*>(context);
        for(std::size_t c=0;c<8;++c) {
            auto revision=self.atomic_[c].revision.load(std::memory_order_acquire);
            if(revision!=self.appliedRevisions_[c]) {
                self.processors_[c].setParameters(self.atomic_[c].load());
                self.appliedRevisions_[c]=revision;
            }
            const auto* input=self.inputs_[c].audioBuffer(frames);
            auto* output=self.outputs_[c].audioBuffer(frames);
            if(output==nullptr) continue;
            if(input==nullptr) { std::fill_n(output,frames,0.0F); continue; }
            for(jack_nframes_t frame=0;frame<frames;++frame)
                output[frame]=self.processors_[c].process(input[frame]);
        }
        return 0;
    }
    static int sampleRateChanged(jack_nframes_t rate,void* context) noexcept {
        auto& self=*static_cast<GateNode*>(context);
        for(auto& processor:self.processors_) processor.prepare(rate);
        self.appliedRevisions_.fill(0);
        return 0;
    }
    JackClient client_;
    std::array<JackPort,8> inputs_, outputs_;
    std::array<GateProcessor,8> processors_;
    std::array<AtomicParameters,8> atomic_;
    std::array<std::uint32_t,8> appliedRevisions_ {};
    std::array<GateParameters,8> settings_ {};
    std::string clientName_="Gate",clientId_="Gate",serialized_;
    QPointer<GateEffectWidget> effectWidget_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> clientNameWidget_;
};
