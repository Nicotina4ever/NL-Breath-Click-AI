# Compilazione e verifica

Windows 10/11 x64, Visual Studio 2022 C++/Windows SDK, CMake 3.28.6 o successivo.
`build.ps1` cerca CMake locale/installato/PATH. `-Clean` pulisce i target CMake,
preserva portable e dati di prova e non crea archivi.

## Dipendenze e build DSP

Sono versionati i sorgenti kissfft, dr_mp3 0.7.4, dr_flac 0.13.4, LAME 3.100
e l'header ONNX Runtime. Licenze nei sorgenti e in LICENSES/.
WAV usa il lettore interno, senza dr_wav o FFTW. Il default include i test
e richiede Python con tests/requirements.txt; per la sola applicazione
aggiungere -DBC_BUILD_TESTS=OFF.

```powershell
powershell -File scripts/verify_dependencies.ps1 -SourcesOnly
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\bin\Release\bc_studio.exe
```

Questa build funziona in DSP senza file di modelli. AI/Fusion richiedono:

- `third_party/onnxruntime/onnxruntime.dll`, build CPU fissata dal lock;
- `models/generale/respiro-en-dinamico.onnx` e `.onnx.data`.

Dimensioni/SHA-256: `docs/DEPENDENCIES.lock.json` e manifest modello.
Questi tre artefatti non sono in Git. Recuperarli dalla distribuzione corrente
o da un deposito esterno con quegli hash. Il lock identifica un'esportazione
dinamica specifica: un checkpoint upstream generico non e' intercambiabile.
Nessun download, conversione o cambio versione automatico. Fonti/licenze in
THIRD_PARTY_NOTICES.md. Possono essere condivisi senza copiarli:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBC_ONNX_RUNTIME_FILE="<runtime>/onnxruntime.dll" -DBC_MODEL_DIR="<modello>/generale"
powershell -File scripts/verify_dependencies.ps1 `
  -RuntimeFile "<runtime>/onnxruntime.dll" -ModelDirectory "<modello>/generale"
```

## Suite completa

Python 3.11, pacchetti in `tests/requirements.txt`. Nessun test installa
pacchetti o richiede registrazioni private. Predisporre gli artefatti sopra.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBC_REQUIRE_RELEASE_TESTS=ON -DBC_PYTHON_EXECUTABLE="<python.exe>"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Sul tag RC2 sono obbligatori **33 test**. Il main successivo ne registra
**35 su Windows completo**, includendo contratti di piattaforma e worker
portabile; il conteggio headless dipende dalla piattaforma.
Il vecchio mock degli envelope P1 e' rimosso insieme all'interfaccia superata;
l'integrazione diretta ha il proprio collaudo REAPER reale.
La GUI richiede un desktop Windows interattivo accessibile. Un errore del
harness non dimostra automaticamente un difetto della GUI.
Per checkout pulito usare una build separata, `BC_STUDIO_TEST_WORK` vuota e
gli artefatti condivisi sopra. I test usano i binari della propria build.

## Classificatori personali legacy

Tre header di coefficienti generati in locale sono esclusi da Git; i file e
la pipeline di training restano intatti. Attivazione in una build privata:

```powershell
cmake -S . -B build -DBC_PRIVATE_MODEL_DIR="<directory-dei-tre-header>"
```

Il default e' vuoto. Sono ancora caricabili profili da file; un classificatore
assente viene rifiutato senza inventare probabilita'. DSP, ONNX e calibratore
Core sono indipendenti da questi pesi. Non distribuire build con pesi privati.

## Pacchetto

```powershell
powershell -File scripts/package_studio.ps1 -SourceCommit "<commit-dei-binari>"
powershell -File scripts/verify_release.ps1 `
  -ZipPath dist/NL_Breath_Click_Studio_Windows_Standalone_1.0.0-rc.2.zip `
  -PythonExecutable "<python.exe>" -RunGuiTests
```

Include runtime VC++, ONNX, modello generale, guide, licenze e sorgenti LAME.
Il nuovo ZIP completo sostituisce quello corrente senza `history/`; lo stage
riuscito viene eliminato. La distribuzione corrente conserva i due ZIP
indipendenti standalone/REAPER e i relativi manifest/checksum.
La verifica controlla hash, dipendenze PE, GUI/audio e codec opzionale in
estrazione isolata. Non equivale a una VM Windows pulita.

## REAPER

```powershell
powershell -File reaper/tests/run_isolated.ps1 `
  -ReaperExecutable "<REAPER>/reaper.exe" -Script reaper/tests/integration.lua
<python.exe> reaper/tests/verify_audio.py <run>/result.json
powershell -File reaper/tests/run_isolated.ps1 `
  -Script reaper/tests/ui_harness.lua -TestProject "<gui_project>" -RunGuiInputTests
```

La prima preparazione legge l'eseguibile indicato. Le prove successive riusano
`build/reaper_sandbox/portable`, con hash e mutex. Non iniziare un test con
l'istanza aperta. Le prove GUI aspettano il puntatore libero e interrompono
l'input se viene mosso contemporaneamente. Il launcher locale
`reaper/Apri_NL_Breath_Click_Studio.cmd` apre la demo senza deploy professionale.
