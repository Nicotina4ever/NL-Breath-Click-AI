# Criteri per NL Breath Click Studio 1.0

La versione corrente resta una candidate. Verifiche attuali e collaudo umano
in TESTING.md. I criteri sotto rimangono le condizioni di uscita della 1.0.

## AC-01 Build riproducibile

- Criterio: checkout del progetto + dipendenze documentate producono
  `bc_studio.exe`, `bc_studio_cli.exe`, `bc_cli.exe` e i modelli accanto ai
  binari in modalita' Release.
- Comando:
  ```powershell
  .\tools\cmake-3.28.6-windows-x86_64\bin\cmake.exe `
    -S . -B build -G "Visual Studio 17 2022" -A x64
  .\tools\cmake-3.28.6-windows-x86_64\bin\cmake.exe `
    --build build --config Release
  ```
- Stato: **PASS RC** da archivio Git separato con dipendenze fissate (111 hash).
- Per 1.0: ripetere su macchina Windows pulita; comandi in `../BUILDING.md`.

## AC-02 Suite automatica

- Criterio: tutti i test CTest obbligatori passano; i test saltati sono
  elencati con motivazione.
- Comando:
  ```powershell
  .\tools\cmake-3.28.6-windows-x86_64\bin\ctest.exe `
    --test-dir build -C Release --output-on-failure
  ```
- Atteso corrente: **31/31**, con `BC_REQUIRE_RELEASE_TESTS=ON`.
- Non e' accettabile dichiarare verde un test saltato per fixture mancante.
- Stato: **PASS RC** (31/31 da build nuova, nessun test obbligatorio saltato).

## AC-03 Pacchetto portatile

- Criterio: ZIP estratto in una cartella esterna si avvia senza `build/`, senza
  workspace di sviluppo e senza Python; GUI e CLI partono; `libmp3lame.dll`
  e' opzionale.
- Comando:
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass `
    -File scripts\package_studio.ps1
  powershell -NoProfile -ExecutionPolicy Bypass `
    -File scripts\verify_release.ps1 `
    -ZipPath dist\NL_Breath_Click_Studio_1.0.0-rc.2.zip `
    -PythonExecutable "<python.exe>" -RunGuiTests
  ```
- Stato: **PASS RC** per manifest, dipendenze PE, GUI con/senza LAME, CLI e
  workflow audio/GUI sui binari estratti; manca la prova su macchina pulita.
- Non committare `dist/`; l'hash va registrato in `STATO_PROGETTO.md`.

## AC-04 WAV, MP3, FLAC

- Criterio:
  - WAV PCM16/PCM24/float importabile ed esportabile con metadati compatibili;
  - MP3 importabile ed esportabile (CBR/VBR), durata/canali coerenti;
  - FLAC importabile; export FLAC non richiesto (default WAV);
  - sniffing dal contenuto, non solo estensione;
  - originale invariato dopo analisi/export.
- Evidenza corrente: `bc_codec_tests`, `bc_studio_pipeline_formats`,
  `bc_studio_workflow`, verifica pacchetto.
- Stato: **PASS RC** su fixture sintetiche e codec CBR/VBR; per 1.0 ripetere su almeno un
  WAV, un MP3 e un FLAC reali con hash prima/dopo.

## AC-05 Selezione visibile e distinguibile

- Criterio:
  - regione selezionata con riempimento/bordo/maniglie chiaramente diverse;
  - stato INCLUSO/ESCLUSO leggibile;
  - selezione multipla distinguibile;
  - leggibile ad almeno due livelli di zoom e su waveform densa.
- Evidenza: `bc_studio_gui_interaction` include cattura `PrintWindow` e
  confronto di pixel chiari prima/dopo la selezione.
- Stato: **PASS RC** su 96 DPI del banco di prova; per 1.0 provare anche
  a DPI 125/150% e tema chiaro/scuro.

## AC-06 Undo/Redo reali

- Criterio:
  - un trascinamento centro/bordo produce **una sola** voce di cronologia;
  - Ctrl+Z/Ctrl+Y e menu aggiornano waveform, pannello, contatori, selezione,
    sessione e anteprima;
  - non modificano l'audio originale;
  - funzionano per regioni manuali, esclusioni, modalita' e modifiche multiple.
- Evidenza: GUI test drag → Undo → Redo, regioni manuali, riconciliazione.
- Stato: **PASS RC** per GUI e 600 eventi nel core: gruppi, journal, inverse
  audio e audio esportabile verificati. Resta la conferma d'uso su take reali.

## AC-07 Regioni manuali complete

- Criterio: aggiunta su selezione, spostamento, confini, esclusione,
  reinclusione, eliminazione definitiva, Attenua/Taglia/Dimezza, Undo/Redo.
- La provenienza `automatic`/`manual` resta distinta; una regione manuale non
  diventa una label di training senza conferma esplicita.
- Evidenza: `bc_studio_gui_interaction`, `bc_document_tests`,
  `bc_learning_tests`.
- Stato: **PASS RC** per i percorsi testati, compresa provenienza dopo restore.

## AC-08 Editing fluido

- Criterio: con almeno 600 eventi il drag non ricostruisce il pannello a ogni
  pixel, non blocca la GUI e resta sotto la soglia concordata; cache picchi
  usata; nessun full redraw a ogni movimento.
- Evidenza: `bc_studio_performance` misura il ridisegno con 600 regioni;
  5,50 ms nella build nuova, 5,26 ms nel pacchetto estratto. Drag con snapshot
  riusato e render/autosave rinviati. Ambiente registrato in `TESTING.md`.
- Stato: **PARZIALE**: la metrica esiste e passa la soglia prudente, ma non e'
  una prova FPS su hardware/audio reali; per 1.0 registrare macchina,
  risoluzione, DPI, numero eventi e tempi.

## AC-09 Follow playhead

- Criterio: ON/OFF, scorrimento continuo/paginato, ritorno alla testina,
  nessun contrasto con lo scroll manuale, testina visibile.
- Evidenza: comandi presenti e test GUI di stato; il test automatico non
  include una prova completa con uscita audio reale.
- Stato: **PARZIALE**. Per 1.0 serve una prova con dispositivo audio reale e
  almeno un caso di vista zoomata.

## AC-10 Impostazioni persistenti

- Criterio: OK conferma e chiude; Applica conferma senza chiudere; Annulla
  ripristina i valori confermati; Predefiniti; Salva/Carica preset;
  persistenza dopo riavvio; distinzione tra default globale, preset personale
  e impostazioni del progetto; nessuna modifica al progetto dall'anteprima.
- Evidenza: `bc_studio_detection_settings`,
  `bc_settings_store_tests`; file in
  `%APPDATA%\BreathClickStudio\detector_settings.ini` e `.bcpreset`.
- Stato: **PASS RC** per OK/Applica/Annulla/riavvio/preset; impostazioni DSP
  complete in progetto/sessione e applicate al motore, roundtrip verificato.

## AC-11 Anteprima interattiva, cache e before/after

- Criterio:
  - finestra locale 10–20 s, non analisi full file;
  - waveform + proposte + conteggi durata/provenienza;
  - confronto Prima/Dopo con mantenuti/aggiunti/rimossi e colori;
  - ascolto della finestra;
  - cache locale delle analisi gia' viste;
  - debounce/cancellazione dei lavori vecchi e nessun blocco GUI;
  - nessuna modifica al progetto prima di OK/Applica;
  - nuova analisi riconcilia con manuali/esclusioni e mostra conflitti.
- Evidenza: `detection_preview_worker.*`, test preview, impostazioni e GUI.
  Snapshot immutabile, debounce 220 ms, cancellazione, ultima generazione;
  cache feature per frame in memoria per la vita del dialogo e riuso esatto.
- Stato: **PASS RC** tecnico: finestra 15 s, proposte Prima/Dopo, ascolto,
  nessuna modifica prima di conferma, conflitti conservati. 100 cambi in
  312 ms sul pacchetto estratto. Ascolto professionale ancora da eseguire.

## AC-12 Apprendimento personale Level A

- Criterio:
  - etichette esplicite per respiro confermato, falso positivo, respiro
    mancato confermato, esclusione editoriale e non verificato;
  - inclusione/esclusione editoriale non diventa automaticamente training;
  - solo esempi validati, almeno due registrazioni distinte, split per file;
  - profilo calibrato su score + feature DSP, metrica di validazione,
    attivazione/disattivazione e rollback;
  - nessun aggiornamento automatico dei pesi ONNX;
  - runtime senza Python.
- Evidenza: `bc_learning_tests` verifica esempi, split, profilo, roundtrip e
  rifiuto di dati insufficienti.
- Stato: **PASS RC** per il calibratore core: split SHA-256 PCM, sole label
  umane abilitate, nessun doppione, soglia sul training e metriche del filtro
  effettivo sul holdout; **PARZIALE** per una prova end-to-end GUI con due
  registrazioni reali. Profili schema 1 rifiutati e conservati. Nessun dato
  percettivo/di accuratezza su voci reali. Level B (fine-tuning PyTorch) e'
  fuori dalla 1.0 e deve restare separato.

## AC-13 Riconciliazione nuova analisi

- Criterio: nuova analisi conserva regioni manuali, esclusioni e label
  verificate; rimuove solo automatici non piu' rilevati; aggiunge nuovi;
  ambiguita' segnalate e non sovrascritte; un solo Undo per l'operazione.
- Evidenza: `bc_learning_tests`, GUI interaction con regione manuale esclusa e
  Undo.
- Stato: **PASS RC**: anche molti-a-uno, confini modificati e conteggio
  conflitti persistente nella barra stato; cronologia con 600 regioni.

## AC-14 Dati protetti e confini

- Criterio: nessuna modifica a `materiale_flat`, `AI_Fiati/_lavoro/modello`,
  dataset, annotazioni, pesi, REAPER, originali; nessun push GitHub.
- Evidenza: hash degli originali nei test workflow; stato Git e policy
  `AGENTS.md`.
- Stato: **PASS RC** per le prove eseguite; la verifica va ripetuta su ogni
  macchina reale.

## AC-15 Documentazione e dipendenze

- Criterio: `README.md`, `BUILDING.md`, `docs/ACCEPTANCE_V1.md`,
  `docs/TESTING.md`, `BUILDING.md` consentono a un nuovo sviluppatore di compilare
  e testare senza storia conversazionale; dipendenze esterne e percorsi
  protetti sono espliciti.
- Stato: **PASS RC**, build da archivio Git separato e packaging verificati;
  stato, progressi, provenienza e collaudo reale documentati.

## AC-16 Criteri di uscita 1.0

La standalone 1.0 non si dichiara finche' non sono soddisfatti tutti:

- [ ] build e tutti i 31 test su macchina Windows pulita (AC-01/AC-02);
- [ ] ZIP estratto verificato con GUI/CLI e WAV/MP3/FLAC reali (AC-03/AC-04);
- [ ] GUI verificata a DPI 125/150% e tema chiaro/scuro (AC-05);
- [ ] Undo/Redo e regioni manuali verificati dall'utente su materiale reale
      (AC-06/AC-07);
- [ ] performance e follow playhead misurati con audio reale (AC-08/AC-09);
- [x] anteprima con worker cancellabile e cache frame/probabilita'
      (AC-11);
- [ ] profilo personale end-to-end su due registrazioni reali (AC-12);
- [x] nessun test obbligatorio saltato e limiti noti dichiarati per questa RC;
- [x] nessun push/REAPER/originale modificato (AC-14).

Fino ad allora la designazione corretta e' **release candidata 1.0.0-rc.2**.
