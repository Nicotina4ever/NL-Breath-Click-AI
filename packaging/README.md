# NL Breath Click Studio — Windows Standalone 1.0.0-rc.3 sperimentale

Un editor per rivedere e trattare i respiri nelle registrazioni vocali.
Funziona senza REAPER e senza Python; l'elaborazione audio è locale.

## Avvio

1. Estrai tutto lo ZIP in una cartella scrivibile.
2. Apri `bc_studio.exe`.
3. Dal menu **Progetto**, apri una cartella di WAV, MP3 o FLAC e scegli **Analizza tutti i file**.
4. Ascolta e correggi le proposte. Seleziona un evento per attenuarlo, silenziarlo, tagliarlo o accorciarlo.
5. Confronta **Originale** ed **Elaborato**, salva il progetto ed esporta nuovi WAV o MP3.

Gli eventi sono già inclusi: correggi solo quelli che ne hanno bisogno.
**Canc** esclude; **A** include di nuovo. **Ctrl+Z/Ctrl+Y** annullano o ripetono.
I bordi delle regioni si possono trascinare. **F1** apre l'aiuto; **F2** le impostazioni.

Per provare respiri e click, leggi **PROVA_RC3.md**. In **F2 → Impostazioni**
puoi attivare soltanto respiri, soltanto click oppure entrambi. Per i click
usa **Ripara click**, poi confronta Originale, Elaborato e Solo rimosso.
Auto può lasciare intatto un evento quando la ricostruzione è incerta.

`GUIDA_STUDIO.md` descrive i comandi di base della RC2; le novità della RC3
e il breve collaudo d'ascolto sono in `PROVA_RC3.md`.
Il programma conserva gli originali. Salva il progetto insieme alle sessioni
e alle registrazioni sorgenti. Attenua e Silenzia mantengono la durata;
Taglia e Dimezza accorciano il risultato.

## English quick start

Extract the entire ZIP into a writable folder and run `bc_studio.exe`.
In **Progetto** (Project), open a recordings folder and choose
**Analizza tutti i file** (Analyze all files). Review the suggestions, apply
a treatment, compare **Originale/Elaborato** (Original/Processed), save and export.
No REAPER or Python installation is required. Original files remain intact.
Use **F2** to enable breaths, clicks or both. Select a click and use its repair
command, then compare Original, Processed and Removed signal. **Auto** may
leave uncertain audio untouched. See **PROVA_RC3.md** for the listening trial.

## Versione candidata / Release candidate

Windows 10/11 x64, CPU. Prova una copia del lavoro e ascolta i raccordi prima
di una consegna. La riparazione degli schiocchi di saliva è sperimentale e
richiede il tuo ascolto: non è ancora validata per uso professionale.
Non sono disponibili applicazioni macOS/Linux in questa versione.

Try a copy of your work and listen to edit transitions before a delivery.
Saliva-click repair is experimental and needs listening validation before
professional use. There are no macOS/Linux applications in this version.

## Licenza e assistenza / License and support

Gratuito e open source / Free and open source: MPL-2.0, vedere / see `LICENSE`.
Componenti di terze parti / Third-party components: `THIRD_PARTY_NOTICES.md`
e / and `LICENSES/`.

[Download e guide / Downloads and guides](https://github.com/Nicotina4ever/NL-Breath-Click-AI)
· [Problemi e suggerimenti / Issues and suggestions](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues)

Nicola Lanci Voice Over · info@nicolalanci.com · https://www.nicolalanci.com
Versione e contatti / Version and contacts: **Aiuto → Informazioni**.
