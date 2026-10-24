// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  bc_cli — Strumento offline per l'editing vocale automatico
//
//  Comandi:
//    device     Mostra quale dispositivo (GPU/CPU) viene realmente usato
//    info       Intestazione e formato di un file WAV
//    analyze    Rileva respiri e click e produce un report
//    process    Rileva e corregge, salvando un nuovo WAV
//    selftest   Verifica il rilevamento su segnale sintetico con verita' a terra
// ============================================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/ai/vad_detector.h"
#include "core/detect/offline_analyzer.h"
#include "core/detect/candidates.h"
#include "core/detect/segment_features.h"
#include "core/dsp/silence_gate.h"
#include "core/model/model_profile.h"
#include "core/model/regions_json.h"
#include "core/dsp/breath_detector.h"
#include <fstream>
#include "core/edit/region_editor.h"
#include "core/edit/wav_io.h"
#include "shared/synthetic_audio.h"
#include "shared/utf8_console.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace bc;

constexpr const char* kProgramName = "bc_cli";
constexpr const char* kVersion = "3.0.0";

// ---------------------------------------------------------------------------
//  Opzioni
// ---------------------------------------------------------------------------
struct Options {
    std::string input;
    std::string output;
    std::string csvPath;
    std::string jsonPath;
    // Formato delle regioni (lo stesso che scrive il Python con l'AI):
    // e' l'unico che legge lo script Lua di REAPER.
    std::string regioniPath;
    // Usa il modello addestrato (breath_model.h) sui fiati trovati.
    bool useAi = false;
    // Usa la rete neurale compilata invece della regressione.
    bool useNet = false;
    // Fiati dai candidati larghi valutati dalla rete (implica --rete).
    bool proposteLarghe = false;
    // L'utente ha scelto la sensibilita' esplicitamente?
    bool sensitivitySet = false;
    // Soglia del modello addestrato. Piu bassa = piu fiati tenuti.
    float aiThreshold = 0.50f;
    std::string scoreDumpPath;
    // Profilo di rilevamento (cartella sotto models\)).
    std::string profile = "base";
    std::string ortDir;
    std::string device = "auto";
    std::string mode = "both";
    std::string format = "pcm16";

    // Silenzio (replica il Dynamic split di REAPER).
    float silenceThresholdDb = -48.0f;
    float silenceHysteresisDb = 6.0f;
    int silenceMinMs = 200;

    DetectorSettings detector;
    EditSettings edit;

    bool listDetections = true;
    bool quiet = false;
    // Il gate vocale e' OPT-IN.
    //
    // Prima era il contrario e il modello Silero si caricava sempre: 13 MB di
    // ONNX Runtime per rispondere a una domanda ("e' voce?") a cui la salienza
    // armonica gia' risponde, calcolata per ogni frame e gia' usata come
    // ripiego. Un modello in meno significa nessuna DLL da caricare, nessun
    // provider da scegliere, e niente che possa funzionare su una macchina e
    // non su un'altra.
    bool repairClicks = true;
    bool useDeviceOverride = false;

    // Impostati dal parsing.
    bool valid = true;
    std::string parseError;
};

void printUsage() {
    std::cout <<
R"(bc_cli — editing vocale automatico (respiri e click)

USO
  bc_cli <comando> [file] [opzioni]

COMANDI
  device                          Mostra il dispositivo di calcolo realmente usato
  info <file.wav>                 Intestazione e formato del file
  analyze <file.wav>              Rileva respiri e click e stampa un report
  candidati <file.wav> --json <f> Esporta i candidati larghi con le 14 feature
                                  (alimenta il dataset di addestramento)
  process <in.wav> <out.wav>      Rileva, corregge e salva un nuovo file
  gen-test [out.wav]              Genera un WAV di prova con respiri e click noti
  selftest                        Verifica il rilevamento su segnale sintetico

OPZIONI PRINCIPALI
  --device auto|gpu|cpu    Dispositivo per i modelli ONNX  (default: auto)
  --mode both|breath|click Cosa rilevare                   (default: both)
  --rete                   Rete neurale compilata (filtra i candidati del DSP)
  --proposte-larghe        Con --rete: candidati larghi, non solo quelli del DSP
  --sensitivity <0..1>     Sensibilita' comune              (default: 0.5)
  --breath-threshold <0..1>   Soglia respiri                (default: 0.55)
  --click-threshold <0..1>    Soglia click                  (default: 0.55)
  --breath-reduction <dB>  Attenuazione respiri             (default: -18)
  --click-reduction <dB>   Attenuazione click               (default: -60)
  --no-repair              Attenua i click invece di ripararli
  --vad                    Usa il gate vocale Silero (predefinito: no)
  --model <file.onnx>      Modello VAD alternativo
  --csv <file.csv>         Esporta i rilevamenti in CSV
  --json <file.json>       Esporta il report completo in JSON
  --dump-scores <file.csv> Esporta i punteggi per frame (taratura delle soglie)
  --format pcm16|pcm24|float32   Formato di uscita       (default: pcm16)
  --quiet                  Riduce l'output
  -h, --help               Questo testo

ESEMPI
  bc_cli gen-test prova.wav
  bc_cli analyze take.wav --csv rilevamenti.csv
  bc_cli process take.wav take_pulito.wav --breath-reduction -22 --mode both
  bc_cli selftest
)";
}

bool parseFloat(const std::string& text, float& out) {
    try {
        size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseArgs(int argc, char** argv, Options& options, std::string& command) {
    if (argc < 2) return false;
    command = argv[1];

    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];

        auto nextValue = [&](std::string& target) -> bool {
            if (i + 1 >= argc) return false;
            target = argv[++i];
            return true;
        };

        if (argument == "-h" || argument == "--help") {
            return false;
        } else if (argument == "--device") {
            if (!nextValue(options.device)) { options.parseError = "--device richiede un valore"; return false; }
            options.useDeviceOverride = true;
        } else if (argument == "--mode") {
            if (!nextValue(options.mode)) { options.parseError = "--mode richiede un valore"; return false; }
        } else if (argument == "--format") {
            if (!nextValue(options.format)) { options.parseError = "--format richiede un valore"; return false; }
        } else if (argument == "--ort-dir") {
            if (!nextValue(options.ortDir)) { options.parseError = "--ort-dir richiede un percorso"; return false; }
        } else if (argument == "--csv") {
            if (!nextValue(options.csvPath)) { options.parseError = "--csv richiede un percorso"; return false; }
        } else if (argument == "--rete") {
            options.useNet = true;
            options.useAi = true;   // la rete e' un modello: --ai implicito
        } else if (argument == "--proposte-larghe") {
            options.proposteLarghe = true;
            options.useNet = true;   // i candidati larghi hanno senso solo con la rete
            options.useAi = true;
        } else if (argument == "--ai") {
            options.useAi = true;
        } else if (argument == "--ai-soglia") {
            std::string value;
            if (!nextValue(value) || !parseFloat(value, options.aiThreshold)) {
                options.parseError = "--ai-soglia richiede un valore 0..1"; return false;
            }
            options.aiThreshold = std::max(0.0f, std::min(1.0f, options.aiThreshold));
        } else if (argument == "--regioni") {
            if (!nextValue(options.regioniPath)) { options.parseError = "--regioni richiede un percorso"; return false; }
        } else if (argument == "--json") {
            if (!nextValue(options.jsonPath)) { options.parseError = "--json richiede un percorso"; return false; }
        } else if (argument == "--dump-scores") {
            if (!nextValue(options.scoreDumpPath)) { options.parseError = "--dump-scores richiede un percorso"; return false; }
        } else if (argument == "--profile") {
            std::string value;
            if (!nextValue(value)) { options.parseError = "--profile richiede un nome"; return false; }
            options.profile = value;
        } else if (argument == "--sensitivity") {
            std::string value;
            if (!nextValue(value)) { options.parseError = "--sensitivity richiede un valore"; return false; }
            float parsed = 0.0f;
            if (!parseFloat(value, parsed)) { options.parseError = "valore non valido: " + value; return false; }
            options.detector.breathSensitivity = parsed;
            options.detector.clickSensitivity = parsed;
            options.sensitivitySet = true;
        } else if (argument == "--breath-threshold") {
            std::string value;
            if (!nextValue(value) || !parseFloat(value, options.detector.breathThreshold)) {
                options.parseError = "--breath-threshold richiede un valore 0..1"; return false;
            }
        } else if (argument == "--click-threshold") {
            std::string value;
            if (!nextValue(value) || !parseFloat(value, options.detector.clickThreshold)) {
                options.parseError = "--click-threshold richiede un valore 0..1"; return false;
            }
        } else if (argument == "--breath-reduction") {
            std::string value;
            if (!nextValue(value) || !parseFloat(value, options.edit.breathReductionDb)) {
                options.parseError = "--breath-reduction richiede un valore in dB"; return false;
            }
        } else if (argument == "--click-reduction") {
            std::string value;
            if (!nextValue(value) || !parseFloat(value, options.edit.clickReductionDb)) {
                options.parseError = "--click-reduction richiede un valore in dB"; return false;
            }
        } else if (argument == "--no-repair") {
            options.repairClicks = false;
        } else if (argument == "--vad" || argument == "--no-vad") {
            // Accettate e IGNORATE.
            //
            // Servivano a scegliere il rilevatore ONNX, che e' stato rimosso:
            // non c'e' piu' nessun modello da attivare. Si accettano ancora
            // perche' gli script e i comandi gia' scritti non si rompano, ma
            // non fanno niente. Prima o poi spariscono.
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (!argument.empty() && argument[0] == '-') {
            options.parseError = "opzione sconosciuta: " + argument;
            return false;
        } else if (options.input.empty()) {
            options.input = argument;
        } else if (options.output.empty()) {
            options.output = argument;
        } else {
            options.parseError = "argomento inatteso: " + argument;
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
//  Utility
// ---------------------------------------------------------------------------
std::string executableDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    path = (slash == std::wstring::npos) ? L"." : path.substr(0, slash);

    const int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string result(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, result.data(), needed, nullptr, nullptr);
    return result;
#else
    return ".";
#endif
}

std::string joinPath(const std::string& directory, const std::string& leaf) {
    if (directory.empty()) return leaf;
    const char last = directory.back();
    if (last == '\\' || last == '/') return directory + leaf;
#ifdef _WIN32
    return directory + "\\" + leaf;
#else
    return directory + "/" + leaf;
#endif
}

std::string formatSeconds(double seconds) {
    const int totalMs = static_cast<int>(std::llround(seconds * 1000.0));
    const int hours = totalMs / 3600000;
    const int minutes = (totalMs / 60000) % 60;
    const int secs = (totalMs / 1000) % 60;
    const int ms = totalMs % 1000;

    char buffer[64];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d.%03d", hours, minutes, secs, ms);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d", minutes, secs, ms);
    }
    return buffer;
}

void applyModeToSettings(const Options& options, DetectorSettings& settings) {
    if (options.mode == "breath") {
        settings.detectBreath = true;
        settings.detectClicks = false;
    } else if (options.mode == "click") {
        settings.detectBreath = false;
        settings.detectClicks = true;
    OfflineAnalyzer analyzer;
    } else {
        settings.detectBreath = true;
        settings.detectClicks = true;
    }
}

bool parseDeviceOption(const Options& options, DeviceRequest& request) {
    if (!parseDeviceRequest(options.device, request)) {
        std::cerr << "Valore --device non riconosciuto: " << options.device
                  << " (attesi: auto, gpu, cpu)\n";
        return false;
    }
    return true;
}

// Cartella dei profili accanto all'eseguibile.
std::string cliModelsRoot() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};

    std::wstring full(path, length);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    full = full.substr(0, slash) + L"\\models";

    const int needed = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string risultato(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, risultato.data(), needed,
                        nullptr, nullptr);
    return risultato;
}

// Applica il profilo scelto con --profile.
//
// Un profilo che non esiste non e' un errore: si avvisa e si continua con i
// valori predefiniti. Meglio un risultato con un avviso che nessun risultato.
void applyProfileToSettingsFromCli(const Options& options, DetectorSettings& settings) {
    if (options.profile.empty() || options.profile == "base") {
        return;   // il base sono i valori predefiniti: niente da fare
    }

    ModelRegistry registro;
    std::string avviso;
    const std::string radice = cliModelsRoot();
    if (radice.empty() || !registro.scan(radice, &avviso)) {
        std::cerr << "Avviso: cartella dei profili non trovata, uso i valori predefiniti.\n";
        return;
    }

    const ModelProfile* profilo = registro.find(options.profile);
    if (!profilo) {
        std::cerr << "Avviso: profilo '" << options.profile
                  << "' non trovato, uso i valori predefiniti.\n";
        return;
    }

    applyProfile(*profilo, settings);
    std::cout << "Profilo     : " << profilo->name
              << " (" << profilo->materialType << ")\n";
}

DetectorSettings buildSettings(const Options& options) {
    DetectorSettings settings = options.detector;
    settings.breathModelThreshold = options.aiThreshold;
    settings.proposteLarghe = options.proposteLarghe;

    // ------------------------------------------------------------------
    //  La rete lavora meglio su un DSP generoso.
    //
    //  La rete non TROVA i fiati: sceglie fra quelli che il DSP propone.
    //  Se il DSP e' severo, i fiati difficili non arrivano mai alla rete e
    //  lei non puo' fare niente, per brava che sia.
    //
    //  Misurato sui 4 file con verita' a terra (attesi 3, 12, 6, 9):
    //      DSP severo (0.8)            errore 10
    //      rete su DSP severo          errore  8
    //      rete su DSP generoso (1.0)  errore  5
    //
    //  Quindi --rete alza la sensibilita' da solo, a meno che l'utente non
    //  l'abbia gia' scelta esplicitamente.
    // ------------------------------------------------------------------
    if (options.useNet && !options.sensitivitySet) {
        settings.breathSensitivity = 1.0f;
        settings.clickSensitivity = 1.0f;
    }

    // Il profilo si applica PRIMA di tutto il resto: le singole opzioni
    // da riga di comando devono poterlo sovrascrivere, non il contrario.
    applyProfileToSettingsFromCli(options, settings);

    // Il profilo definisce il modello, ma una richiesta esplicita da riga di
    // comando non viene mai spenta: --ai e --rete valgono anche se il
    // profilo scelto non dichiara pesi.
    settings.useBreathModel = settings.useBreathModel || options.useAi;
    settings.usaReteNeurale = settings.usaReteNeurale || options.useNet;
    settings.proposteLarghe = settings.proposteLarghe || options.proposteLarghe;
    applyModeToSettings(options, settings);
    if (!settings.detectBreath) {
        settings.requirePauseForBreath = false;
    }
    // I punteggi si copiano solo se servono davvero: sono decine di migliaia di
    // valori per file.
    settings.includeScores = !options.scoreDumpPath.empty();

    DeviceRequest request = DeviceRequest::Auto;
    parseDeviceOption(options, request);
    settings.device = request;
    return settings;
}

EditSettings buildEditSettings(const Options& options) {
    EditSettings settings = options.edit;
    settings.repairClicks = options.repairClicks;
    return settings;
}

bool writeCsv(const std::string& path,
              const AnalysisResult& result,
              std::string* error) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        if (error) *error = "Impossibile creare il CSV: " + path;
        return false;
    }
    // BOM UTF-8: Excel apre correttamente gli accenti.
    std::fputs("\xEF\xBB\xBF", file);
    std::fputs("tipo,inizio_s,fine_s,inizio_campioni,fine_campioni,durata_ms,"
               "punteggio_picco,punteggio_medio\n", file);

    for (const Detection& detection : result.detections) {
        const double startSeconds = detection.startSeconds(result.sampleRate);
        const double endSeconds = detection.endSeconds(result.sampleRate);
        const double durationMs = 1000.0 * (endSeconds - startSeconds);
        std::fprintf(file, "%s,%.6f,%.6f,%lld,%lld,%.3f,%.4f,%.4f\n",
                     detectionKindName(detection.kind),
                     startSeconds,
                     endSeconds,
                     static_cast<long long>(detection.startSample),
                     static_cast<long long>(detection.endSample),
                     durationMs,
                     static_cast<double>(detection.peakScore),
                     static_cast<double>(detection.meanScore));
    }

    std::fclose(file);
    if (error) error->clear();
    return true;
}

bool writeJson(const std::string& path,
               const std::string& inputFile,
               const AnalysisResult& result,
               const std::string& deviceLabel,
               std::string* error) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        if (error) *error = "Impossibile creare il JSON: " + path;
        return false;
    }

    auto escape = [](const std::string& text) {
        std::string output;
        for (char c : text) {
            if (c == '"' || c == '\\') {
                output.push_back('\\');
                output.push_back(c);
            } else if (c == '\n') {
                output += "\\n";
            } else {
                output.push_back(c);
            }
        }
        return output;
    };

    std::fprintf(file, "{\n");
    std::fprintf(file, "  \"versione\": \"%s\",\n", kVersion);
    std::fprintf(file, "  \"file\": \"%s\",\n", escape(inputFile).c_str());
    std::fprintf(file, "  \"sample_rate\": %.0f,\n", result.sampleRate);
    std::fprintf(file, "  \"campioni\": %lld,\n", static_cast<long long>(result.totalSamples));
    std::fprintf(file, "  \"durata_s\": %.6f,\n",
                 result.sampleRate > 0.0
                     ? static_cast<double>(result.totalSamples) / result.sampleRate
                     : 0.0);
    std::fprintf(file, "  \"dispositivo\": \"%s\",\n", escape(deviceLabel).c_str());
    std::fprintf(file, "  \"vad_attivo\": %s,\n", result.vadActive ? "true" : "false");
    std::fprintf(file, "  \"vad_stato\": \"%s\",\n", escape(result.vadStatus).c_str());
    std::fprintf(file, "  \"tempo_analisi_s\": %.3f,\n", result.analysisSeconds);
    std::fprintf(file, "  \"finestre\": %zu,\n", result.framesAnalyzed);
    std::fprintf(file, "  \"fondo_rumore_db\": %.2f,\n", static_cast<double>(result.noiseFloorDb));
    std::fprintf(file, "  \"rilevamenti\": [\n");

    for (size_t i = 0; i < result.detections.size(); ++i) {
        const Detection& detection = result.detections[i];
        std::fprintf(file,
                     "    {\"tipo\": \"%s\", \"inizio_s\": %.6f, \"fine_s\": %.6f, "
                     "\"durata_ms\": %.3f, \"picco\": %.4f, \"medio\": %.4f}%s\n",
                     detectionKindName(detection.kind),
                     detection.startSeconds(result.sampleRate),
                     detection.endSeconds(result.sampleRate),
                     1000.0 * (detection.endSeconds(result.sampleRate) -
                               detection.startSeconds(result.sampleRate)),
                     static_cast<double>(detection.peakScore),
                     static_cast<double>(detection.meanScore),
                     (i + 1 < result.detections.size()) ? "," : "");
    }

    std::fprintf(file, "  ]\n}\n");
    std::fclose(file);
    if (error) error->clear();
    return true;
}

bool writeScoreDump(const std::string& path, const AnalysisResult& result, std::string* error) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        if (error) *error = "Impossibile creare il file dei punteggi: " + path;
        return false;
    }

    std::fputs("sezione,indice,tempo_s,punteggio,armonicita,flatness,livello_db,centroide_hz,zcr,banda_bassa,banda_media,banda_alta\n", file);

    for (size_t i = 0; i < result.breathScores.size(); ++i) {
        const double time = result.hopSize > 0
            ? static_cast<double>(i * static_cast<size_t>(result.hopSize)) / result.sampleRate
            : 0.0;
        const double harmonicity = i < result.frameHarmonicity.size() ? result.frameHarmonicity[i] : 0.0;
        const double flatness = i < result.frameFlatness.size() ? result.frameFlatness[i] : 0.0;
        const double level = i < result.frameLevelDb.size() ? result.frameLevelDb[i] : 0.0;
        const double cent = i < result.frameCentroid.size() ? result.frameCentroid[i] : 0.0;
        const double zc = i < result.frameZcr.size() ? result.frameZcr[i] : 0.0;
        const double lb = i < result.frameLowBand.size() ? result.frameLowBand[i] : 0.0;
        const double mb = i < result.frameMidBand.size() ? result.frameMidBand[i] : 0.0;
        const double hb = i < result.frameHighBand.size() ? result.frameHighBand[i] : 0.0;
        std::fprintf(file, "respiro,%zu,%.6f,%.6f,%.3f,%.3f,%.1f,%.0f,%.4f,%.3f,%.3f,%.3f\n", i, time,
        static_cast<double>(result.breathScores[i]), harmonicity, flatness, level,
        cent, zc, lb, mb, hb);
    }
    for (size_t i = 0; i < result.clickScores.size(); ++i) {
        const double time = result.clickSubWindow > 0
            ? static_cast<double>(i * static_cast<size_t>(result.clickSubWindow)) / result.sampleRate
            : 0.0;
        const double ratio = i < result.clickRatios.size() ? result.clickRatios[i] : 0.0;
        const double narrow = i < result.clickNarrowness.size() ? result.clickNarrowness[i] : 0.0;
        std::fprintf(file, "click,%zu,%.6f,%.6f,%.3f,%.3f\n", i, time,
                     static_cast<double>(result.clickScores[i]), ratio, narrow);    }
    for (size_t i = 0; i < result.voiceProbabilities.size(); ++i) {
        const double time = result.hopSize > 0
            ? static_cast<double>(i * static_cast<size_t>(result.hopSize)) / result.sampleRate
            : 0.0;
        std::fprintf(file, "voce,%zu,%.6f,%.6f,,\n", i, time,
                     static_cast<double>(result.voiceProbabilities[i]));
    }

    std::fclose(file);
    if (error) error->clear();
    return true;
}

// Genera un file WAV di prova con respiri e click in posizioni note.
int commandGenerateTest(const Options& options) {
    const std::string path = options.input.empty() ? std::string("test_respiri_click.wav")
                                                   : options.input;

    synthetic::TakeOptions takeOptions;
    const synthetic::TestTake take = synthetic::makeVocalTake(takeOptions);

    AudioBuffer buffer;
    buffer.channels = 1;
    buffer.sampleRate = static_cast<int>(take.sampleRate);
    buffer.samples = take.samples;

    std::string error;
    WavFormat format = WavFormat::Pcm24;
    if (options.format == "pcm16") format = WavFormat::Pcm16;
    else if (options.format == "float32") format = WavFormat::Float32;

    if (!writeWav(path, buffer, format, &error)) {
        std::cerr << "Errore: " << error << "\n";
        return 1;
    }

    std::cout << "File di prova generato: " << path << "\n";
    std::cout << "  Durata   : " << std::fixed << std::setprecision(2)
              << take.durationSeconds() << " s a " << buffer.sampleRate << " Hz\n";
    std::cout << "  Contenuto: " << take.countOf(DetectionKind::Breath) << " respiri, "
              << take.countOf(DetectionKind::Click) << " click\n\n";
    std::cout << "Posizioni attese (per verificare a orecchio o con analyze):\n";
    for (const synthetic::GroundTruthEvent& event : take.events) {
        std::cout << "  " << std::setw(9) << std::left
                  << (event.kind == DetectionKind::Breath ? "Respiro" : "Click")
                  << formatSeconds(static_cast<double>(event.startSample) / take.sampleRate)
                  << "\n";
    }
    std::cout << "\nSuggerimento: importare il file in REAPER e ascoltare con il plugin "
                 "inserito sulla traccia.\n";
    return 0;
}

void printAnalysisReport(const std::string& fileName,
                         const AudioBuffer& buffer,
                         const AnalysisResult& result,
                         const std::string& deviceLabel) {
    const bool stereo = buffer.channels > 1;
    std::cout << "\nFile             : " << fileName << "\n";
    std::cout << "Canali           : " << buffer.channels
              << (stereo ? " (analisi sul mixdown mono)" : "") << "\n";
    std::cout << "Sample rate      : " << buffer.sampleRate << " Hz\n";
    std::cout << "Durata           : " << std::fixed << std::setprecision(3)
              << buffer.durationSeconds() << " s\n";
    std::cout << "Dispositivo VAD  : " << deviceLabel << "\n";
    std::cout << "Gate vocale      : " << result.vadStatus << "\n";
    std::cout << "Finestre         : " << result.framesAnalyzed << "\n";
    std::cout << "Fondo rumore     : " << std::setprecision(1)
              << static_cast<double>(result.noiseFloorDb) << " dB\n";
    std::cout << "Tempo analisi    : " << std::setprecision(3) << result.analysisSeconds << " s";

    if (result.analysisSeconds > 0.001 && buffer.durationSeconds() > 0.0) {
        const double speed = buffer.durationSeconds() / result.analysisSeconds;
        std::cout << "  (" << std::setprecision(1) << speed << "x realtime)";
    }
    std::cout << "\n";

    const size_t breathCount = result.countOf(DetectionKind::Breath);
    const size_t clickCount = result.countOf(DetectionKind::Click);

    std::cout << "\nRILEVAMENTI\n";
    if (result.detections.empty()) {
        std::cout << "  Nessun respiro o click rilevato.\n";
    } else {
        std::cout << "   #  Tipo      Inizio        Fine          Durata    Confidenza\n";
        std::cout << "  --- --------- ------------- ------------- --------- ----------\n";
        for (size_t i = 0; i < result.detections.size(); ++i) {
            const Detection& detection = result.detections[i];
            const double startSeconds = detection.startSeconds(result.sampleRate);
            const double endSeconds = detection.endSeconds(result.sampleRate);
            char row[256];
            std::snprintf(row, sizeof(row), "  %3zu %-9s %-13s %-13s %6.0f ms      %.2f",
                          i + 1,
                          detection.kind == DetectionKind::Breath ? "Respiro" : "Click",
                          formatSeconds(startSeconds).c_str(),
                          formatSeconds(endSeconds).c_str(),
                          1000.0 * (endSeconds - startSeconds),
                          static_cast<double>(detection.peakScore));
            std::cout << row << "\n";
        }
    }

    std::cout << "\nTotali: " << breathCount << " respiri, " << clickCount << " click\n";
}

// ---------------------------------------------------------------------------
//  Comandi
// ---------------------------------------------------------------------------
int commandInfo(const Options& options) {
    if (options.input.empty()) {
        std::cerr << "Uso: bc_cli info <file.wav>\n";
        return 2;
    }

    WavInfo info;
    std::string error;
    if (!probeWav(options.input, info, &error)) {
        std::cerr << "Errore: " << error << "\n";
        return 1;
    }

    std::cout << "File        : " << options.input << "\n";
    std::cout << "Canali      : " << info.channels << "\n";
    std::cout << "Sample rate : " << info.sampleRate << " Hz\n";
    std::cout << "Profondita' : " << info.bitsPerSample << " bit "
              << (info.isFloat ? "(float)" : "(PCM)") << "\n";
    std::cout << "Frame       : " << info.frames << "\n";
    if (info.sampleRate > 0) {
        std::cout << "Durata      : " << std::fixed << std::setprecision(3)
                  << static_cast<double>(info.frames) / info.sampleRate << " s\n";
    }
    return 0;
}

int commandAnalyze(const Options& options) {
    if (options.input.empty()) {
        std::cerr << "Uso: bc_cli analyze <file.wav>\n";
        return 2;
    }

    AudioBuffer buffer;
    std::string error;
    if (!readWav(options.input, buffer, &error)) {
        std::cerr << "Errore di lettura: " << error << "\n";
        return 1;
    }

    const std::vector<float> mono = buffer.toMono();
    OfflineAnalyzer analyzer;


    if (!options.quiet) {
        std::cout << "Analisi in corso";
        std::cout << "...\n";
    }

    const DetectorSettings settings = buildSettings(options);
    const AnalysisResult result =
        analyzer.analyze(mono.data(), mono.size(), static_cast<double>(buffer.sampleRate), settings);

    if (!result.error.empty()) {
        std::cerr << "Errore: " << result.error << "\n";
        return 1;
    }

    const std::string deviceLabel = result.vadActive
        ? ("usato per il VAD: " + result.vadDevice.shortLabel())
        : "solo CPU (nessun modello ONNX attivo)";

    // --quiet significa "non stampare il report": lo usa lo script Lua, che
    // non legge lo stdout e non deve riempire la console di REAPER (era uno
    // dei modi in cui l'analisi poteva sembrare bloccata).
    if (!options.quiet) {
        printAnalysisReport(options.input, buffer, result, deviceLabel);
    }

    if (!options.csvPath.empty()) {
        if (writeCsv(options.csvPath, result, &error)) {
            if (!options.quiet) {
                std::cout << "\nCSV salvato      : " << options.csvPath << "\n";
            }
        } else {
            std::cerr << "Avviso: " << error << "\n";
        }
    }

    if (!options.scoreDumpPath.empty()) {
        if (writeScoreDump(options.scoreDumpPath, result, &error)) {
            if (!options.quiet) {
                std::cout << "Punteggi salvati : " << options.scoreDumpPath << "\n";
            }
        } else {
            std::cerr << "Avviso: " << error << "\n";
        }
    }

    if (!options.jsonPath.empty()) {
        if (writeJson(options.jsonPath, options.input, result, deviceLabel, &error)) {
            if (!options.quiet) {
                std::cout << "JSON salvato     : " << options.jsonPath << "\n";
            }
        } else {
            std::cerr << "Avviso: " << error << "\n";
        }
    }

    // ------------------------------------------------------------------
    //  Formato "regioni": lo stesso che produce il Python con l'AI.
    //
    //  Serve allo script Lua di REAPER, che deve leggere UN formato solo
    //  indipendentemente da quale rilevatore ha girato. Cosi' il DSP e la
    //  rete neurale sono intercambiabili dal punto di vista di chi consuma.
    // ------------------------------------------------------------------
    if (!options.regioniPath.empty()) {
        AiRegions regioni;
        regioni.version = 1;
        regioni.sourceFile = options.input;
        regioni.sampleRate = static_cast<int>(result.sampleRate);
        regioni.samples = result.totalSamples;
        regioni.durationSeconds = result.sampleRate > 0.0
            ? static_cast<double>(result.totalSamples) / result.sampleRate
            : 0.0;
        regioni.source = "dsp";

        for (const Detection& d : result.detections) {
            AiRegion r;
            r.kind = (d.kind == DetectionKind::Click) ? AiRegion::Kind::Click
                                                      : AiRegion::Kind::Breath;
            r.startSeconds = d.startSeconds(result.sampleRate);
            r.endSeconds = d.endSeconds(result.sampleRate);
            r.confidence = d.peakScore;
            regioni.regions.push_back(r);
        }

        if (writeAiRegions(options.regioniPath, regioni, &error)) {
            if (!options.quiet) {
                std::cout << "Regioni salvate  : " << options.regioniPath
                          << "  (" << regioni.regions.size() << ")\n";
            }
        } else {
            std::cerr << "Avviso: " << error << "\n";
        }
    }

    return 0;
}

int commandProcess(const Options& options) {
    if (options.input.empty() || options.output.empty()) {
        std::cerr << "Uso: bc_cli process <in.wav> <out.wav>\n";
        return 2;
    }

    AudioBuffer buffer;
    std::string error;
    if (!readWav(options.input, buffer, &error)) {
        std::cerr << "Errore di lettura: " << error << "\n";
        return 1;
    }

    const std::vector<float> mono = buffer.toMono();
    OfflineAnalyzer analyzer;


    if (!options.quiet) {
        std::cout << "Analisi di " << options.input << "...\n";
    }

    const DetectorSettings settings = buildSettings(options);
    const AnalysisResult result =
        analyzer.analyze(mono.data(), mono.size(), static_cast<double>(buffer.sampleRate), settings);

    if (!result.error.empty()) {
        std::cerr << "Errore: " << result.error << "\n";
        return 1;
    }

    EditSettings editSettings = options.edit;
    editSettings.repairClicks = options.repairClicks;

    const EditReport report = RegionEditor::apply(buffer.samples,
                                                  buffer.channels,
                                                  static_cast<double>(buffer.sampleRate),
                                                  result.detections,
                                                  editSettings);

    WavFormat format = WavFormat::Pcm16;
    if (options.format == "pcm24") {
        format = WavFormat::Pcm24;
    } else if (options.format == "float32") {
        format = WavFormat::Float32;
    }

    if (!writeWav(options.output, buffer, format, &error)) {
        std::cerr << "Errore di scrittura: " << error << "\n";
        return 1;
    }

    std::cout << "\nCorrezione applicata\n";
    std::cout << "====================\n";
    std::cout << "Respiri rilevati        : " << result.countOf(DetectionKind::Breath) << "\n";
    std::cout << "Click rilevati          : " << result.countOf(DetectionKind::Click) << "\n";
    std::cout << "Respiro attenuati       : " << report.breathRegions << "\n";
    std::cout << "Click riparati          : " << report.clickRegionsRepaired << "\n";
    std::cout << "Click attenuati         : " << report.clickRegionsAttenuated << "\n";
    std::cout << "Attenuazione massima    : " << std::fixed << std::setprecision(1)
              << report.maxGainReductionDb << " dB\n";
    std::cout << "Campioni modificati     : " << report.samplesModified << "\n";
    std::cout << "File di uscita          : " << options.output << "\n";

    if (!options.csvPath.empty()) {
        if (writeCsv(options.csvPath, result, &error)) {
            std::cout << "CSV rilevamenti         : " << options.csvPath << "\n";
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
//  Selftest: verifica oggettiva su segnale sintetico
// ---------------------------------------------------------------------------
struct MatchOutcome {
    size_t matched = 0;
    size_t missed = 0;
    size_t falsePositives = 0;
    double worstStartErrorMs = 0.0;
};

MatchOutcome matchDetections(const std::vector<synthetic::GroundTruthEvent>& truth,
                             const std::vector<Detection>& detections,
                             DetectionKind kind,
                             double sampleRate,
                             double toleranceMs) {
    MatchOutcome outcome;
    std::vector<bool> used(detections.size(), false);
    const double toleranceSamples = toleranceMs * sampleRate / 1000.0;

    for (const synthetic::GroundTruthEvent& event : truth) {
        if (event.kind != kind) continue;

        bool found = false;
        for (size_t i = 0; i < detections.size(); ++i) {
            if (used[i] || detections[i].kind != kind) continue;

            const double center = 0.5 * static_cast<double>(event.startSample + event.endSample);
            const double detectedCenter =
                0.5 * static_cast<double>(detections[i].startSample + detections[i].endSample);
            const double delta = std::abs(center - detectedCenter);

            if (delta <= toleranceSamples) {
                used[i] = true;
                found = true;
                outcome.worstStartErrorMs =
                    std::max(outcome.worstStartErrorMs, 1000.0 * delta / sampleRate);
                break;
            }
        }

        if (found) {
            ++outcome.matched;
        } else {
            ++outcome.missed;
        }
    }

    for (size_t i = 0; i < detections.size(); ++i) {
        if (!used[i] && detections[i].kind == kind) {
            ++outcome.falsePositives;
        }
    }

    return outcome;
}

int commandSelftest(const Options& options) {
    std::cout << "Selftest del rilevamento\n";
    std::cout << "========================\n\n";

    DetectorSettings settings = buildSettings(options);
    settings.detectBreath = true;
    settings.detectClicks = true;
    OfflineAnalyzer analyzer;

    // Nessun gate vocale: il rilevatore e' DSP puro, senza modelli esterni.
    // Prima qui si apriva un modello ONNX; ora non c'e' piu' niente da aprire.

    int failures = 0;

    // ---------------- 1. materiale con eventi noti -------------------------
    const synthetic::TestTake take = synthetic::makeVocalTake();

    const AnalysisResult result = analyzer.analyze(take.samples.data(),
                                                   take.samples.size(),
                                                   take.sampleRate,
                                                   settings);

    if (!result.error.empty()) {
        std::cerr << "ERRORE: " << result.error << "\n";
        return 1;
    }

    std::cout << "Prova 1 — materiale vocale sintetico\n";
    std::cout << "  Durata            : " << std::fixed << std::setprecision(2)
              << take.durationSeconds() << " s\n";
    std::cout << "  Dispositivo VAD   : "
              << (result.vadActive ? result.vadDevice.shortLabel() : std::string("CPU"))
              << "\n";
    std::cout << "  Tempo analisi     : " << std::setprecision(3) << result.analysisSeconds << " s\n";

    const size_t truthBreaths = take.countOf(DetectionKind::Breath);
    const size_t truthClicks = take.countOf(DetectionKind::Click);

    const MatchOutcome breathOutcome =
        matchDetections(take.events, result.detections, DetectionKind::Breath, take.sampleRate, 120.0);
    const MatchOutcome clickOutcome =
        matchDetections(take.events, result.detections, DetectionKind::Click, take.sampleRate, 30.0);

    std::cout << "  Respiri attesi    : " << truthBreaths
              << "  trovati: " << breathOutcome.matched
              << "  mancati: " << breathOutcome.missed
              << "  falsi positivi: " << breathOutcome.falsePositives << "\n";
    std::cout << "  Click attesi      : " << truthClicks
              << "  trovati: " << clickOutcome.matched
              << "  mancati: " << clickOutcome.missed
              << "  falsi positivi: " << clickOutcome.falsePositives << "\n";

    const double breathRecall = truthBreaths > 0
        ? static_cast<double>(breathOutcome.matched) / static_cast<double>(truthBreaths)
        : 1.0;
    const double clickRecall = truthClicks > 0
        ? static_cast<double>(clickOutcome.matched) / static_cast<double>(truthClicks)
        : 1.0;

    std::cout << "  Richiamo respiri  : " << std::setprecision(0) << breathRecall * 100.0 << " %\n";
    std::cout << "  Richiamo click    : " << clickRecall * 100.0 << " %\n";

    // Soglie di accettazione: il 100% e' l'obiettivo, ma si tollera un evento
    // mancato per non rendere il test dipendente da un singolo campione.
    if (breathRecall < 0.99) {
        std::cout << "  ESITO             : FALLITO (richiamo respiri < 99 %)\n";
        ++failures;
    } else if (clickRecall < 0.99) {
        std::cout << "  ESITO             : FALLITO (richiamo click < 99 %)\n";
        ++failures;
    } else if (breathOutcome.falsePositives > 1 || clickOutcome.falsePositives > 1) {
        std::cout << "  ESITO             : FALLITO (troppi falsi positivi)\n";
        ++failures;
    } else {
        std::cout << "  ESITO             : SUPERATO\n";
    }

    // ---------------- 2. silenzio: nessun falso positivo -------------------
    auto runSilenceTest = [&](const char* label, const synthetic::TestTake& silence) {
        const AnalysisResult silenceResult = analyzer.analyze(silence.samples.data(),
                                                              silence.samples.size(),
                                                              silence.sampleRate,
                                                              settings);
        std::cout << "\n" << label << "\n";
        std::cout << "  Rilevamenti totali: " << silenceResult.detections.size() << "\n";
        if (silenceResult.detections.empty()) {
            std::cout << "  ESITO             : SUPERATO\n";
        } else {
            std::cout << "  ESITO             : FALLITO (falsi positivi sul silenzio)\n";
            ++failures;
        }
    };

    runSilenceTest("Prova 2 — silenzio con solo fondo di rumore",
                   synthetic::makeSilenceTake(3.0, 48000.0, 11u));

    // ---------------- 3. solo voce: nessun respiro -------------------------
    {
        const synthetic::TestTake voice = synthetic::makeVoiceOnlyTake();
        const AnalysisResult voiceResult = analyzer.analyze(voice.samples.data(),
                                                            voice.samples.size(),
                                                            voice.sampleRate,
                                                            settings);
        std::cout << "\nProva 3 — solo voce, nessun respiro\n";
        std::cout << "  Respiri rilevati  : " << voiceResult.countOf(DetectionKind::Breath) << "\n";
        std::cout << "  Click rilevati    : " << voiceResult.countOf(DetectionKind::Click) << "\n";
        if (voiceResult.countOf(DetectionKind::Breath) == 0) {
            std::cout << "  ESITO             : SUPERATO\n";
        } else {
            std::cout << "  ESITO             : FALLITO (la voce e' stata scambiata per respiro)\n";
            ++failures;
        }
    }

    // ---------------- 4. round-trip WAV -----------------------------------
    {
        std::cout << "\nProva 4 — scrittura e rilettura WAV\n";
        const std::string path = "bc_selftest_roundtrip.wav";

        AudioBuffer output;
        output.channels = 1;
        output.sampleRate = 44100;
        output.samples = take.samples;

        std::string error;
        bool ok = writeWav(path, output, WavFormat::Pcm16, &error);

        AudioBuffer readBack;
        if (ok) {
            ok = readWav(path, readBack, &error);
        }

        if (ok) {
            ok = readBack.channels == output.channels &&
                 readBack.sampleRate == output.sampleRate &&
                 std::llabs(static_cast<long long>(readBack.samples.size()) -
                            static_cast<long long>(output.samples.size())) <= 2;
        }

        std::remove(path.c_str());

        std::cout << "  ESITO             : " << (ok ? "SUPERATO" : "FALLITO") << "\n";
        if (!ok) {
            std::cout << "  Dettaglio         : " << error << "\n";
            ++failures;
        }
    }

    // ---------------- 5. correzione effettiva ------------------------------
    {
        std::cout << "\nProva 5 — la correzione riduce davvero i respiri\n";
        std::vector<float> processed = take.samples;

        EditSettings editSettings = options.edit;
        editSettings.repairClicks = options.repairClicks;

        const EditReport report = RegionEditor::apply(processed,
                                                     1,
                                                     take.sampleRate,
                                                     result.detections,
                                                     editSettings);

        // Energia dentro le regioni di respiro, prima e dopo.
        double energyBefore = 0.0;
        double energyAfter = 0.0;
        int64_t counted = 0;

        for (const Detection& detection : result.detections) {
            if (detection.kind != DetectionKind::Breath) continue;
            for (int64_t i = detection.startSample; i < detection.endSample; ++i) {
                if (i < 0 || i >= static_cast<int64_t>(take.samples.size())) continue;
                energyBefore += static_cast<double>(take.samples[static_cast<size_t>(i)]) *
                                take.samples[static_cast<size_t>(i)];
                energyAfter += static_cast<double>(processed[static_cast<size_t>(i)]) *
                               processed[static_cast<size_t>(i)];
                ++counted;
            }
        }

        const double reductionDb = (energyBefore > 1.0e-12 && energyAfter > 1.0e-12)
            ? 10.0 * std::log10(energyAfter / energyBefore)
            : 0.0;

        std::cout << "  Regioni attenuate : " << report.breathRegions << "\n";
        std::cout << "  Riduzione energia : " << std::fixed << std::setprecision(1)
                  << reductionDb << " dB\n";

        const bool ok = report.breathRegions > 0 && reductionDb < -6.0 && counted > 0;
        std::cout << "  ESITO             : " << (ok ? "SUPERATO" : "FALLITO") << "\n";
        if (!ok) ++failures;
    }

    std::cout << "\n========================\n";
    if (failures == 0) {
        std::cout << "TUTTE LE PROVE SUPERATE\n";
        return 0;
    }
    std::cout << "PROVE FALLITE: " << failures << "\n";
    return 1;
}

} // namespace

// Definita in fondo al file: dichiarata qui perche' il dispatch dei comandi
// la richiama prima della sua definizione.
int commandSilence(const Options& options);
int commandRegioni(const Options& options);
int commandCandidati(const Options& options);

static int runOfflineCli(int argc, char** argv) {
#ifdef _WIN32
    // Output UTF-8: gli accenti italiani vengono visualizzati correttamente.
    SetConsoleOutputCP(CP_UTF8);
#endif

    Options options;
    std::string command;

    if (argc < 2) {
        printUsage();
        return 2;
    }

    const std::string firstArgument = argv[1];
    if (firstArgument == "-h" || firstArgument == "--help" || firstArgument == "help") {
        printUsage();
        return 0;
    }
    if (firstArgument == "--version") {
        std::cout << kProgramName << " " << kVersion << "\n";
        return 0;
    }

    if (!parseArgs(argc, argv, options, command)) {
        if (!options.parseError.empty()) {
            std::cerr << "Errore negli argomenti: " << options.parseError << "\n\n";
            printUsage();
            return 2;
        }
        printUsage();
        return 2;
    }

    DeviceRequest probe;
    if (!parseDeviceRequest(options.device, probe)) {
        std::cerr << "Valore --device non riconosciuto: " << options.device << "\n";
        return 2;
    }

    if (command == "info")     return commandInfo(options);
    if (command == "analyze")  return commandAnalyze(options);
    if (command == "candidati") return commandCandidati(options);
    if (command == "process")  return commandProcess(options);
    if (command == "selftest") return commandSelftest(options);
    if (command == "silence") return commandSilence(options);
    if (command == "gen-test") return commandGenerateTest(options);
    if (command == "regioni") return commandRegioni(options);

    std::cerr << "Comando sconosciuto: " << command << "\n\n";
    printUsage();
    return 2;
}


// ---------------------------------------------------------------------------
//  Rilevamento del silenzio (replica il Dynamic split di REAPER)
// ---------------------------------------------------------------------------
int commandSilence(const Options& options) {
    if (options.input.empty()) {
        std::cerr << "Errore: indicare un file WAV.\n";
        return 1;
    }

    AudioBuffer buffer;
    std::string error;
    if (!readWav(options.input, buffer, &error)) {
        std::cerr << "Errore: " << error << "\n";
        return 1;
    }

    const std::vector<float> mono = buffer.toMono();
    OfflineAnalyzer analyzer;

    // ---------------------------------------------------------------------
    //  includeScores va forzato.
    //
    //  L'analizzatore riempie frameLevelDb SOLO se includeScores e' vero, e
    //  buildSettings lo mette a vero solo quando si chiede --dump-scores.
    //  Senza questa riga result.frameLevelDb resta vuoto e detectSilence()
    //  risponde "zero silenzi" su qualunque file: era esattamente il bug.
    // ---------------------------------------------------------------------
    DetectorSettings settings = buildSettings(options);
    settings.includeScores = true;

    const AnalysisResult result =
        analyzer.analyze(mono.data(), mono.size(),
                        static_cast<double>(buffer.sampleRate), settings);

    if (!result.error.empty()) {
        std::cerr << "Errore: " << result.error << "\n";
        return 1;
    }

    if (result.frameLevelDb.empty()) {
        std::cerr << "Errore: il livello per frame non e' stato calcolato "
                     "(nessun frame analizzato).\n";
        return 1;
    }

    SilenceGateConfig config;
    if (options.silenceThresholdDb < 0.0f) config.thresholdDb = options.silenceThresholdDb;
    if (options.silenceHysteresisDb >= 0.0f) config.hysteresisDb = options.silenceHysteresisDb;
    if (options.silenceMinMs > 0) config.minSilenceMs = options.silenceMinMs;

    const std::vector<SilenceRange> silenzi =
        detectSilence(result.frameLevelDb, result.hopSize, result.sampleRate, config);

    const double durata = buffer.durationSeconds();
    double totale = 0.0;
    for (const SilenceRange& s : silenzi) totale += s.lengthMs(result.sampleRate) / 1000.0;

    std::cout << "\nFile            : " << options.input << "\n";
    std::cout << "Durata          : " << std::fixed << std::setprecision(3) << durata << " s\n";
    std::cout << "Soglia          : " << std::setprecision(1) << config.thresholdDb << " dB\n";
    std::cout << "Isteresi        : " << config.hysteresisDb << " dB  (si esce sopra "
              << (config.thresholdDb + config.hysteresisDb) << " dB)\n";
    std::cout << "Silenzio minimo : " << config.minSilenceMs << " ms\n";
    std::cout << "\nSILENZI TROVATI: " << silenzi.size() << "\n";
    std::cout << "  totale silenzio: " << std::setprecision(1) << totale << " s su "
              << durata << " s (";

    if (durata > 0.0) {
        std::cout << static_cast<int>(100.0 * totale / durata + 0.5) << "%)";
    }
    std::cout << ")\n\n";

    const size_t mostrati = std::min<size_t>(silenzi.size(), 25);
    std::cout << "   #   Inizio        Fine          Durata\n";
    std::cout << "  --- ------------- ------------- ---------\n";
    for (size_t i = 0; i < mostrati; ++i) {
        char riga[128];
        std::snprintf(riga, sizeof(riga), "  %3zu %-13s %-13s %7.0f ms",
                      i + 1,
                      formatSeconds(silenzi[i].startSeconds(result.sampleRate)).c_str(),
                      formatSeconds(silenzi[i].endSeconds(result.sampleRate)).c_str(),
                      silenzi[i].lengthMs(result.sampleRate));
        std::cout << riga << "\n";
    }
    if (silenzi.size() > mostrati) {
        std::cout << "  ... e altri " << (silenzi.size() - mostrati) << "\n";
    }

    if (!options.csvPath.empty()) {
        std::ofstream file(options.csvPath, std::ios::binary);
        if (file) {
            file << "tipo,inizio_s,fine_s,durata_ms\n";
            for (const SilenceRange& s : silenzi) {
                file << "silenzio," << std::fixed << std::setprecision(6)
                     << s.startSeconds(result.sampleRate) << ","
                     << s.endSeconds(result.sampleRate) << ","
                     << std::setprecision(1) << s.lengthMs(result.sampleRate) << "\n";
            }
            std::cout << "\nCSV scritto in " << options.csvPath << "\n";
        }
    }

    return 0;
}


// ---------------------------------------------------------------------------
//  Legge le regioni prodotte dall'analisi AI
//
//  Il rilevamento con la rete neurale gira in Python; il risultato arriva
//  qui come JSON. Questo comando e' il punto di verifica del ponte: se le
//  regioni si leggono, il resto del programma puo' applicarle.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  Candidati larghi: il dataset di addestramento
//
//  Il DSP propone solo cio' che supera le sue porte; la rete ha bisogno di
//  vedere anche cio' che le porte scartano (il fiato tonale a 17 s). Questo
//  comando esporta TUTTI i candidati larghi con le 14 grandezze calcolate
//  dalla stessa funzione usata in analisi: addestra_estrai_dataset.py si
//  limita ad aggiungere le etichette, non ricalcola niente.
// ---------------------------------------------------------------------------
int commandCandidati(const Options& options) {
    if (options.input.empty() || options.jsonPath.empty()) {
        std::cerr << "Uso: bc_cli candidati <file.wav> --json <out.json>\n";
        return 2;
    }

    AudioBuffer buffer;
    std::string error;
    if (!readWav(options.input, buffer, &error)) {
        std::cerr << "Errore di lettura: " << error << "\n";
        return 1;
    }

    const std::vector<float> mono = buffer.toMono();

    // Servono le tracce per frame e il FONDO RUMORE: il BreathDetector lo
    // insegue in process(), quindi va lasciato girare. I rilevamenti che
    // produce non servono, ma il fondo si'. Niente click.
    DetectorSettings settings;
    settings.includeScores = true;
    settings.detectBreath = true;
    settings.detectClicks = false;
    settings.requirePauseForBreath = false;

    OfflineAnalyzer analyzer;
    const AnalysisResult result = analyzer.analyze(
        mono.data(), mono.size(), static_cast<double>(buffer.sampleRate), settings);
    if (!result.error.empty()) {
        std::cerr << "Errore: " << result.error << "\n";
        return 1;
    }

    FrameTracks tracks;
    tracks.hopSize = result.hopSize;
    tracks.sampleRate = result.sampleRate;
    tracks.harmonicity = result.frameHarmonicity;
    tracks.flatness = result.frameFlatness;
    tracks.centroid = result.frameCentroid;
    tracks.zcr = result.frameZcr;
    tracks.levelDb = result.frameLevelDb;
    tracks.lowBand = result.frameLowBand;
    tracks.midBand = result.frameMidBand;
    tracks.highBand = result.frameHighBand;
    tracks.noiseFloorDb = result.noiseFloorDb;
    tracks.speechLevelDb = speechLevelDb75(tracks.levelDb);
    tracks.pauses = findPauses(tracks.levelDb, tracks.hopSize, tracks.sampleRate);

    const std::vector<WideCandidate> candidati = findWideCandidates(
        tracks.harmonicity, tracks.levelDb, tracks.hopSize, tracks.sampleRate);

    std::ofstream file(options.jsonPath, std::ios::binary);
    if (!file) {
        std::cerr << "Errore: non riesco a scrivere " << options.jsonPath << "\n";
        return 1;
    }

    auto virgolette = [](const std::string& testo) {
        std::string fuori = "\"";
        for (char c : testo) {
            if (c == '\\' || c == '"') fuori += '\\';
            fuori += c;
        }
        fuori += '"';
        return fuori;
    };

    const double hopSeconds = tracks.sampleRate > 0.0
        ? static_cast<double>(tracks.hopSize) / tracks.sampleRate : 0.0;

    file << "{\n";
    file << " \"file\": " << virgolette(options.input) << ",\n";
    file << " \"sample_rate\": " << result.sampleRate << ",\n";
    file << " \"hop_size\": " << tracks.hopSize << ",\n";
    file << " \"frame\": " << result.framesAnalyzed << ",\n";
    file << " \"durata_s\": " << std::fixed << std::setprecision(6)
         << (result.sampleRate > 0.0
                 ? static_cast<double>(result.totalSamples) / result.sampleRate
                 : 0.0)
         << ",\n";
    file << " \"fondo_rumore_db\": " << std::setprecision(3)
         << static_cast<double>(tracks.noiseFloorDb) << ",\n";
    file << " \"livello_parlato_db\": " << static_cast<double>(tracks.speechLevelDb)
         << ",\n";
    file << " \"candidati\": [\n";

    for (size_t i = 0; i < candidati.size(); ++i) {
        const WideCandidate& c = candidati[i];
        const int64_t startSample = static_cast<int64_t>(c.firstFrame) * tracks.hopSize;
        const int64_t endSample = (static_cast<int64_t>(c.lastFrame) + 1) * tracks.hopSize;
        const BreathSegmentFeatures f = computeSegmentFeatures(
            tracks, c.firstFrame, c.lastFrame, startSample, endSample);
        const float probabilita = reteProbability(f);

        file << "  {\"inizio_s\": "
             << (static_cast<double>(c.firstFrame) * hopSeconds)
             << ", \"fine_s\": " << (static_cast<double>(c.lastFrame + 1) * hopSeconds)
             << ", \"prima_finestra\": " << c.firstFrame
             << ", \"ultima_finestra\": " << c.lastFrame
             << ", \"probabilita_rete\": " << (std::isfinite(probabilita) ? std::to_string(probabilita) : "null")
             << ", \"features\": {"
             << "\"durata_ms\": " << f.durataMs
             << ", \"flat_media\": " << f.flatMedia
             << ", \"harm_media\": " << f.harmMedia
             << ", \"db_media\": " << f.dbMedia
             << ", \"cent_media\": " << f.centMedia
             << ", \"cent_min\": " << f.centMin
             << ", \"cent_max\": " << f.centMax
             << ", \"zcr_media\": " << f.zcrMedia
             << ", \"bmedia_media\": " << f.bmediaMedia
             << ", \"balta_media\": " << f.baltaMedia
             << ", \"vicini_db\": " << f.viciniDb
             << ", \"sotto_vicini_db\": " << f.sottoViciniDb
             << ", \"sopra_fondo_db\": " << f.sopraFondoDb
             << ", \"in_pausa\": " << f.inPausa
             << "}}";
        if (i + 1 < candidati.size()) file << ",";
        file << "\n";
    }
    file << " ]\n}\n";

    if (!file) {
        std::cerr << "Errore: scrittura interrotta su " << options.jsonPath << "\n";
        return 1;
    }

    std::cout << "Candidati       : " << candidati.size() << "\n";
    std::cout << "Fondo rumore    : " << std::fixed << std::setprecision(1)
              << static_cast<double>(result.noiseFloorDb) << " dB\n";
    std::cout << "Livello parlato : " << static_cast<double>(tracks.speechLevelDb)
              << " dB\n";
    std::cout << "JSON salvato    : " << options.jsonPath << "\n";
    return 0;
}

int commandRegioni(const Options& options) {
    if (options.input.empty()) {
        std::cerr << "Errore: indicare un file JSON di regioni.\n";
        return 1;
    }

    AiRegions regioni;
    std::string errore;
    if (!readAiRegions(options.input, &regioni, &errore)) {
        std::cerr << "Errore: " << errore << "\n";
        return 1;
    }

    std::cout << "\nFile regioni   : " << options.input << "\n";
    std::cout << "Versione        : " << regioni.version << "\n";
    if (!regioni.sourceFile.empty()) {
        std::cout << "Audio           : " << regioni.sourceFile << "\n";
    }
    if (regioni.durationSeconds > 0.0) {
        std::printf("Durata          : %.3f s\n", regioni.durationSeconds);
    }
    if (regioni.sampleRate > 0) {
        std::cout << "Frequenza       : " << regioni.sampleRate << " Hz\n";
    }
    if (!regioni.source.empty()) {
        std::cout << "Sorgente        : " << regioni.source << "\n";
    }

    const int fiati = regioni.countOf(AiRegion::Kind::Breath);
    const int click = regioni.countOf(AiRegion::Kind::Click);

    std::cout << "\nREGIONI TROVATE: " << regioni.regions.size()
              << "  (" << fiati << " fiati, " << click << " click)\n";

    std::cout << "\n   #   Tipo    Inizio        Fine          Durata\n";
    std::cout << "  --- ------- ------------- ------------- -----------\n";
    int indice = 0;
    for (const AiRegion& r : regioni.regions) {
        ++indice;
        const char* tipo = (r.kind == AiRegion::Kind::Breath) ? "fiato"
                          : (r.kind == AiRegion::Kind::Click) ? "click" : "?";
        std::printf("  %3d %-7s %13.3f %13.3f %10.1f ms\n",
                    indice, tipo, r.startSeconds, r.endSeconds,
                    (r.endSeconds - r.startSeconds) * 1000.0);
    }
    std::cout << "\n";

    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return bc::runWithUtf8Arguments(argc, argv, runOfflineCli); }
#else
int main(int argc, char** argv) { return runOfflineCli(argc, argv); }
#endif
