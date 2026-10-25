# NL Breath Click Studio 1.0.0-rc.2

[Italiano](#italiano) · [English](#english)

**[Segnala un problema / Suggerisci una funzione · Report a problem / Suggest a feature](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues/new)**

## Italiano

Prima pre-release per **Windows 10/11 a 64 bit**, dedicata all'editing dei
respiri nel doppiaggio e nello speakeraggio.

### Scegli il download

- **[Windows Standalone](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_Windows_Standalone_1.0.0-rc.2.zip)**: applicazione completa per importare, rivedere ed esportare registrazioni. Non richiede REAPER.
- **[REAPER Windows Beta](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_REAPER_Windows_Beta_1.0.0-rc.2.zip)**: pannello per intervenire sugli item del progetto. Richiede REAPER 7.x x64, non incluso; non richiede la standalone.

I due ZIP sono indipendenti e comprendono motore, modelli e componenti
necessari. Non serve Python. I file denominati **Source code** sono per
chi vuole compilare il programma, non per installarlo.

### Come iniziare

**Standalone:** estrai tutto lo ZIP, avvia `bc_studio.exe`, apri una cartella
dal menu Progetto e analizza i file. Rivedi i respiri, confronta Originale
ed Elaborato ed esporta nuovi WAV o MP3.
[Guida standalone](https://github.com/Nicotina4ever/NL-Breath-Click-AI/blob/main/docs/USER_GUIDE.md).

**REAPER:** segui il `README.txt` nello ZIP per installare il pannello nelle
azioni, oppure usa ReaPack. Seleziona gli item vocali, analizza e tratta
il respiro selezionato direttamente sulla stessa take.
[Installazione ZIP, ReaPack e guida REAPER](https://github.com/Nicotina4ever/NL-Breath-Click-AI/blob/main/docs/REAPER_GUIDE.md).

### Cosa contiene

Confini modificabili, interventi manuali, selezione multipla, Attenua,
Silenzia, Taglia e Dimezza, confronto Originale/Elaborato e Undo/Redo.
La standalone importa WAV/MP3/FLAC, esporta WAV/MP3 e lavora anche in batch.
In REAPER puoi scegliere **Compatta voce** oppure **Mantieni sincronizzazione**,
senza tracce duplicate permanenti. Il profilo personale dalle correzioni
confermate è facoltativo.

Gli originali su disco restano intatti. Salva il lavoro e conserva
**BreathClick Media** insieme al progetto REAPER.

### Limiti della candidata

- È una release candidate, con integrazione REAPER in beta. Provala su una copia del progetto e ascolta i raccordi prima di una consegna. Collaudo su Windows pulito e ascolto professionale restano da completare; prova anche le tue catene FX e le impostazioni di visualizzazione.
- In REAPER non sono supportati take con envelope, stretch marker, sorgenti section/reverse o registrazione in corso.
- La standalone tratta anche click brevi; la riparazione avanzata degli schiocchi di saliva e i nuovi controlli indipendenti respiri/click sono ancora in sviluppo e **non sono inclusi nella RC2**.
- Questa release offre pacchetti Windows x64 con elaborazione CPU. Non include applicazioni native macOS/Linux o un backend GPU.

## English

The first pre-release for **Windows 10/11, 64-bit**, for breath editing
in voice-over, narration and dubbing.

### Choose your download

- **[Windows Standalone](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_Windows_Standalone_1.0.0-rc.2.zip)**: the complete application for importing, reviewing and exporting recordings. REAPER is not required.
- **[REAPER Windows Beta](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_REAPER_Windows_Beta_1.0.0-rc.2.zip)**: a panel for editing items in your project. Requires REAPER 7.x x64, not included; the standalone is not required.

Each ZIP includes its own engine, models and required components.
No Python installation is needed. GitHub's **Source code** downloads are
for building the software, not for installing it.

### Get started

**Standalone:** extract the entire ZIP, open `bc_studio.exe`, open a folder
from **Progetto** (Project), then analyze the files. Review the breaths,
compare **Originale/Elaborato** (Original/Processed) and export new WAV or MP3 files.

**REAPER:** follow the English `README.txt` in the ZIP to install the panel
as an action, or use ReaPack. Select voice items, analyze, then apply a
treatment to the selected breath on the same take.
[English overview and quick start](https://github.com/Nicotina4ever/NL-Breath-Click-AI/blob/main/README.en.md).

### Included

Adjustable boundaries, manual edits, multiple selection, attenuation,
silence, cut and halve, Original/Processed comparison and Undo/Redo.
The standalone imports WAV/MP3/FLAC and exports WAV/MP3, including batches.
In REAPER, choose **Compatta voce** (Compact voice) or **Mantieni sincronizzazione**
(Keep synchronization), without permanent duplicate tracks.
A personal profile based on confirmed corrections is optional.

Original audio files remain intact. Save your work and keep
**BreathClick Media** with the REAPER project.

### Candidate limitations

- This is a release candidate; the REAPER integration is a beta. Try a copy of your project and listen to transitions before a delivery. Clean-Windows testing and professional listening validation are still pending; also try your own FX chains and display settings.
- REAPER direct editing does not support take envelopes, stretch markers, section/reverse sources or active recording.
- The standalone also treats short clicks. Advanced saliva-click repair and the new independent breath/click controls are in development and **are not included in RC2**.
- Windows x64 CPU packages only. No native macOS/Linux applications or GPU backend are included.

## Licenza / License · Assistenza / Support

Gratuito e open source / Free and open source:
[MPL-2.0](https://github.com/Nicotina4ever/NL-Breath-Click-AI/blob/main/LICENSE).
Licenze dei componenti / Third-party licenses:
[THIRD_PARTY_NOTICES](https://github.com/Nicotina4ever/NL-Breath-Click-AI/blob/main/THIRD_PARTY_NOTICES.md)
e / and `LICENSES` nei pacchetti / in the packages.

[Segnala un problema o suggerisci un miglioramento / Report a problem or suggest an improvement](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues).

Per controllare i download / To check the downloads:
[SHA256SUMS.txt](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/SHA256SUMS.txt).
Il tag RC2 identifica i sorgenti dei pacchetti; gli aggiornamenti alle guide
non cambiano i binari. / The RC2 tag identifies the package sources;
documentation updates do not change the binaries.

**Nicola Lanci Voice Over** · [info@nicolalanci.com](mailto:info@nicolalanci.com) ·
[www.nicolalanci.com](https://www.nicolalanci.com)
