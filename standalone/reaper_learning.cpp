// SPDX-License-Identifier: MPL-2.0
// Thin CLI adapter: labels, features, recording identity and validation belong
// to the very same Core calibrator used by the standalone editor.
#include "reaper_slice.h"
#include "studio_engine.h"
#include "studio_session.h"
#include "core/learning/personal_profile.h"
#include "core/util/atomic_file.h"
#include <iostream>
#include <filesystem>

namespace {
bool documentForPlan(const bc::studio::EditPlan& plan, bc::AudioDocument& doc, std::string& error) {
    std::string hash;
    if (plan.sourceSha256.empty()) { error = "piano etichette privo di hash sorgente"; return false; }
    if (!bc::studio::sha256File(plan.sourcePath, hash, &error)) { error += ": " + plan.sourcePath; return false; }
    if (hash != plan.sourceSha256) { error = "sorgente delle etichette cambiata"; return false; }
    if (!doc.load(plan.sourcePath, &error)) return false;
    if (!bc::studio::sha256File(plan.sourcePath, hash, &error) || hash != plan.sourceSha256) {
        error = "registrazione cambiata durante la lettura delle etichette"; return false;
    }
    for (const auto& e : plan.events) {
        if (!e.trainingEligible || e.decisionSource != "user" || e.manualOperation) continue;
        if (e.startSample < 0 || e.endSample <= e.startSample || e.endSample > doc.audio().frameCount()) {
            error = "etichetta fuori dalla registrazione originale"; return false;
        }
        bc::EditRegion r;
        r.kind = e.kind; r.startSample = e.startSample; r.endSample = e.endSample;
        r.score = e.score; r.learningLabel = e.learningLabel;
        r.trainingEligible = true; r.decisionSource = "user";
        r.enabled = e.enabled; r.approved = e.approved;
        r.fromDetection = e.learningLabel != "missed_breath" && e.learningLabel != "missed_click" && e.source != "manual";
        r.manualOperation = false;
        doc.addRegion(r);
    }
    return true;
}
}

int commandLearning(int argc, char** argv) {
    const std::string command = argv[1];
    std::string output, profile, error;
    bc::DetectionKind kind=bc::DetectionKind::Breath;
    bool kindSpecified=false;
    std::vector<std::string> plans;
    for (int i = 2; i < argc; ++i) {
        std::string flag = argv[i];
        if (++i >= argc) return 2;
        if (flag == "--out") output = argv[i];
        else if (flag == "--profile") profile = argv[i];
        else if (flag == "--plan") plans.push_back(argv[i]);
        else if (flag == "--kind") {
            kindSpecified=true;
            if(std::string(argv[i])=="click")kind=bc::DetectionKind::Click;
            else if(std::string(argv[i])!="breath")return 2;
        }
        else return 2;
    }
    if (output.empty() || plans.empty()) return 2;
    std::vector<bc::LearningExample> examples;
    for (const auto& path : plans) {
        bc::studio::EditPlan plan;
        if (!bc::studio::loadEditPlanJson(path, &plan, &error)) break;
        if (bc::sameFilePath(output, plan.sourcePath) || bc::sameFilePath(output, path) ||
            (!profile.empty() && bc::sameFilePath(output, profile))) {
            error = "la destinazione non puo' sovrascrivere sorgente, piano o profilo in uso"; break;
        }
        if (command == "profile-filter") {
            bc::PersonalProfile personal;
            bc::AudioDocument doc;
            if (plans.size() != 1 || !bc::loadPersonalProfile(profile, personal, &error) ||
                !documentForPlan(plan,doc,error)) { if (error.empty()) error="profilo non valido"; break; }
            if(kindSpecified && personal.kind!=kind){error="categoria del profilo diversa da quella richiesta";break;}
            for (auto& e : plan.events) {
                if (e.kind != personal.kind || e.decisionSource == "user") continue;
                if (!personal.accepts(bc::extractLearningFeatures(doc.audio(), e.startSample, e.endSample, e.score))) {
                    e.enabled = e.approved = false; e.reason = "personal_profile";
                }
            }
            if (!bc::studio::writeEditPlanJson(output, plan, &error)) break;
        } else {
            bc::AudioDocument doc;
            if (!documentForPlan(plan, doc, error)) break;
            const auto more = bc::collectLearningExamples(doc,kind);
            examples.insert(examples.end(), more.begin(), more.end());
            if (command == "labels-export") {
                if (plans.size() != 1) return 2;
                bc::studio::SessionData session;
                session.sourcePath = doc.sourcePath(); session.sourceSha256 = plan.sourceSha256;
                session.regions = doc.regions(); session.events = plan.events;
                if (!bc::studio::saveSession(output, session, &error)) break;
            }
        }
    }
    if (error.empty() && command == "profile-train") {
        bc::PersonalProfile trained;
        if (bc::trainPersonalProfile(examples, trained, &error,kind))
            bc::savePersonalProfile(output, trained, &error);
    }
    if (!error.empty()) { std::cerr << error << '\n'; return 1; }
    std::cout << "LEARNING OK: " << examples.size() << " esempi espliciti; calibratore Core, nessun fine-tuning ONNX\n";
    return 0;
}
