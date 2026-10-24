// SPDX-License-Identifier: MPL-2.0
#include "core/util/atomic_file.h"
#include "core/util/json_validation.h"
#include "studio_project.h"
#include "core/detect/detector_settings_codec.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

const char* findKey(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return nullptr;
    return std::strchr(json.c_str() + pos + needle.size(), ':');
}

bool readString(const std::string& json, const std::string& key, std::string& out) {
    const char* p = findKey(json, key);
    if (!p) return false;
    ++p;
    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    size_t position = static_cast<size_t>(p - json.c_str());
    return decodeJsonString(json, position, out);
}

bool readNumber(const std::string& json, const std::string& key, double& out) {
    const char* p = findKey(json, key);
    if (!p) return false;
    char* end = nullptr;
    out = std::strtod(p + 1, &end);
    return end != p + 1;
}

bool readBool(const std::string& json, const std::string& key, bool& out) {
    const char* p = findKey(json, key);
    if (!p) return false;
    ++p;
    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    if (std::strncmp(p, "true", 4) == 0) { out = true; return true; }
    if (std::strncmp(p, "false", 5) == 0) { out = false; return true; }
    return false;
}

std::vector<std::string> objectsInArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    const char* p = findKey(json, key);
    if (!p) return result;
    ++p;
    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p != '[') return result;
    ++p;
    int depth = 0;
    bool inString = false;
    std::string current;
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

std::string normalizeSlashes(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string relativeIfPossible(const std::filesystem::path& base,
                               const std::filesystem::path& file) {
    std::error_code ec;
    const auto baseAbs = std::filesystem::absolute(base, ec).lexically_normal();
    const auto fileAbs = std::filesystem::absolute(file, ec).lexically_normal();
    if (ec) return {};
    const auto rel = fileAbs.lexically_relative(baseAbs);
    if (rel.empty() || rel == ".") return {};
    const std::string text = rel.generic_u8string();
    if (text.rfind("../", 0) == 0 || text == "..") return {};
    return text;
}


void writeSettings(std::ostream& out, const Settings& settings) {
    out << "  \"settings\": {\n";
    if (settings.dspSettings) out << "    \"dsp_settings\": \""
        << escape(serializeDetectorSettings(*settings.dspSettings)) << "\",\n";
    out
        << "    \"reduction_db\": " << settings.reductionDb << ",\n"
        << "    \"fade_ms\": " << settings.fadeMs << ",\n"
        << "    \"pre_pad_ms\": " << settings.prePadMs << ",\n"
        << "    \"post_pad_ms\": " << settings.postPadMs << ",\n"
        << "    \"sensitivity\": " << settings.sensitivity << ",\n"
        << "    \"crossfade_ms\": " << settings.crossfadeMs << ",\n"
        << "    \"threshold\": " << settings.threshold << ",\n"
        << "    \"detect_clicks\": " << (settings.detectClicks ? "true" : "false") << ",\n"
        << "    \"use_respiro_en\": " << (settings.useRespiroEn ? "true" : "false") << ",\n"
        << "    \"ai_threshold\": " << settings.aiThreshold << ",\n"
        << "    \"fusion_ai_floor\": " << settings.fusionAiFloor << ",\n"
        << "    \"cut_margin_ms\": " << settings.cutMarginMs << ",\n"
        << "    \"detector\": \"" << escape(settings.detector) << "\"\n"
        << "  },\n";
}

bool readSettings(const std::string& json, Settings* settings, std::string* error) {
    if (!settings) return false;
    std::string preset;
    if (readString(json, "dsp_settings", preset)) {
        DetectorSettings detector;
        if (!parseDetectorSettings(preset, detector, error)) return false;
        settings->dspSettings = detector;
    }
    double number = 0.0;
    if (readNumber(json, "reduction_db", number)) settings->reductionDb = static_cast<float>(number);
    if (readNumber(json, "fade_ms", number)) settings->fadeMs = number;
    if (readNumber(json, "pre_pad_ms", number)) settings->prePadMs = number;
    if (readNumber(json, "post_pad_ms", number)) settings->postPadMs = number;
    if (readNumber(json, "sensitivity", number)) settings->sensitivity = static_cast<float>(number);
    if (readNumber(json, "crossfade_ms", number)) settings->crossfadeMs = number;
    if (readNumber(json, "threshold", number)) settings->threshold = static_cast<float>(number);
    if (readNumber(json, "ai_threshold", number)) settings->aiThreshold = static_cast<float>(number);
    if (readNumber(json, "fusion_ai_floor", number)) settings->fusionAiFloor = static_cast<float>(number);
    if (readNumber(json, "cut_margin_ms", number)) settings->cutMarginMs = number;
    readBool(json, "detect_clicks", settings->detectClicks);
    readBool(json, "use_respiro_en", settings->useRespiroEn);
    readString(json, "detector", settings->detector);
    return true;
}

} // namespace

std::string projectSessionPath(const std::string& projectPath, size_t index,
                               const std::string& sourcePath) {
    const std::filesystem::path project = std::filesystem::u8path(projectPath);
    std::string stem = std::filesystem::u8path(sourcePath).stem().u8string();
    if (stem.empty()) stem = "audio";
    std::ostringstream name;
    name << static_cast<unsigned>(index + 1) << "_" << stem << ".bcnstudio.json";
    return (project.parent_path() / ".breathclick_sessions" / name.str()).u8string();
}

std::string makeProjectSessionRelative(const std::string& projectPath,
                                       size_t index, const std::string& sourcePath) {
    const std::filesystem::path project = std::filesystem::u8path(projectPath);
    const std::filesystem::path absoluteSession =
        std::filesystem::u8path(projectSessionPath(projectPath, index, sourcePath));
    return normalizeSlashes(relativeIfPossible(project.parent_path(), absoluteSession));
}

bool saveProjectFile(const std::string& path, const ProjectData& project,
                     std::string* error) {
    for (const auto& file : project.files) {
        if (sameFilePath(path, file.sourcePath)) {
            if (error) *error = "il progetto non puo' sovrascrivere l'audio sorgente";
            return false;
        }
    }
    if (path.empty()) {
        if (error) *error = "percorso di progetto vuoto";
        return false;
    }
    std::ostringstream out;
    out << "{\n  \"schema\": 1,\n"
        << "  \"project\": \"" << escape(project.name) << "\",\n";
    writeSettings(out, project.settings);
    out << "  \"files\": [\n";
    const std::filesystem::path projectPath = std::filesystem::u8path(path);
    for (size_t i = 0; i < project.files.size(); ++i) {
        const ProjectFile& file = project.files[i];
        out << "    {\n"
            << "      \"source\": \"" << escape(file.sourcePath) << "\",\n"
            << "      \"source_relative\": \"" << escape(file.relativeSource) << "\",\n"
            << "      \"session_relative\": \"" << escape(file.sessionRelative) << "\",\n"
            << "      \"output_relative\": \"" << escape(file.outputRelative) << "\",\n"
            << "      \"status\": \"" << escape(file.status) << "\",\n"
            << "      \"events\": " << file.events << ",\n"
            << "      \"approved\": " << file.approved << ",\n"
            << "      \"analyzed\": " << (file.analyzed ? "true" : "false") << ",\n"
            << "      \"reviewed\": " << (file.reviewed ? "true" : "false") << ",\n"
            << "      \"exported\": " << (file.exported ? "true" : "false") << ",\n"
            << "      \"error\": \"" << escape(file.error) << "\"\n"
            << "    }" << (i + 1 < project.files.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    (void)projectPath;
    if (!validJsonDocument(out.str())) {
        if (error) *error = "dati di salvataggio non validi";
        return false;
    }
    return writeTextAtomic(path, out.str(), error);
}

bool loadProjectFile(const std::string& path, ProjectData* output,
                     std::string* error) {
    ProjectData pending;
    auto* project = &pending;
    if (!output) {
        if (error) *error = "progetto nullo";
        return false;
    }
    std::ifstream in(nativeFilePath(std::filesystem::u8path(path)), std::ios::binary);
    if (!in) {
        if (error) *error = "progetto non apribile";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    double schema = 0;
    if (!validJsonDocument(json) || !readNumber(json, "schema", schema) || schema != 1) {
        if (error) *error = "progetto non valido";
        return false;
    }
    project->schema = 1;
    project->name.clear();
    project->files.clear();
    readString(json, "project", project->name);
    if (!readSettings(json, &project->settings, error)) return false;
    const std::filesystem::path projectPath = std::filesystem::u8path(path);
    const std::filesystem::path projectDir = projectPath.parent_path();

    for (const std::string& object : objectsInArray(json, "files")) {
        ProjectFile file;
        readString(object, "source", file.sourcePath);
        readString(object, "source_relative", file.relativeSource);
        readString(object, "session_relative", file.sessionRelative);
        readString(object, "output_relative", file.outputRelative);
        readString(object, "status", file.status);
        readString(object, "error", file.error);
        double number = 0.0;
        if (readNumber(object, "events", number)) file.events = static_cast<int>(number);
        if (readNumber(object, "approved", number)) file.approved = static_cast<int>(number);
        readBool(object, "analyzed", file.analyzed);
        readBool(object, "reviewed", file.reviewed);
        readBool(object, "exported", file.exported);
        if (!file.relativeSource.empty()) {
            const auto candidate = (projectDir / std::filesystem::u8path(file.relativeSource)).lexically_normal();
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec)) file.sourcePath = candidate.u8string();
        }
        if (file.status.empty()) file.status = "da_analizzare";
        project->files.push_back(file);
    }
    if (project->files.empty()) {
        if (error) *error = "progetto senza file";
        return false;
    }
    project->name = projectPath.stem().u8string();
    *output = std::move(pending);
    if (error) error->clear();
    return true;
}

} // namespace studio
} // namespace bc
