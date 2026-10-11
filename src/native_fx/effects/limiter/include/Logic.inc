class LimiterNode final : public WinJACKNexus::Node::INode {
    struct AtomicParameters final {
        std::atomic<float> thresholdDb{-1.0F},ceilingDb{-1.0F},releaseMs{100.0F},lookaheadDb{0.0F};
        std::atomic<int> mode{1},oversampling{2};
        std::atomic<bool> autoGain{false},enabled{true};
        std::atomic<std::uint32_t> revision{1};
        void store(const WinJACKNexus::Common::LimiterParameters& p) noexcept {
            thresholdDb.store(p.thresholdDb,std::memory_order_relaxed);
            ceilingDb.store(p.ceilingDb,std::memory_order_relaxed);
            releaseMs.store(p.releaseMs,std::memory_order_relaxed);
            lookaheadDb.store(p.lookaheadDb,std::memory_order_relaxed);
            mode.store(p.mode,std::memory_order_relaxed);oversampling.store(p.oversampling,std::memory_order_relaxed);
            autoGain.store(p.autoGain,std::memory_order_relaxed);enabled.store(p.enabled,std::memory_order_relaxed);
            revision.fetch_add(1,std::memory_order_release);
        }
        WinJACKNexus::Common::LimiterParameters load() const noexcept {
            WinJACKNexus::Common::LimiterParameters p;
            p.thresholdDb=thresholdDb.load(std::memory_order_relaxed);p.ceilingDb=ceilingDb.load(std::memory_order_relaxed);
            p.releaseMs=releaseMs.load(std::memory_order_relaxed);p.lookaheadDb=lookaheadDb.load(std::memory_order_relaxed);
            p.mode=mode.load(std::memory_order_relaxed);p.oversampling=oversampling.load(std::memory_order_relaxed);
            p.autoGain=autoGain.load(std::memory_order_relaxed);p.enabled=enabled.load(std::memory_order_relaxed);return p;
        }
    };
public:
  explicit LimiterNode(uint32_t mode = WJN_NODE_MODE_JACK) : mode_(mode)
  {
      for (std::size_t i = 0; i < 8; ++i)
      {
          atomic_[i].store(values_[i]);
          processors_[i].setParameters(values_[i]);
      }
  }
    ~LimiterNode() override {destroyWindow(nullptr);destroySettingsWidget(settingsWidget_);shutdown();}
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"Limiter","Native Fx","0.1.0","WinJACKNexus","Eight-channel native limiter UI."};
    }
    bool init() noexcept override {
        if (mode_ != WJN_NODE_MODE_JACK)
            return false;
        if(client_.isOpen())return true;
        if(!client_.open({clientName_.c_str(),true,false,nullptr}))return false;
        try {clientId_=client_.name()==nullptr?clientName_:client_.name();}catch(...){shutdown();return false;}
        for(std::size_t i=0;i<8;++i){char in[16]{},out[16]{};
            std::snprintf(in,sizeof(in),"in_%s",channelNames[i]);std::snprintf(out,sizeof(out),"out_%s",channelNames[i]);
            if(!inputs_[i].registerPort(client_,in,JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput)
                ||!outputs_[i].registerPort(client_,out,JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput)){shutdown();return false;}
            processors_[i].prepare(client_.sampleRate());
            processors_[i].setParameters(values_[i]);
        }
        if(client_.setProcessCallback(&LimiterNode::process,this)!=0
            ||client_.setSampleRateCallback(&LimiterNode::sampleRateChanged,this)!=0||!client_.activate()){shutdown();return false;}
        appliedRevisions_.fill(atomic_[0].revision.load(std::memory_order_acquire));
        return true;
    }
    void shutdown() noexcept override
    {
        if (mode_ == WJN_NODE_MODE_VIRT)
        {
            virtualStop();
            return;
        }
        client_.deactivate();
        for (auto &p : inputs_)
            p.unregisterPort();
        for (auto &p : outputs_)
            p.unregisterPort();
        client_.close();
    }
    HWND createWindow(HWND) noexcept override {
        auto* widget=createEffectWidget();if(!widget)return nullptr;
        widget->show();widget->raise();widget->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(widget->winId()));
    }
    WinJACKNexus::UI::LimiterWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {if(!window_){auto* w=new WinJACKNexus::UI::LimiterWidget(parent);
                w->setAttribute(Qt::WA_DeleteOnClose,false);
                WinJACKNexus::NativeFx::I18n::bind(w, [this, w] {
                    w->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Limiter — %1").arg(QString::fromStdString(clientName_)));
                    w->update();
                });
                w->setParameters(values_);w->setChangeHandler([this](std::size_t i,const auto& v){values_[i]=v;atomic_[i].store(v);});
                w->setMeterReader([this]{
                    std::array<WinJACKNexus::Common::LimiterMeter,8> meters{};
                    for(std::size_t c=0;c<meters.size();++c){
                        meters[c].inputDb=meterInputDb_[c].load(std::memory_order_relaxed);
                        meters[c].outputDb=meterOutputDb_[c].load(std::memory_order_relaxed);
                        meters[c].gainReductionDb=meterGainReductionDb_[c].load(std::memory_order_relaxed);
                        meters[c].active=meterActive_[c].load(std::memory_order_acquire);
                    }
                    return meters;
                });window_=w;}
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
        try {auto next=trimmed.toStdString();if(next==clientName_)return true;
            if(client_.isOpen())return false;
            clientName_=next;clientId_=next;
            if(window_)window_->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Limiter — %1").arg(trimmed));
            if(settingsWidget_)settingsWidget_->setClientName(trimmed);
            return true;
        }catch(...){return false;}
    }
    const char* settingsJson() noexcept {
        try {QJsonArray channels;for(const auto& s:values_)channels.append(QJsonObject{
            {"thresholdDb",s.thresholdDb},{"ceilingDb",s.ceilingDb},{"releaseMs",s.releaseMs},{"lookaheadDb",s.lookaheadDb},
            {"mode",s.mode},{"oversampling",s.oversampling},{"autoGain",s.autoGain},{"enabled",s.enabled}});
            json_=QJsonDocument(QJsonObject{{"channels",channels},{"clientName",QString::fromStdString(clientName_)}}).toJson(QJsonDocument::Compact).toStdString();return json_.c_str();
        }catch(...){return "";}
    }
    int restoreSettingsJson(const char* text) noexcept {
        if(!text)return 0;try {QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(text),&error);
            if(error.error!=QJsonParseError::NoError||!doc.isObject())return 0;
            auto a=doc.object().value("channels").toArray();if(a.size()!=8)return 0;auto next=values_;
            for(int i=0;i<8;++i){if(!a[i].isObject())return 0;auto o=a[i].toObject();
                auto read=[&](const char* key,float lo,float hi,float& v){auto x=o.value(QLatin1String(key));if(!x.isDouble()||!std::isfinite(x.toDouble())||x.toDouble()<lo||x.toDouble()>hi)return false;v=float(x.toDouble());return true;};
                if(!read("thresholdDb",-24,6,next[i].thresholdDb)||!read("ceilingDb",-12,12,next[i].ceilingDb)
                    ||!read("releaseMs",10,500,next[i].releaseMs)||!read("lookaheadDb",0,12,next[i].lookaheadDb))return 0;
                auto mode=o.value("mode"),over=o.value("oversampling"),gain=o.value("autoGain"),enabled=o.value("enabled");
                if(!mode.isDouble()||mode.toInt(-1)<0||mode.toInt(-1)>2||!over.isDouble()||
                    (over.toInt()!=2&&over.toInt()!=4&&over.toInt()!=8)||!gain.isBool()||!enabled.isBool())return 0;
                next[i].mode=mode.toInt();next[i].oversampling=over.toInt();next[i].autoGain=gain.toBool();next[i].enabled=enabled.toBool();
            }
            auto name=doc.object().value("clientName");
            if(!name.isUndefined()&&(!name.isString()||!setClientName(name.toString())))return 0;
            values_=next;for(std::size_t i=0;i<next.size();++i)atomic_[i].store(next[i]);
            if(window_)window_->setParameters(values_);return 1;
        }catch(...){return 0;}
    }
    const char* clientId() const noexcept {return clientId_.c_str();}
    const char* clientName() const noexcept { return clientName_.c_str(); }
#include "../../../src/VirtualNode.inc"
    bool hasFixedPortLayout() const noexcept { return fixedNodePorts(inputs_, outputs_); }
private:
  static int process(jack_nframes_t frames, void *context) noexcept
  {
      auto &self = *static_cast<LimiterNode *>(context);
      if (self.mode_ != WJN_NODE_MODE_JACK)
          return 0;
      std::array<const float *, 8> inputs{};
      std::array<float *, 8> outputs{};
      for (std::size_t channel = 0; channel < 8; ++channel)
      {
          inputs[channel] = self.inputs_[channel].audioBuffer(frames);
          outputs[channel] = self.outputs_[channel].audioBuffer(frames);
      }
      return processBuffers(frames, self, inputs, outputs);
  }
  static int processBuffers(jack_nframes_t frames, LimiterNode &self,
                            const std::array<const float *, 8> &sourceInputs,
                            const std::array<float *, 8> &sourceOutputs) noexcept
  {
      for (std::size_t c = 0; c < 8; ++c)
      {
          const auto revision = self.atomic_[c].revision.load(std::memory_order_acquire);
          if (revision != self.appliedRevisions_[c])
          {
              self.processors_[c].setParameters(self.atomic_[c].load());
              self.appliedRevisions_[c] = revision;
          }
          auto *out = sourceOutputs[c];
          if (!out)
          {
              self.processors_[c].reset();
              self.meterInputDb_[c].store(-120.0F, std::memory_order_relaxed);
              self.meterOutputDb_[c].store(-120.0F, std::memory_order_relaxed);
              self.meterGainReductionDb_[c].store(0.0F, std::memory_order_relaxed);
              self.meterActive_[c].store(false, std::memory_order_release);
              continue;
          }
          const auto *in = sourceInputs[c];
          if (!in)
          {
              std::fill_n(out, frames, 0.0F);
              self.processors_[c].reset();
              self.meterInputDb_[c].store(-120.0F, std::memory_order_relaxed);
              self.meterOutputDb_[c].store(-120.0F, std::memory_order_relaxed);
              self.meterGainReductionDb_[c].store(0.0F, std::memory_order_relaxed);
              self.meterActive_[c].store(false, std::memory_order_release);
              continue;
          }
          for (jack_nframes_t frame = 0; frame < frames; ++frame)
          {
              out[frame] = self.processors_[c].process(in[frame]);
          }
          const auto meter = self.processors_[c].meter();
          self.meterInputDb_[c].store(meter.inputDb, std::memory_order_relaxed);
          self.meterOutputDb_[c].store(meter.outputDb, std::memory_order_relaxed);
          self.meterGainReductionDb_[c].store(meter.gainReductionDb, std::memory_order_relaxed);
          self.meterActive_[c].store(meter.active, std::memory_order_release);
      }
      return 0;
  }
    static int sampleRateChanged(jack_nframes_t rate,void* context) noexcept {
        auto& self=*static_cast<LimiterNode*>(context);
        for(auto& processor:self.processors_)processor.prepare(static_cast<double>(rate));
        self.appliedRevisions_.fill(0);return 0;
    }
    JackClient client_;std::array<JackPort,8> inputs_,outputs_;
    std::array<WinJACKNexus::UI::LimiterParameters,8> values_{};
    std::array<AtomicParameters,8> atomic_{};
    std::array<WinJACKNexus::Common::LimiterProcessor,8> processors_{};
    std::array<std::uint32_t,8> appliedRevisions_{};
    std::array<std::atomic<float>,8> meterInputDb_{},meterOutputDb_{},meterGainReductionDb_{};
    std::array<std::atomic<bool>,8> meterActive_{};
    std::string clientName_="Limiter",clientId_="Limiter",json_;
    QPointer<WinJACKNexus::UI::LimiterWidget> window_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> settingsWidget_;
};
