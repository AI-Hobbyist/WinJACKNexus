// Offline synthesis gate, no JACK API or engine driver creation.
// SPDX-License-Identifier: AGPL-3.0-only
#include <fluidsynth.h>
#include <sfizz.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
void check(bool b,const char* message) { if(!b)throw std::runtime_error(message); }
using Settings=std::unique_ptr<fluid_settings_t,decltype(&delete_fluid_settings)>;
using Fluid=std::unique_ptr<fluid_synth_t,decltype(&delete_fluid_synth)>;
using Sfz=std::unique_ptr<sfizz_synth_t,decltype(&sfizz_free)>;
struct FluidFixture {
 Settings settings{new_fluid_settings(),delete_fluid_settings};
 Fluid synth{nullptr,delete_fluid_synth}; int soundfont{};
 explicit FluidFixture(const std::string& path) {
  check(bool(settings),"fluid settings");
  for(const auto key:{"synth.audio-groups","synth.audio-channels","synth.midi-channels"})
   check(fluid_settings_setint(settings.get(),key,16)==FLUID_OK,"fluid 16 channels");
  for(const auto key:{"synth.reverb.active","synth.chorus.active","synth.threadsafe-api","synth.dynamic-sample-loading"})
   check(fluid_settings_setint(settings.get(),key,0)==FLUID_OK,"fluid disable shared returns/mutex/dynamic samples");
  fluid_settings_setint(settings.get(),"synth.polyphony",1024);
  fluid_settings_setint(settings.get(),"synth.cpu-cores",1);
  fluid_settings_setnum(settings.get(),"synth.sample-rate",48000);
  fluid_settings_setstr(settings.get(),"synth.midi-bank-select","mma");
  synth.reset(new_fluid_synth(settings.get()));check(bool(synth),"fluid create");
  soundfont=fluid_synth_sfload(synth.get(),path.c_str(),0);check(soundfont>=0,"self-authored SF2 load");
  for(int c=0;c<16;++c)check(fluid_synth_program_select(synth.get(),c,soundfont,c==9?128:0,0)==FLUID_OK,"SF2 bank/preset/drum mapping");
 }
 void render(int frames,std::array<std::vector<float>,32>& audio,int offset=0) {
  std::array<float*,32> out{};
  for(int c=0;c<32;++c)out[c]=audio[c].data()+offset;
  check(fluid_synth_process(synth.get(),frames,0,nullptr,32,out.data())==FLUID_OK,"fluid render");
 }
};
std::array<std::vector<float>,32> audio(int size) {std::array<std::vector<float>,32> out;for(auto& a:out)a.resize(size);return out;}
float peak(const std::vector<float>& v) {float p=0;for(float a:v){check(std::isfinite(a),"finite output");p=std::max(p,std::abs(a));}return p;}
int onset(const std::vector<float>& a) {for(unsigned i=0;i<a.size();++i)if(std::abs(a[i])>0.000001f)return int(i);return -1;}
Sfz makeSfz(const std::string& path) {
 Sfz s(sfizz_create_synth(),sfizz_free);check(bool(s),"sfizz create");
 sfizz_set_sample_rate(s.get(),48000);sfizz_set_samples_per_block(s.get(),256);
 sfizz_set_num_voices(s.get(),64);sfizz_set_preload_size(s.get(),48000);
 check(sfizz_load_file(s.get(),path.c_str()),"self-authored SFZ load");return s;
}
void renderSfz(sfizz_synth_t* s,std::array<std::vector<float>,2>& out) {float* channels[]{out[0].data(),out[1].data()};sfizz_render_block(s,channels,2,int(out[0].size()));}
int main(int argc,char** argv) {
 std::cout << std::unitbuf;
 try {
  check(argc==2,"fixture root required");const std::string root=argv[1];
  std::cout<<"FluidSynth "<<fluid_version_str()<<" / sfizz 1.2.3; rate=48000, block=256, JACK absent\n";
  for(int channel=0;channel<16;++channel){
   FluidFixture f(root+"/tone.sf2");auto out=audio(256);
   check(fluid_synth_noteon(f.synth.get(),channel,60,100)==FLUID_OK,"fluid noteon");f.render(256,out);
   for(int c=0;c<16;++c)check(c==channel?(peak(out[c*2])>0.001f):(peak(out[c*2])==0 && peak(out[c*2+1])==0),"16 isolated stereo groups");
  }
  std::cout<<"PASS SF2 all 16 isolated groups, explicit bank 0/preset 0 and drum bank 128\n";
  for(int frame:{0,1,63,64,127,255,256,257}){
   FluidFixture f(root+"/tone.sf2");auto out=audio(768);
   if(frame)f.render(frame,out);
   fluid_synth_noteon(f.synth.get(),0,60,100);f.render(768-frame,out,frame);
   const int start=onset(out[0]);check(start>=frame && start-frame<=fluid_synth_get_internal_bufsize(f.synth.get())+2,"bounded FluidSynth internal quantum");
   std::cout<<"fluid event="<<frame<<" onset="<<start<<" error="<<start-frame<<" frames\n";
  }
  {FluidFixture f(root+"/tone.sf2");auto out=audio(256);fluid_synth_noteon(f.synth.get(),0,60,100);f.render(256,out);
   fluid_synth_program_select(f.synth.get(),0,f.soundfont,0,1);fluid_synth_noteoff(f.synth.get(),0,60);
   fluid_synth_all_sounds_off(f.synth.get(),0);auto silence=audio(512);f.render(512,silence);
   check(peak(silence[0])<0.00001f,"release after program change");
   check(fluid_synth_program_select(f.synth.get(),0,f.soundfont,0,127)==FLUID_FAILED,"missing SF2 mapping is not silently selected");}
  for(int frame:{0,1,63,64,127,255}){
   auto synth=makeSfz(root+"/tone.sfz");std::array<std::vector<float>,2> out{std::vector<float>(256),std::vector<float>(256)};
   sfizz_send_note_on(synth.get(),frame,60,100);renderSfz(synth.get(),out);
   int start=onset(out[0]);
   if(start<0){out={std::vector<float>(256),std::vector<float>(256)};renderSfz(synth.get(),out);start=256+onset(out[0]);}
   check(start>=frame && start-frame<=2,"SFZ delay at first/middle/last frame");
   std::cout<<"sfizz event="<<frame<<" onset="<<start<<" error="<<start-frame<<" frames\n";
  }
  // CC120 must be dispatched at the split-render boundary: sfizz 1.2.3
  // resets voices immediately even when its CC API receives a nonzero delay.
  for(int frame:{0,1,127,255,256,257}) {
   FluidFixture f(root+"/tone.sf2"); auto warm=audio(256);
   fluid_synth_noteon(f.synth.get(),0,60,100);f.render(256,warm);
   auto out=audio(768);if(frame)f.render(frame,out);
   fluid_synth_cc(f.synth.get(),0,120,0);f.render(768-frame,out,frame);
   int last=-1;for(int i=0;i<768;++i)if(std::abs(out[0][i])>0.000001f)last=i;
   check(last < frame+fluid_synth_get_internal_bufsize(f.synth.get()),"Fluid CC120 bounded pending buffer");
   std::cout<<"fluid CC120 event="<<frame<<" last="<<last<<" frames\n";
   auto z=makeSfz(root+"/tone.sfz");
   std::array<std::vector<float>,2> b{std::vector<float>(256),std::vector<float>(256)};
   sfizz_send_note_on(z.get(),0,60,100);renderSfz(z.get(),b);
   int remaining=frame;
   while(remaining){const int n=std::min(remaining,256);b={std::vector<float>(n),std::vector<float>(n)};renderSfz(z.get(),b);remaining-=n;}
   sfizz_send_cc(z.get(),0,120,0);b={std::vector<float>(256),std::vector<float>(256)};renderSfz(z.get(),b);
   check(peak(b[0])==0 && peak(b[1])==0,"sfizz CC120 split-render boundary");
   std::cout<<"sfizz CC120 boundary="<<frame<<" post-boundary silence PASS\n";
  }
  {auto z=makeSfz(root+"/tone.sfz");std::array<std::vector<float>,2> b{std::vector<float>(256),std::vector<float>(256)};
   renderSfz(z.get(),b);check(peak(b[0])==0,"cross-cycle first block silence");
   sfizz_send_note_on(z.get(),1,60,100);renderSfz(z.get(),b);
   check(onset(b[0])==2,"sfizz event at absolute frame 257");
   std::cout<<"sfizz cross-cycle event=257 onset=258; reported engine bytes="<<sfizz_get_num_bytes(z.get())<<" (partial accounting, not RSS)\n";}
  {auto a=makeSfz(root+"/tone.sfz"),b=makeSfz(root+"/tone.sfz");std::array<std::vector<float>,2> out{std::vector<float>(256),std::vector<float>(256)};
   sfizz_send_note_on(a.get(),0,60,100);renderSfz(a.get(),out);check(peak(out[0])>0.001f,"SFZ nonzero output");
   renderSfz(b.get(),out);check(peak(out[0])==0 && peak(out[1])==0,"SFZ isolated instances");
   sfizz_send_cc(a.get(),0,64,127);sfizz_send_note_off(a.get(),0,60,0);renderSfz(a.get(),out);check(peak(out[0])>0,"SFZ sustain");
   sfizz_send_cc(a.get(),0,64,0);for(int i=0;i<32;++i)renderSfz(a.get(),out);check(peak(out[0])<0.00001f,"SFZ pedal release");}
  std::cout<<"PASS engine fixture loading, event onset bounds, presets, sustain and release. No audio/MIDI drivers created.\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
