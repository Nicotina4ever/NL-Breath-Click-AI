// SPDX-License-Identifier: MPL-2.0
#include "core/util/atomic_file.h"
// ============================================================================
//  editor_project.cpp — ambiente multi-file e progetto NL Breath Click Studio
//
//  Estende la finestra esistente senza riscrivere l'editor: il documento
//  attivo continua a vivere in document_, gli altri in tracks_. Il pannello
//  a sinistra mostra i file e lo stato; il progetto salva una sessione per
//  ogni file e un JSON con l'elenco, le decisioni e il percorso di output.
// ============================================================================
#include "editor_window.h"

#ifdef _WIN32

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <commctrl.h>
#include <windowsx.h>

#include "editor_commands.h"
#include "studio_dialogs.h"
#include "studio_engine.h"
#include "studio_project.h"
#include "studio_session.h"

using namespace bc;

namespace {

constexpr int kFileListId = 3001;
constexpr int kFilePanelWidth = 300;

std::string jsonText(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), needed);
    return out;
}

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::string displayStatus(const std::string& status) {
    if (status == "da_analizzare") return "da analizzare";
    if (status == "analizzato") return "analizzato";
    if (status == "da_revisionare") return "da revisionare";
    if (status == "approvato") return "approvato";
    if (status == "elaborato") return "elaborato";
    if (status == "errore") return "errore";
    return status.empty() ? "da analizzare" : status;
}

std::string lowerExtension(const std::filesystem::path& file) {
    std::string ext = file.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string relativeToProject(const std::string& projectPath,
                              const std::string& filePath) {
    std::error_code ec;
    const auto projectDir = std::filesystem::absolute(
        std::filesystem::u8path(projectPath).parent_path(), ec).lexically_normal();
    const auto fileAbs = std::filesystem::absolute(std::filesystem::u8path(filePath), ec)
                             .lexically_normal();
    if (ec) return {};
    const auto rel = fileAbs.lexically_relative(projectDir);
    if (rel.empty() || rel == ".") return {};
    const std::string text = rel.generic_u8string();
    if (text.rfind("../", 0) == 0 || text == "..") return {};
    return text;
}

bool exportAsMp3(const ExportOptions& options, const AudioFileInfo& source) {
    if (options.format == ExportFormat::Mp3) return true;
    if (options.format == ExportFormat::Wav) return false;
    return source.format == AudioFileFormat::Mp3;
}

bool exportAsWav(const ExportOptions& options, const AudioFileInfo& source) {
    return !exportAsMp3(options, source);
}

std::string exportExtension(const ExportOptions& options, const AudioFileInfo& source) {
    return exportAsMp3(options, source) ? "mp3" : "wav";
}

WavFormat exportWavFormat(const ExportOptions& options, const AudioFileInfo& source) {
    if (options.format == ExportFormat::Wav) return WavFormat::Pcm24;
    if (source.isFloat || source.bitsPerSample == 32) return WavFormat::Float32;
    if (source.bitsPerSample <= 16) return WavFormat::Pcm16;
    return WavFormat::Pcm24;
}

Mp3EncodeSettings exportMp3Settings(const ExportOptions& options, const AudioFileInfo& source) {
    Mp3EncodeSettings settings = options.mp3;
    if (options.format == ExportFormat::SameAsSource && source.format == AudioFileFormat::Mp3) {
        if (source.mp3Vbr) {
            settings.vbr = true;
            settings.vbrQuality = source.mp3VbrQuality >= 0
                ? source.mp3VbrQuality : 2;
        } else if (source.mp3BitrateKbps == 128 || source.mp3BitrateKbps == 192 ||
                   source.mp3BitrateKbps == 256 || source.mp3BitrateKbps == 320) {
            settings.vbr = false;
            settings.bitrateKbps = source.mp3BitrateKbps;
        }
    }
    return settings;
}

std::string uniqueOutputPath(const std::filesystem::path& directory,
                             const std::string& stem, const std::string& extension) {
    std::error_code ec;
    std::filesystem::create_directories(nativeFilePath(directory), ec);
    const std::string base = stem.empty() ? "audio" : stem;
    std::filesystem::path candidate = directory / (base + "_breathclick." + extension);
    int index = 2;
    while (std::filesystem::exists(nativeFilePath(candidate), ec)) {
        candidate = directory / (base + "_breathclick_" + std::to_string(index) + "." + extension);
        ++index;
        if (index > 9999) break;
    }
    return candidate.u8string();
}

bool writeExportAtomic(const std::string& sourcePath, const std::string& outputPath,
                       const AudioBuffer& audio, const ExportOptions& options,
                       const AudioFileInfo& sourceInfo, std::string* error) {
    if (sameFilePath(sourcePath, outputPath)) {
        if (error) *error = "l'esportazione non puo' sovrascrivere l'audio originale";
        return false;
    }
    const std::filesystem::path target = nativeFilePath(std::filesystem::u8path(outputPath));
    std::filesystem::path temp = target;
    temp += ".part-" + std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    bool written = false;
    std::string writeError;
    if (exportAsMp3(options, sourceInfo)) {
        AudioTags tags = sourceInfo.tags;
        if (tags.count("title") == 0) {
            tags["title"] = std::filesystem::u8path(sourcePath).stem().u8string();
        }
        written = writeMp3(temp.u8string(), audio, exportMp3Settings(options, sourceInfo),
                           tags, nullptr, &writeError);
    } else {
        written = studio::writeWavWithMetadata(sourcePath, temp.u8string(), audio,
                                               exportWavFormat(options, sourceInfo),
                                               &writeError, nullptr);
    }
    if (!written) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        if (error) *error = writeError;
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.wstring().c_str(), target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        if (error) *error = "impossibile sostituire il file di uscita";
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        if (error) *error = "impossibile sostituire il file di uscita";
        return false;
    }
#endif
    return true;
}

void pumpMessages() {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

} // namespace

void EditorWindow::createFileListPanel() {
    fileList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, kToolbarHeight, kFilePanelWidth, 100, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileListId)), instance_, nullptr);
    if (!fileList_) return;
    ListView_SetExtendedListViewStyle(fileList_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SendMessageW(fileList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.iSubItem = 0;
    column.cx = 160;
    column.pszText = const_cast<wchar_t*>(L"File");
    ListView_InsertColumn(fileList_, 0, &column);
    column.iSubItem = 1;
    column.cx = 82;
    column.pszText = const_cast<wchar_t*>(L"Stato");
    ListView_InsertColumn(fileList_, 1, &column);
    column.iSubItem = 2;
    column.cx = 56;
    column.pszText = const_cast<wchar_t*>(L"Eventi");
    ListView_InsertColumn(fileList_, 2, &column);
    column.iSubItem = 3;
    column.cx = 72;
    column.pszText = const_cast<wchar_t*>(L"Inclusi");
    ListView_InsertColumn(fileList_, 3, &column);
}

void EditorWindow::layoutFileListPanel() {
    if (!fileList_) return;
    RECT client = {};
    GetClientRect(window_, &client);
    const int width = std::min(kFilePanelWidth, std::max(220, static_cast<int>(client.right) / 3));
    const int height = std::max(40, static_cast<int>(client.bottom) - kToolbarHeight - kStatusHeight);
    MoveWindow(fileList_, 0, kToolbarHeight, width, height, TRUE);
}

AudioDocument* EditorWindow::trackDocument(int index) {
    if (index < 0 || index >= static_cast<int>(tracks_.size())) return nullptr;
    if (index == activeTrack_) return &document_;
    return &tracks_[static_cast<size_t>(index)];
}

const AudioDocument* EditorWindow::trackDocument(int index) const {
    if (index < 0 || index >= static_cast<int>(tracks_.size())) return nullptr;
    if (index == activeTrack_) return &document_;
    return &tracks_[static_cast<size_t>(index)];
}

std::string EditorWindow::globalEditSummary(bool allFiles) const {
    int files = 0;
    int total = 0;
    int included = 0;
    int excluded = 0;
    int manual = 0;
    int timeEvents = 0;
    double removedSeconds = 0.0;
    auto addDocument = [&](const AudioDocument& doc) {
        ++files;
        const double sampleRate = std::max(1, doc.sampleRate());
        for (const EditRegion& region : doc.regions()) {
            ++total;
            const bool isIncluded = region.enabled && region.approved;
            if (isIncluded) ++included;
            else ++excluded;
            if (!region.fromDetection) ++manual;
            if (!isIncluded) continue;
            if (region.editMode == 1 || region.editMode == 2) {
                ++timeEvents;
                double frames = static_cast<double>(region.endSample - region.startSample);
                if (region.editMode == 2) frames *= (1.0 - region.keepFraction);
                removedSeconds += frames / sampleRate;
            }
        }
    };
    if (allFiles) {
        for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
            const AudioDocument* doc = trackDocument(i);
            if (doc && doc->isLoaded()) addDocument(*doc);
        }
    } else if (document_.isLoaded()) {
        addDocument(document_);
    }

    bool outputMp3 = exportOptions_.format == ExportFormat::Mp3;
    bool sourceMp3 = document_.sourceInfo().format == AudioFileFormat::Mp3;
    bool sourceFlac = document_.sourceInfo().format == AudioFileFormat::Flac;
    if (exportOptions_.format == ExportFormat::SameAsSource) outputMp3 = sourceMp3;
    std::ostringstream out;
    out << "Riepilogo prima dell'esportazione\n\n"
        << "File: " << files << "\n"
        << "Eventi rilevati: " << total << "\n"
        << "Inclusi: " << included << "\n"
        << "Esclusi: " << excluded << "\n"
        << "Aggiunti manualmente: " << manual << "\n"
        << "Interventi che accorciano: " << timeEvents << "\n"
        << "Durata tagliata stimata: " << std::fixed << std::setprecision(2)
        << removedSeconds << " s\n"
        << "Formato di uscita: "
        << (outputMp3 ? "MP3" : "WAV PCM")
        << (exportOptions_.format == ExportFormat::SameAsSource
                ? " (stesso formato dell'originale)"
                : " (scelta esplicita)")
        << "\n";
    if (sourceFlac && !outputMp3 && exportOptions_.format == ExportFormat::SameAsSource) {
        out << "Nota: FLAC e' importato in PCM; l'esportazione FLAC non e' inclusa, "
               "quindi il risultato e' WAV PCM.\n";
    }
    if (outputMp3) {
        out << "Attenzione: l'esportazione MP3 e' una nuova codifica lossy, "
               "non lossless; i dettagli persi non sono recuperabili.\n";
    }
    out << "\nGli eventi automatici sono proposte incluse, non etichette di training.\n"
        << "Procedere con l'esportazione?";
    return out.str();
}

std::string EditorWindow::makeStatusLabel(const AudioDocument&, bool analyzed,
                                         bool exported) const {
    if (exported) return "elaborato";
    if (!analyzed) return "da_analizzare";
    // Dopo l'analisi le proposte sono gia' incluse: la revisione e' solo per
    // le eccezioni, non un passaggio obbligatorio evento per evento.
    return "analizzato";
}

void EditorWindow::setTrackStatus(int index) {
    if (index < 0 || index >= static_cast<int>(trackPaths_.size())) return;
    const AudioDocument* doc = trackDocument(index);
    if (!doc || !doc->isLoaded()) {
        trackStatus_[static_cast<size_t>(index)] = "errore";
        return;
    }
    if (index < static_cast<int>(trackErrors_.size()) &&
        !trackErrors_[static_cast<size_t>(index)].empty()) {
        trackStatus_[static_cast<size_t>(index)] = "errore";
        return;
    }
    const bool analyzed = index < static_cast<int>(trackAnalyzed_.size()) &&
                          trackAnalyzed_[static_cast<size_t>(index)];
    const bool exported = index < static_cast<int>(trackExported_.size()) &&
                          trackExported_[static_cast<size_t>(index)];
    trackStatus_[static_cast<size_t>(index)] = makeStatusLabel(*doc, analyzed, exported);
}

void EditorWindow::refreshTrackStatus() {
    const size_t size = tracks_.size();
    trackPaths_.resize(size);
    trackStatus_.resize(size);
    trackSessionRelative_.resize(size);
    trackOutputPaths_.resize(size);
    trackAnalyzed_.resize(size, false);
    trackReviewed_.resize(size, false);
    trackExported_.resize(size, false);
    trackErrors_.resize(size);
    for (int i = 0; i < static_cast<int>(size); ++i) setTrackStatus(i);
    rebuildFileList();
}

void EditorWindow::rebuildFileList() {
    if (!fileList_) return;
    fileListSync_ = true;
    ListView_DeleteAllItems(fileList_);
    const size_t count = std::min({tracks_.size(), trackNames_.size(), trackStatus_.size()});
    for (size_t i = 0; i < count; ++i) {
        const std::wstring name = wide(trackNames_[i]);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        item.lParam = static_cast<LPARAM>(i);
        const int row = ListView_InsertItem(fileList_, &item);
        const std::wstring status = wide(displayStatus(trackStatus_[i]));
        ListView_SetItemText(fileList_, row, 1, const_cast<wchar_t*>(status.c_str()));

        const AudioDocument* doc = trackDocument(static_cast<int>(i));
        int events = 0;
        int approved = 0;
        if (doc) {
            events = static_cast<int>(doc->regions().size());
            for (const EditRegion& region : doc->regions()) {
                if (region.enabled && region.approved) ++approved;
            }
        }
        const std::wstring eventText = std::to_wstring(events);
        const std::wstring approvedText = std::to_wstring(approved);
        ListView_SetItemText(fileList_, row, 2, const_cast<wchar_t*>(eventText.c_str()));
        ListView_SetItemText(fileList_, row, 3, const_cast<wchar_t*>(approvedText.c_str()));
        if (static_cast<int>(i) == activeTrack_) {
            ListView_SetItemState(fileList_, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(fileList_, row, FALSE);
        }
    }
    fileListSync_ = false;
}

void EditorWindow::onFileListSelectionChanged() {
    if (fileListSync_ || !fileList_) return;
    const int row = ListView_GetNextItem(fileList_, -1, LVNI_SELECTED);
    if (row < 0) return;
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!ListView_GetItem(fileList_, &item)) return;
    const int index = static_cast<int>(item.lParam);
    if (index >= 0 && index < static_cast<int>(tracks_.size()) && index != activeTrack_) {
        switchToTrack(index);
    }
}

studio::Settings EditorWindow::makeStudioSettings() const {
    studio::Settings settings;
    settings.reductionDb = editSettings_.breathReductionDb;
    settings.fadeMs = editFadeMs_;
    settings.crossfadeMs = editCrossfadeMs_;
    settings.prePadMs = editSettings_.prePadMs;
    settings.postPadMs = editSettings_.postPadMs;
    settings.sensitivity = detectorSettings_.breathSensitivity;
    settings.threshold = detectorSettings_.breathThreshold;
    settings.detectClicks = detectorSettings_.detectClicks;
    settings.dspSettings = detectorSettings_;
    settings.detector = studioDetector_;
    settings.aiThreshold = aiThreshold_;
    return settings;
}

studio::SessionData EditorWindow::makeSessionFor(const AudioDocument& doc) const {
    studio::SessionData session;
    session.sourcePath = doc.sourcePath();
    session.detectorSettings = detectorSettings_;
    session.regions = doc.regions();
    session.corrections = doc.corrections();
    session.fade = doc.fade();
    session.masterGainDb = doc.masterGainDb();
    session.reductionDb = editSettings_.breathReductionDb;
    session.fadeMs = editFadeMs_;
    session.crossfadeMs = editCrossfadeMs_;
    session.detector = studioDetector_;
    session.analysis = doc.analysisProvenance();
    session.aiThreshold = aiThreshold_;
    for (const EditRegion& region : doc.regions()) {
        studio::Event event = studio::eventFromRegion(region);
        session.events.push_back(event);
    }
    return session;
}

bool EditorWindow::applySessionTo(AudioDocument* doc,
                                  const studio::SessionData& session) const {
    if (!doc || !sameFilePath(doc->sourcePath(), session.sourcePath)) return false;
    for (const auto& region : session.regions) {
        if (region.startSample < 0 || region.endSample > doc->frameCount() || region.endSample <= region.startSample)
            return false;
    }
    doc->clearRegions();
    doc->clearCorrections();
    for (const EditRegion& region : session.regions) doc->addRegion(region);
    doc->clearCorrections();
    for (const Correction& correction : session.corrections) {
        doc->recordCorrection(correction.kind, correction.startSample,
                              correction.endSample, correction.positive,
                              correction.source);
    }
    doc->setFade(session.fade);
    doc->setAnalysisProvenance(session.analysis);
    doc->setMasterGainDb(session.masterGainDb);
    // Le decisioni sono gia' nelle regioni; gli eventi servono a ripristinare
    // modalita' e stato per sessioni scritte da versioni precedenti.
    std::vector<bool> restored(doc->regions().size(), false);
    for (const studio::Event& event : session.events) {
        for (size_t i = 0; i < doc->regions().size(); ++i) {
            EditRegion region = doc->regions()[i];
            if (restored[i] || region.manualOperation != event.manualOperation || region.kind != event.kind ||
                region.startSample != event.startSample || region.endSample != event.endSample) continue;
            restored[i] = true;
            region.approved = event.approved;
            region.enabled = event.enabled;
            region.editMode = static_cast<int>(event.mode);
            region.keepFraction = event.keepFraction;
            region.fadeMs = event.fadeMs;
            region.safetyMarginMs = event.safetyMarginMs;
            region.decisionSource = event.decisionSource;
            region.classification = event.classification;
            region.trainingEligible = event.trainingEligible;
            if (event.reductionDb) region.reductionDb = *event.reductionDb;
            region.repair = event.repair;
            doc->updateRegion(static_cast<int>(i), region, false);
            break;
        }
    }
    return true;
}

std::string EditorWindow::makeRecoveryPath() const {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "BreathClick";
    return (dir / "recovery.bcnproj.json").u8string();
}

bool EditorWindow::saveProjectTo(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (error) *error = "percorso di progetto vuoto";
        return false;
    }
    if (tracks_.empty()) {
        if (error) *error = "nessun file nel progetto";
        return false;
    }
    studio::ProjectData project;
    project.name = std::filesystem::u8path(path).stem().u8string();
    project.settings = makeStudioSettings();

    // Immutable session generation: an error on any member or on the final
    // project write leaves the previously saved project AND sessions intact.
    const auto generation = std::to_string(GetCurrentProcessId()) + "_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto projectFs = std::filesystem::u8path(path);
    const auto sessionRoot = projectFs.parent_path() / ".breathclick_sessions" / generation;

    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        const AudioDocument* doc = trackDocument(i);
        if (!doc || !doc->isLoaded()) continue;
        studio::ProjectFile file;
        file.sourcePath = doc->sourcePath();
        file.relativeSource = relativeToProject(path, file.sourcePath);
        file.status = i < static_cast<int>(trackStatus_.size())
                          ? trackStatus_[static_cast<size_t>(i)]
                          : makeStatusLabel(*doc, false, false);
        file.events = static_cast<int>(doc->regions().size());
        file.approved = 0;
        for (const EditRegion& region : doc->regions()) {
            if (region.enabled && region.approved) ++file.approved;
        }
        file.analyzed = i < static_cast<int>(trackAnalyzed_.size()) &&
                        trackAnalyzed_[static_cast<size_t>(i)];
        file.reviewed = file.approved >= file.events && file.events > 0;
        file.exported = i < static_cast<int>(trackExported_.size()) &&
                        trackExported_[static_cast<size_t>(i)];
        file.error = i < static_cast<int>(trackErrors_.size())
                         ? trackErrors_[static_cast<size_t>(i)]
                         : std::string();
        const std::string sessionPath = (sessionRoot / (std::to_string(i + 1) + ".bcnstudio.json")).u8string();
        std::error_code sessionDirError;
        std::filesystem::create_directories(
            nativeFilePath(std::filesystem::u8path(sessionPath).parent_path()), sessionDirError);
        std::string sessionError;
        if (!studio::saveSession(sessionPath, makeSessionFor(*doc), &sessionError)) {
            if (error) *error = "sessione non salvata: " + sessionError;
            return false;
        }
        file.sessionRelative = relativeToProject(path, sessionPath);
        if (i < static_cast<int>(trackOutputPaths_.size()) &&
            !trackOutputPaths_[static_cast<size_t>(i)].empty()) {
            file.outputRelative = relativeToProject(path, trackOutputPaths_[static_cast<size_t>(i)]);
        }
        project.files.push_back(file);
    }
    if (project.files.empty()) {
        if (error) *error = "nessun file valido nel progetto";
        return false;
    }
    if (!studio::saveProjectFile(path, project, error)) return false;
    return true;
}

bool EditorWindow::loadProjectFrom(const std::string& path, std::string* error) {
    studio::ProjectData project;
    if (!studio::loadProjectFile(path, &project, error)) return false;

    decltype(tracks_) pending_tracks_;
    decltype(trackNames_) pending_trackNames_;
    decltype(trackPaths_) pending_trackPaths_;
    decltype(trackStatus_) pending_trackStatus_;
    decltype(trackSessionRelative_) pending_trackSessionRelative_;
    decltype(trackOutputPaths_) pending_trackOutputPaths_;
    decltype(trackAnalyzed_) pending_trackAnalyzed_;
    decltype(trackReviewed_) pending_trackReviewed_;
    decltype(trackExported_) pending_trackExported_;
    decltype(trackErrors_) pending_trackErrors_;

    const std::filesystem::path projectDir = std::filesystem::u8path(path).parent_path();
    for (size_t i = 0; i < project.files.size(); ++i) {
        const studio::ProjectFile& entry = project.files[i];
        AudioDocument doc;
        std::string loadError;
        if (!doc.load(entry.sourcePath, &loadError)) {
            if (error) *error = "progetto non aperto; audio non leggibile: " + entry.sourcePath + ": " + loadError;
            return false;
        }
        std::filesystem::path sessionPath;
        if (!entry.sessionRelative.empty()) {
            sessionPath = (projectDir / std::filesystem::u8path(entry.sessionRelative)).lexically_normal();
        } else {
            sessionPath = std::filesystem::u8path(
                studio::projectSessionPath(path, i, entry.sourcePath));
        }
        studio::SessionData session;
        std::string sessionError;
        if (std::filesystem::exists(nativeFilePath(sessionPath))) {
            if (!studio::loadSession(sessionPath.u8string(), &session, &sessionError)) {
                if (error) *error = "progetto non aperto; sessione non leggibile: " + sessionError;
                return false;
            }
            if (!applySessionTo(&doc, session)) {
                if (error) *error = "sessione non compatibile con l'audio del progetto";
                return false;
            }
        } else if (!entry.sessionRelative.empty()) {
            if (error) *error = "progetto non aperto; sessione mancante: " + sessionPath.u8string();
            return false;
        }
        const std::string name = doc.displayName();
        pending_tracks_.push_back(std::move(doc));
        pending_trackNames_.push_back(name);
        pending_trackPaths_.push_back(entry.sourcePath);
        pending_trackStatus_.push_back(entry.status.empty() ? "da_analizzare" : entry.status);
        pending_trackSessionRelative_.push_back(entry.sessionRelative);
        pending_trackOutputPaths_.push_back(entry.outputRelative.empty()
                                        ? std::string()
                                        : (projectDir / std::filesystem::u8path(entry.outputRelative)).u8string());
        pending_trackAnalyzed_.push_back(entry.analyzed);
        pending_trackReviewed_.push_back(entry.reviewed);
        pending_trackExported_.push_back(entry.exported);
        pending_trackErrors_.push_back(entry.error);
    }
    if (pending_tracks_.empty()) {
        if (error) *error = "nessun file del progetto e' leggibile";
        return false;
    }

    tracks_ = std::move(pending_tracks_);
    trackNames_ = std::move(pending_trackNames_);
    trackPaths_ = std::move(pending_trackPaths_);
    trackStatus_ = std::move(pending_trackStatus_);
    trackSessionRelative_ = std::move(pending_trackSessionRelative_);
    trackOutputPaths_ = std::move(pending_trackOutputPaths_);
    trackAnalyzed_ = std::move(pending_trackAnalyzed_);
    trackReviewed_ = std::move(pending_trackReviewed_);
    trackExported_ = std::move(pending_trackExported_);
    trackErrors_ = std::move(pending_trackErrors_);
    activeTrack_ = 0;
    document_ = std::move(tracks_[0]);
    tracks_[0] = AudioDocument{};
    projectPath_ = path;
    projectDirty_ = false;
    editSettings_.breathReductionDb = project.settings.reductionDb;
    editFadeMs_ = project.settings.fadeMs;
    editCrossfadeMs_ = project.settings.crossfadeMs;
    editSettings_.attackMs = static_cast<float>(project.settings.fadeMs);
    editSettings_.releaseMs = static_cast<float>(project.settings.fadeMs);
    editSettings_.prePadMs = static_cast<float>(project.settings.prePadMs);
    editSettings_.postPadMs = static_cast<float>(project.settings.postPadMs);
    detectorSettings_ = project.settings.dspSettings.value_or(DetectorSettings{});
    detectorSettings_.breathSensitivity = project.settings.sensitivity;
    detectorSettings_.breathThreshold = project.settings.threshold;
    studioDetector_ = project.settings.detector.empty() ? "dsp" : project.settings.detector;
    aiThreshold_ = project.settings.aiThreshold;
    view_.setDocument(&document_);
    refreshActiveTrack();
    refreshTrackStatus();
    setStatusMessage("progetto aperto: " + path + " (" +
                     std::to_string(tracks_.size()) + " file)");
    return true;
}

void EditorWindow::saveRecovery() {
    if (tracks_.empty()) return;
    const std::string recovery = makeRecoveryPath();
    const std::string savedProject = projectPath_;
    std::string error;
    if (!saveProjectTo(recovery, &error)) {
        setStatusMessage("autosalvataggio non riuscito: " + error);
        return;
    }
    recoveryPending_ = false;
    if (savedProject.empty()) projectPath_.clear();
    else projectPath_ = savedProject;
}

void EditorWindow::commandOpenFolder() {
    std::string directory;
    if (!chooseDirectory(window_, L"Cartella con registrazioni WAV/MP3/FLAC", directory)) return;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    const auto root = std::filesystem::u8path(directory);
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) {
            const std::string dirName = it->path().filename().u8string();
            if (dirName.rfind("breathclick_out", 0) == 0 ||
                dirName.rfind("breathclick_reviewed", 0) == 0 ||
                dirName == ".breathclick_sessions") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it->is_regular_file(ec) && isSupportedAudioPath(it->path().u8string())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        MessageBoxW(window_, L"Nessun file WAV, MP3 o FLAC trovato nella cartella.",
                    L"NL Breath Click Studio", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!tracks_.empty()) {
        const int answer = MessageBoxW(window_,
            L"Sostituire il progetto attualmente aperto con la cartella scelta?",
            L"NL Breath Click Studio", MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES) return;
    }
    tracks_.clear();
    trackNames_.clear();
    document_ = AudioDocument{};
    activeTrack_ = -1;
    projectPath_.clear();
    projectDirty_ = false;
    for (const auto& file : files) {
        addTrackAndLoad(file.u8string());
        if (activeTrack_ >= 0) {
            if (trackPaths_.size() > static_cast<size_t>(activeTrack_)) {
                trackPaths_[static_cast<size_t>(activeTrack_)] = file.u8string();
            }
            trackAnalyzed_[static_cast<size_t>(activeTrack_)] = false;
            trackExported_[static_cast<size_t>(activeTrack_)] = false;
            trackReviewed_[static_cast<size_t>(activeTrack_)] = false;
            trackStatus_[static_cast<size_t>(activeTrack_)] = "da_analizzare";
        }
    }
    projectDirty_ = true;
    recoveryPending_ = true;
    refreshTrackStatus();
    setStatusMessage("cartella aperta: " + std::to_string(files.size()) + " file");
}

bool EditorWindow::openProjectPath(const std::string& path) {
    return loadProjectFrom(path, nullptr);
}

bool EditorWindow::saveProjectPath(const std::string& path) {
    return saveProjectTo(path, nullptr);
}

void EditorWindow::commandOpenProject() {
    std::string path;
    if (!chooseFilePath(window_, false,
                        L"Progetto NL Breath Click Studio (*.bcnproj.json)\0*.bcnproj.json\0JSON (*.json)\0*.json\0",
                        L"bcnproj.json", path)) return;
    std::string error;
    if (!loadProjectFrom(path, &error)) {
        MessageBoxW(window_, wide("Progetto non leggibile:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
}

void EditorWindow::commandSaveProject(bool saveAs) {
    if (tracks_.empty()) {
        setStatusMessage("nessun file aperto");
        return;
    }
    std::string path = projectPath_;
    if (saveAs || path.empty()) {
        if (!chooseFilePath(window_, true,
                            L"Progetto NL Breath Click Studio (*.bcnproj.json)\0*.bcnproj.json\0JSON (*.json)\0*.json\0",
                            L"bcnproj.json", path)) return;
    }
    std::string error;
    if (!saveProjectTo(path, &error)) {
        MessageBoxW(window_, wide("Salvataggio progetto non riuscito:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    projectPath_ = path;
    projectDirty_ = false;
    setStatusMessage("progetto salvato: " + path);
}

void EditorWindow::commandRecoverProject() {
    const std::string recovery = makeRecoveryPath();
    if (!std::filesystem::exists(std::filesystem::u8path(recovery))) {
        setStatusMessage("nessun autosalvataggio disponibile");
        return;
    }
    std::string error;
    if (!loadProjectFrom(recovery, &error)) {
        MessageBoxW(window_, wide("Autosalvataggio non leggibile:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    projectPath_.clear();
    projectDirty_ = true;
    recoveryPending_ = true;
    setStatusMessage("lavoro ripristinato dall'autosalvataggio");
}

bool EditorWindow::analyzeActiveTrack(bool interactive, std::string* error) {
    if (!document_.isLoaded()) {
        if (error) *error = "aprire prima un file audio";
        if (interactive) setStatusMessage(*error);
        return false;
    }
    if (document_.sourcePath().empty()) {
        if (error) *error = "analisi disponibile solo su un file audio aperto da disco (WAV, MP3 o FLAC)";
        if (interactive) setStatusMessage(*error);
        return false;
    }
    player_.stop();
    const studio::Settings settings = makeStudioSettings();

    studio::Analysis analysis;
    std::string analysisError;
    if (!studio::analyzeFile(document_.sourcePath(), settings, &analysis, &analysisError)) {
        if (error) *error = analysisError;
        if (activeTrack_ >= 0 && activeTrack_ < static_cast<int>(trackErrors_.size())) {
            trackErrors_[static_cast<size_t>(activeTrack_)] = analysisError;
            trackStatus_[static_cast<size_t>(activeTrack_)] = "errore";
            rebuildFileList();
        }
        if (interactive) {
            MessageBoxW(window_, wide("Analisi non riuscita:\n\n" + analysisError).c_str(),
                        L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        }
        return false;
    }

    // Riconciliazione: una nuova analisi non deve cancellare le correzioni
    // manuali, le esclusioni e le label verificate. Gli eventi automatici non
    // piu' rilevati vengono rimossi, quelli nuovi aggiunti; le sovrapposizioni
    // ambigue restano intatte e vengono conteggiate.
    std::vector<Detection> detections;
    detections.reserve(analysis.events.size());
    int profileRejected = 0;
    for (const studio::Event& event : analysis.events) {
        if (event.kind == DetectionKind::Breath && personalProfileActive_ && personalProfile_.valid) {
            const LearningFeatures features = extractLearningFeatures(
                document_.audio(), event.startSample, event.endSample, event.score);
            if (!personalProfile_.accepts(features)) {
                ++profileRejected;
                continue;
            }
        }
        Detection detection = studio::detectionFromEvent(event);
        detections.push_back(detection);
    }
    const AudioDocument::ReconcileResult reconciled =
        document_.reconcileRegions(detections, editSettings_);
    document_.setAnalysisProvenance({analysis.detector, analysis.aiInferenceFrames});
    if (profileRejected > 0) {
        setStatusMessage("profilo personale: " + std::to_string(profileRejected) +
                         " proposte filtrate");
    }
    if (reconciled.ambiguous > 0) {
        setStatusMessage(std::to_string(reconciled.ambiguous) +
                         " sovrapposizioni ambigue conservate senza sovrascrivere");
    }
    if (activeTrack_ >= 0) {
        if (activeTrack_ < static_cast<int>(trackAnalyzed_.size())) {
            trackAnalyzed_[static_cast<size_t>(activeTrack_)] = true;
            trackReviewed_[static_cast<size_t>(activeTrack_)] = false;
            trackExported_[static_cast<size_t>(activeTrack_)] = false;
            trackErrors_[static_cast<size_t>(activeTrack_)].clear();
        }
        setTrackStatus(activeTrack_);
    }
    view_.setSelectedRegion(-1);
    view_.refreshDocument(false);
    analysisSummary_ = "analisi " + analysis.detector + ": " +
                       std::to_string(analysis.events.size()) + " eventi";
    if (analysis.aiInferenceFrames > 0)
        analysisSummary_ += "; ONNX eseguito: " + std::to_string(analysis.aiInferenceFrames) + " frame";
    deviceStatus_ = analysis.aiInferenceFrames > 0 ? "CPU + Respiro-en ONNX" : "CPU DSP";
    if (reconciled.ambiguous > 0)
        analysisSummary_ += "; " + std::to_string(reconciled.ambiguous) + " conflitti conservati";
    if (profileRejected > 0)
        analysisSummary_ += "; " + std::to_string(profileRejected) + " proposte filtrate dal profilo";
    statusMessage_ = analysisSummary_;
    markDocumentChanged(false);
    refreshTrackStatus();
    return true;
}

bool EditorWindow::exportActiveTrackTo(const std::string& outputPath, std::string* error) {
    if (!document_.isLoaded()) {
        if (error) *error = "nessun file attivo";
        return false;
    }
    studio::EditPlan plan;
    if (!buildEditPlan(&plan, error)) return false;
    studio::EditResult result;
    if (!studio::applyEditPlan(document_.audio(), plan, &result, error)) return false;
    if (!writeExportAtomic(document_.sourcePath(), outputPath, result.audio,
                           exportOptions_, document_.sourceInfo(), error)) {
        return false;
    }
    std::string reportError;
    studio::writeEditReportJson(outputPath + ".edit.json", plan, result, outputPath, &reportError);
    studio::writeEditPlanJson(outputPath + ".plan.json", plan, &reportError);
    for (size_t i = 0; i < document_.regions().size(); ++i) {
        EditRegion region = document_.regions()[i];
        if (!region.approved || !region.enabled) continue;
        bool applied = false;
        for (const auto& operation : result.operations) {
            if (operation.applied && operation.event.kind == region.kind &&
                operation.event.startSample == region.startSample && operation.event.endSample == region.endSample) applied = true;
        }
        document_.markRegionApplied(static_cast<int>(i), applied);
    }
    if (activeTrack_ >= 0) {
        trackExported_[static_cast<size_t>(activeTrack_)] = true;
        trackOutputPaths_[static_cast<size_t>(activeTrack_)] = outputPath;
        trackErrors_[static_cast<size_t>(activeTrack_)].clear();
        setTrackStatus(activeTrack_);
    }
    view_.refreshDocument(false);
    engineApplied_ = result.appliedEvents; engineRejected_ = result.rejectedEvents;
    refreshRegionPanel(true);
    refreshTrackStatus();
    return true;
}

bool EditorWindow::analyzeAllTracks() {
    if (tracks_.empty()) {
        setStatusMessage("aprire una cartella o un progetto");
        return false;
    }
    int success = 0;
    int failures = 0;
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        switchToTrack(i);
        SetWindowTextW(window_, (L"Analisi " + std::to_wstring(i + 1) + L"/" +
                                 std::to_wstring(tracks_.size()) + L" - " +
                                 wide(trackNames_[static_cast<size_t>(i)])).c_str());
        pumpMessages();
        std::string error;
        if (analyzeActiveTrack(false, &error)) {
            ++success;
        } else {
            ++failures;
        }
        pumpMessages();
    }
    refreshTrackStatus();
    updateTitle();
    setStatusMessage("analisi completata: " + std::to_string(success) +
                     " file, " + std::to_string(failures) + " errori");
    return failures == 0;
}

void EditorWindow::commandProjectAnalyzeAll() {
    analyzeAllTracks();
}

void EditorWindow::approveAllTracks() {
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        switchToTrack(i);
        for (size_t r = 0; r < document_.regions().size(); ++r) {
            EditRegion region = document_.regions()[r];
            region.approved = true;
            region.enabled = true;
            region.decisionSource = "user";
            region.trainingEligible = false;
            document_.updateRegion(static_cast<int>(r), region);
        }
        if (activeTrack_ >= 0) {
            trackReviewed_[static_cast<size_t>(activeTrack_)] = true;
            setTrackStatus(activeTrack_);
        }
    }
    refreshTrackStatus();
    markDocumentChanged(false);
}

int EditorWindow::exportAllTo(const std::string& outputDirectory, bool showSummary) {
    if (tracks_.empty()) {
        if (showSummary) {
            MessageBoxW(window_, L"Nessun file aperto.", L"NL Breath Click Studio", MB_OK);
        }
        return 0;
    }
    if (showSummary) {
        const std::wstring summary = wide(globalEditSummary(true));
        if (MessageBoxW(window_, summary.c_str(), L"NL Breath Click Studio - riepilogo",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return 0;
        }
    }
    const std::filesystem::path out = std::filesystem::u8path(outputDirectory);
    int exported = 0;
    int skipped = 0;
    int errors = 0;
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        switchToTrack(i);
        pumpMessages();
        const std::string source = document_.sourcePath();
        const std::string stem = std::filesystem::u8path(source).stem().u8string();
        const std::string target = uniqueOutputPath(
            out, stem, exportExtension(exportOptions_, document_.sourceInfo()));
        std::string error;
        if (exportActiveTrackTo(target, &error)) {
            ++exported;
        } else if (error == "nessun evento incluso") {
            ++skipped;
            trackStatus_[static_cast<size_t>(i)] = "inclusi";
        } else {
            ++errors;
            trackErrors_[static_cast<size_t>(i)] = error;
            trackStatus_[static_cast<size_t>(i)] = "errore";
        }
        rebuildFileList();
        pumpMessages();
    }
    // Report del batch di progetto: un file JSON per riprendere/verificare
    // l'esito senza riaprire ogni WAV.
    {
        const std::filesystem::path reportPath = out / "breathclick_export_report.json";
        std::ofstream report(nativeFilePath(reportPath), std::ios::binary | std::ios::trunc);
        if (report) {
            report << "{\n  \"schema\": 1,\n  \"output_directory\": \""
                   << jsonText(out.generic_u8string()) << "\",\n  \"files\": [\n";
            for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
                if (i > 0) report << ",\n";
                report << "    {\"source\": \"" << jsonText(trackPaths_[static_cast<size_t>(i)])
                       << "\", \"status\": \"" << jsonText(trackStatus_[static_cast<size_t>(i)])
                       << "\", \"output\": \"" << jsonText(trackOutputPaths_[static_cast<size_t>(i)])
                       << "\", \"error\": \"" << jsonText(trackErrors_[static_cast<size_t>(i)])
                       << "\"}";
            }
            report << "\n  ]\n}\n";
        }
    }
    refreshTrackStatus();
    if (showSummary) {
        const std::string summary = "Export: " + std::to_string(exported) + " elaborati, " +
            std::to_string(skipped) + " senza interventi, " + std::to_string(errors) + " errori.\n\n" +
            "Uscita: " + outputDirectory;
        MessageBoxW(window_, wide(summary).c_str(), L"NL Breath Click Studio", MB_OK);
        setStatusMessage("export completato: " + std::to_string(exported));
    }
    saveRecovery();
    return exported;
}

void EditorWindow::commandProjectExportAll() {
    if (tracks_.empty()) {
        setStatusMessage("aprire una cartella o un progetto");
        return;
    }
    std::string outputDirectory;
    if (!chooseDirectory(window_, L"Cartella di uscita per i file elaborati",
                        outputDirectory)) return;
    exportAllTo(outputDirectory, true);
}

void EditorWindow::commandProjectMarkReviewed() {
    if (!document_.isLoaded()) return;
    document_.beginUndoGroup("Inclusione file");
    for (size_t i = 0; i < document_.regions().size(); ++i) {
        EditRegion region = document_.regions()[i];
        region.approved = true;
        region.enabled = true;
        region.decisionSource = "user";
        document_.updateRegion(static_cast<int>(i), region);
    }
    document_.endUndoGroup();
    if (activeTrack_ >= 0) {
        trackReviewed_[static_cast<size_t>(activeTrack_)] = true;
        setTrackStatus(activeTrack_);
    }
    markDocumentChanged(false);
    refreshTrackStatus();
    setStatusMessage("tutti gli eventi del file sono inclusi");
}

void EditorWindow::commandProjectNextFile(int delta) {
    if (tracks_.empty()) return;
    int index = activeTrack_ + delta;
    if (index < 0) index = 0;
    if (index >= static_cast<int>(tracks_.size())) index = static_cast<int>(tracks_.size()) - 1;
    if (index != activeTrack_) switchToTrack(index);
}

#endif
