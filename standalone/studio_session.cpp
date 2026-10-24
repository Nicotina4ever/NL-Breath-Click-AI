// SPDX-License-Identifier: MPL-2.0
#include "studio_session.h"
#include "core/detect/detector_settings_codec.h"
#include "core/util/atomic_file.h"
#include "core/util/json_validation.h"
#include <iomanip>

#include <cctype>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bc {
namespace studio {
namespace {

std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default: out += c; break;
        }
    }
    return out;
}

const char* skipSpace(const char* p) {
    while (p && *p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    return p;
}

const char* findKey(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return nullptr;
    return std::strchr(json.c_str() + pos + needle.size(), ':');
}

bool readString(const std::string& json, const std::string& key, std::string& out) {
    const char* p = findKey(json, key);
    if (!p) return false;
    p = skipSpace(p + 1);
    size_t position = static_cast<size_t>(p - json.c_str());
    return decodeJsonString(json, position, out);
}

bool readNumber(const std::string& json, const std::string& key, double& out) {
    const char* p = findKey(json, key);
    if (!p) return false;
    p = skipSpace(p + 1);
    char* end = nullptr;
    out = std::strtod(p, &end);
    const double limit = (key == "kind" || key == "edit_mode" || key == "fade_in_shape" || key == "fade_out_shape")
        ? double(std::numeric_limits<int>::max()) : 1.0e12;
    return end != p && std::isfinite(out) && std::abs(out) <= limit;
}

bool readBool(const std::string& json, const std::string& key, bool& out) {
    const char* p = findKey(json, key);
    if (!p) return false;
    p = skipSpace(p + 1);
    if (std::strncmp(p, "true", 4) == 0) { out = true; return true; }
    if (std::strncmp(p, "false", 5) == 0) { out = false; return true; }
    return false;
}

std::vector<std::string> objectsInArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    const char* p = findKey(json, key);
    if (!p) return result;
    p = skipSpace(p + 1);
    if (*p != '[') return result;
    ++p;
    int depth = 0;
    std::string current;
    bool inString = false;
    for (; *p; ++p) {
        const char c = *p;
        if (inString) {
            current += c;
            if (c == '\\' && p[1]) current += *++p;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
            current += c;
        } else if (c == '{') {
            if (depth == 0) current.clear();
            ++depth;
            current += c;
        } else if (c == '}') {
            --depth;
            current += c;
            if (depth == 0) result.push_back(current);
        } else if (depth > 0) {
            current += c;
        } else if (c == ']') {
            break;
        }
    }
    return result;
}

} // namespace

bool saveSession(const std::string& path, const SessionData& session,
                 std::string* error) {
    if (sameFilePath(path, session.sourcePath)) {
        if (error) *error = "la sessione non puo' sovrascrivere l'audio sorgente";
        return false;
    }
    std::ostringstream out;
    out << std::setprecision(17);
    std::string sourceRelative;
    std::string sourceSha;
    std::error_code relativeError;
    const std::filesystem::path sourceFs = std::filesystem::u8path(session.sourcePath);
    const std::filesystem::path sessionFs = std::filesystem::u8path(path);
    if (std::filesystem::exists(nativeFilePath(sourceFs), relativeError)) {
        sourceRelative = sourceFs.lexically_relative(sessionFs.parent_path()).u8string();
        if (sourceRelative == "." ) sourceRelative.clear();
        std::string hashError;
        sha256File(session.sourcePath, sourceSha, &hashError);
        if (!hashError.empty()) sourceSha.clear();
    }
    out << "{\n  \"schema\": 2,\n";
    if (session.detectorSettings) out << "  \"dsp_settings\": \""
        << escape(serializeDetectorSettings(*session.detectorSettings)) << "\",\n";
    out
        << "  \"source\": \"" << escape(session.sourcePath) << "\",\n"
        << "  \"source_relative\": \"" << escape(sourceRelative) << "\",\n"
        << "  \"source_sha256\": \"" << escape(sourceSha) << "\",\n"
        << "  \"fade_in_seconds\": " << session.fade.inSeconds << ",\n"
        << "  \"fade_out_seconds\": " << session.fade.outSeconds << ",\n"
        << "  \"fade_in_shape\": " << static_cast<int>(session.fade.inShape) << ",\n"
        << "  \"fade_out_shape\": " << static_cast<int>(session.fade.outShape) << ",\n"
        << "  \"fade_in_enabled\": " << (session.fade.inEnabled ? "true" : "false") << ",\n"
        << "  \"fade_out_enabled\": " << (session.fade.outEnabled ? "true" : "false") << ",\n"
        << "  \"reduction_db\": " << session.reductionDb << ",\n"
        << "  \"fade_ms\": " << session.fadeMs << ",\n"
        << "  \"crossfade_ms\": " << session.crossfadeMs << ",\n"
        << "  \"detector\": \"" << escape(session.detector) << "\",\n"
        << "  \"effective_detector\": \"" << escape(session.analysis.detector) << "\",\n"
        << "  \"ai_inference_frames\": " << session.analysis.aiFrames << ",\n"
        << "  \"ai_threshold\": " << session.aiThreshold << ",\n"
        << "  \"master_gain_db\": " << session.masterGainDb << ",\n"
        << "  \"regions\": [\n";
    for (size_t i = 0; i < session.regions.size(); ++i) {
        const EditRegion& r = session.regions[i];
        out << "    {\"kind\": " << static_cast<int>(r.kind)
            << ", \"start\": " << r.startSample
            << ", \"end\": " << r.endSample
            << ", \"reduction_db\": " << r.reductionDb
            << ", \"enabled\": " << (r.enabled ? "true" : "false")
            << ", \"repair\": " << (r.repair ? "true" : "false")
            << ", \"score\": " << r.score
            << ", \"from_detection\": " << (r.fromDetection ? "true" : "false")
            << ", \"approved\": " << (r.approved ? "true" : "false")
            << ", \"edit_mode\": " << r.editMode
            << ", \"keep_fraction\": " << r.keepFraction
            << ", \"fade_ms\": " << r.fadeMs
            << ", \"safety_margin_ms\": " << r.safetyMarginMs
            << ", \"applied\": " << (r.applied ? "true" : "false")
            << ", \"decision_source\": \"" << escape(r.decisionSource) << "\""
            << ", \"classification\": \"" << escape(r.classification) << "\""
            << ", \"training_eligible\": " << (r.trainingEligible ? "true" : "false")
            << ", \"manual_operation\": " << (r.manualOperation ? "true" : "false")
            << ", \"reason\": \"" << escape(r.reason) << "\""
            << ", \"dsp_score\": " << r.dspScore
            << ", \"ai_score\": " << r.aiScore
            << ", \"ai_max_probability\": " << r.aiMaxProbability
            << ", \"learning_label\": \"" << escape(r.learningLabel) << "\"}";
        if (i + 1 < session.regions.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n  \"corrections\": [\n";
    for (size_t i = 0; i < session.corrections.size(); ++i) {
        const Correction& c = session.corrections[i];
        out << "    {\"kind\": " << static_cast<int>(c.kind)
            << ", \"start\": " << c.startSample
            << ", \"end\": " << c.endSample
            << ", \"positive\": " << (c.positive ? "true" : "false")
            << ", \"source\": \"" << escape(c.source) << "\"}";
        if (i + 1 < session.corrections.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n  \"events\": [\n";
    for (size_t i = 0; i < session.events.size(); ++i) {
        const Event& event = session.events[i];
        out << "    {\"kind\": " << static_cast<int>(event.kind);
        if (event.reductionDb) out << ", \"event_reduction_db\": " << *event.reductionDb;
        out << ", \"repair\": " << (event.repair ? "true" : "false")
            << ", \"start\": " << event.startSample
            << ", \"end\": " << event.endSample
            << ", \"score\": " << event.score
            << ", \"enabled\": " << (event.enabled ? "true" : "false")
            << ", \"approved\": " << (event.approved ? "true" : "false")
            << ", \"state\": \"" << escape(event.state) << "\""
            << ", \"mode\": \"" << editModeName(event.mode) << "\""
            << ", \"keep_fraction\": " << event.keepFraction
            << ", \"fade_ms\": " << event.fadeMs
            << ", \"safety_margin_ms\": " << event.safetyMarginMs
            << ", \"classification\": \"" << escape(event.classification) << "\""
            << ", \"decision_source\": \"" << escape(event.decisionSource) << "\""
            << ", \"training_eligible\": " << (event.trainingEligible ? "true" : "false")
            << ", \"learning_label\": \"" << escape(event.learningLabel) << "\""
            << ", \"manual_operation\": " << (event.manualOperation ? "true" : "false")
            << ", \"source\": \"" << escape(event.source) << "\""
            << ", \"reason\": \"" << escape(event.reason) << "\""
            << ", \"dsp_score\": " << event.dspScore
            << ", \"ai_score\": " << event.aiScore
            << ", \"ai_max_probability\": " << event.aiMaxProbability << "}";
        if (i + 1 < session.events.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    if (!validJsonDocument(out.str())) {
        if (error) *error = "dati di salvataggio non validi";
        return false;
    }
    return writeTextAtomic(path, out.str(), error);
}

bool loadSession(const std::string& path, SessionData* output,
                 std::string* error) {
    if (!output) return false;
    SessionData pending;
    auto* session = &pending;
    std::ifstream in(nativeFilePath(std::filesystem::u8path(path)), std::ios::binary);
    if (!in) {
        if (error) *error = "sessione non apribile";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    if (!validJsonDocument(json)) {
        if (error) *error = "sessione JSON incompleta o non valida";
        return false;
    }
    double schema = 0;
    if (!readNumber(json, "schema", schema) || (schema != 1 && schema != 2)) {
        if (error) *error = "schema sessione non supportato";
        return false;
    }

    session->schema = 2;
    std::string preset;
    if (readString(json, "dsp_settings", preset)) {
        DetectorSettings detector;
        if (!parseDetectorSettings(preset, detector, error)) return false;
        session->detectorSettings = detector;
    }
    if (!readString(json, "source", session->sourcePath)) {
        if (error) *error = "sessione senza campo source";
        return false;
    }
    readString(json, "source_relative", session->sourceRelative);
    readString(json, "source_sha256", session->sourceSha256);
    if (!session->sourceRelative.empty()) {
        std::error_code resolveError;
        const auto candidate = (std::filesystem::u8path(path).parent_path() /
            std::filesystem::u8path(session->sourceRelative)).lexically_normal();
        if (std::filesystem::exists(nativeFilePath(candidate), resolveError)) {
            session->sourcePath = candidate.u8string();
        }
    }
    double number = 0.0;
    if (!session->sourceSha256.empty() && std::filesystem::exists(nativeFilePath(std::filesystem::u8path(session->sourcePath)))) {
        std::string actual, hashError;
        if (!sha256File(session->sourcePath, actual, &hashError) || actual != session->sourceSha256) {
            if (error) *error = "audio sorgente cambiato: la sessione non viene applicata";
            return false;
        }
    }
    if (readNumber(json, "reduction_db", number)) session->reductionDb = static_cast<float>(number);
    if (readNumber(json, "fade_ms", number)) session->fadeMs = number;
    if (readNumber(json, "crossfade_ms", number)) session->crossfadeMs = number;
    if (readNumber(json, "master_gain_db", number)) session->masterGainDb = static_cast<float>(number);

    if (readNumber(json, "fade_in_seconds", number)) session->fade.inSeconds = number;
    if (readNumber(json, "fade_out_seconds", number)) session->fade.outSeconds = number;
    if (readNumber(json, "fade_in_shape", number)) session->fade.inShape = static_cast<FadeShape>(static_cast<int>(number));
    if (readNumber(json, "fade_out_shape", number)) session->fade.outShape = static_cast<FadeShape>(static_cast<int>(number));
    readBool(json, "fade_in_enabled", session->fade.inEnabled);
    readBool(json, "fade_out_enabled", session->fade.outEnabled);

    session->regions.clear();
    readString(json, "detector", session->detector);
    readString(json, "effective_detector", session->analysis.detector);
    if (readNumber(json, "ai_inference_frames", number)) session->analysis.aiFrames = static_cast<int64_t>(number);
    double aiThreshold = .064;
    if (readNumber(json, "ai_threshold", aiThreshold) && (aiThreshold < 0 || aiThreshold > 1)) {
        if (error) *error = "soglia AI non valida"; return false;
    }
    session->aiThreshold = static_cast<float>(aiThreshold);
    if (!session->detector.empty() && session->detector != "dsp" && session->detector != "ai" && session->detector != "fusion") {
        if (error) *error = "metodo di rilevamento non valido"; return false;
    }
    for (const std::string& object : objectsInArray(json, "regions")) {
        EditRegion region;
        int kind = 0;
        double value = 0.0;
        if (readNumber(object, "kind", value)) kind = static_cast<int>(value);
        region.kind = kind == 1 ? DetectionKind::Click : DetectionKind::Breath;
        if (!readNumber(object, "start", value) || value < 0) { if (error) *error = "inizio regione non valido"; return false; }
        region.startSample = static_cast<int64_t>(value);
        if (!readNumber(object, "end", value) || value <= region.startSample) { if (error) *error = "fine regione non valida"; return false; }
        region.endSample = static_cast<int64_t>(value);
        if (readNumber(object, "reduction_db", value)) region.reductionDb = static_cast<float>(value);
        readBool(object, "enabled", region.enabled);
        readBool(object, "repair", region.repair);
        if (readNumber(object, "score", value)) region.score = static_cast<float>(value);
        readBool(object, "from_detection", region.fromDetection);
        readBool(object, "approved", region.approved);
        if (readNumber(object, "edit_mode", value)) region.editMode = static_cast<int>(value);
        if (readNumber(object, "keep_fraction", value)) region.keepFraction = value;
        if (readNumber(object, "fade_ms", value)) region.fadeMs = value;
        if (readNumber(object, "safety_margin_ms", value)) region.safetyMarginMs = value;
        readBool(object, "applied", region.applied);
        readString(object, "decision_source", region.decisionSource);
        readString(object, "classification", region.classification);
        readBool(object, "training_eligible", region.trainingEligible);
        readString(object, "learning_label", region.learningLabel);
        readBool(object, "manual_operation", region.manualOperation);
        readString(object, "reason", region.reason);
        if (readNumber(object, "dsp_score", value)) region.dspScore = static_cast<float>(value);
        if (readNumber(object, "ai_score", value)) region.aiScore = static_cast<float>(value);
        if (readNumber(object, "ai_max_probability", value)) region.aiMaxProbability = static_cast<float>(value);
        session->regions.push_back(region);
    }
    session->events.clear();
    for (const std::string& object : objectsInArray(json, "events")) {
        Event event;
        double value = 0.0;
        if (readNumber(object, "kind", value)) event.kind = value == 1 ? DetectionKind::Click : DetectionKind::Breath;
        if (readNumber(object, "event_reduction_db", value)) event.reductionDb = static_cast<float>(value);
        readBool(object, "repair", event.repair);
        if (readNumber(object, "start", value)) event.startSample = static_cast<int64_t>(value);
        if (readNumber(object, "end", value)) event.endSample = static_cast<int64_t>(value);
        if (readNumber(object, "score", value)) event.score = static_cast<float>(value);
        readBool(object, "enabled", event.enabled);
        readBool(object, "approved", event.approved);
        readString(object, "state", event.state);
        std::string mode;
        if (readString(object, "mode", mode)) editModeFromName(mode, &event.mode);
        if (readNumber(object, "keep_fraction", value)) event.keepFraction = value;
        if (readNumber(object, "fade_ms", value)) event.fadeMs = value;
        if (readNumber(object, "safety_margin_ms", value)) event.safetyMarginMs = value;
        readString(object, "classification", event.classification);
        readString(object, "learning_label", event.learningLabel);
        readBool(object, "manual_operation", event.manualOperation);
        readString(object, "decision_source", event.decisionSource);
        readBool(object, "training_eligible", event.trainingEligible);
        readString(object, "source", event.source);
        readString(object, "reason", event.reason);
        if (readNumber(object, "dsp_score", value)) event.dspScore = static_cast<float>(value);
        if (readNumber(object, "ai_score", value)) event.aiScore = static_cast<float>(value);
        if (readNumber(object, "ai_max_probability", value)) event.aiMaxProbability = static_cast<float>(value);
        session->events.push_back(event);
    }
    session->corrections.clear();
    for (const std::string& object : objectsInArray(json, "corrections")) {
        Correction correction;
        int kind = 0;
        double value = 0.0;
        if (readNumber(object, "kind", value)) kind = static_cast<int>(value);
        correction.kind = kind == 1 ? DetectionKind::Click : DetectionKind::Breath;
        if (readNumber(object, "start", value)) correction.startSample = static_cast<int64_t>(value);
        if (readNumber(object, "end", value)) correction.endSample = static_cast<int64_t>(value);
        readBool(object, "positive", correction.positive);
        readString(object, "source", correction.source);
        session->corrections.push_back(correction);
    }
    if (!std::isfinite(session->reductionDb) || session->reductionDb > 0 ||
        !std::isfinite(session->masterGainDb) || session->masterGainDb > 0 ||
        !std::isfinite(session->fadeMs) || session->fadeMs < 0 || session->fadeMs > 60000 ||
        !std::isfinite(session->crossfadeMs) || session->crossfadeMs < 0 || session->crossfadeMs > 60000 ||
        session->fade.inSeconds < 0 || session->fade.inSeconds > 3600 ||
        session->fade.outSeconds < 0 || session->fade.outSeconds > 3600) {
        if (error) *error = "parametri sessione non validi";
        return false;
    }
    for (const auto& region : session->regions) {
        if (region.reductionDb > 0 || region.fadeMs < 0 || region.fadeMs > 60000 ||
            region.keepFraction < 0 || region.keepFraction > 1 ||
            region.safetyMarginMs < 0 || region.safetyMarginMs > 60000 || region.editMode < 0 || region.editMode > 3) {
            if (error) *error = "parametri regione non validi";
            return false;
        }
    }
    *output = std::move(pending);
    if (error) error->clear();
    return true;
}

} // namespace studio
} // namespace bc
