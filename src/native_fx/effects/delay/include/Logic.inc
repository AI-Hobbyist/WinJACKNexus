struct AtomicDelayParameters final {
    using Parameters=WinJACKNexus::Common::DelayParameters;
    std::atomic<float> timeMs{500},feedback{35},wetDb{-6},dryDb{0},highPassHz{200},lowPassHz{8000},bpm{120};
    std::atomic<bool> enabled{true},sync{false},dotted{false},highPass{true},lowPass{true};
    std::atomic<int> note{2},mixMode{0};
    void store(const Parameters& p) noexcept {
        timeMs.store(p.timeMs);feedback.store(p.feedback);wetDb.store(p.wetDb);dryDb.store(p.dryDb);
        highPassHz.store(p.highPassHz);lowPassHz.store(p.lowPassHz);bpm.store(p.bpm);
        enabled.store(p.enabled);sync.store(p.sync);dotted.store(p.dotted);
        highPass.store(p.highPass);lowPass.store(p.lowPass);note.store(p.note);mixMode.store(p.mixMode);
    }
    Parameters load() const noexcept {
        Parameters p; p.timeMs=timeMs.load();p.feedback=feedback.load();p.wetDb=wetDb.load();p.dryDb=dryDb.load();
        p.highPassHz=highPassHz.load();p.lowPassHz=lowPassHz.load();p.bpm=bpm.load();
        p.enabled=enabled.load();p.sync=sync.load();p.dotted=dotted.load();
        p.highPass=highPass.load();p.lowPass=lowPass.load();p.note=note.load();p.mixMode=mixMode.load();return p;
    }
};
class DelayNode final : public WinJACKNexus::Node::INode {
public:
    ~DelayNode() override {destroyWindow(nullptr);destroySettingsWidget(settingsWidget_);shutdown();}
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"Delay","Native Fx","0.1.0","WinJACKNexus","Eight-channel native delay interface."};
    }
    bool init() noexcept override {
        if(client_.isOpen())return true;
        if(!client_.open({clientName_.c_str(),true,false,nullptr}))return false;
        try {clientId_=client_.name()==nullptr?clientName_:client_.name();}catch(...){shutdown();return false;}
        for(std::size_t i=0;i<8;++i){char in[16]{},out[16]{};
            std::snprintf(in,sizeof(in),"in_%s",channelNames[i]);std::snprintf(out,sizeof(out),"out_%s",channelNames[i]);
            if(!inputs_[i].registerPort(client_,in,JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput)
               ||!outputs_[i].registerPort(client_,out,JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput)){shutdown();return false;}
        }
        try {for(auto& processor:processors_)processor.setSampleRate(client_.sampleRate());}catch(...){shutdown();return false;}
        if(client_.setProcessCallback(&DelayNode::process,this)!=0
           ||client_.setSampleRateCallback(&DelayNode::sampleRateChanged,this)!=0
           ||!client_.activate()){shutdown();return false;}
        return true;
    }
    void shutdown() noexcept override {client_.deactivate();for(auto& p:inputs_)p.unregisterPort();for(auto& p:outputs_)p.unregisterPort();client_.close();}
    HWND createWindow(HWND) noexcept override {
        auto* widget=createEffectWidget();if(!widget)return nullptr;
        widget->show();widget->raise();widget->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(widget->winId()));
    }
    WinJACKNexus::UI::DelayWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {if(!window_){auto* w=new WinJACKNexus::UI::DelayWidget(parent);
                w->setAttribute(Qt::WA_DeleteOnClose,false);
                WinJACKNexus::NativeFx::I18n::bind(w, [this, w] {
                    w->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Delay — %1").arg(QString::fromStdString(clientName_)));
                    w->update();
                });
                w->setParameters(values_);w->setChangeHandler([this](std::size_t i,const auto& v){values_[i]=v;atomic_[i].store(v);});window_=w;}
            return window_;
        }catch(...){return nullptr;}
    }
    void destroyWindow(HWND) noexcept override {delete window_;window_=nullptr;}
    QWidget* createSettingsWidget(QWidget* parent) noexcept {
        try {if(settingsWidget_)return settingsWidget_;
            auto* widget=new WinJACKNexus::UI::GateClientNameWidget(QString::fromStdString(clientName_),
                [this](const QString& name){return setClientName(name);},parent);
            settingsWidget_=widget;return widget;
        }catch(...){return nullptr;}
    }
    void destroySettingsWidget(QWidget* widget) noexcept {if(widget&&widget==settingsWidget_){delete widget;settingsWidget_=nullptr;}}
    bool setClientName(const QString& requested) noexcept {
        const auto trimmed=requested.trimmed();if(trimmed.isEmpty()||trimmed.size()>32)return false;
        try {const auto next=trimmed.toStdString();if(next==clientName_)return true;
            if(client_.isOpen())return false;
            clientName_=next;clientId_=next;
            if(window_)window_->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Delay — %1").arg(trimmed));
            if(settingsWidget_)settingsWidget_->setClientName(trimmed);return true;
        }catch(...){return false;}
    }
    const char* settingsJson() noexcept {
        try {QJsonArray channels;for(const auto& v:values_)channels.append(QJsonObject{
            {"timeMs",v.timeMs},{"feedback",v.feedback},{"wetDb",v.wetDb},{"dryDb",v.dryDb},
            {"highPassHz",v.highPassHz},{"lowPassHz",v.lowPassHz},{"bpm",v.bpm},{"enabled",v.enabled},
            {"sync",v.sync},{"dotted",v.dotted},{"highPass",v.highPass},{"lowPass",v.lowPass},{"note",v.note},{"mixMode",v.mixMode}});
            json_=QJsonDocument(QJsonObject{{"channels",channels},{"clientName",QString::fromStdString(clientName_)}}).toJson(QJsonDocument::Compact).toStdString();return json_.c_str();
        }catch(...){return "";}
    }
    int restoreSettingsJson(const char* text) noexcept {
        if(!text)return 0;try {QJsonParseError error;const auto doc=QJsonDocument::fromJson(QByteArray(text),&error);
            if(error.error!=QJsonParseError::NoError||!doc.isObject())return 0;
            const auto a=doc.object().value("channels").toArray();if(a.size()!=8)return 0;auto next=values_;
            for(int i=0;i<8;++i){if(!a[i].isObject())return 0;const auto o=a[i].toObject();
                auto read=[&](const char* key,float lo,float hi,float& v){const auto x=o.value(QLatin1String(key));if(!x.isDouble()||!std::isfinite(x.toDouble())||x.toDouble()<lo||x.toDouble()>hi)return false;v=float(x.toDouble());return true;};
                if(!read("timeMs",1,2000,next[i].timeMs)||!read("feedback",0,95,next[i].feedback)
                   ||!read("wetDb",-60,6,next[i].wetDb)||!read("dryDb",-60,6,next[i].dryDb)
                   ||!read("highPassHz",20,2000,next[i].highPassHz)||!read("lowPassHz",500,20000,next[i].lowPassHz))return 0;
                if(!o.value("bpm").isUndefined()&&!read("bpm",30,300,next[i].bpm))return 0;
                const auto enabled=o.value("enabled"),sync=o.value("sync"),dotted=o.value("dotted"),hp=o.value("highPass"),lp=o.value("lowPass"),note=o.value("note"),mix=o.value("mixMode");
                if(!enabled.isBool()||!sync.isBool()||(!dotted.isUndefined()&&!dotted.isBool())||!hp.isBool()||!lp.isBool()||!note.isDouble()||note.toInt(-1)<0||note.toInt(-1)>4||!mix.isDouble()||mix.toInt(-1)<0||mix.toInt(-1)>1)return 0;
                next[i].enabled=enabled.toBool();next[i].sync=sync.toBool();next[i].dotted=dotted.toBool(false);next[i].highPass=hp.toBool();next[i].lowPass=lp.toBool();next[i].note=note.toInt();next[i].mixMode=mix.toInt();
            }
            const auto name=doc.object().value("clientName");if(!name.isUndefined()&&(!name.isString()||!setClientName(name.toString())))return 0;
            values_=next;for(std::size_t i=0;i<8;++i)atomic_[i].store(values_[i]);
            if(window_)window_->setParameters(values_);return 1;
        }catch(...){return 0;}
    }
    const char* clientId() const noexcept {return clientId_.c_str();}
    const char* clientName() const noexcept { return clientName_.c_str(); }
    bool hasFixedPortLayout() const noexcept { return fixedNodePorts(inputs_, outputs_); }
private:
    static int process(jack_nframes_t frames,void* context) noexcept {
        auto& self=*static_cast<DelayNode*>(context);
        for(std::size_t c=0;c<8;++c){auto* out=self.outputs_[c].audioBuffer(frames);if(!out)continue;
            const auto* in=self.inputs_[c].audioBuffer(frames);
            self.processors_[c].process(in,out,frames,self.atomic_[c].load());
        }return 0;
    }
    static int sampleRateChanged(jack_nframes_t rate,void* context) noexcept {
        auto& self=*static_cast<DelayNode*>(context);
        try {for(auto& processor:self.processors_)processor.setSampleRate(rate);return 0;}catch(...){return 1;}
    }
    JackClient client_;std::array<JackPort,8> inputs_,outputs_;
    std::array<WinJACKNexus::Common::DelayProcessor,8> processors_{};
    std::array<AtomicDelayParameters,8> atomic_{};
    std::array<WinJACKNexus::UI::DelayParameters,8> values_{};
    std::string clientName_="Delay",clientId_="Delay",json_;
    QPointer<WinJACKNexus::UI::DelayWidget> window_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> settingsWidget_;
};
