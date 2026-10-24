// SPDX-License-Identifier: MPL-2.0
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <limits>

#include "core/edit/audio_document.h"
#include "core/learning/personal_profile.h"

namespace {

int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main() {
    bc::AudioBuffer audio;
    audio.channels = 1;
    audio.sampleRate = 48000;
    const int frames = 48000 * 10;
    audio.samples.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        audio.samples[static_cast<size_t>(i)] =
            0.1f * std::sin(2.0 * 3.14159265358979323846 * 220.0 * i / 48000.0);
    }
    bc::AudioDocument document;
    if (!document.adopt(audio, "synthetic.wav")) return fail("adopt");

    bc::EditRegion manual;
    manual.startSample = 48000;
    manual.endSample = 96000;
    manual.fromDetection = false;
    manual.approved = true;
    manual.enabled = true;
    manual.learningLabel = "breath_confirmed";
    document.addRegion(manual);

    bc::EditRegion excluded;
    excluded.startSample = 144000;
    excluded.endSample = 192000;
    excluded.fromDetection = true;
    excluded.approved = false;
    excluded.enabled = false;
    excluded.decisionSource = "user";
    document.addRegion(excluded);

    bc::EditRegion automatic;
    automatic.startSample = 240000;
    automatic.endSample = 288000;
    automatic.fromDetection = true;
    automatic.approved = true;
    automatic.enabled = true;
    automatic.decisionSource = "automatic";
    document.addRegion(automatic);

    std::vector<bc::Detection> detections;
    auto addDetection = [&](int64_t start, int64_t end) {
        bc::Detection detection;
        detection.kind = bc::DetectionKind::Breath;
        detection.startSample = start;
        detection.endSample = end;
        detection.peakScore = 0.8f;
        detections.push_back(detection);
    };
    addDetection(49000, 95000);    // manual: must be preserved
    addDetection(145000, 191000);  // excluded: must be preserved
    addDetection(7 * 48000, 8 * 48000);  // new
    bc::EditSettings settings;
    const auto reconcile = document.reconcileRegions(detections, settings);
    if (reconcile.kept != 2 || reconcile.added != 1 || reconcile.removed != 1) {
        std::fprintf(stderr, "kept=%d added=%d removed=%d ambiguous=%d\n",
                     reconcile.kept, reconcile.added, reconcile.removed, reconcile.ambiguous);
        return fail("reconcile counts");
    }
    if (document.regions().size() != 3) return fail("reconcile size");
    if (document.regions()[0].startSample != 48000) return fail("manual preserved");
    if (document.regions()[1].enabled || document.regions()[1].approved) {
        return fail("exclusion preserved");
    }
    if (!document.undo()) return fail("reconcile undo");
    if (document.regions().size() != 3) return fail("reconcile undo size");
    if (document.regions()[2].startSample != 240000) return fail("reconcile undo content");

    std::vector<bc::LearningExample> examples;
    for (int i = 0; i < 8; ++i) {
        bc::LearningExample positive;
        positive.label = i % 3 == 0 ? bc::LearningLabel::MissedBreath
                                    : bc::LearningLabel::BreathConfirmed;
        positive.sourcePath = (i % 2 == 0) ? "A.wav" : "B.wav";
        positive.baselineAccepted = positive.label != bc::LearningLabel::MissedBreath;
        positive.features.modelScore = 0.85f + 0.03f * i;
        positive.features.durationMs = 220.0f;
        positive.features.rmsDb = -30.0f;
        positive.features.zeroCrossRate = 0.05f;
        positive.features.meanAbs = 0.03f;
        examples.push_back(positive);

        bc::LearningExample negative;
        negative.label = bc::LearningLabel::FalsePositive;
        negative.sourcePath = (i % 2 == 0) ? "A.wav" : "B.wav";
        negative.features.modelScore = 0.12f + 0.02f * i;
        negative.features.durationMs = 650.0f;
        negative.features.rmsDb = -58.0f;
        negative.features.zeroCrossRate = 0.30f;
        negative.features.meanAbs = 0.004f;
        examples.push_back(negative);
    }
    bc::PersonalProfile profile;
    std::string error;
    if (!bc::trainPersonalProfile(examples, profile, &error)) {
        std::fprintf(stderr, "profile: %s\n", error.c_str());
        return fail("train profile");
    }
    if (!profile.valid || profile.positives < 6 || profile.negatives < 6) {
        return fail("profile metadata");
    }
    if (!profile.accepts(examples.front().features)) return fail("profile positive");
    if (profile.accepts(examples.back().features)) return fail("profile negative");

    // Holdout feature changes must never fit weights or select the threshold.
    auto perturbed = examples;
    for (auto& example : perturbed) {
        if (example.sourcePath == "A.wav") example.features.rmsDb += 1.0f;
    }
    bc::PersonalProfile independent;
    if (!bc::trainPersonalProfile(perturbed, independent, &error) ||
        independent.threshold != profile.threshold || independent.bias != profile.bias ||
        independent.weightRms != profile.weightRms || independent.weightScore != profile.weightScore)
        return fail("holdout used for fitting or threshold selection");
    if (profile.validationMisses != 2 || profile.validationFalsePositives != 0 ||
        profile.baselineFalsePositives != 4 || profile.baselineMisses != 2)
        return fail("separate miss/false-positive metrics");
    auto copies = examples;
    for (auto& example : copies) example.recordingId = "same-decoded-recording";
    if (bc::trainPersonalProfile(copies, independent, &error)) return fail("copies leaked across split");
    auto oneFile = examples;
    for (auto& example : oneFile) example.sourcePath = "same.wav";
    if (bc::trainPersonalProfile(oneFile, independent, &error)) return fail("single recording accepted");
    if (bc::trainPersonalProfile({}, independent, &error)) return fail("empty training accepted");
    auto duplicated = examples;
    for (size_t i = 0; i < duplicated.size(); ++i) {
        duplicated[i].startFrame = int64_t(i) * 1000;
        duplicated[i].endFrame = duplicated[i].startFrame + 500;
    }
    const auto originals = duplicated;
    duplicated.insert(duplicated.end(), originals.begin(), originals.end());
    if (!bc::trainPersonalProfile(duplicated, independent, &error) ||
        independent.positives != 8 || independent.negatives != 8)
        return fail("duplicate examples counted twice");
    duplicated.back().label = bc::LearningLabel::BreathConfirmed;
    if (bc::trainPersonalProfile(duplicated, independent, &error)) return fail("contradictory duplicate accepted");
    duplicated.back().startFrame += 1;
    if (bc::trainPersonalProfile(duplicated, independent, &error)) return fail("overlapping opposite labels accepted");

    const auto directory = std::filesystem::temp_directory_path() / "bc_learning_tests";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const std::string path = (directory / "profile.ini").u8string();
    if (!bc::savePersonalProfile(path, profile, &error)) return fail("save profile");
    bc::PersonalProfile loaded;
    if (!bc::loadPersonalProfile(path, loaded, &error)) return fail("load profile");
    if (!loaded.valid || !near(loaded.threshold, profile.threshold, 1e-4)) {
        return fail("profile roundtrip");
    }

    // Etichette esplicite: solo confermati e falsi positivi entrano negli
    // esempi; l'esclusione editoriale e il non verificato restano fuori.
    bc::AudioDocument labelled;
    if (!labelled.adopt(audio, "labelled.wav")) return fail("labelled adopt");
    auto addLabelled = [&](const char* label) {
        bc::EditRegion region;
        region.startSample = 1000;
        region.endSample = 2000;
        region.learningLabel = label;
        region.trainingEligible = true;
        region.decisionSource = "user";
        region.fromDetection = false;
        labelled.addRegion(region);
    };
    addLabelled("breath_confirmed");
    addLabelled("false_positive");
    addLabelled("editorial_exclusion");
    addLabelled("");
    const auto labelledExamples = bc::collectLearningExamples(labelled);
    if (labelledExamples.size() != 2) return fail("explicit labels filtering");
    int positiveLabels = 0;
    int negativeLabels = 0;
    for (const auto& example : labelledExamples) {
        if (example.label == bc::LearningLabel::BreathConfirmed) ++positiveLabels;
        if (example.label == bc::LearningLabel::FalsePositive) ++negativeLabels;
    }
    if (positiveLabels != 1 || negativeLabels != 1) return fail("explicit labels mapping");
    if (labelledExamples[0].recordingId.empty()) return fail("missing recording fingerprint");
    bc::AudioDocument renamed;
    if (!renamed.adopt(audio, "renamed.wav")) return fail("renamed adopt");
    auto human = labelled.regions().front();
    renamed.addRegion(human);
    if (bc::collectLearningExamples(renamed).front().recordingId != labelledExamples[0].recordingId)
        return fail("recording copy identity");
    human.trainingEligible = false;
    renamed.addRegion(human);
    human.trainingEligible = true;
    human.decisionSource = "review_harness";
    renamed.addRegion(human);
    if (bc::collectLearningExamples(renamed).size() != 1) return fail("ineligible or harness example collected");
    const auto exported = (directory / "corrections.json").u8string();
    if (!renamed.exportCorrections(exported, &error)) return fail("verified examples export");
    std::ifstream exportedFile(exported);
    std::ostringstream exportedText; exportedText << exportedFile.rdbuf();
    const auto json = exportedText.str();
    const auto firstExample = json.find("\"training_eligible\": true");
    if (firstExample == std::string::npos || json.find("\"training_eligible\": true", firstExample + 1) != std::string::npos)
        return fail("editorial/harness examples leaked into export");

    { std::ofstream legacy(path); legacy << "schema=1\nvalid=1\n"; }
    if (bc::loadPersonalProfile(path, loaded, &error)) return fail("legacy leakage profile accepted");
    if (std::string(bc::learningLabelName(bc::LearningLabel::EditorialExclusion)) !=
        "editorial_exclusion") {
        return fail("editorial label name");
    }

    std::filesystem::remove_all(directory, ec);
    std::printf("LEARNING TESTS OK: riconciliazione, undo, profilo e rollback\n");
    return 0;
}
