// SPDX-License-Identifier: MPL-2.0
#include "standalone/studio_engine.h"
#include "standalone/studio_session.h"
#include "standalone/respiro_onnx.h"
#include "core/learning/personal_profile.h"
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <algorithm>

using namespace bc;
using namespace bc::studio;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static EditPlan planFor(const AudioDocument& doc) {
    EditPlan p; p.sampleRate = doc.sampleRate(); p.channels = doc.channels();
    p.sourcePath = doc.sourcePath(); p.settings.crossfadeMs = 0;
    for (const auto& r : doc.regions()) p.events.push_back(eventFromRegion(r));
    return p;
}

int main() try {
    const auto root = std::filesystem::temp_directory_path() / ("bc_rc2_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    AudioBuffer source; source.sampleRate = 48000; source.channels = 2;
    source.samples.assign(48000 * 2, .2f);
    const auto input = (root / "synthetic.wav").u8string(); std::string error;
    check(writeWav(input, source, WavFormat::Float32, &error), "fixture WAV");
    AudioDocument doc; check(doc.load(input, &error), "load");
    const auto original = doc.audio().samples;
    EditResult out;
    for (int mode : {0, 3, 1, 2}) {
        const auto undo = doc.undoDepth();
        check(doc.applyManualRange(10000, 20000, mode, -12, 2) >= 0, "manual in empty region");
        check(doc.audio().samples == original && doc.undoDepth() == undo + 1, "original/single Undo");
        auto plan = planFor(doc);
        plan.settings.crossfadeMs = 5; // manual duration must remain exact with GUI defaults
        check(applyEditPlan(doc.audio(), plan, &out, &error), "render manual");
        const int64_t removed = mode == 1 ? 10000 : mode == 2 ? 5000 : 0;
        check(out.audio.frameCount() == 48000 - removed, "mode duration");
        check(out.map.mapOriginal(30000) == 30000 - removed && out.map.mapEdited(30000 - removed) == 30000, "TimeMap");
        if (mode == 0) check(std::abs(out.audio.samples[15000 * 2] - .2f * std::pow(10.f, -.6f)) < 1e-7, "attenuation samples");
        if (mode == 3) check(out.audio.samples[15000 * 2] == 0 && out.audio.samples[500 * 2] == .2f, "silence samples");
        const auto curve = doc.gainCurve(EditSettings{}, GainCurveMode::StudioSource);
        for (size_t frame = 0; frame < curve.size(); ++frame) {
            if (mode == 0 || mode == 3) {
                for (int channel = 0; channel < 2; ++channel)
                    check(std::abs(curve[frame] * original[frame * 2 + channel] - out.audio.samples[frame * 2 + channel]) < 1e-7,
                          "source gain overlay differs from rendered amplitude/fades");
            } else check(curve[frame] == 1, "time edit drawn as attenuation");
        }
        SessionData session; session.sourcePath = input; session.regions = doc.regions(); session.events = plan.events;
        session.detector = "fusion";
        session.crossfadeMs = 7;
        const auto path = (root / "manual.bcnstudio.json").u8string();
        check(saveSession(path, session, &error), "save manual");
        SessionData loaded; check(loadSession(path, &loaded, &error), "load manual");
        check(loaded.regions.size() == 1 && loaded.regions[0].manualOperation && loaded.detector == "fusion", "manual persistence");
        check(loaded.crossfadeMs == 7, "session crossfade persistence");
        plan.events = loaded.events;
        EditResult reloaded; check(applyEditPlan(doc.audio(), plan, &reloaded, &error), "render saved");
        check(reloaded.audio.samples == out.audio.samples, "preview/export samples identical");
        check(doc.undo() && doc.regions().empty() && doc.redo(), "manual Undo/Redo");
        check(doc.restoreManualRange(10000, 20000) == 1 && doc.regions().empty(), "restore manual");
        check(doc.undo() && doc.regions().size() == 1, "restore Undo");
        check(doc.redo() && doc.regions().empty(), "restore Redo");
    }
    AudioDocument replacement; check(replacement.load(input, &error), "load replacement fixture");
    const int manual = replacement.applyManualRange(10000, 20000, 3, -12, 15);
    for (int mode : {0, 1, 2, 3}) {
        const int priorMode = replacement.regions()[manual].editMode;
        const auto undo = replacement.undoDepth();
        check(replacement.applyManualRange(10000, 20000, mode, -12, 15) == manual &&
              replacement.regions().size() == 1 && replacement.undoDepth() == undo + 1, "replace manual treatment/single Undo");
        check(replacement.undo() && replacement.regions()[manual].editMode == priorMode &&
              replacement.redo() && replacement.regions()[manual].editMode == mode, "replace Undo/Redo");
        check(applyEditPlan(replacement.audio(), planFor(replacement), &out, &error), "replaced mode conflicts with previous mode");
        check(replacement.applyManualRange(10000, 20000, mode, -12, 15) == manual &&
              replacement.undoDepth() == undo + 1, "identical treatment adds redundant Undo");
    }
    EditRegion verified; verified.startSample = 10000; verified.endSample = 20000;
    verified.learningLabel = "false_positive"; verified.trainingEligible = true;
    verified.decisionSource = "user";
    replacement.addRegion(verified);
    replacement.applyManualRange(19000, 22000, 0, -6, 0);
    replacement.applyManualRange(10000, 20000, 0, -18, 5);
    check(replacement.regions().size() == 3 && replacement.regions()[1].learningLabel == "false_positive" &&
          replacement.regions()[2].startSample == 19000, "replacement changed another overlap or verified label");
    auto ignored = replacement.regions()[manual]; ignored.approved = false;
    replacement.updateRegion(manual, ignored);
    const auto ignoredCurve = replacement.gainCurve(EditSettings{}, GainCurveMode::StudioSource);
    check(ignoredCurve[15000] == 1 && ignoredCurve[20500] < 1, "overlay includes unapproved event or drops independent overlap");
    // Degenerate intervals, zero fade and fades longer than the interval.
    for (int64_t length : {1, 2, 3, 20}) for (double fade : {0., 200.}) {
        AudioDocument shortRange; check(shortRange.load(input, &error), "short range");
        shortRange.applyManualRange(10000, 10000 + length, 3, -12, fade);
        check(applyEditPlan(shortRange.audio(), planFor(shortRange), &out, &error), "short fade render");
        const auto curve = shortRange.gainCurve(EditSettings{}, GainCurveMode::StudioSource);
        for (size_t frame = 9999; frame <= 10000 + static_cast<size_t>(length); ++frame)
            check(std::abs(out.audio.samples[frame * 2] - original[frame * 2] * curve[frame]) < 1e-7, "short fade overlay differs");
    }
    // Overlap attenuations: strongest reduction once, never a product.
    Event a; a.enabled = a.approved = true; a.startSample = 10000; a.endSample = 20000;
    a.reductionDb = -12.f; a.fadeMs = 0;
    EditPlan p = planFor(doc); p.events = {a,a};
    check(applyEditPlan(doc.audio(), p, &out, &error), "overlap");
    check(std::abs(out.audio.samples[15000 * 2] - .2f * std::pow(10.f, -.6f)) < 1e-7, "duplicate attenuation");
    a.enabled = false; p.events = {a};
    check(applyEditPlan(doc.audio(), p, &out, &error) && out.audio.samples == original && out.appliedEvents == 0, "excluded unchanged");
    a.kind = DetectionKind::Click; a.repair = true; a.enabled = true; a.endSample = a.startSample + 120;
    p.events = {a}; EditResult single; check(applyEditPlan(doc.audio(), p, &single, &error), "single click");
    p.events = {a,a}; check(applyEditPlan(doc.audio(), p, &out, &error) && out.audio.samples == single.audio.samples, "duplicate click repair");
    // Negative truth stays negative even if editorially included.
    EditRegion negative; negative.startSample = 21000; negative.endSample = 23000;
    negative.fromDetection = true; negative.enabled = negative.approved = false;
    negative.learningLabel = "false_positive"; negative.trainingEligible = true; negative.decisionSource = "user";
    int index = doc.addRegion(negative); negative.enabled = negative.approved = true; doc.updateRegion(index, negative);
    auto examples = collectLearningExamples(doc);
    check(examples.size() == 1 && examples[0].label == LearningLabel::FalsePositive, "editorial decision changed negative label");
    Event e = eventFromRegion(negative); check(e.state == "rejected", "negative converted to confirmed");
    e.classification = "shared"; e.aiScore = .8f; e.aiMaxProbability = .9f; e.dspScore = .3f;
    auto d = detectionFromEvent(e); AudioDocument fresh; check(fresh.load(input, &error), "fresh");
    fresh.reconcileRegions({d}, EditSettings{});
    fresh.setAnalysisProvenance({"fusion", 100});
    const auto exportUndo = fresh.undoDepth();
    fresh.markRegionApplied(0, true);
    check(fresh.undoDepth() == exportUndo && fresh.regions()[0].decisionSource == "automatic", "export altered review provenance/history");
    check(eventFromRegion(fresh.regions()[0]).aiScore == .8f && fresh.regions()[0].classification == "shared", "Fusion bridge");
    SessionData session; session.sourcePath = input; session.regions = fresh.regions(); session.events = {eventFromRegion(fresh.regions()[0])};
    session.analysis = fresh.analysisProvenance();
    const auto sessionPath = (root / "fusion.bcnstudio.json").u8string();
    check(saveSession(sessionPath, session, &error) && loadSession(sessionPath, &session, &error), "Fusion session");
    check(session.regions[0].aiMaxProbability == .9f && session.events[0].aiScore == .8f, "Fusion serialisation");
    check(session.analysis.detector == "fusion" && session.analysis.aiFrames == 100, "effective detector persistence");
    p.events = session.events;
    p.analysis = session.analysis;
    const auto planPath = (root / "fusion.plan.json").u8string();
    check(writeEditPlanJson(planPath, p, &error) && loadEditPlanJson(planPath, &p, &error), "Fusion plan");
    check(p.events[0].aiScore == .8f && p.events[0].classification == "shared", "Fusion plan lost score");
    check(p.analysis.aiFrames == 100, "effective detector plan");
    check(fresh.undo() && fresh.analysisProvenance().detector.empty() && fresh.redo() && fresh.analysisProvenance().aiFrames == 100, "analysis provenance Undo/Redo");
    check(doc.audio().samples == original, "source buffer changed");
    // Coextensive Silence and reconstruction have different observed outcomes,
    // even when both receive the same overlapping de-click diagnostics.
    EditPlan observed; observed.sampleRate=source.sampleRate; observed.channels=source.channels;
    Event click; click.kind=DetectionKind::Click; click.repair=true; click.repairMethod="linear";
    click.startSample=10000; click.endSample=10041; click.approved=true; click.fadeMs=0;
    Event silenced=click; silenced.mode=EditMode::Silence;
    observed.events={silenced,click};
    check(applyEditPlan(source,observed,&out,&error), "overlapping click outcomes");
    check(std::string(eventOutcome(silenced,&out))=="applied" &&
          std::string(eventOutcome(click,&out))=="repaired", "Silence falsely reported as click reconstruction");
    auto signature=renderPlanSignature(observed);
    observed.events[1].repairFeedback="unsatisfactory";
    check(renderPlanSignature(observed)==signature,"quality feedback changed audio signature");
    observed.events[1].endSample++;
    check(renderPlanSignature(observed)!=signature,"unnotified bounds reused render signature");
    const auto pcmRevision=doc.audioRevision();
    doc.setMasterGainDb(-3);
    check(doc.audioRevision()==pcmRevision,"envelope change copied source PCM unnecessarily");
    check(doc.applyGainToRange(0,20,-3) && doc.audioRevision()!=pcmRevision,"silent PCM edit failed revision tracking");
    const auto gainRevision=doc.audioRevision();
    check(doc.undo() && doc.audioRevision()!=gainRevision,"PCM Undo failed revision tracking");
    std::vector<float> probabilities(120,0);
    std::fill(probabilities.begin(),probabilities.begin()+20,.99f); // incomplete leading context
    std::fill(probabilities.begin()+23,probabilities.begin()+43,.99f);
    std::fill(probabilities.begin()+44,probabilities.begin()+65,.99f); // existing long output stays separate
    std::fill(probabilities.begin()+66,probabilities.begin()+73,.9f); // too short
    std::fill(probabilities.begin()+74,probabilities.begin()+93,.1f); // weak evidence
    std::fill(probabilities.begin()+100,probabilities.end(),.99f); // incomplete trailing context
    const auto shortAi=shortBreathReviewProposals(probabilities,.064f);
    check(shortAi.size()==1 && shortAi[0].startFrame==23 && shortAi[0].endFrame==43,"short AI proposal boundaries/confidence/context");
    Detection shortBreath;shortBreath.kind=DetectionKind::Breath;shortBreath.classification="ai_short_review";
    shortBreath.startSample=1000;shortBreath.endSample=10600;shortBreath.peakScore=.99f;
    fresh.reconcileRegions({shortBreath},EditSettings{});
    bool reviewFound=false;
    for (const auto& r:fresh.regions()) if(r.classification=="ai_short_review") reviewFound=!r.approved && !r.trainingEligible;
    check(reviewFound,"new short breath was approved automatically");
    AudioDocument reconsider;check(reconsider.load(input,&error),"reconsider fixture");
    Detection initiallyAutomatic=shortBreath;initiallyAutomatic.classification="ai";
    reconsider.reconcileRegions({initiallyAutomatic},EditSettings{});
    check(reconsider.regions()[0].approved,"ordinary automatic proposal fixture");
    reconsider.reconcileRegions({shortBreath},EditSettings{});
    check(!reconsider.regions()[0].approved,"automatic approval survived reclassification to review-only");
    bc::AnalysisResult fragments;fragments.sampleRate=48000;fragments.totalSamples=96000;fragments.hopSize=256;
    fragments.breathScores.assign(375,0);fragments.frameLevelDb.assign(375,-50);
    std::fill(fragments.breathScores.begin()+100,fragments.breathScores.begin()+112,.3f);
    std::fill(fragments.breathScores.begin()+113,fragments.breathScores.begin()+125,.3f);
    std::vector<float> corroboration(200,0);std::fill(corroboration.begin()+52,corroboration.begin()+70,.3f);
    auto joined=fragmentedBreathReviewProposals(fragments,corroboration,.064f);
    check(joined.size()==1 && !joined[0].approved && !joined[0].trainingEligible &&
        joined[0].classification=="dsp_fragment_review","corroborated fragmented breath must remain review-only");
    check(fragmentedBreathReviewProposals(fragments,std::vector<float>(200,0),.064f).empty(),"fragment grouping without AI evidence");
    fragments.frameLevelDb.assign(375,-20);std::fill(fragments.frameLevelDb.begin()+100,fragments.frameLevelDb.begin()+125,-50);
    check(fragmentedBreathReviewProposals(fragments,corroboration,.064f).empty(),"fragment grouping bypassed consonant/pause guard");
    EditPlan bounded;bounded.sampleRate=source.sampleRate;bounded.channels=source.channels;
    bounded.settings.fadeMs=15;bounded.settings.crossfadeMs=5;
    Event cut;cut.startSample=10000;cut.endSample=20000;cut.approved=true;cut.mode=EditMode::Cut;
    for (bool manualJoin:{false,true}) for (double margin:{0.,10.}) {
        cut.manualOperation=manualJoin;cut.safetyMarginMs=margin;bounded.events={cut};
        check(applyEditPlan(source,bounded,&out,&error),"bounded join render");
        for (int64_t i=0;i<source.frameCount();++i) if(i<cut.startSample || i>=cut.endSample) {
            bool removed=false;auto mapped=out.map.mapOriginal(i,&removed);
            check(!removed,"crossfade removed an unapproved source sample");
            for(int c=0;c<source.channels;++c)
                check(out.audio.samples[mapped*source.channels+c]==source.samples[i*source.channels+c],"join modified PCM outside approved region");
        }
        if(margin==0) check(out.removedSamples==10000 && out.crossfadeFrames==0 && out.operations[0].fadeFrames==0,"exact cut borrowed unapproved handles");
    }
    std::cout << "RC2 AUDIO OK: manual modes/replacement, sample-exact gain overlay, silence, TimeMap, restore, Undo/Redo, exclusions, overlap, click, labels, Fusion bridge\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "RC2 FAILED: " << error.what() << "\n"; return 1; }
