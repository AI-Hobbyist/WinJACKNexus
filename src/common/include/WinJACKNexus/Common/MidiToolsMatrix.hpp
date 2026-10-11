// Bounded port routing/merge and source-owned note/pedal release ledger.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Common/MidiToolsCore.hpp>
#include <array>
#include <algorithm>
#include <atomic>
namespace WinJACKNexus::Common::MidiTools {
using MatrixRoutes=std::array<uint16_t,16>;
inline MatrixRoutes diagonalRoutes(){MatrixRoutes r{};for(unsigned i=0;i<16;++i)r[i]=uint16_t(1u<<i);return r;}
class MatrixSnapshot final {
    std::array<std::atomic<uint16_t>,16> rows_{};
    std::atomic<unsigned> revision_{};
public:
    MatrixSnapshot(){publish(diagonalRoutes());}
    void publish(const MatrixRoutes& rows)noexcept {
        revision_.fetch_add(1,std::memory_order_seq_cst);
        for(unsigned i=0;i<16;++i)rows_[i].store(rows[i],std::memory_order_seq_cst);
        revision_.fetch_add(1,std::memory_order_seq_cst);
    }
    bool read(MatrixRoutes& rows)const noexcept {
        const auto before=revision_.load(std::memory_order_seq_cst);if(before&1)return false;
        MatrixRoutes candidate;for(unsigned i=0;i<16;++i)candidate[i]=rows_[i].load(std::memory_order_seq_cst);
        if(before!=revision_.load(std::memory_order_seq_cst))return false;
        rows=candidate;return true;
    }
};
class MatrixProcessor final {
    MatrixRoutes routes_=diagonalRoutes();
    // Counts retain overlapping repeated note-on ownership, not just a boolean.
    uint8_t notes_[16][16][16][128]{};
    bool pedal_[16][16][16]{};
    uint16_t panic_[16]{};
    unsigned notesCount_{};
    struct InputEvent {EventView event;unsigned input,sequence;};
    std::array<InputEvent,maxEvents> events_{};
    uint64_t dropped_{},overflow_{};
    void forget(unsigned output,unsigned channel)noexcept {
        for(unsigned input=0;input<16;++input){for(auto& n:notes_[input][output][channel]){notesCount_-=n;n=0;}pedal_[input][output][channel]=false;}
    }
public:
    void reset()noexcept {
        for(unsigned output=0;output<16;++output){panic_[output]=0;for(unsigned c=0;c<16;++c)forget(output,c);}
    }
    void requestRelease()noexcept{for(auto& p:panic_)p=0xffff;}
    uint64_t dropped()const noexcept{return dropped_;}
    uint64_t overflow()const noexcept{return overflow_;}
    template<class Count,class Reader,class Writer>
    void process(uint32_t frames,const MatrixRoutes& next,Count count,Reader read,Writer write)noexcept {
        if(!frames || frames>maxFrames){requestRelease();++overflow_;return;}
        // Release removed edges at the snapshot boundary before accepting new notes.
        for(unsigned input=0;input<16;++input)for(unsigned output=0;output<16;++output)
            if((routes_[input] & (1u<<output)) && !(next[input] & (1u<<output))) {
                for(unsigned c=0;c<16;++c){
                    for(unsigned key=0;key<128;++key)while(notes_[input][output][c][key]) {
                        const uint8_t b[]{uint8_t(0x80+c),uint8_t(key),0};
                        if(!write(output,EventView{0,b})){panic_[output]|=uint16_t(1u<<c);break;}
                        --notes_[input][output][c][key];--notesCount_;
                    }
                    if(pedal_[input][output][c]){const uint8_t b[]{uint8_t(0xb0+c),64,0};
                        if(!write(output,EventView{0,b}))panic_[output]|=uint16_t(1u<<c);
                        else pedal_[input][output][c]=false;
                    }
                }
            }
        routes_=next;
        for(unsigned output=0;output<16;++output)for(unsigned c=0;c<16;++c)if(panic_[output] & (1u<<c)) {
            bool ok=true;for(uint8_t cc:{uint8_t(64),uint8_t(123),uint8_t(120)}) {
                const uint8_t b[]{uint8_t(0xb0+c),cc,0};ok=write(output,EventView{0,b}) && ok;
            }
            if(ok){forget(output,c);panic_[output]&=uint16_t(~(1u<<c));}
        }
        unsigned size=0;
        for(unsigned input=0;input<16;++input){
            const unsigned total=count(input);const unsigned available=maxEvents-size;
            const unsigned limit=(std::min)(total,available);
            if(total>limit){++overflow_;for(unsigned output=0;output<16;++output)if(next[input]&(1u<<output))panic_[output]=0xffff;}
            for(unsigned seq=0;seq<limit;++seq){const auto event=read(input,seq);
                if(validEvent(event,frames))events_[size++]={event,input,seq};else ++dropped_;
            }
        }
        std::sort(events_.begin(),events_.begin()+size,[](const InputEvent& a,const InputEvent& b){
            if(a.event.frame!=b.event.frame)return a.event.frame<b.event.frame;
            if(a.input!=b.input)return a.input<b.input;return a.sequence<b.sequence;
        });
        for(unsigned i=0;i<size;++i){const auto& e=events_[i];const auto b=e.event.bytes;
            for(unsigned output=0;output<16;++output)if(next[e.input]&(1u<<output)){
                const unsigned c=b[0]&15;const unsigned type=b[0]>>4;
                const bool noteOn=type==9 && b[2]!=0;
                if(panic_[output] && !releaseMessage(b)){++dropped_;continue;}
                if(noteOn && (notesCount_>=maxEvents || notes_[e.input][output][c][b[1]]==255)){
                    panic_[output]|=uint16_t(1u<<c);++overflow_;continue;
                }
                if(!write(output,e.event)){panic_[output]|=b[0]>=0xf0?0xffff:uint16_t(1u<<c);++dropped_;continue;}
                if(noteOn){++notes_[e.input][output][c][b[1]];++notesCount_;}
                else if(type==8 || (type==9 && b[2]==0)){
                    auto& n=notes_[e.input][output][c][b[1]];if(n){--n;--notesCount_;}
                }else if(type==11){
                    if(b[1]==64)pedal_[e.input][output][c]=b[2]>=64;
                    else if(b[1]==120 || b[1]==123)forget(output,c);
                    else if(b[1]==121)for(unsigned source=0;source<16;++source)pedal_[source][output][c]=false;
                }
            }
        }
    }
};
}
