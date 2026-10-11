struct AtomicReverbParameters final {
    using Parameters=WinJACKNexus::Common::ReverbParameters;
    std::atomic<int> scene{1};std::atomic<bool> enabled{true};
    std::atomic<float> decay{2.4F},size{60},preDelayMs{20},diffusion{100};
    std::atomic<float> earlyDb{-6},earlyTimeMs{80},highFreqHz{4000},highDampDb{-6};
    std::atomic<float> lowFreqHz{200},lowDampDb{-3},wet{30},dryDb{-3},width{120},outputDb{0};
    void store(const Parameters& p) noexcept {
        scene.store(p.scene);enabled.store(p.enabled);
#define STORE_REVERB(field) field.store(p.field)
        STORE_REVERB(decay);STORE_REVERB(size);STORE_REVERB(preDelayMs);STORE_REVERB(diffusion);
        STORE_REVERB(earlyDb);STORE_REVERB(earlyTimeMs);STORE_REVERB(highFreqHz);STORE_REVERB(highDampDb);
        STORE_REVERB(lowFreqHz);STORE_REVERB(lowDampDb);STORE_REVERB(wet);STORE_REVERB(dryDb);
        STORE_REVERB(width);STORE_REVERB(outputDb);
#undef STORE_REVERB
    }
    Parameters load() const noexcept {
        Parameters p;p.scene=scene.load();p.enabled=enabled.load();
#define LOAD_REVERB(field) p.field=field.load()
        LOAD_REVERB(decay);LOAD_REVERB(size);LOAD_REVERB(preDelayMs);LOAD_REVERB(diffusion);
        LOAD_REVERB(earlyDb);LOAD_REVERB(earlyTimeMs);LOAD_REVERB(highFreqHz);LOAD_REVERB(highDampDb);
        LOAD_REVERB(lowFreqHz);LOAD_REVERB(lowDampDb);LOAD_REVERB(wet);LOAD_REVERB(dryDb);
        LOAD_REVERB(width);LOAD_REVERB(outputDb);
#undef LOAD_REVERB
        return p;
    }
};
class ReverbNode final : public WinJACKNexus::Node::INode {
public:
  explicit ReverbNode(uint32_t mode = WJN_NODE_MODE_JACK) : mode_(mode) {}

  using Parameters = WinJACKNexus::Common::ReverbParameters;
  ~ReverbNode() override
  {
      destroyWindow(nullptr);
      destroySettingsWidget(settingsWidget_);
      shutdown();
  }
    WinJACKNexus::Node::NodeInfo info() const noexcept override {
        return {"Reverb","Native Fx","0.1.0","WinJACKNexus","Eight-channel native room reverb."};
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
        }
        try {for(auto& processor:processors_)processor.setSampleRate(client_.sampleRate());}catch(...){shutdown();return false;}
        if(client_.setProcessCallback(&ReverbNode::process,this)!=0
           ||client_.setSampleRateCallback(&ReverbNode::sampleRateChanged,this)!=0
           ||!client_.activate()){shutdown();return false;}
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
    WinJACKNexus::UI::ReverbWidget* createEffectWidget(QWidget* parent = nullptr) noexcept {
        try {if(!window_){auto* widget=new WinJACKNexus::UI::ReverbWidget(parent);
                widget->setAttribute(Qt::WA_DeleteOnClose,false);
                WinJACKNexus::NativeFx::I18n::bind(widget, [this, widget] {
                    widget->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Reverb — %1").arg(QString::fromStdString(clientName_)));
                    widget->update();
                });
                widget->setParameters(values_);
                widget->setChangeHandler([this](std::size_t i,const Parameters& v){values_[i]=v;atomic_[i].store(v);});window_=widget;}
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
            if(window_)window_->setWindowTitle(WinJACKNexus::NativeFx::I18n::text("window.title", "Reverb — %1").arg(trimmed));
            if(settingsWidget_)settingsWidget_->setClientName(trimmed);return true;
        }catch(...){return false;}
    }
    const char* settingsJson() noexcept {
        try {QJsonArray channels;for(const auto& v:values_)channels.append(QJsonObject{
            {"scene",v.scene},{"decay",v.decay},{"size",v.size},{"preDelayMs",v.preDelayMs},{"diffusion",v.diffusion},
            {"earlyDb",v.earlyDb},{"earlyTimeMs",v.earlyTimeMs},{"highFreqHz",v.highFreqHz},{"highDampDb",v.highDampDb},
            {"lowFreqHz",v.lowFreqHz},{"lowDampDb",v.lowDampDb},{"wet",v.wet},{"dryDb",v.dryDb},
            {"width",v.width},{"outputDb",v.outputDb},{"enabled",v.enabled}});
            json_=QJsonDocument(QJsonObject{{"channels",channels},{"clientName",QString::fromStdString(clientName_)}}).toJson(QJsonDocument::Compact).toStdString();return json_.c_str();
        }catch(...){return "";}
    }
    int restoreSettingsJson(const char* text) noexcept {
        if(!text)return 0;try {QJsonParseError error;const auto doc=QJsonDocument::fromJson(QByteArray(text),&error);
            if(error.error!=QJsonParseError::NoError||!doc.isObject())return 0;
            const auto channels=doc.object().value("channels").toArray();if(channels.size()!=8)return 0;auto next=values_;
            for(int i=0;i<8;++i){if(!channels[i].isObject())return 0;const auto o=channels[i].toObject();
                auto read=[&](const char* key,float lo,float hi,float& v){const auto x=o.value(QLatin1String(key));if(!x.isDouble()||!std::isfinite(x.toDouble())||x.toDouble()<lo||x.toDouble()>hi)return false;v=float(x.toDouble());return true;};
                auto& v=next[i];const auto scene=o.value("scene"),enabled=o.value("enabled");
                if(!scene.isDouble()||scene.toDouble()!=scene.toInt()||scene.toInt()<0||scene.toInt()>6||!enabled.isBool())return 0;
                v.scene=scene.toInt();v.enabled=enabled.toBool();
                if(!read("decay",.1F,15,v.decay)||!read("size",0,100,v.size)||!read("preDelayMs",0,150,v.preDelayMs)
                   ||!read("diffusion",0,100,v.diffusion)||!read("earlyDb",-60,6,v.earlyDb)||!read("earlyTimeMs",1,150,v.earlyTimeMs)
                   ||!read("highFreqHz",500,16000,v.highFreqHz)||!read("highDampDb",-24,0,v.highDampDb)
                   ||!read("lowFreqHz",20,2000,v.lowFreqHz)||!read("lowDampDb",-24,0,v.lowDampDb)
                   ||!read("wet",0,100,v.wet)||!read("dryDb",-60,6,v.dryDb)||!read("width",0,200,v.width)
                   ||!read("outputDb",-24,12,v.outputDb))return 0;
            }
            const auto name=doc.object().value("clientName");if(!name.isUndefined()&&(!name.isString()||!setClientName(name.toString())))return 0;
            values_=next;for(std::size_t i=0;i<8;++i)atomic_[i].store(values_[i]);
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
      auto &self = *static_cast<ReverbNode *>(context);
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
  static int processBuffers(jack_nframes_t frames, ReverbNode &self,
                            const std::array<const float *, 8> &sourceInputs,
                            const std::array<float *, 8> &sourceOutputs) noexcept
  {
      std::array<float *, 8> outputs{};
      std::array<const float *, 8> inputs{};
      std::array<Parameters, 8> settings{};
      for (std::size_t c = 0; c < 8; ++c)
      {
          outputs[c] = sourceOutputs[c];
          if (!outputs[c])
              continue;
          inputs[c] = sourceInputs[c];
          settings[c] = self.atomic_[c].load();
          self.processors_[c].process(inputs[c], outputs[c], frames, settings[c]);
      }
      constexpr std::size_t pairs[][2] = {{0, 1}, {4, 5}, {6, 7}};
      for (const auto &pair : pairs)
      {
          const auto a = pair[0], b = pair[1];
          if (!outputs[a] || !outputs[b] || !settings[a].enabled || !settings[b].enabled)
              continue;
          const float widthA = std::clamp(settings[a].width * .01F, 0.0F, 2.0F);
          const float widthB = std::clamp(settings[b].width * .01F, 0.0F, 2.0F);
          if (widthA == 1 && widthB == 1)
              continue;
          const float dryA = std::pow(10.0F, (settings[a].dryDb + settings[a].outputDb) / 20.0F);
          const float dryB = std::pow(10.0F, (settings[b].dryDb + settings[b].outputDb) / 20.0F);
          for (unsigned n = 0; n < frames; ++n)
          {
              const float originalA = inputs[a] ? inputs[a][n] : 0.0F,
                          originalB = inputs[b] ? inputs[b][n] : 0.0F;
              const float wetA = outputs[a][n] - originalA * dryA,
                          wetB = outputs[b][n] - originalB * dryB;
              const float mid = (wetA + wetB) * .5F, side = (wetA - wetB) * .5F;
              outputs[a][n] = originalA * dryA + mid + side * widthA;
              outputs[b][n] = originalB * dryB + mid - side * widthB;
          }
      }
      return 0;
  }
    static int sampleRateChanged(jack_nframes_t rate,void* context) noexcept {
        auto& self=*static_cast<ReverbNode*>(context);
        try {for(auto& processor:self.processors_)processor.setSampleRate(rate);return 0;}catch(...){return 1;}
    }
    JackClient client_;std::array<JackPort,8> inputs_,outputs_;
    std::array<WinJACKNexus::Common::ReverbProcessor,8> processors_{};
    std::array<AtomicReverbParameters,8> atomic_{};
    std::array<Parameters,8> values_{};
    std::string clientName_="Reverb",clientId_="Reverb",json_;
    QPointer<WinJACKNexus::UI::ReverbWidget> window_;
    QPointer<WinJACKNexus::UI::GateClientNameWidget> settingsWidget_;
};
