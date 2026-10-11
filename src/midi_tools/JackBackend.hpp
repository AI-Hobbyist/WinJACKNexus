// Control-thread client replacement; the processing callback owns only RT state.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Common/MidiToolsRename.hpp>
#include <algorithm>
#include <cerrno>
#include <functional>
namespace WinJACKNexus::MidiTools {
namespace Core=WinJACKNexus::Common::MidiTools;
class JackBackend final : public Core::ClientBackend {
    Audio::JackClient client_;
    std::vector<jack_port_t*> handles_;
    JackProcessCallback callback_{};void* owner_{};
    std::function<void()> reset_;
    std::function<void()> quiesce_;
public:
    JackBackend(JackProcessCallback callback,void* owner,std::function<void()> reset,std::function<void()> quiesce={})
        :callback_(callback),owner_(owner),reset_(std::move(reset)),quiesce_(std::move(quiesce)){}
    ~JackBackend(){close();}
    bool active() const noexcept{return client_.isActive();}
    jack_port_t* port(unsigned index) const noexcept{return index<handles_.size()?handles_[index]:nullptr;}
    std::string actualName() const override {const auto* n=client_.name();return n?n:"";}
    bool validName(const std::string& s) const override {
        const auto limit=Audio::JackApi::instance().clientNameSize();
        return !s.empty() && s.size()<size_t(limit>0?limit:64) &&
            std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32 || c==127 || c==':';});
    }
    void close() noexcept {
        if(client_.isActive() && quiesce_)quiesce_();
        client_.deactivate();
        for(auto* p:handles_)Audio::JackApi::instance().portUnregister(client_.nativeHandle(),p);
        handles_.clear();client_.close();
    }
    bool replace(const std::string& name,const std::vector<Core::PortIdentity>& ports) override {
        close();if(reset_)reset_();
        if(!validName(name) || !client_.open({name.c_str(),false,false,nullptr}))return false;
        try {
            handles_.reserve(ports.size());
            for(const auto& p:ports){
                auto* handle=Audio::JackApi::instance().portRegister(client_.nativeHandle(),p.shortName.c_str(),
                    p.type==Core::PortType::midi?JACK_DEFAULT_MIDI_TYPE:JACK_DEFAULT_AUDIO_TYPE,
                    p.input?JackPortIsInput:JackPortIsOutput,0);
                if(!handle){close();return false;}handles_.push_back(handle);
            }
        }catch(...){close();return false;}
        if(client_.setProcessCallback(callback_,owner_) || !client_.activate()){close();return false;}
        return true;
    }
    bool snapshot(const std::vector<Core::PortIdentity>& ports,std::vector<Core::Connection>& out) override {
        if(!active() || ports.size()!=handles_.size())return false;
        try {
            auto& api=Audio::JackApi::instance();
            for(unsigned i=0;i<handles_.size();++i){
                const char* local=api.portName(handles_[i]);if(!local)return false;
                const auto** list=api.portGetConnections(handles_[i]);
                // JACK returns null for an unconnected port.
                if(!list)continue;
                struct Free {Audio::JackApi& a;const char** p;~Free(){a.freeMemory(p);}} release{api,list};
                for(unsigned j=0;list[j];++j){Core::Connection c=ports[i].input?
                    Core::Connection{list[j],local}:Core::Connection{local,list[j]};
                    if(std::none_of(out.begin(),out.end(),[&](const auto& old){return old.source==c.source && old.destination==c.destination;}))out.push_back(std::move(c));
                }
            }
            return true;
        }catch(...){return false;}
    }
    bool connect(const Core::Connection& c) override {
        const int result=Audio::JackApi::instance().connect(client_.nativeHandle(),c.source.c_str(),c.destination.c_str());
        return result==0 || result==EEXIST;
    }
};
inline std::vector<Core::PortIdentity> splitPorts(){
    std::vector<Core::PortIdentity> ports{{"MIDI_IN",true,Core::PortType::midi}};
    for(unsigned c=1;c<=16;++c)ports.push_back({"MIDI_OUT_"+std::string(c<10?"0":"")+std::to_string(c),false,Core::PortType::midi});
    return ports;
}
inline std::vector<Core::PortIdentity> matrixPorts(){
    std::vector<Core::PortIdentity> ports;
    for(bool input:{true,false})for(unsigned c=1;c<=16;++c)ports.push_back({std::string(input?"MIDI_IN_":"MIDI_OUT_")+std::string(c<10?"0":"")+std::to_string(c),input,Core::PortType::midi});
    return ports;
}
}
