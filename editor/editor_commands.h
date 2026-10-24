// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  editor_commands.h — Identificatori dei comandi dell'editor
//
//  Condivisi fra la finestra principale (menu e barra) e la vista della forma
//  d'onda, che inoltra i tasti premuti come WM_COMMAND al genitore.
// ============================================================================
#pragma once

namespace bc {
namespace EditorCommand {

enum Id : int {
    kToolRegions = 2500, kToolTimeSelection = 2501,
    kManualAttenuate = 2502, kManualSilence = 2503, kManualCut = 2504,
    kManualHalve = 2505, kManualRestore = 2506, kTimeSelectionBounds = 2507,
    kRegionsInTimeSelection = 2508, kClicksDisable = 2509, kClicksEnable = 2510,
    kRegionFilter = 2511,
    // Aiuto.
    //
    // 1701 e non 1400: 1400 era gia' occupato da kViewZoomIn, quindi F1 e lo
    // zoom avrebbero avuto lo stesso identificatore e uno dei due comandi non
    // sarebbe mai stato raggiunto.
    kHelpShortcuts = 1701,

    kFileOpen = 1001,
    kFileSaveAs = 1002,
    kFileExport = 1003,
    kFileExit = 1004,
    // Apre un altro file accanto a quello gia' aperto, come seconda traccia.
    kFileOpenInTrack = 1005,
    // Esporta le correzioni come dati di addestramento.
    kFileExportDataset = 1006,
    kFileExportOptions = 1011,
    kExportFormatSame = 1060,
    kExportFormatWav,
    kExportMp3128,
    kExportMp3192,
    kExportMp3256,
    kExportMp3320,
    kExportMp3VbrQ0,
    kExportMp3VbrQ1,
    kExportMp3VbrQ2,
    kExportMp3VbrQ3,
    kExportMp3VbrQ4,
    kExportMp3VbrQ5,
    kExportMp3VbrQ6,
    kExportMp3VbrQ7,
    kExportMp3VbrQ8,
    kExportMp3VbrQ9,
    // Sessione BreathClick Studio.
    kFileSaveSession = 1007,
    kFileOpenSession = 1008,
    // Batch su una cartella.
    kFileBatchFolder = 1009,
    kFileBatchReviewed = 1010,
    // Progetto multi-file e coda di lavoro.
    kProjectOpenFolder = 2200,
    kProjectOpen,
    kProjectSave,
    kProjectSaveAs,
    kProjectRecover,
    kProjectAnalyzeAll,
    kProjectExportAll,
    kProjectMarkReviewed,
    kProjectNextFile,
    kProjectPreviousFile,
    kProjectApproveAllFiles,

    // Pannello contestuale della regione.
    kRegionApplyBounds = 2310,
    kRegionEditBounds = 2311,
    kRegionGlobalAttenuate = 2320,
    kRegionGlobalCut,
    kRegionGlobalHalve,

    // Navigazione/zoom verticale.
    kViewVerticalReset = 2330,
    kLearningConfirmBreath = 2400,
    kLearningFalsePositive,
    kLearningMissedBreath,
    kLearningEditorialExclusion,
    kLearningUnverified,
    kLearningTrain,
    kLearningActivate,
    kLearningReset,
    kLearningStatus,
    kViewFollowPlayhead = 2331,
    kViewGoPlayhead = 2332,
    kGoToSelectedRegion,

    // -------------------------------------------------------------------
    //  Gli identificatori dei comandi edit hanno TUTTI il valore esplicito.
    //
    //  Non e' pedanteria: erano progressivi, e inserendo "Taglia" in mezzo
    //  tutti i successivi sono slittati di uno. Il test funzionale manda il
    //  comando 1105 aspettandosi "Seleziona tutto" e colpiva invece "Ritaglia
    //  sulla selezione". Un identificatore non deve MAI cambiare perche' si
    //  aggiunge una voce sopra: se ne accorge solo chi lo usa.
    //
    //  Le nuove voci si aggiungono in fondo con un valore libero.
    // -------------------------------------------------------------------
    kEditUndo = 1100,
    kEditRedo = 1101,
    kEditDeleteSelection = 1102,
    kEditSilenceSelection = 1103,
    kEditCropToSelection = 1104,
    kEditSelectAll = 1105,
    kEditDeselect = 1106,
    kEditInsertSilence = 1107,
    kEditCut = 1108,

    kAnalyze = 1200,
    kAnalyzeSettings,
    kClearRegions,
    // Importa le regioni prodotte dall'analisi AI (file JSON).
    kAnalyzeImportAi = 1203,
    kStudioAttenuationSettings = 1204,
    kStudioDetectorDsp = 1205,
    kStudioDetectorAi = 1206,
    kStudioDetectorFusion = 1207,
    kStudioPreview = 1208,
    kStudioApply = 1209,
    kStudioExportPlan = 1210,

    kRegionDelete = 1300,
    kRegionToggleEnabled,
    kRegionSetBreath,
    kRegionSetClick,
    kRegionGainDown,
    kRegionGainUp,
    kRegionGainReset,
    kRegionRepairToggle,
    kRegionAddAtSelection,
    kRegionConfirm = 1313,
    kRegionReject = 1314,
    kRegionModeAttenuate = 1315,
    kRegionModeCut = 1316,
    kRegionModeHalve = 1317,
    kRegionKeepFraction = 1318,
    kRegionAudition = 1319,
    kRegionNext = 1320,
    kRegionPrevious = 1321,
    kRegionSelectAll = 1322,
    kRegionClearSelection = 1323,
    kRegionApproveAll = 1324,
    kRegionRejectAll = 1325,
    kRegionKeep25 = 1326,
    kRegionKeep50 = 1327,
    kRegionKeep75 = 1328,
    kRegionMargin0 = 1329,
    kRegionMargin10 = 1330,
    kRegionMargin20 = 1331,

    // Accorciamento dei fiati: taglia una parte della regione e richiude il
    // buco, spostando indietro tutto cio' che segue. E' l'operazione che
    // serve nell'editing degli spot: un fiato da 1 s ne dura 0,5 e il resto
    // va a posto da solo.
    kRegionShortenHalf = 1310,
    kRegionShortenThird,
    kRegionShortenQuarter,

    kViewZoomIn = 1400,
    kViewZoomOut,
    kViewZoomFit,
    kViewZoomSelection,

    kTransportPlay = 1500,
    kTransportPlaySelection,
    kTransportStop,
    kGoToStart,
    kGoToEnd,
    kTransportPlayOriginal = 1510,
    kTransportPlayProcessed = 1511,

    kFadeSet = 1600,
    kFadeClear,

    // -------------------------------------------------------------------
    //  Tracce multiple.
    //
    //  La traccia N si attiva col comando kTrackSwitchBase + N, quindi la
    //  fascia 1800-1898 e' riservata. Il menu viene ricostruito ogni volta
    //  che si apre o si chiude una traccia, cosi' gli indici restano
    //  allineati al vettore senza doverli cercare.
    // -------------------------------------------------------------------
    kTrackSwitchBase = 1800,
    kTrackSwitchMax = 1898,
    kTrackClose = 1899,

    // -------------------------------------------------------------------
    //  Profili di modello.
    //
    //  Il profilo N si attiva con kModelSwitchBase + N: la fascia
    //  1910-1999 e' riservata. 1900 e' il ritorno al profilo di partenza.
    // -------------------------------------------------------------------
    kModelResetToBase = 1900,
    kModelSwitchBase = 1910,
    kModelSwitchMax = 1999,
    kModelReload = 1901,

    kHelpAbout = 1700
};

} // namespace EditorCommand
} // namespace bc
