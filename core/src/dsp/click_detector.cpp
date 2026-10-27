// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/click_detector.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

float smoothstep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    const float x = clampf((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

ClickDetector::ClickDetector(const ClickDetectorConfig& config)
    : config_(config) {
    config_.subWindowSamples = std::max(8, config_.subWindowSamples);
    config_.historyFrames = std::max(16, config_.historyFrames);

    history_.assign(static_cast<size_t>(config_.historyFrames), 0.0f);
    sortScratch_.assign(static_cast<size_t>(config_.historyFrames), 0.0f);
}

ClickDetector::~ClickDetector() = default;

void ClickDetector::setSensitivity(float sensitivity) {
    config_.sensitivity = clampf(sensitivity, 0.0f, 1.0f);
}

void ClickDetector::reset() {
    std::fill(history_.begin(), history_.end(), 0.0f);
    historyCount_ = 0;
    historyWrite_ = 0;
    delayA_ = 0.0;
    delayB_ = 0.0;
    delayCount_ = 0;
    medianAbsError_ = 0.0f;
}

void ClickDetector::pushHistory(float value) {
    history_[historyWrite_] = value;
    historyWrite_ = (historyWrite_ + 1) % history_.size();
    if (historyCount_ < history_.size()) {
        ++historyCount_;
    }
}

float ClickDetector::robustMedian() {
    if (historyCount_ == 0) {
        return 0.0f;
    }

    std::copy_n(history_.begin(), historyCount_, sortScratch_.begin());
    std::sort(sortScratch_.begin(),
              sortScratch_.begin() + static_cast<std::ptrdiff_t>(historyCount_));

    const size_t mid = historyCount_ / 2;
    return (historyCount_ % 2 == 0)
        ? 0.5f * (sortScratch_[mid - 1] + sortScratch_[mid])
        : sortScratch_[mid];
}

ClickScore ClickDetector::processBlock(const float* samples, size_t count) {
    ClickScore result;

    if (!samples || count == 0) {
        return result;
    }

    // ------------------------------------------- errore di predizione d[n]
    float peak = 0.0f;
    int peakIndex = -1;
    int aboveHalf = 0;
    size_t valuesConsidered = 0;

    for (size_t i = 0; i < count; ++i) {
        const double x = samples[i];

        if (delayCount_ >= 2) {
            // d corrisponde al campione delayB_ = x[n-1].
            const double derivative = delayB_ - 0.5 * (delayA_ + x);
            const float magnitude = static_cast<float>(std::abs(derivative));
            ++valuesConsidered;
            if (magnitude > peak) {
                peak = magnitude;
                peakIndex = static_cast<int>(i) - 1;
            }
        }

        delayA_ = delayB_;
        delayB_ = x;
        if (delayCount_ < 2) ++delayCount_;
    }

    if (valuesConsidered == 0) {
        result.peakIndex = -1;
        return result;
    }

    // Secondo passaggio: quanti campioni superano meta' del picco? Serve a
    // distinguere un click (strettissimo) da un attacco di nota (largo).
    if (peak > 0.0f) {
        const double halfPeak = 0.5 * static_cast<double>(peak);
        // Si ricalcola sui campioni gia' consumati usando lo stesso stato: per
        // semplicita' e costo contenuto si analizza il blocco con un secondo
        // predittore locale che riparte dai campioni del blocco.
        for (size_t i = 1; i + 1 < count; ++i) {
            const double derivative =
                static_cast<double>(samples[i]) -
                0.5 * (static_cast<double>(samples[i - 1]) + static_cast<double>(samples[i + 1]));
            if (std::abs(derivative) > halfPeak) {
                ++aboveHalf;
            }
        }
    }

    const float narrowness = valuesConsidered > 0
        ? 1.0f - static_cast<float>(aboveHalf) / static_cast<float>(valuesConsidered)
        : 0.0f;
    result.narrowness = clampf(narrowness, 0.0f, 1.0f);
    result.peak = peak;
    result.peakIndex = peakIndex;

    // ------------------------------------------------------- stima robusta
    const float median = robustMedian();
    result.median = median;

    // Il denominatore non deve mai essere nullo: si usa un pavimento legato
    // alla risoluzione del segnale.
    const float denominator = std::max(median, 1.0e-7f);
    const float ratio = peak / denominator;
    result.ratio = ratio;

    // ---------------------------------------------------------- punteggio
    // Sensibilita': 0.5 -> soglie nominali; alta -> soglie piu' basse.
    // L'escursione e' volutamente contenuta: le soglie nominali stanno in una
    // zona di separazione ampia e non vanno spostate fino a confondere un
    // respiro con un click.
    const float scale = 1.2f - 0.5f * config_.sensitivity;   // 1.2 .. 0.7
    const float low = config_.ratioLow * scale;
    const float high = config_.ratioHigh * scale;

    const float ratioTerm = smoothstep(low, high, ratio);

    // Un evento largo (attacco di nota, plosiva) non e' un click.
    const float narrowTerm = 1.0f - smoothstep(config_.narrowFraction,
                                                config_.narrowFraction * 2.5f,
                                                1.0f - result.narrowness);

    const float levelTerm = smoothstep(config_.minPeakLevel, config_.minPeakLevel * 20.0f, peak);

    float score = ratioTerm * narrowTerm * levelTerm;

    // Pavimento di storia: senza abbastanza campioni la mediana non e'
    // affidabile e si preferisce non rilevare.
    if (historyCount_ < 8) {
        score *= 0.3f;
    }

    result.score = clampf(score, 0.0f, 1.0f);
    result.detected = result.score > 0.0f;

    // La storia registra il picco della sotto-finestra, non il punteggio.
    pushHistory(peak);
    medianAbsError_ = robustMedian();

    return result;
}

std::vector<Detection> detectMouthClicks(const float* x,size_t count,double rate,const DetectorSettings& settings,const std::function<bool()>& cancelled){
    std::vector<Detection> out;
    if(!x||count<5||!std::isfinite(rate)||rate<=0||rate>768000)return out;
    const size_t hop=std::max<size_t>(4,static_cast<size_t>(rate*.0005));
    const size_t frames=(count+hop-1)/hop;
    std::vector<float> peak(frames),rms(frames),level(frames),base(frames),ratios(frames);
    std::vector<int> active(frames);
    for(size_t f=0;f<frames;++f){
        if((f&255)==0 && cancelled && cancelled())return {};
        double error=0,energy=0;size_t n=0;
        for(size_t i=std::max<size_t>(1,f*hop);i<std::min(count-1,(f+1)*hop);++i){
            const float d=std::abs(x[i]-.5f*(x[i-1]+x[i+1]));
            peak[f]=std::max(peak[f],d);error+=d*d;energy+=x[i]*x[i];++n;
        }
        rms[f]=static_cast<float>(std::sqrt(error/std::max<size_t>(1,n)));
        level[f]=static_cast<float>(std::sqrt(energy/std::max<size_t>(1,n)));
    }
    std::vector<float> context;context.reserve(128);
    const float threshold=20.f-12.f*std::clamp(settings.clickSensitivity,0.f,1.f);
    for(size_t f=0;f<frames;++f){
        if((f&255)==0 && cancelled && cancelled())return {};
        context.clear();
        for(size_t j=f>60?f-60:0;j<std::min(frames,f+61);++j)
            if(j+8<f||j>f+8)context.push_back(rms[j]);
        if(context.empty())continue;
        auto middle=context.begin()+context.size()/2;std::nth_element(context.begin(),middle,context.end());
        base[f]=std::max(1e-5f,*middle);
        ratios[f]=peak[f]/base[f];
        active[f]=peak[f]>.001f && ratios[f]>threshold && rms[f]>base[f]*3;
    }
    // Keep the original pass byte-for-byte stable. A separate, review-only
    // pass can expose quieter impulses that the old absolute floor concealed,
    // without merging them into existing events or admitting automatic edits.
    for(int pass=0;pass<2;++pass){
    if(pass==1)for(size_t f=0;f<frames;++f)
        active[f]=peak[f]>.0001f && ratios[f]>threshold && rms[f]>base[f]*3;
    for(size_t f=0;f<frames;){
        if(cancelled && cancelled())return {};
        if(!active[f]){++f;continue;}
        const size_t begin=f;size_t end=f+1;
        while(end<frames&&(active[end]||(end+1<frames&&active[end+1])))++end;
        f=end;
        if((end-begin)*hop>rate*settings.maxClickMs/1000.)continue;
        size_t a=std::max<size_t>(1,begin*hop),b=std::min(count-1,end*hop);
        float maximum=0,ratio=0;double error=0,energy=0;
        for(size_t j=begin;j<end;++j){maximum=std::max(maximum,peak[j]);ratio=std::max(ratio,ratios[j]);error+=rms[j]*rms[j];energy+=level[j]*level[j];}
        if(pass==1 && maximum>.001f)continue;
        const float edge=std::max(base[begin]*3,maximum*.04f);
        auto residual=[&](size_t i){return std::abs(x[i]-.5f*(x[i-1]+x[i+1]));};
        while(a+1<b&&residual(a)<edge)++a;
        while(b>a+1&&residual(b-1)<edge)--b;
        a=a>1?a-1:a;b=std::min(count-1,b+1);
        if(b<=a)continue;
        const double ms=1000.*(b-a)/rate;
        // Periodic glottal pulses can be narrow and very large relative to the
        // median derivative. Compare neighbouring cycles before calling them clicks.
        double bestCorrelation=0;size_t bestLag=0;
        const size_t radius=static_cast<size_t>(rate*.004);
        for(size_t lag=std::max<size_t>(1,static_cast<size_t>(rate/350));lag<std::min<size_t>(count/2,static_cast<size_t>(rate/65));lag+=2){
            double xy=0,xx=0,yy=0;
            const size_t from=a>radius+lag?a-radius:lag;
            const size_t to=std::min(count-lag,b+radius);
            for(size_t i=from;i<to;i+=4){
                if(i>=a&&i<b)continue;
                const double prediction=.5*(x[i-lag]+x[i+lag]);
                xy+=x[i]*prediction;xx+=x[i]*x[i];yy+=prediction*prediction;
            }
            const double correlation=xy/std::sqrt(std::max(1e-24,xx*yy));
            if(correlation>bestCorrelation){bestCorrelation=correlation;bestLag=lag;}
        }
        if(bestCorrelation>.85 && a>=bestLag && b+bestLag<count){
            double mismatch=0,signal=0;
            for(size_t i=a;i<b;++i){const double delta=x[i]-.5*(x[i-bestLag]+x[i+bestLag]);mismatch+=delta*delta;signal+=x[i]*x[i];}
            if(mismatch<std::max(1e-15,signal)*.15)continue;
        }
        if(ms<.6){
            double nearby=0;size_t neighbours=0;
            for(size_t j=begin>4?begin-4:0;j<std::min(frames,end+4);++j)
                if(j+1<begin||j>end){nearby+=rms[j];++neighbours;}
            if(neighbours && maximum<6*nearby/neighbours)continue;
        }
        double contextEnergy=0;size_t n=0;
        for(size_t j=begin>40?begin-40:0;j<std::min(frames,end+40);++j)
            if(j<begin||j>=end){contextEnergy+=level[j]*level[j];++n;}
        const double relativeContext=n?std::sqrt(contextEnergy/n)/std::max(1e-8,std::sqrt(energy/std::max<size_t>(1,end-begin))):0;
        Detection d;d.kind=DetectionKind::Click;d.startSample=static_cast<int64_t>(a);d.endSample=static_cast<int64_t>(b);
        d.peakScore=std::clamp(1.f-std::exp(-ratio/24.f),0.f,1.f);d.meanScore=d.peakScore;d.dspScore=d.peakScore;
        if(error<std::max(1e-15,energy)*.035)continue; // preserve low-band/glottal attacks at every width
        if(ms<.2)d.classification="digital_click";
        else if(relativeContext>.35 || bestCorrelation>.6)d.classification="speech_overlap";
        else d.classification=ms<=8?"saliva_click":"mouth_click";
        if(pass==1){
            if(std::any_of(out.begin(),out.end(),[&](const Detection& old){return a<static_cast<size_t>(old.endSample)&&b>static_cast<size_t>(old.startSample);}))continue;
            d.classification="low_level_impulse";
        }
        d.reason=(d.classification=="digital_click"||d.classification=="saliva_click")?
            "DSP: impulso rispetto al contesto locale; verificare con A/B":
            "DSP: evento incerto o transiente vocale; revisione richiesta, nessuna riparazione automatica";
        if(pass==1)d.reason="DSP: impulso debole da verificare; nessuna riparazione automatica";
        if(d.peakScore>=settings.clickThreshold)out.push_back(d);
    }
    }
    // A one-sample predictor sees broadband spikes well, but suppresses softer
    // band-limited mouth clicks. Compare two wider predictors against EACH side
    // independently: a quiet side must not hide a consonant onset on the other.
    // These are hypotheses for review, not permission to reconstruct speech.
    const size_t step=std::max<size_t>(1,static_cast<size_t>(std::llround(rate*.001)));
    const size_t blocks=count/step;
    if(blocks>120) {
        const size_t lags[]={std::max<size_t>(1,static_cast<size_t>(std::llround(rate*.0001))),
                            std::max<size_t>(1,static_cast<size_t>(std::llround(rate*.00025)))};
        std::vector<float> bands[2];
        for(auto& band:bands)band.resize(blocks);
        for(size_t f=0;f<blocks;++f) {
            if((f&255)==0 && cancelled && cancelled())return {};
            double power[2]={};
            for(size_t i=f*step;i<(f+1)*step;++i) {
                for(int band=0;band<2;++band) {
                    const size_t lag=lags[band];
                    if(i<lag || i+lag>=count)continue;
                    const double d=x[i]-.5*(x[i-lag]+x[i+lag]);power[band]+=d*d;
                }
            }
            for(int band=0;band<2;++band)bands[band][f]=static_cast<float>(std::sqrt(power[band]/step));
        }
        std::vector<float> scratch;scratch.reserve(42);
        auto side75=[&](const std::vector<float>& values,size_t f,bool right) {
            scratch.clear();
            for(size_t k=9;k<=50;++k)scratch.push_back(values[right?f+k:f-k]);
            const size_t q=scratch.size()*3/4;
            std::nth_element(scratch.begin(),scratch.begin()+q,scratch.end());return scratch[q];
        };
        auto baseline=[&](const std::vector<float>& values,size_t f) {
            const float left=side75(values,f,false),right=side75(values,f,true);
            return std::max(1e-6f,std::max(left,right));
        };
        const float enter=10.f-4.f*std::clamp(settings.clickSensitivity,0.f,1.f);
        for(size_t f=60;f+60<blocks;++f) {
            if((f&255)==0 && cancelled && cancelled())return {};
            const float bandPeak=bands[0][f];
            if(bandPeak<.0001f)continue;
            bool maximum=true;
            for(size_t k=f-10;k<=f+10;++k)if(bands[0][k]>bandPeak){maximum=false;break;}
            if(!maximum)continue;
            const float floor=baseline(bands[0],f),ratio=bandPeak/floor;
            if(ratio<enter || bands[1][f]/baseline(bands[1],f)<4)continue;
            const float edge=std::max(2*floor,bandPeak*.08f);
            size_t first=f,last=f+1;
            while(first>0 && bands[0][first-1]>edge)--first;
            while(last<blocks && bands[0][last]>edge)++last;
            if((last-first)*step<rate*.002 || (last-first)*step>rate*std::min(20,settings.maxClickMs)/1000.)continue;
            const size_t a=first*step,b=last*step;
            // The two-scale evidence also finds quiet broad-band impulses
            // which never crossed the one-sample detector's absolute/local
            // threshold. Keep these distinct from soft band-limited clicks;
            // both are review proposals, never automatic repair permission.
            double d1Power=0,signalPower=0;
            for(size_t i=std::max<size_t>(1,a);i<std::min(count-1,b);++i) {
                const double d=x[i]-.5*(x[i-1]+x[i+1]);
                d1Power+=d*d;signalPower+=double(x[i])*x[i];
            }
            if(signalPower<1e-15)continue;
            const bool bandLimited = d1Power < signalPower*.035;
            if (!bandLimited) {
                // A quiet release after a vowel can look isolated over 50 ms
                // (notably /k/). This complementary pass is for quiet gaps,
                // not for reopening the original detector's phonetic rejects.
                // A much stronger neighbouring syllable keeps that case out;
                // the original broadband detector still handles voiced clicks.
                const size_t window = std::max<size_t>(1, static_cast<size_t>(rate*.02/hop));
                const size_t reach = std::max<size_t>(window, static_cast<size_t>(rate*.12/hop));
                double surrounding = 0;
                const size_t center = (a+b)/2/hop;
                for (int side : {-1, 1}) {
                    for (size_t offset = window; offset + window <= reach; offset += window/2+1) {
                        if (side < 0 && center < offset+window) continue;
                        const size_t from = side < 0 ? center-offset-window : center+offset;
                        if (from+window > level.size()) continue;
                        double power = 0;
                        for (size_t j=from; j<from+window; ++j) power += double(level[j])*level[j];
                        surrounding = std::max(surrounding, power/window);
                    }
                }
                if (surrounding > signalPower/(b-a)*16) continue;
            }
            const size_t gap=static_cast<size_t>(rate*.001);
            if(std::any_of(out.begin(),out.end(),[&](const Detection& d){
                return static_cast<int64_t>(a)<=d.endSample+static_cast<int64_t>(gap) &&
                    static_cast<int64_t>(b)+static_cast<int64_t>(gap)>=d.startSample;
            }))continue;
            // Repeated glottal pulses are predictable at neighbouring periods.
            // Exclude the proposed gap from the correlation estimate.
            double recurrence=0;size_t period=0;
            for(size_t lag=std::max<size_t>(1,static_cast<size_t>(rate/400));lag<static_cast<size_t>(rate/70);lag+=4) {
                double xy=0,xx=0,yy=0;
                const size_t radius=static_cast<size_t>(rate*.008);
                for(size_t i=std::max(lag,a>radius?a-radius:0);i<std::min(count-lag,b+radius);i+=4) {
                    if(i>=a && i<b)continue;
                    const double y=.5*(x[i-lag]+x[i+lag]);xy+=x[i]*y;xx+=double(x[i])*x[i];yy+=y*y;
                }
                const double correlation=xy/std::sqrt(std::max(1e-24,xx*yy));
                if(correlation>recurrence){recurrence=correlation;period=lag;}
            }
            if(recurrence>.6 && a>=period && b+period<count) {
                double mismatch=0,power=0;
                for(size_t i=a;i<b;++i) {
                    const double delta=x[i]-.5*(x[i-period]+x[i+period]);
                    mismatch+=delta*delta;power+=double(x[i])*x[i];
                }
                if(mismatch<std::max(1e-15,power)*.5)continue;
            }
            Detection d;d.kind=DetectionKind::Click;d.startSample=a;d.endSample=b;
            d.peakScore=d.meanScore=d.dspScore=std::clamp(1.f-std::exp(-ratio/12.f),0.f,1.f);
            if(d.peakScore<settings.clickThreshold)continue;
            d.classification=bandLimited?"band_limited_click":"isolated_click_review";
            d.reason=bandLimited?
                "Schiocco a banda limitata: evidenza a due scale e contesto bilaterale; rivedere con A/B":
                "Impulso isolato: evidenza a due scale e contesto bilaterale; rivedere con A/B";
            out.push_back(std::move(d));
        }
    }
    std::sort(out.begin(),out.end(),[](const Detection& a,const Detection& b){return a.startSample<b.startSample;});
    return out;
}

} // namespace bc
