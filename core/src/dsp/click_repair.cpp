// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/click_repair.h"
#include "core/dsp/frame_features.h"
#include <algorithm>
#include <cmath>
namespace bc {
namespace {
std::vector<double> predict(const std::vector<double>& context, int count) {
    const int order=std::min<int>(24,static_cast<int>(context.size())/4);
    if(order<2)return {};
    std::vector<double> a(order+1,0),ef(context.begin()+1,context.end()),eb(context.begin(),context.end()-1);
    double energy=0;for(double x:context)energy+=x*x;
    if(energy<1e-16)return std::vector<double>(count,0);
    a[0]=1;
    // Burg's forward/backward estimate avoids the finite-window bias of
    // autocorrelation LPC on short, strongly periodic voice context.
    for(int k=1;k<=order;++k){
        double numerator=0,denominator=0;
        for(size_t n=0;n<ef.size();++n){numerator+=ef[n]*eb[n];denominator+=ef[n]*ef[n]+eb[n]*eb[n];}
        if(denominator<energy*1e-12)break;
        const double reflection=std::clamp(-2*numerator/denominator,-.9999999,.9999999);
        auto next=a;for(int j=1;j<k;++j)next[j]=a[j]+reflection*a[k-j];next[k]=reflection;a=next;
        std::vector<double> nf,nb;
        for(size_t n=1;n<ef.size();++n){nf.push_back(ef[n]+reflection*eb[n]);nb.push_back(eb[n-1]+reflection*ef[n-1]);}
        ef=std::move(nf);eb=std::move(nb);
    }
    std::vector<double> history(context.end()-order,context.end()),output;
    double limit=0;for(double v:context)limit=std::max(limit,std::abs(v));
    limit=std::max(.001,limit*2);
    for(int n=0;n<count;++n){
        double value=0;for(int j=1;j<=order;++j)value-=a[j]*history[history.size()-j];
        if(!std::isfinite(value)||std::abs(value)>limit)return {};
        output.push_back(value);history.push_back(value);
    }
    return output;
}
double predictionError(const std::vector<double>& context, int gap) {
    // Cross-check on real, unmodified samples beside the gap. A voiced context
    // which Burg cannot reconstruct there is not safe to extrapolate inside it.
    const int n=std::min<int>(gap,context.size()/3);
    if(n<2)return 1e6;
    auto estimated=predict(std::vector<double>(context.begin(),context.end()-n),n);
    if(estimated.size()!=static_cast<size_t>(n))return 1e6;
    double error=0,power=0;
    for(int i=0;i<n;++i){const double actual=context[context.size()-n+i];error+=std::pow(estimated[i]-actual,2);power+=actual*actual;}
    return std::min(1e6,error/std::max(power,1e-12));
}
struct BridgeErrors { double linear=1e6, predictive=1e6; };
BridgeErrors bridgeErrors(const std::vector<double>& context, int gap) {
    // Compare the ACTUAL bidirectional repairs on the SAME hidden intact
    // samples. A one-sided forecast error is not the error of a Burg bridge.
    const int size=static_cast<int>(context.size());
    if(gap<1 || size<gap+288)return {};
    double linear=0,predictive=0,power=0,contextPower=0;
    bool valid=true;
    for(double v:context)contextPower+=v*v;
    for(int offset:{(size-gap)/3,2*(size-gap)/3}) {
        auto forward=predict(std::vector<double>(context.begin(),context.begin()+offset),gap);
        auto backward=predict(std::vector<double>(context.rbegin(),context.rend()-offset-gap),gap);
        const bool predicted=forward.size()==static_cast<size_t>(gap)&&backward.size()==static_cast<size_t>(gap);
        valid=valid&&predicted;
        for(int i=0;i<gap;++i) {
            const double actual=context[offset+i];
            const double t=double(i+1)/(gap+1);
            const double value=(1-t)*context[offset-1]+t*context[offset+gap];
            linear+=(value-actual)*(value-actual);power+=actual*actual;
            if(predicted) {
                const double estimate=(1-t)*forward[i]+t*backward[gap-1-i];
                predictive+=(estimate-actual)*(estimate-actual);
            }
        }
    }
    const double denominator=std::max({power,contextPower/size*gap*.04,1e-12});
    return {std::min(1e6,linear/denominator),valid?std::min(1e6,predictive/denominator):1e6};
}

double boundaryRms(const std::vector<double>& context,int count) {
    double power=0;
    for(auto it=context.end()-count;it!=context.end();++it)power+=(*it)*(*it);
    return std::sqrt(power/std::max(1,count));
}
double impulseRatio(const std::vector<double>& before,const std::vector<double>& after,
                    const std::vector<float>& audio,int channel,int channels,int64_t first,int64_t last) {
    std::vector<double> residual;
    for(const auto* context:{&before,&after})
        for(size_t i=1;i+1<context->size();++i)
            residual.push_back(std::abs((*context)[i]-.5*((*context)[i-1]+(*context)[i+1])));
    if(residual.empty())return 0;
    auto mid=residual.begin()+residual.size()/2;std::nth_element(residual.begin(),mid,residual.end());
    double peak=0;
    for(int64_t i=first;i<last;++i)
        peak=std::max(peak,std::abs(double(audio[i*channels+channel])-.5*(audio[(i-1)*channels+channel]+audio[(i+1)*channels+channel])));
    return std::min(1e6,peak/std::max(*mid,1e-5));
}
}
ClickRepairStatus repairClick(std::vector<float>& audio,int channels,double rate,int64_t first,int64_t last,const std::string& method,ClickRepairDiagnostics* diagnostics){
    ClickRepairDiagnostics d;d.requestedMethod=method;
    auto finish=[&](ClickRepairStatus status,const char* reason){d.reason=reason;if(diagnostics)*diagnostics=d;return status;};
    if(channels<=0||audio.size()%channels||!std::isfinite(rate)||rate<=0||rate>768000||first<0||last<=first||last>static_cast<int64_t>(audio.size()/channels))return finish(ClickRepairStatus::Invalid,"invalid_input");
    d.durationMs=1000.*(last-first)/rate;
    if(method=="none")return finish(ClickRepairStatus::Bypassed,"manual_bypass");
    if(first<1||last>=static_cast<int64_t>(audio.size()/channels)||last-first>rate*.045)return finish(ClickRepairStatus::Invalid,"bounds_or_duration");
    if(method!="auto"&&method!="linear"&&method!="predictive")return finish(ClickRepairStatus::Invalid,"unknown_strategy");
    const int count=static_cast<int>(last-first);
    const int context=std::min<int>(static_cast<int>(rate*.025),static_cast<int>(std::min(first,static_cast<int64_t>(audio.size()/channels)-last)));
    d.contextMs=1000.*context/rate;
    std::vector<std::vector<double>> linear(channels),predicted(channels);
    bool linearSafe=true,predictiveSafe=true,microImpulse=true;
    if(method=="auto") {d.linearValidationError=0;d.linearVoiceRisk=0;d.predictiveBridgeError=0;d.predictiveVoiceRisk=0;d.impulseRatio=1e6;d.contextLevelRatio=1;d.boundaryLevelRatio=1;d.contextHighBandRatio=0;}
    for(int channel=0;channel<channels;++channel){
        const double left=audio[(first-1)*channels+channel],right=audio[last*channels+channel];
        std::vector<double> before,after;
        for(int i=context;i>0;--i)before.push_back(audio[(first-i)*channels+channel]);
        for(int i=context-1;i>=0;--i)after.push_back(audio[(last+i)*channels+channel]);
        for(int64_t i=first-context;i<last+context;++i)
            if(!std::isfinite(audio[i*channels+channel]))return finish(ClickRepairStatus::Invalid,"nonfinite_audio");
        for(int i=0;i<count;++i)linear[channel].push_back(left+(right-left)*double(i+1)/(count+1));
        // A manual algorithm choice is not permission to erase voice. The RC3
        // listening study exposed an 11.52 ms speech-overlap proposal where
        // both forced bridges removed a phonetic fragment. Validate the chosen
        // reconstruction on intact context with the same impact budget as Auto,
        // before committing ANY channel. Do not crossfade back damaged samples
        // or silently substitute another algorithm. Tiny linear edge repairs
        // remain possible when the file cannot supply a validation context.
        if(method!="auto" && !(method=="linear" && count<=rate*.00025 && context<count+288)) {
            const auto bv=bridgeErrors(before,count),av=bridgeErrors(after,count);
            const double factor=double(count)/std::max(1,context)*(1+d.durationMs*d.durationMs/16.);
            const double le=std::max(bv.linear,av.linear),be=std::max(bv.predictive,av.predictive);
            d.linearValidationError=std::max(d.linearValidationError,le);
            d.linearVoiceRisk=std::max(d.linearVoiceRisk,le*factor);
            d.predictiveBridgeError=std::max(d.predictiveBridgeError,be);
            d.predictiveVoiceRisk=std::max(d.predictiveVoiceRisk,be*factor);
            if((method=="linear"?le:be)*factor>.10)
                return finish(ClickRepairStatus::Declined,"unsafe_manual_reconstruction");
        }
        if(method=="linear")continue;
        bool channelLinear=false,channelPredictive=false;
        if(method=="auto") {
            const auto bv=bridgeErrors(before,count),av=bridgeErrors(after,count);
            const double le=std::max(bv.linear,av.linear),be=std::max(bv.predictive,av.predictive);
            d.linearValidationError=std::max(d.linearValidationError,le);
            // A pointwise waveform error overstates the impact of a very short
            // bridge. Budget its footprint against the surrounding 25 ms of
            // voice, with an increasing penalty for longer replacements.
            const double voiceRisk=le*count/std::max(1,context)*(1+d.durationMs*d.durationMs/16.);
            d.linearVoiceRisk=std::max(d.linearVoiceRisk,voiceRisk);
            const double burgRisk=be*count/std::max(1,context)*(1+d.durationMs*d.durationMs/16.);
            d.predictiveBridgeError=std::max(d.predictiveBridgeError,be);
            d.predictiveVoiceRisk=std::max(d.predictiveVoiceRisk,burgRisk);
            const double impulse=impulseRatio(before,after,audio,channel,channels,first,last);
            d.impulseRatio=std::min(d.impulseRatio,impulse);
            const bool micro=count<=rate*.00025 && impulse>=8;
            microImpulse=microImpulse&&micro;
            FrameAnalyzer::Config config;config.sampleRate=static_cast<float>(rate);
            config.fftSize=128;while(config.fftSize<rate*.02 && config.fftSize<4096)config.fftSize*=2;
            FrameAnalyzer analyzer(config);
            std::vector<float> b(before.begin(),before.end()),a(after.begin(),after.end());
            const auto bf=analyzer.analyze(b.data(),b.size());analyzer.reset();
            const auto af=analyzer.analyze(a.data(),a.size());
            const double level=std::max(bf.rms,af.rms)/std::max(1e-5f,std::min(bf.rms,af.rms));
            const double high=std::max(bf.highBandRatio,af.highBandRatio);
            d.contextLevelRatio=std::max(d.contextLevelRatio,level);d.contextHighBandRatio=std::max(d.contextHighBandRatio,high);
            // Long context is useful for prediction, but includes other phonemes.
            // Protect a level transition at the actual boundaries, not anywhere
            // within 25 ms. Both context vectors end at the repair boundary.
            const int near=std::min(context,std::max(count,static_cast<int>(rate*.002)));
            const double br=boundaryRms(before,near),ar=boundaryRms(after,near);
            const double boundaryLevel=std::max(br,ar)/std::max(1e-5,std::min(br,ar));
            d.boundaryLevelRatio=std::max(d.boundaryLevelRatio,boundaryLevel);
            const bool onset=boundaryLevel>8;
            const bool noisy=high>.75 && std::max(bf.harmonicRatio,af.harmonicRatio)<.25;
            channelLinear=(micro || (voiceRisk<=.10 && impulse>=8)) && (!onset||micro) && (!noisy||micro);
            channelPredictive=burgRisk<=.10 && impulse>=8 && !onset && !noisy;
            linearSafe=linearSafe&&channelLinear;
        }
        // Evaluate both candidates: a safe straight line can still erase more
        // voice curvature than a validated predictive bridge.
        auto forward=context>=96?predict(before,count):std::vector<double>{};
        auto backward=context>=96?predict(after,count):std::vector<double>{};
        if(forward.size()!=static_cast<size_t>(count)||backward.size()!=static_cast<size_t>(count)) {
            predictiveSafe=false;continue;
        }
        const double pe=std::max(predictionError(before,count),predictionError(after,count));
        d.predictiveValidationError=std::max(d.predictiveValidationError,pe);
        double disagreement=0,power=0;
        for(int i=0;i<count;++i){disagreement+=std::pow(forward[i]-backward[count-1-i],2);power+=forward[i]*forward[i]+backward[i]*backward[i];
            const double t=double(i+1)/(count+1);predicted[channel].push_back((1-t)*forward[i]+t*backward[count-1-i]);}
        const double agreement=disagreement/std::max(power,1e-12);
        d.predictionDisagreement=std::max(d.predictionDisagreement,agreement);
        if(method=="auto" && (!channelPredictive||agreement*count/std::max(1,context)>.10))predictiveSafe=false;
    }
    bool useLinear=method=="linear"||(method=="auto"&&linearSafe&&
        (!predictiveSafe||d.predictiveVoiceRisk>=d.linearVoiceRisk*.8));
    if(!useLinear) {
        for(const auto& channel:predicted)if(channel.size()!=static_cast<size_t>(count))predictiveSafe=false;
        if(!predictiveSafe)return finish(ClickRepairStatus::Declined,context<96?"insufficient_context_for_burg":d.boundaryLevelRatio>8?"vocal_onset_or_level_change":"context_does_not_justify_reconstruction");
    }
    d.selectedMethod=useLinear?"linear":"predictive";
    // Commit all channels together, using the exact requested interval.
    for(int i=0;i<count;++i)for(int c=0;c<channels;++c)
        audio[(first+i)*channels+c]=static_cast<float>((useLinear?linear:predicted)[c][i]);
    return finish(ClickRepairStatus::Repaired,method!="auto"?"manual_strategy":useLinear?(microImpulse?"isolated_micro_impulse":"linear_context_validated"):"burg_bridge_context_validated");
}
}
