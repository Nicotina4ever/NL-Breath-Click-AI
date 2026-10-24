// SPDX-License-Identifier: MPL-2.0
#include "version.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "shared/synthetic_audio.h"
#include "shared/utf8_console.h"
#include "studio_engine.h"
#include "studio_session.h"
#include "reaper_slice.h"
#include "core/edit/audio_io.h"

namespace {

std::string cliJsonEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

} // namespace

namespace {

void usage() {
    std::cout <<
        "NL Breath Click Studio CLI\n\n"
        "USO\n"
        "  bc_studio_cli analyze <file.wav> [--json <report.json>] [opzioni]\n"
        "  bc_studio_cli process <file.wav> --out-dir <cartella> [opzioni]\n"
        "  bc_studio_cli batch <cartella> --out-dir <cartella> [--batch-report <file>]\n"
        "  bc_studio_cli selftest\n"
        "  bc_studio_cli session-selftest\n"
        "  bc_studio_cli review <file.wav> --reject <secondi> --out-dir <cartella>\n"
        "  bc_studio_cli batch-review <cartella> --out-dir <cartella> --plans <cartella>\n"
        "  bc_studio_cli plan <file.wav> --out <plan.json> [opzioni]\n"
        "  bc_studio_cli edit <file.wav> --out-dir <cartella> [--plan <plan.json>]\n"
        "                       [--mode attenuate|cut|halve] [--keep-fraction 0..1]\n"
        "  bc_studio_cli probe <file.wav|file.mp3|file.flac>\n\n"
        "  bc_studio_cli slice <audio> --out <nuovo.wav> --start <s> --duration <s>\n"
        "  bc_studio_cli verify-source <audio> --sha256 <hash>\n\n"
        "  bc_studio_cli capabilities\n\n"
        "  bc_studio_cli labels-export --plan <etichette-originale.json> --out <sessione.bcnstudio.json>\n"
        "  bc_studio_cli profile-train --plan <etichette1.json> --plan <etichette2.json> --out <profilo.ini>\n"
        "  bc_studio_cli profile-filter --plan <proposte.json> --profile <profilo.ini> --out <nuovo.json>\n\n"
        "OPZIONI\n"
        "  --reduction-db <dB>   attenuazione respiri (default -12)\n"
        "  --fade-ms <ms>        fade di attacco/rilascio (default 15)\n"
        "  --sensitivity <0..1>  sensibilita' DSP (default 0.8)\n"
        "  --threshold <0..1>    soglia DSP (default 0.02)\n"
        "  --pre-pad-ms <ms>     margine prima del respiro (default 5)\n"
        "  --post-pad-ms <ms>    margine dopo il respiro (default 10)\n"
        "  --detector dsp|ai|fusion  rilevatore (default dsp)\n"
        "  --model <file.onnx>  modello Respiro-en (default accanto all'eseguibile)\n"
        "  --ai-threshold <0..1> soglia Respiro-en (default 0.064)\n"
        "  --out <file.json>     piano in uscita per il comando plan\n"
        "  --plan <file.json>    piano da applicare per il comando edit\n"
        "  --mode <modo>         attenuate, cut o halve\n"
        "  --keep-fraction <f>   percentuale conservata per halve (default 0.5)\n"
        "  --margin-ms <ms>      margine non tagliato ai bordi (default 0)\n"
        "  --crossfade-ms <ms>  overlap-add tra i bordi di Taglia/Dimezza (default 0)\n";
}

bool parseDouble(const std::string& text, double& value) {
    try {
        size_t used = 0;
        value = std::stod(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

bc::studio::Settings parseSettings(int argc, char** argv, std::string& outDir,
                                   std::string& jsonPath, std::string& batchReport) {
    bc::studio::Settings settings;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--out-dir" && next(value)) outDir = value;
        else if (arg == "--json" && next(value)) jsonPath = value;
        else if (arg == "--out" && next(value)) jsonPath = value;
        else if (arg == "--batch-report" && next(value)) batchReport = value;
        else if (arg == "--reduction-db" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.reductionDb = static_cast<float>(d);
        } else if (arg == "--fade-ms" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.fadeMs = d;
        } else if (arg == "--crossfade-ms" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.crossfadeMs = d;
        } else if (arg == "--sensitivity" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.sensitivity = static_cast<float>(d);
        } else if (arg == "--threshold" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.threshold = static_cast<float>(d);
        } else if (arg == "--pre-pad-ms" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.prePadMs = d;
        } else if (arg == "--post-pad-ms" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.postPadMs = d;
        } else if (arg == "--detector" && next(value)) {
            settings.detector = value;
        } else if (arg == "--model" && next(value)) {
            settings.modelPath = value;
        } else if (arg == "--ai-threshold" && next(value)) {
            double d = 0; if (parseDouble(value, d)) settings.aiThreshold = static_cast<float>(d);
        }
    }
    return settings;
}

int commandAnalyze(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    std::string outDir, jsonPath, batchReport;
    const auto settings = parseSettings(argc, argv, outDir, jsonPath, batchReport);
    bc::studio::Analysis analysis;
    std::string error;
    if (!bc::studio::analyzeFile(argv[2], settings, &analysis, &error)) {
        std::cerr << "analisi fallita: " << error << "\n";
        return 1;
    }
    std::cout << "rilevatore: " << analysis.detector << "\n";
    std::cout << "file: " << analysis.inputPath << "\n"
              << "formato: " << analysis.sampleRate << " Hz, "
              << analysis.channels << " canali, "
              << analysis.bitsPerSample << " bit\n"
              << "durata: " << analysis.durationSeconds << " s\n"
              << "respiri: " << analysis.events.size() << "\n";
    for (size_t i = 0; i < analysis.events.size(); ++i) {
        const auto& e = analysis.events[i];
        std::printf("  %2zu  %8.3f - %8.3f s  score %.3f\n", i + 1,
                    static_cast<double>(e.startSample) / analysis.sampleRate,
                    static_cast<double>(e.endSample) / analysis.sampleRate,
                    e.score);
    }
    if (!jsonPath.empty()) {
        if (!bc::studio::writeAnalysisJson(jsonPath, analysis, &error)) {
            std::cerr << "report non scritto: " << error << "\n";
            return 1;
        }
    }
    return 0;
}

int commandProcess(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    std::string outDir, jsonPath, batchReport;
    const auto settings = parseSettings(argc, argv, outDir, jsonPath, batchReport);
    if (outDir.empty()) {
        std::cerr << "--out-dir obbligatorio\n";
        return 2;
    }
    bc::studio::Report report;
    std::string error;
    if (!bc::studio::processFile(argv[2], outDir, settings, &report, &error)) {
        std::cerr << "elaborazione fallita: " << error << "\n";
        return 1;
    }
    const std::filesystem::path out = std::filesystem::u8path(report.outputPath);
    const std::string json = out.string() + ".report.json";
    const std::string text = out.string() + ".report.txt";
    bc::studio::writeReportJson(json, report, &error);
    bc::studio::writeReportText(text, report, &error);
    std::cout << "scritto: " << report.outputPath << "\n"
              << "eventi: " << report.processedEvents << "\n"
              << "report: " << json << "\n";
    for (const auto& metric : report.metrics) {
        std::printf("  %.3f-%.3f s: %.2f -> %.2f dB (%.2f dB)\n",
                    static_cast<double>(metric.startSample) / report.analysis.sampleRate,
                    static_cast<double>(metric.endSample) / report.analysis.sampleRate,
                    metric.rmsBeforeDb, metric.rmsAfterDb, metric.deltaDb);
    }
    std::printf("fuori intervalli: medio %.3f dB, massimo %.3f dB\n",
                report.outsideMeanDeltaDb, report.outsideMaxDeltaDb);
    return 0;
}

int commandBatch(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    std::string outDir, jsonPath, batchReport;
    const auto settings = parseSettings(argc, argv, outDir, jsonPath, batchReport);
    if (outDir.empty()) {
        std::cerr << "--out-dir obbligatorio\n";
        return 2;
    }
    if (batchReport.empty()) {
        batchReport = (std::filesystem::u8path(outDir) / "batch_report.json").u8string();
    }
    std::vector<bc::studio::Report> reports;
    std::string error;
    if (!bc::studio::processFolder(argv[2], outDir, settings, batchReport, &reports, &error)) {
        std::cerr << "batch fallito: " << error << "\n";
        return 1;
    }
    std::cout << "file elaborati: " << reports.size() << "\n"
              << "report batch: " << batchReport << "\n";
    for (const auto& report : reports) {
        std::printf("  %s: %d eventi, fuori max %.3f dB\n",
                    report.outputPath.c_str(), report.processedEvents,
                    report.outsideMaxDeltaDb);
    }
    return 0;
}

int selftest() {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("bc_studio_selftest_" + std::to_string(
                          static_cast<unsigned long long>(std::rand())));
    std::error_code ec;
    std::filesystem::create_directories(temp, ec);
    const std::string input = (temp / "synthetic.wav").u8string();
    const std::string outDir = (temp / "out").u8string();

    const bc::synthetic::TestTake take = bc::synthetic::makeVocalTake();
    bc::AudioBuffer buffer;
    buffer.channels = 1;
    buffer.sampleRate = static_cast<int>(take.sampleRate);
    buffer.samples = take.samples;
    std::string error;
    if (!bc::writeWav(input, buffer, bc::WavFormat::Pcm24, &error)) {
        std::cerr << "selftest: scrittura sintetico fallita: " << error << "\n";
        return 1;
    }

    bc::studio::Settings settings;
    settings.reductionDb = -12.0f;
    settings.fadeMs = 15.0;
    bc::studio::Analysis analysis;
    if (!bc::studio::analyzeFile(input, settings, &analysis, &error)) {
        std::cerr << "selftest: analisi fallita: " << error << "\n";
        return 1;
    }
    if (analysis.events.size() < 2) {
        std::cerr << "selftest: respiri trovati " << analysis.events.size() << " (<2)\n";
        return 1;
    }

    bc::studio::Report report;
    if (!bc::studio::processFile(input, outDir, settings, &report, &error)) {
        std::cerr << "selftest: elaborazione fallita: " << error << "\n";
        return 1;
    }

    bc::AudioBuffer processed;
    if (!bc::readWav(report.outputPath, processed, &error)) {
        std::cerr << "selftest: output non leggibile: " << error << "\n";
        return 1;
    }
    if (processed.sampleRate != buffer.sampleRate || processed.channels != buffer.channels) {
        std::cerr << "selftest: formato di uscita cambiato\n";
        return 1;
    }
    if (std::llabs(processed.frameCount() - buffer.frameCount()) > 1) {
        std::cerr << "selftest: durata cambiata\n";
        return 1;
    }

    std::string sourceHashBefore, sourceHashAfter;
    bc::studio::sha256File(input, sourceHashBefore, &error);
    bc::studio::sha256File(input, sourceHashAfter, &error);
    if (!sourceHashBefore.empty() && sourceHashBefore != sourceHashAfter) {
        std::cerr << "selftest: sorgente modificata\n";
        return 1;
    }

    for (const auto& metric : report.metrics) {
        if (std::abs(metric.deltaDb + 12.0) > 2.5) {
            std::cerr << "selftest: attenuazione evento " << metric.deltaDb << " dB\n";
            return 1;
        }
    }
    if (report.outsideMaxDeltaDb > 0.25) {
        std::cerr << "selftest: fuori intervalli alterato di "
                  << report.outsideMaxDeltaDb << " dB\n";
        return 1;
    }

    std::cout << "SELFTEST OK\n"
              << "  respiri rilevati: " << analysis.events.size() << "\n"
              << "  eventi misurati: " << report.metrics.size() << "\n"
              << "  attenuazione media: ";
    double mean = 0.0;
    for (const auto& metric : report.metrics) mean += metric.deltaDb;
    if (!report.metrics.empty()) mean /= static_cast<double>(report.metrics.size());
    std::cout << mean << " dB\n"
              << "  scostamento fuori: " << report.outsideMaxDeltaDb << " dB\n"
              << "  input: " << input << "\n"
              << "  output: " << report.outputPath << "\n";
    return 0;
}

} // namespace


int commandPlan(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    std::string outDir, planPath, batchReport;
    const auto settings = parseSettings(argc, argv, outDir, planPath, batchReport);
    if (planPath.empty()) {
        std::cerr << "--out <plan.json> obbligatorio\n";
        return 2;
    }
    bc::studio::Analysis analysis;
    std::string error;
    if (!bc::studio::analyzeFile(argv[2], settings, &analysis, &error)) {
        std::cerr << "analisi fallita: " << error << "\n";
        return 1;
    }
    bc::studio::EditPlan plan;
    plan.sourcePath = analysis.inputPath;
    plan.sampleRate = analysis.sampleRate;
    plan.channels = analysis.channels;
    plan.settings = settings;
    plan.analysis = {analysis.detector, analysis.aiInferenceFrames};
    plan.events = analysis.events;
    for (auto& event : plan.events) {
        event.mode = bc::studio::EditMode::Attenuate;
        event.approved = false;
        event.fadeMs = settings.fadeMs;
    }
    if (!bc::studio::writeEditPlanJson(planPath, plan, &error)) {
        std::cerr << "piano non scritto: " << error << "\n";
        return 1;
    }
    std::cout << "piano scritto: " << planPath << "\n"
              << "eventi proposti: " << plan.events.size() << "\n";
    return 0;
}

int commandEdit(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    std::string outDir, planPath, batchReport;
    const auto settings = parseSettings(argc, argv, outDir, planPath, batchReport);
    bc::studio::EditMode mode = bc::studio::EditMode::Attenuate;
    double keepFraction = 0.5;
    double cutMarginMs = 0.0;
    bool applyAll = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--mode" && next(value)) {
            if (!bc::studio::editModeFromName(value, &mode)) {
                std::cerr << "modo non valido: " << value << "\n";
                return 2;
            }
        } else if (arg == "--keep-fraction" && next(value)) {
            double d = 0; if (parseDouble(value, d)) keepFraction = d;
        } else if (arg == "--margin-ms" && next(value)) {
            double d = 0; if (parseDouble(value, d)) cutMarginMs = std::max(0.0, d);
        } else if (arg == "--plan" && next(value)) {
            planPath = value;
        }
    }
    if (outDir.empty()) {
        std::cerr << "--out-dir obbligatorio\n";
        return 2;
    }

    bc::AudioBuffer original;
    std::string error;
    if (!bc::readWav(argv[2], original, &error)) {
        std::cerr << "lettura fallita: " << error << "\n";
        return 1;
    }
    bc::WavInfo info;
    if (!bc::probeWav(argv[2], info, &error)) {
        std::cerr << "intestazione non leggibile: " << error << "\n";
        return 1;
    }

    bc::studio::EditPlan plan;
    if (!planPath.empty()) {
        if (!bc::studio::loadEditPlanJson(planPath, &plan, &error)) {
            std::cerr << "piano non leggibile: " << error << "\n";
            return 1;
        }
    } else {
        bc::studio::Analysis analysis;
        if (!bc::studio::analyzeFile(argv[2], settings, &analysis, &error)) {
            std::cerr << "analisi fallita: " << error << "\n";
            return 1;
        }
        plan.sourcePath = analysis.inputPath;
        plan.sampleRate = analysis.sampleRate;
        plan.channels = analysis.channels;
        plan.settings = settings;
        plan.analysis = {analysis.detector, analysis.aiInferenceFrames};
        plan.events = analysis.events;
        for (auto& event : plan.events) {
            event.mode = mode;
            event.keepFraction = keepFraction;
            event.approved = applyAll;
            event.fadeMs = settings.fadeMs;
            event.safetyMarginMs = cutMarginMs;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(outDir), ec);
    const std::filesystem::path inputPath = std::filesystem::u8path(argv[2]);
    const std::string suffix = mode == bc::studio::EditMode::Cut ? "_cut" :
                               (mode == bc::studio::EditMode::Halve ? "_halve" : "_edit");
    const std::string output = (std::filesystem::u8path(outDir) /
        (inputPath.stem().string() + suffix + ".wav")).u8string();

    bc::studio::EditResult result;
    if (!bc::studio::applyEditPlan(original, plan, &result, &error)) {
        std::cerr << "piano non applicabile: " << error << "\n";
        return 1;
    }
    const auto format = info.isFloat ? bc::WavFormat::Float32 :
                        (info.bitsPerSample <= 16 ? bc::WavFormat::Pcm16 : bc::WavFormat::Pcm24);
    if (!bc::studio::writeWavWithMetadata(argv[2], output, result.audio, format, &error,
                                          &result.wav)) {
        std::cerr << "scrittura fallita: " << error << "\n";
        return 1;
    }
    const std::string reportPath = output + ".edit.json";
    if (!bc::studio::writeEditReportJson(reportPath, plan, result, output, &error)) {
        std::cerr << "report non scritto: " << error << "\n";
    }
    std::cout << "scritto: " << output << "\n"
              << "durata: " << result.map.originalFrames / static_cast<double>(plan.sampleRate)
              << " -> " << result.map.editedFrames / static_cast<double>(plan.sampleRate)
              << " s\n"
              << "rimossi: " << result.removedSamples << " campioni\n"
              << "report: " << reportPath << "\n";
    return 0;
}

int sessionSelftest() {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("bc_studio_session_" + std::to_string(static_cast<unsigned long long>(std::rand())));
    std::error_code ec;
    std::filesystem::create_directories(temp, ec);
    const std::string path = (temp / "test.bcnstudio.json").u8string();

    bc::studio::SessionData source;
    source.sourcePath = R"(C:\audio\take.wav)";
    bc::EditRegion region;
    region.kind = bc::DetectionKind::Breath;
    region.startSample = 1000;
    region.endSample = 2000;
    region.reductionDb = -12.0f;
    region.enabled = true;
    region.score = 0.9f;
    region.fromDetection = true;
    region.approved = true;
    region.editMode = 2;
    region.keepFraction = 0.35;
    region.fadeMs = 11.0;
    source.regions.push_back(region);
    bc::Correction correction;
    correction.kind = bc::DetectionKind::Breath;
    correction.startSample = 1000;
    correction.endSample = 2000;
    correction.positive = true;
    correction.source = "conferma";
    source.corrections.push_back(correction);

    bc::studio::Event event;
    event.startSample = 1000;
    event.endSample = 2000;
    event.score = 0.9f;
    event.approved = true;
    event.state = "confirmed";
    event.mode = bc::studio::EditMode::Cut;
    event.keepFraction = 0.4;
    event.fadeMs = 12.0;
    source.events.push_back(event);

    std::string error;
    if (!bc::studio::saveSession(path, source, &error)) {
        std::cerr << "session-selftest: salvataggio fallito: " << error << "\n";
        return 1;
    }
    bc::studio::SessionData loaded;
    if (!bc::studio::loadSession(path, &loaded, &error)) {
        std::cerr << "session-selftest: lettura fallita: " << error << "\n";
        return 1;
    }
    if (loaded.sourcePath != source.sourcePath || loaded.regions.size() != 1 ||
        !loaded.regions[0].approved || loaded.regions[0].editMode != 2 ||
        std::abs(loaded.regions[0].keepFraction - 0.35) > 1e-9 ||
        loaded.regions[0].startSample != 1000 || loaded.regions[0].endSample != 2000 ||
        loaded.corrections.size() != 1 || loaded.corrections[0].source != "conferma" ||
        loaded.events.size() != 1 || !loaded.events[0].approved ||
        loaded.events[0].mode != bc::studio::EditMode::Cut ||
        std::abs(loaded.events[0].keepFraction - 0.4) > 1e-9) {
        std::cerr << "session-selftest: dati non conservati\n";
        return 1;
    }
    std::cout << "SESSION SELFTEST OK\n  file: " << path << "\n";
    return 0;
}

int commandReview(int argc, char** argv) {
    if (argc < 4) {
        usage();
        return 2;
    }
    std::string outDir, planPath, batchReport;
    const auto settings = parseSettings(argc, argv, outDir, planPath, batchReport);
    std::string rejectText;
    std::string sessionPath;
    double keepFraction = 0.5;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--reject" && next(value)) rejectText = value;
        else if (arg == "--session" && next(value)) sessionPath = value;
        else if (arg == "--keep-fraction" && next(value)) {
            double d = 0; if (parseDouble(value, d)) keepFraction = std::max(0.0, std::min(1.0, d));
        }
    }
    if (outDir.empty() || rejectText.empty()) {
        std::cerr << "--out-dir e --reject sono obbligatori\n";
        return 2;
    }
    double rejectSeconds = 0.0;
    if (!parseDouble(rejectText, rejectSeconds)) {
        std::cerr << "tempo di rifiuto non valido\n";
        return 2;
    }

    bc::studio::Analysis analysis;
    std::string error;
    if (!bc::studio::analyzeFile(argv[2], settings, &analysis, &error)) {
        std::cerr << "analisi fallita: " << error << "\n";
        return 1;
    }
    const int64_t rejectSample = static_cast<int64_t>(
        std::llround(rejectSeconds * analysis.sampleRate));
    int rejectIndex = -1;
    int64_t bestDistance = std::numeric_limits<int64_t>::max();
    const int64_t rejectTolerance = static_cast<int64_t>(analysis.sampleRate * 0.1);
    for (size_t i = 0; i < analysis.events.size(); ++i) {
        const auto& event = analysis.events[i];
        int64_t distance = 0;
        if (rejectSample < event.startSample) distance = event.startSample - rejectSample;
        else if (rejectSample > event.endSample) distance = rejectSample - event.endSample;
        if (distance < bestDistance && distance <= rejectTolerance) {
            bestDistance = distance;
            rejectIndex = static_cast<int>(i);
        }
    }
    if (rejectIndex < 0) {
        std::cerr << "nessun evento vicino al tempo di rifiuto (tolleranza 100 ms)\n";
        return 1;
    }

    bc::studio::SessionData session;
    session.sourcePath = analysis.inputPath;
    session.reductionDb = settings.reductionDb;
    session.fadeMs = settings.fadeMs;
    session.crossfadeMs = settings.crossfadeMs;
    session.events = analysis.events;
    session.regions.clear();
    for (size_t i = 0; i < session.events.size(); ++i) {
        bc::studio::Event& event = session.events[i];
        const bool rejected = static_cast<int>(i) == rejectIndex;
        event.approved = !rejected;
        event.enabled = !rejected;
        event.classification = event.classification.empty()
            ? (event.source.empty() ? event.state : event.source)
            : event.classification;
        event.decisionSource = "review_harness";
        event.trainingEligible = false;
        event.mode = bc::studio::EditMode::Attenuate;
        event.keepFraction = keepFraction;
        event.fadeMs = settings.fadeMs;
        event.reason = rejected
            ? "rifiuto dimostrativo del test, non etichetta umana verificata"
            : "approvazione automatica del test, non etichetta umana verificata";
        bc::EditRegion region;
        region.kind = bc::DetectionKind::Breath;
        region.startSample = event.startSample;
        region.endSample = event.endSample;
        region.reductionDb = settings.reductionDb;
        region.enabled = event.enabled;
        region.approved = event.approved;
        region.score = event.score;
        region.fromDetection = true;
        region.editMode = 0;
        region.keepFraction = keepFraction;
        region.fadeMs = settings.fadeMs;
        region.safetyMarginMs = 0.0;
        region.decisionSource = "review_harness";
        region.classification = event.classification;
        region.trainingEligible = false;
        session.regions.push_back(region);
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(outDir), ec);
    const std::string inputStem = std::filesystem::u8path(argv[2]).stem().string();
    if (sessionPath.empty()) {
        sessionPath = (std::filesystem::u8path(outDir) /
            (inputStem + ".review.bcnstudio.json")).u8string();
    }
    if (!bc::studio::saveSession(sessionPath, session, &error)) {
        std::cerr << "sessione non salvata: " << error << "\n";
        return 1;
    }
    bc::studio::SessionData reloaded;
    if (!bc::studio::loadSession(sessionPath, &reloaded, &error)) {
        std::cerr << "sessione non rileggibile: " << error << "\n";
        return 1;
    }
    if (reloaded.events.size() != session.events.size()) {
        std::cerr << "sessione riletta con eventi diversi\n";
        return 1;
    }

    bc::AudioBuffer original;
    if (!bc::readWav(argv[2], original, &error)) {
        std::cerr << "lettura fallita: " << error << "\n";
        return 1;
    }
    bc::WavInfo info;
    if (!bc::probeWav(argv[2], info, &error)) {
        std::cerr << "formato non leggibile: " << error << "\n";
        return 1;
    }
    const bc::WavFormat format = info.isFloat ? bc::WavFormat::Float32 :
        (info.bitsPerSample <= 16 ? bc::WavFormat::Pcm16 : bc::WavFormat::Pcm24);
    const std::string stem = inputStem;
    const std::string baseName = (std::filesystem::u8path(outDir) / stem).u8string();

    const char* modes[] = {"attenuate", "cut", "halve"};
    const char* labels[] = {"Attenua", "Taglia", "Dimezza"};
    int failures = 0;
    std::ofstream summary(std::filesystem::u8path((std::filesystem::u8path(outDir) /
                           "review.json").u8string()), std::ios::binary);
    summary << "{\n  \"schema\": 1,\n  \"demo_only\": true,\n"
            << "  \"training_eligible\": false,\n"
            << "  \"source\": \"" << cliJsonEscape(argv[2]) << "\",\n"
            << "  \"reject_seconds\": " << std::setprecision(9) << rejectSeconds << ",\n"
            << "  \"session\": \"" << cliJsonEscape(sessionPath) << "\",\n  \"modes\": [\n";
    for (size_t m = 0; m < 3; ++m) {
        bc::studio::EditPlan plan;
        plan.sourcePath = reloaded.sourcePath;
        plan.sampleRate = analysis.sampleRate;
        plan.channels = analysis.channels;
        plan.settings = settings;
        plan.events = reloaded.events;
        bc::studio::EditMode mode = bc::studio::EditMode::Attenuate;
        bc::studio::editModeFromName(modes[m], &mode);
        for (auto& event : plan.events) {
            if (!event.approved) continue;
            event.mode = mode;
            event.keepFraction = keepFraction;
            event.fadeMs = settings.fadeMs;
        }
        plan.settings.detector = analysis.detector;
        const std::string output = baseName + "_reviewed_" + modes[m] + ".wav";
        const std::string planOut = output + ".plan.json";
        const std::string editOut = output + ".edit.json";
        bc::studio::EditResult result;
        if (!bc::studio::applyEditPlan(original, plan, &result, &error)) {
            std::cerr << "applicazione fallita per " << modes[m] << ": " << error << "\n";
            ++failures;
            continue;
        }
        if (!bc::studio::writeWavWithMetadata(argv[2], output, result.audio, format,
                                              &error, &result.wav)) {
            std::cerr << "scrittura fallita per " << modes[m] << ": " << error << "\n";
            ++failures;
            continue;
        }
        bc::studio::writeEditPlanJson(planOut, plan, &error);
        bc::studio::writeEditReportJson(editOut, plan, result, output, &error);

        bool rejectedRemoved = false;
        const int64_t rejectStart = reloaded.events[static_cast<size_t>(rejectIndex)].startSample;
        const int64_t rejectEnd = reloaded.events[static_cast<size_t>(rejectIndex)].endSample;
        const int64_t rejectInnerStart = rejectStart + (rejectEnd - rejectStart) / 4;
        const int64_t rejectInnerEnd = rejectEnd - (rejectEnd - rejectStart) / 4;
        bool removed = false;
        const int64_t mappedStart = result.map.mapOriginal(rejectInnerStart, &removed);
        rejectedRemoved = rejectedRemoved || removed;
        const int64_t mappedEnd = result.map.mapOriginal(rejectInnerEnd, &removed);
        rejectedRemoved = rejectedRemoved || removed;
        double maxDiff = 0.0;
        if (!rejectedRemoved && mappedEnd > mappedStart) {
            const int64_t count = std::min(mappedEnd - mappedStart,
                rejectInnerEnd - rejectInnerStart);
            for (int64_t i = 0; i < count; ++i) {
                const int64_t srcIndex = (rejectInnerStart + i) * original.channels;
                const int64_t dstIndex = (mappedStart + i) * result.audio.channels;
                for (int c = 0; c < original.channels; ++c) {
                    maxDiff = std::max(maxDiff, std::abs(
                        static_cast<double>(original.samples[static_cast<size_t>(srcIndex + c)]) -
                        static_cast<double>(result.audio.samples[static_cast<size_t>(dstIndex + c)])));
                }
            }
        }
        const bool preserved = !rejectedRemoved && maxDiff <= 1e-6;
        if (!preserved) ++failures;
        summary << "    {\"mode\": \"" << modes[m] << "\", "
                << "\"output\": \"" << cliJsonEscape(output) << "\", "
                << "\"plan\": \"" << cliJsonEscape(planOut) << "\", "
                << "\"edit_report\": \"" << cliJsonEscape(editOut) << "\", "
                << "\"rejected_preserved\": " << (preserved ? "true" : "false")
                << ", \"rejected_removed\": " << (rejectedRemoved ? "true" : "false")
                << ", \"max_abs_diff\": " << std::setprecision(9) << maxDiff
                << ", \"removed_samples\": " << result.removedSamples << "}";
        if (m + 1 < 3) summary << ",";
        summary << "\n";
        std::cout << modes[m] << ": " << output
                  << ", rifiutato conservato=" << (preserved ? "si" : "NO")
                  << ", diff max=" << maxDiff
                  << ", rimossi=" << result.removedSamples << "\n";
        (void)labels;
    }
    summary << "  ]\n}\n";
    if (failures > 0) {
        std::cerr << "revisione fallita: " << failures << " controlli\n";
        return 1;
    }
    std::cout << "REVIEW OK\n  sessione: " << sessionPath << "\n";
    return 0;
}

int commandBatchReview(int argc, char** argv) {
    if (argc < 4) {
        usage();
        return 2;
    }
    std::string inputDir = argv[2];
    std::string outDir;
    std::string plansDir;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--out-dir" && next(value)) outDir = value;
        else if (arg == "--plans" && next(value)) plansDir = value;
    }
    if (outDir.empty() || plansDir.empty()) {
        std::cerr << "--out-dir e --plans sono obbligatori\n";
        return 2;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(std::filesystem::u8path(inputDir), ec) ||
        !std::filesystem::is_directory(std::filesystem::u8path(plansDir), ec)) {
        std::cerr << "cartella input o piani non trovata\n";
        return 1;
    }
    std::filesystem::create_directories(std::filesystem::u8path(outDir), ec);
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(
             std::filesystem::u8path(inputDir), ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) &&
            it->path().extension().u8string() == ".wav") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    std::ofstream batch(std::filesystem::u8path(
        (std::filesystem::u8path(outDir) / "batch_review_report.json").u8string()),
        std::ios::binary);
    batch << "{\n  \"schema\": 1,\n  \"input\": \"" << cliJsonEscape(inputDir)
          << "\",\n  \"plans\": \"" << cliJsonEscape(plansDir)
          << "\",\n  \"files\": [\n";
    int done = 0, skipped = 0;
    for (size_t fi = 0; fi < files.size(); ++fi) {
        const auto& file = files[fi];
        const std::string stem = file.stem().string();
        std::filesystem::path planPath;
        const std::vector<std::string> candidates = {
            stem + ".plan.json",
            stem + "_reviewed_cut.wav.plan.json",
            stem + "_reviewed_halve.wav.plan.json",
            stem + "_reviewed_attenuate.wav.plan.json",
            stem + "_reviewed.wav.plan.json",
        };
        for (const std::string& candidate : candidates) {
            const auto p = std::filesystem::u8path(plansDir) / candidate;
            if (std::filesystem::exists(p, ec)) { planPath = p; break; }
        }
        if (planPath.empty()) {
            ++skipped;
            batch << "    {\"file\": \"" << cliJsonEscape(file.u8string())
                  << "\", \"status\": \"da_rivedere\", \"reason\": \"nessun piano revisionato\"}";
            if (fi + 1 < files.size()) batch << ",";
            batch << "\n";
            continue;
        }
        bc::studio::EditPlan plan;
        std::string error;
        if (!bc::studio::loadEditPlanJson(planPath.u8string(), &plan, &error)) {
            ++skipped;
            batch << "    {\"file\": \"" << cliJsonEscape(file.u8string())
                  << "\", \"status\": \"errore_piano\", \"error\": \""
                  << cliJsonEscape(error) << "\"}";
            if (fi + 1 < files.size()) batch << ",";
            batch << "\n";
            continue;
        }
        bc::AudioBuffer original;
        bc::WavInfo info;
        if (!bc::readWav(file.u8string(), original, &error) ||
            !bc::probeWav(file.u8string(), info, &error)) {
            ++skipped;
            batch << "    {\"file\": \"" << cliJsonEscape(file.u8string())
                  << "\", \"status\": \"errore_audio\", \"error\": \""
                  << cliJsonEscape(error) << "\"}";
            if (fi + 1 < files.size()) batch << ",";
            batch << "\n";
            continue;
        }
        bc::studio::EditResult result;
        if (!bc::studio::applyEditPlan(original, plan, &result, &error)) {
            ++skipped;
            batch << "    {\"file\": \"" << cliJsonEscape(file.u8string())
                  << "\", \"status\": \"errore_piano\", \"error\": \""
                  << cliJsonEscape(error) << "\"}";
            if (fi + 1 < files.size()) batch << ",";
            batch << "\n";
            continue;
        }
        const std::filesystem::path output = std::filesystem::u8path(outDir) /
            (stem + "_reviewed.wav");
        const bc::WavFormat format = info.isFloat ? bc::WavFormat::Float32 :
            (info.bitsPerSample <= 16 ? bc::WavFormat::Pcm16 : bc::WavFormat::Pcm24);
        if (!bc::studio::writeWavWithMetadata(file.u8string(), output.u8string(),
                                              result.audio, format, &error, &result.wav)) {
            ++skipped;
            batch << "    {\"file\": \"" << cliJsonEscape(file.u8string())
                  << "\", \"status\": \"errore_scrittura\", \"error\": \""
                  << cliJsonEscape(error) << "\"}";
            if (fi + 1 < files.size()) batch << ",";
            batch << "\n";
            continue;
        }
        bc::studio::writeEditReportJson(output.u8string() + ".edit.json", plan, result,
                                        output.u8string(), &error);
        ++done;
        batch << "    {\"file\": \"" << cliJsonEscape(file.u8string())
              << "\", \"status\": \"applicato\", \"output\": \""
              << cliJsonEscape(output.u8string()) << "\", \"plan\": \""
              << cliJsonEscape(planPath.u8string()) << "\"}";
        if (fi + 1 < files.size()) batch << ",";
        batch << "\n";
    }
    batch << "  ],\n  \"applied\": " << done << ",\n  \"skipped\": " << skipped << "\n}\n";
    std::cout << "batch revisionato: " << done << " applicati, " << skipped
              << " da rivedere/saltati\n";
    return 0;
}

static int runStudioCli(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string command = argv[1];
    if (command == "selftest") return selftest();
    if (command == "--version") { std::cout << "NL Breath Click Studio " << BC_STUDIO_VERSION << "\n"; return 0; }
    if (command == "session-selftest") return sessionSelftest();
    if (command == "review") return commandReview(argc, argv);
    if (command == "batch-review") return commandBatchReview(argc, argv);
    if (command == "plan") return commandPlan(argc, argv);
    if (command == "edit") return commandEdit(argc, argv);
    if (command == "slice") return commandSlice(argc, argv);
    if (command == "verify-source") return commandVerifySource(argc, argv);
    if (command == "profile-train" || command == "profile-filter" || command == "labels-export") return commandLearning(argc, argv);
    if (command == "capabilities") {
        std::cout << "{\"reaper_bridge\":1,\"direct_editing\":2,\"personal_profiles\":1,\"edit_plan_schema\":2}\n";
        return 0;
    }
    if (command == "probe") {
        if (argc < 3) { usage(); return 2; }
        bc::AudioBuffer audio;
        bc::AudioFileInfo info;
        std::string error;
        if (!bc::readAudio(argv[2], audio, &info, &error)) {
            std::cerr << "errore: " << error << "\n";
            return 1;
        }
        std::cout << "format=" << bc::audioFormatName(info.format)
                  << "\nchannels=" << info.channels
                  << "\nsample_rate=" << info.sampleRate
                  << "\nframes=" << info.frames
                  << "\nduration=" << info.durationSeconds
                  << "\nsamples=" << audio.samples.size()
                  << "\n";
        for (const auto& pair : info.tags) std::cout << "tag:" << pair.first << "=" << pair.second << "\n";
        return 0;
    }
    if (command == "analyze") return commandAnalyze(argc, argv);
    if (command == "process") return commandProcess(argc, argv);
    if (command == "batch") return commandBatch(argc, argv);
    usage();
    return 2;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return bc::runWithUtf8Arguments(argc, argv, runStudioCli); }
#else
int main(int argc, char** argv) { return runStudioCli(argc, argv); }
#endif
