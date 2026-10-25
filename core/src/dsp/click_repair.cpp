// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/click_repair.h"
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
bool predictable(const std::vector<double>& context, int gap) {
    // Cross-check on real, unmodified samples beside the gap. A voiced context
    // which Burg cannot reconstruct there is not safe to extrapolate inside it.
    const int n=std::min<int>(gap,context.size()/3);
    if(n<2)return false;
    auto estimated=predict(std::vector<double>(context.begin(),context.end()-n),n);
    if(estimated.size()!=static_cast<size_t>(n))return false;
    double error=0,power=0;
    for(int i=0;i<n;++i){const double actual=context[context.size()-n+i];error+=std::pow(estimated[i]-actual,2);power+=actual*actual;}
    return error<=.1*std::max(power,1e-12);
}
}
ClickRepairStatus repairClick(std::vector<float>& audio,int channels,double rate,int64_t first,int64_t last,const std::string& method){
    if(channels<=0||!std::isfinite(rate)||rate<=0||first<1||last<=first||last>=static_cast<int64_t>(audio.size()/channels))return ClickRepairStatus::Invalid;
    if(last-first>rate*.045)return ClickRepairStatus::Invalid;
    if(method!="auto"&&method!="linear"&&method!="predictive")return ClickRepairStatus::Invalid;
    const int count=static_cast<int>(last-first);
    const bool predictive=method=="predictive"||(method=="auto"&&count>rate*.00025);
    const int context=std::min<int>(static_cast<int>(rate*.025),static_cast<int>(std::min(first,static_cast<int64_t>(audio.size()/channels)-last)));
    std::vector<float> replacement(static_cast<size_t>(count)*channels);
    for(int channel=0;channel<channels;++channel){
        const double left=audio[(first-1)*channels+channel],right=audio[last*channels+channel];
        std::vector<double> forward,backward;
        if(predictive&&context>=96){
            std::vector<double> before,after;
            for(int i=context;i>0;--i)before.push_back(audio[(first-i)*channels+channel]);
            for(int i=context-1;i>=0;--i)after.push_back(audio[(last+i)*channels+channel]);
            forward=predict(before,count);backward=predict(after,count);
            if(method=="auto") {
                if(!predictable(before,count)||!predictable(after,count))return ClickRepairStatus::Declined;
                double disagreement=0,power=0;
                if(forward.size()!=static_cast<size_t>(count)||backward.size()!=static_cast<size_t>(count))return ClickRepairStatus::Declined;
                for(int i=0;i<count;++i){disagreement+=std::pow(forward[i]-backward[count-1-i],2);power+=forward[i]*forward[i]+backward[i]*backward[i];}
                if(disagreement>.1*std::max(power,1e-12))return ClickRepairStatus::Declined;
            }
        }
        if(predictive && (forward.size()!=static_cast<size_t>(count) || backward.size()!=static_cast<size_t>(count)) && count>rate*.0005)
            return method=="auto"?ClickRepairStatus::Declined:ClickRepairStatus::Invalid;
        for(int i=0;i<count;++i){
            const double t=double(i+1)/(count+1);
            double value=left+(right-left)*t;
            if(forward.size()==static_cast<size_t>(count)&&backward.size()==static_cast<size_t>(count))
                value=(1-t)*forward[i]+t*backward[count-1-i];
            replacement[static_cast<size_t>(i)*channels+channel]=static_cast<float>(value);
        }
    }
    std::copy(replacement.begin(),replacement.end(),audio.begin()+first*channels);
    return ClickRepairStatus::Repaired;
}
}
