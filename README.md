# BreathClick Studio

Editor audio offline Windows e integrazione REAPER per respiri e click nel
doppiaggio e nello speakeraggio. Standalone **1.0.0-rc.2**, REAPER **0.2 preview**.
Ascolto professionale e prova su Windows pulito restano necessari per la 1.0.

## Funzioni

- WAV/MP3/FLAC in ingresso; DSP, Respiro-en ONNX e Fusion.
- Regioni manuali, selezione multipla, bordi modificabili, Undo/Redo.
- Attenua, Silenzia, Taglia e Dimezza; A/B, export WAV/MP3, batch e report.
- Progetti/sessioni e TimeMap cumulativa in campioni.
- REAPER: applicazione immediata al singolo respiro sulla stessa take/traccia;
  Compatta voce oppure Mantieni sincronizzazione, senza doppie tracce permanenti.
- Calibratore personale Core condiviso: etichette esplicite e registrazioni
  indipendenti per training/validazione. Nessun fine-tuning automatico ONNX.

Gli originali su disco rimangono intatti. Conservare i media generati insieme
ai progetti. Esclusione editoriale e label di training sono decisioni distinte.

## Compilazione

Windows x64, Visual Studio 2022 C++/Windows SDK, CMake 3.28.6 o successivo.
I sorgenti delle dipendenze sono inclusi. La configurazione predefinita include
i test e richiede Python con `tests/requirements.txt`. Per compilare la sola
applicazione senza i test passare `-DBC_BUILD_TESTS=OFF`.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\bin\Release\bc_studio.exe
```

La build DSP non richiede modelli privati. DLL ONNX e modello generale sono
artefatti esterni opzionali, con hash fissati; per Fusion, test completi e
packaging seguire [BUILDING.md](BUILDING.md).

## Utilizzo

Standalone: **Importa -> Analizza -> Rivedi -> Anteprima -> Esporta**.
[Guida operativa](docs/USER_GUIDE.md).

REAPER: caricare `reaper/BreathClickStudio.lua` nelle azioni e scegliere la CLI
corrente con **Motore**. Selezionare un item e analizzarlo, poi selezionare un
respiro e premere il trattamento. [Istruzioni REAPER](reaper/LEGGIMI.txt).
I test riusano una sola portable isolata; nessun deploy professionale automatico.

## Componenti

| Percorso | Responsabilita' |
| --- | --- |
| `core/` | Audio I/O, DSP, documento, regioni, learning e persistenza |
| `standalone/engine/` | StudioEngine: ONNX, EditPlan, TimeMap, sessioni e progetti |
| `standalone/`, `editor/` | GUI Windows e CLI |
| `reaper/` | Pannello Lua, bridge CLI, test nativi |
| `third_party/` | Sorgenti delle dipendenze; binari esclusi |
| `scripts/` | Dipendenze, packaging e verifica ZIP |

Core e StudioEngine sono i punti di condivisione per NL Audio Tools.
GUI e adapter REAPER rimangono separati; il motore non dipende da REAPER.

## Verifica e licenze

[Test e collaudo](docs/TESTING.md), [criteri 1.0](docs/ACCEPTANCE_V1.md),
[Mozilla Public License 2.0 (MPL-2.0)](LICENSE), [attribuzioni](THIRD_PARTY_NOTICES.md).
Audio privato, dataset, pesi personali, modelli ONNX, output, cache e runtime
REAPER sono esclusi dal sorgente pubblico. Storia locale di sviluppo e ramo
destinato a GitHub rimangono separati.
