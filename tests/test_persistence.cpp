// SPDX-License-Identifier: MPL-2.0
#include "core/util/atomic_file.h"
#include "core/util/json_validation.h"
#include "standalone/studio_session.h"
#include "standalone/studio_project.h"
#include "core/detect/offline_analyzer.h"
#include "shared/synthetic_audio.h"
#include "core/detect/detector_settings_codec.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
int fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; return 1; }
std::string read(const std::filesystem::path& path) {
    std::ifstream in(bc::nativeFilePath(path), std::ios::binary); std::ostringstream out; out << in.rdbuf(); return out.str();
}
std::string escapedAccent(std::string text) {
    size_t p = 0;
    while ((p = text.find(u8"è", p)) != std::string::npos) {
        text.replace(p, std::string(u8"è").size(), "\\u00e8"); p += 6;
    }
    return text;
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("bc_persistence_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto file = root / "state.json";
    std::string error;
    if (!bc::writeTextAtomic(file.u8string(), "previous", &error)) return fail("first save");
    #ifdef _WIN32
    HANDLE lock = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return fail("failure injection lock");
    const bool wrote = bc::writeTextAtomic(file.u8string(), "replacement", &error);
    CloseHandle(lock);
    #else
    // POSIX permits replacing open files: exercise a real rename conflict instead.
    const auto blocked=root/"blocked";
    std::filesystem::create_directory(blocked);
    if(!bc::writeTextAtomic((blocked/"previous").u8string(),"previous",&error))return fail("conflict fixture");
    const bool wrote=bc::writeTextAtomic(blocked.u8string(),"replacement",&error);
    if(read(blocked/"previous")!="previous")return fail("rename conflict lost previous content");
    #endif
    if (wrote || error.empty() || read(file) != "previous") return fail("previous save lost on replacement failure");
    if (!bc::writeTextAtomic(file.u8string(), "replacement", &error) || read(file) != "replacement")
        return fail("atomic retry");
    const auto longDirectory = root / std::string(90, 'a') / std::string(90, 'b') / std::string(90, 'c');
    std::filesystem::create_directories(bc::nativeFilePath(longDirectory));
    const auto longTarget = longDirectory / "long_state.json";
    if (!bc::writeTextAtomic(longTarget.u8string(), "long path", &error) || read(longTarget) != "long path")
        return fail("atomic save beyond MAX_PATH");
    for (const char* text : {"{", "{\"source\":\"x\"", "{\"x\":1,}", "{\"x\":1,\"x\":2}",
                            "[1,]", "{\"x\":NaN}", "{\"x\":1e999}", "{\"x\":01}", "{}garbage"}) {
        if (bc::validJsonDocument(text)) return fail("invalid JSON accepted");
    }
    if (!bc::validJsonDocument("{\"x\":[true,false,null,-1.2e3,\"quote\\\"\\\\\"]}")) return fail("valid JSON rejected");
    std::string decodedString = "unchanged";
    size_t position = 0;
    if (!bc::decodeJsonString("\"voce_\\u00e8_\\ud83c\\udf99\\n\"", position, decodedString) ||
        decodedString != u8"voce_è_🎙\n") return fail("JSON Unicode/surrogate decoding");
    for (const char* text : {"\"\\ud800\"", "\"\\udc00\"", "\"\\ud800\\u0041\"", "\"\\u0000\"", "\"\\u12"}) {
        position = 0; decodedString = "unchanged";
        if (bc::decodeJsonString(text, position, decodedString) || position != 0 || decodedString != "unchanged" ||
            bc::validJsonDocument(text)) return fail("invalid Unicode/NUL accepted or changed output");
    }
    if (bc::validJsonDocument(u8"{\"è\":1,\"\\u00e8\":2}")) return fail("Unicode duplicate key accepted");

    const auto audio = root / std::filesystem::u8path(u8"voce_è.wav");
    std::ofstream(audio) << "synthetic source bytes";
    bc::studio::SessionData session;
    session.sourcePath = audio.u8string();
    bc::EditRegion region;
    region.startSample = 12; region.endSample = 456;
    region.learningLabel = "breath_confirmed"; region.trainingEligible = true; region.decisionSource = "user";
    region.repairFeedback="unsatisfactory";region.repairFeedbackMethod="attenuate";
    session.regions.push_back(region);
    session.events.push_back(bc::studio::eventFromRegion(region));
    session.detectorSettings = bc::DetectorSettings{};
    session.detectorSettings->minBreathMs = 247;
    session.detectorSettings->requirePauseForBreath = false;
    session.fade.inEnabled = true; session.fade.inSeconds = .123;
    session.fade.outEnabled = true; session.fade.outShape = bc::FadeShape::Linear;
    const auto path = root / std::filesystem::u8path(u8"sessione_è.bcnstudio.json");
    if (!bc::studio::saveSession(path.u8string(), session, &error)) return fail("session unicode save");
    if (!bc::writeTextAtomic(path.u8string(), escapedAccent(read(path)), &error)) return fail("escaped session fixture");
    bc::studio::SessionData loaded;
    if (!bc::studio::loadSession(path.u8string(), &loaded, &error) || loaded.regions.size() != 1 ||
        loaded.regions[0].learningLabel != region.learningLabel || !loaded.regions[0].trainingEligible ||
        !loaded.detectorSettings || loaded.detectorSettings->minBreathMs != 247 ||
        loaded.detectorSettings->requirePauseForBreath || loaded.sourcePath != audio.u8string())
        return fail("session labels roundtrip");
    if(loaded.regions[0].repairFeedback!="unsatisfactory" || loaded.regions[0].repairFeedbackMethod!="attenuate" ||
       loaded.events.size()!=1 || loaded.events[0].repairFeedback!="unsatisfactory" ||
       loaded.events[0].repairFeedbackMethod!="attenuate") return fail("independent repair feedback lost");
    if (bc::studio::saveSession(audio.u8string(), session, &error) || read(audio) != "synthetic source bytes")
        return fail("session overwrote original");
    if (!loaded.fade.inEnabled || loaded.fade.inSeconds != .123 || !loaded.fade.outEnabled ||
        loaded.fade.outShape != bc::FadeShape::Linear) return fail("session lost fade envelope");
    const auto longSession = (longDirectory / "session.bcnstudio.json").u8string();
    if (!bc::studio::saveSession(longSession, session, &error) ||
        !bc::studio::loadSession(longSession, &loaded, &error) || loaded.regions.size() != 1)
        return fail("long session save/reload");
    const auto valid = read(path);
    std::ofstream(path) << valid.substr(0, valid.size() / 2);
    if (bc::studio::loadSession(path.u8string(), &loaded, &error) || loaded.regions.size() != 1)
        return fail("truncated session replaced work");
    bc::writeTextAtomic(path.u8string(), valid);
    std::ofstream(audio) << "changed synthetic source";
    if (bc::studio::loadSession(path.u8string(), &loaded, &error)) return fail("source fingerprint ignored");

    bc::studio::ProjectData project;
    project.settings.dspSettings = *session.detectorSettings;
    bc::studio::ProjectFile entry;
    entry.sourcePath = audio.u8string(); project.files.push_back(entry);
    if (!bc::studio::saveProjectFile(file.u8string(), project, &error)) return fail("project save");
    if (!bc::writeTextAtomic(file.u8string(), escapedAccent(read(file)), &error)) return fail("escaped project fixture");
    if (!bc::studio::loadProjectFile(file.u8string(), &project, &error)) return fail("project load");
    if (project.files[0].sourcePath != audio.u8string()) return fail("escaped project source path");
    if (!project.settings.dspSettings || project.settings.dspSettings->minBreathMs != 247 ||
        project.settings.dspSettings->requirePauseForBreath) return fail("project advanced settings lost");
    std::ofstream(file) << "{\"schema\":1,\"files\":[";
    if (bc::studio::loadProjectFile(file.u8string(), &project, &error) || project.files.size() != 1)
        return fail("truncated project replaced work");
    if (bc::studio::saveProjectFile(audio.u8string(), project, &error)) return fail("project overwrote original");

    const auto take = bc::synthetic::makeVocalTake();
    bc::AudioBuffer buffer;
    buffer.samples = take.samples; buffer.channels = 1; buffer.sampleRate = int(take.sampleRate);
    const auto wav = (root / "synthetic.wav").u8string();
    if (!bc::writeWav(wav, buffer, bc::WavFormat::Float32, &error)) return fail("synthetic WAV");
    const auto sourceBytes = read(std::filesystem::u8path(wav));
    const auto longSource = (longDirectory / std::filesystem::u8path(u8"voce_è.wav")).u8string();
    const auto longOutput = (longDirectory / std::filesystem::u8path(u8"elaborato_è.wav")).u8string();
    bc::AudioBuffer longAudio;
    bc::AudioFileInfo longInfo;
    std::string longHash;
    if (!bc::writeWav(longSource, buffer, bc::WavFormat::Float32, &error) ||
        !bc::readAudio(longSource, longAudio, &longInfo, &error) || longAudio.samples != buffer.samples ||
        !bc::probeAudio(longSource, longInfo, &error) || longInfo.frames != buffer.frameCount() ||
        !bc::studio::sha256File(longSource, longHash, &error) || longHash.empty())
        return fail("Unicode WAV import/hash beyond MAX_PATH");
    if (!bc::studio::writeWavWithMetadataAtomic(longSource, longOutput, buffer,
            bc::WavFormat::Float32, &error) ||
        !bc::readAudio(longOutput, longAudio, &longInfo, &error) || longAudio.samples != buffer.samples)
        return fail("atomic WAV export beyond MAX_PATH");
    bc::studio::SessionData longAudioSession;
    longAudioSession.sourcePath = longSource; longAudioSession.crossfadeMs = 7;
    const auto longAudioSessionPath = (longDirectory / "audio.bcnstudio.json").u8string();
    if (!bc::studio::saveSession(longAudioSessionPath, longAudioSession, &error) ||
        !bc::studio::loadSession(longAudioSessionPath, &longAudioSession, &error) ||
        longAudioSession.sourceSha256 != longHash || longAudioSession.crossfadeMs != 7)
        return fail("long audio session lost integrity hash/settings");
    // Failure to replace must preserve both the earlier output and source.
    const auto longOutputNative = bc::nativeFilePath(std::filesystem::u8path(longOutput));
    #ifdef _WIN32
    lock = CreateFileW(longOutputNative.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return fail("long WAV failure injection lock");
    const auto preservedOutput=longOutputNative;
    #else
    const auto preservedOutput=longOutputNative/"previous.wav";
    const auto moved=longOutputNative.parent_path()/"saved_output.wav";
    std::filesystem::rename(longOutputNative,moved);
    std::filesystem::create_directory(longOutputNative);
    std::filesystem::rename(moved,preservedOutput);
    #endif
    const auto previousOutput = read(preservedOutput);
    const bool replacedLong = bc::studio::writeWavWithMetadataAtomic(longSource, longOutput,
        buffer, bc::WavFormat::Pcm16, &error);
    #ifdef _WIN32
    CloseHandle(lock);
    #endif
    std::string hashAfter;
    if (replacedLong || error.empty() || read(preservedOutput) != previousOutput ||
        !bc::studio::sha256File(longSource, hashAfter, &error) || hashAfter != longHash)
        return fail("long WAV failure damaged previous output/source");
    bc::studio::Settings settings;
    settings.dspSettings = bc::DetectorSettings{};
    settings.prePadMs = settings.postPadMs = 0;
    for (int pass = 0; pass < 4; ++pass) {
        auto& dsp = *settings.dspSettings;
        dsp.breathSensitivity = .5f + pass * .1f;
        dsp.minBreathMs = 60 + pass * 80;
        dsp.mergeGapMs = 10 + pass * 30;
        dsp.requirePauseForBreath = pass % 2 == 0;
        bc::OfflineAnalyzer reference;
        const auto expected = reference.analyze(buffer.samples.data(), buffer.samples.size(), buffer.sampleRate, dsp);
        bc::studio::Analysis actual;
        if (!bc::studio::analyzeFile(wav, settings, &actual, &error) ||
            actual.events.size() != expected.detections.size()) return fail("advanced settings not applied to Studio engine");
        for (size_t i = 0; i < actual.events.size(); ++i) {
            if (actual.events[i].kind != expected.detections[i].kind ||
                actual.events[i].startSample != expected.detections[i].startSample ||
                actual.events[i].endSample != expected.detections[i].endSample)
                return fail("engine and local DSP proposal bounds differ");
        }
    }
    bc::studio::EditPlan plan;
    plan.sampleRate = buffer.sampleRate; plan.channels = 1; plan.sourcePath = wav;
    plan.settings.reductionDb = -30;
    bc::studio::Event event;
    event.startSample = 100; event.endSample = 2000; event.approved = true;
    event.fadeMs = 0; event.reductionDb = -6.0f;
    event.reason = u8"voce è valida";
    event.repairFeedback="unsatisfactory";event.repairFeedbackMethod="attenuate";
    plan.events.push_back(event);
    const auto planPath = (root / "edit.plan.json").u8string();
    if (!bc::studio::writeEditPlanJson(planPath, plan, &error) ||
        !bc::writeTextAtomic(planPath, escapedAccent(read(std::filesystem::u8path(planPath))), &error) ||
        !bc::studio::loadEditPlanJson(planPath, &plan, &error) ||
        plan.events.size() != 1 || plan.events[0].reductionDb != -6 ||
        plan.events[0].reason != event.reason || plan.events[0].repairFeedback!=event.repairFeedback ||
        plan.events[0].repairFeedbackMethod!=event.repairFeedbackMethod) return fail("per-region attenuation/feedback/escaped Unicode roundtrip");
    bc::studio::EditResult edited;
    if (!bc::studio::applyEditPlan(buffer, plan, &edited, &error)) return fail("apply region gain");
    const auto expectedGain = float(std::pow(10., -6. / 20.));
    if (std::abs(edited.audio.samples[500] - buffer.samples[500] * expectedGain) > 1e-7 ||
        edited.audio.frameCount() != buffer.frameCount()) return fail("export ignored per-region attenuation");
    if (bc::studio::writeWavWithMetadataAtomic(wav, wav, edited.audio, bc::WavFormat::Float32, &error) ||
        read(std::filesystem::u8path(wav)) != sourceBytes) return fail("WAV export overwrote original");
    bc::AudioDocument reference;
    reference.adopt(buffer, "gain.wav");
    plan.events.clear(); plan.masterGainDb = -6; plan.fade = session.fade;
    reference.setFade(plan.fade); reference.setMasterGainDb(plan.masterGainDb);
    if (!bc::studio::writeEditPlanJson(planPath, plan, &error) ||
        !bc::studio::loadEditPlanJson(planPath, &plan, &error) ||
        !bc::studio::applyEditPlan(buffer, plan, &edited, &error)) return fail("envelope plan");
    const auto rendered = reference.render(bc::EditSettings{});
    for (size_t i = 0; i < rendered.samples.size(); ++i)
        if (std::abs(rendered.samples[i] - edited.audio.samples[i]) > 1e-7) return fail("preview/export global gain and fade mismatch");
    plan.masterGainDb = 0; plan.fade = bc::FadeSettings{};
    event.mode = bc::studio::EditMode::Cut; event.startSample = 1000; event.endSample = 2000;
    plan.events = {event};
    event.mode = bc::studio::EditMode::Halve; event.startSample = 3000; event.endSample = 5000;
    event.keepFraction = .5; plan.events.push_back(event);
    if (!bc::studio::applyEditPlan(buffer, plan, &edited, &error) ||
        edited.map.mapOriginal(6000) != 4000 || edited.map.mapEdited(4000) != 6000 ||
        edited.map.mapEdited(edited.map.mapOriginal(1500)) != 2000)
        return fail("A/B and follow coordinates after Cut/Halve");
    std::cout << "PERSISTENCE TESTS OK: atomic failure/retry, strict JSON, source integrity, session labels\n";
    return 0;
}
