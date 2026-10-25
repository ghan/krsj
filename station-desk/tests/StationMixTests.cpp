#ifndef STATION_MIX_HEADER
#define STATION_MIX_HEADER "../Source/StationMix.h"
#endif
#include STATION_MIX_HEADER
#include <iostream>
#include <random>
#include <string>
int main() {
    int passed=0,failed=0;
    auto check=[&](bool ok,const std::string& id) {std::cout<<(ok?"PASS ":"FAIL ")<<id<<'\n';ok?++passed:++failed;};
    auto near=[](float a,float b,float eps=.003f){return std::isfinite(a)&&std::abs(a-b)<eps;};
    for(double rate:{44100.,48000.,96000.}) {
        auto suffix="."+std::to_string(int(rate));StationMix m;m.reset(rate);
        check(near(m.program(.4f,.9f,.8f,1,1),.4f),"mix.local_isolation"+suffix);
        for(int i=0;i<int(rate/2);++i)m.advance(true,1,false,0);
        check(near(m.program(.4f,.8f,0,1,1),.6f),"mix.half_fade"+suffix);
        for(int i=0;i<int(rate/2)+100;++i)m.advance(true,1,false,0);
        check(near(m.program(.4f,.8f,0,1,1),.8f),"mix.remote_isolation"+suffix);
        for(int i=0;i<int(rate);++i)m.advance(true,1,true,.5f);
        check(near(m.program(0,.8f,.2f,1,1),.3f),"mix.duck_voice_independent"+suffix);
        for(int i=0;i<int(rate)+100;++i)m.advance(false,1,false,0);
        check(near(m.program(.4f,.8f,1,1,1),.4f),"mix.fade_back_and_mic_off"+suffix);
        m.advance(true,0,false,0);check(near(m.remoteMix,1),"mix.zero_fade"+suffix);
        m.reset(rate);for(int i=0;i<int(rate/4);++i)m.advance(true,1,false,0);
        auto before=m.remoteMix;m.advance(false,1,false,0);
        check(m.remoteMix<before&&before-m.remoteMix<.0001f,"mix.reversal_continuity"+suffix);
        m.reset(rate);m.advance(false,0,false,0);
        check(near(m.program(.8f,1,0,.25f,.9f),.2f),"mix.local_gain"+suffix);
        m.advance(true,0,false,0);
        check(near(m.program(1,.8f,0,.9f,.25f),.2f),"mix.remote_gain"+suffix);
    }
    std::mt19937 rng(731);std::uniform_real_distribution<float> samples(-1,1),gains(0,1.5);
    bool finite=true,bounded=true,linear=true;
    StationMix m;
    for(int i=0;i<50000;++i){
        m.advance((rng()%2)!=0,(rng()%5)*.25f,(rng()%2)!=0,gains(rng));
        float l=samples(rng),r=samples(rng),v=samples(rng),lg=gains(rng),rg=gains(rng);
        float a=m.program(l,r,v,lg,rg);
        finite &= std::isfinite(a);bounded &= m.remoteMix>=0&&m.remoteMix<=1&&m.duckGain>=.25f&&m.duckGain<=1;
        linear &= near(m.program(l*.5f,r*.5f,v*.5f,lg,rg),a*.5f,.00001f);
    }
    check(finite,"mix.random_finite");check(bounded,"mix.random_envelopes_bounded");check(linear,"mix.signal_linearity");
    std::cout<<"SUMMARY "<<passed<<" passed "<<failed<<" failed\n";
    return failed?1:0;
}
