NL Breath Click Studio 1.0.0-rc.2 — prima pre-release Windows x64

Due prodotti indipendenti:
- **Windows Standalone**: estrarre lo ZIP Standalone e avviare bc_studio.exe.
  Non richiede REAPER o Python.
- **REAPER Windows Beta**: estrarre lo ZIP REAPER e seguire README.txt;
  motore CLI, runtime CPU, modelli AI e codec inclusi. Non richiede la standalone.
  Nessuna copia di REAPER inclusa. Provare prima su una copia del progetto.

**ReaPack:** importare
https://raw.githubusercontent.com/Nicotina4ever/NL-Breath-Click-AI/main/index.xml
Cercare NL Breath Click Studio e scegliere esplicitamente 1.0.0-rc.2,
una pre-release. Al primo avvio il backend viene scaricato dal medesimo
asset REAPER, verificato con SHA-256 e installato senza bloccare il pannello.
ReaPack gestisce gli aggiornamenti; chiudere/riaprire il pannello dopo un update.

Revisione dei respiri con bordi modificabili, Attenua/Silenzia/Taglia/Dimezza
sui selezionati, TimeMap cumulativa, Compatta voce/Mantieni sincronizzazione,
A/B sulla stessa traccia, Undo/Redo e calibratore personale condiviso.
Gli originali su disco restano intatti; conservare BreathClick Media con il progetto.

Sorgente esatto nel tag v1.0.0-rc.2; commit in BUILD_INFO.json e capabilities.
Manifest per file incluso e checksum SHA-256 allegati. Licenza MPL-2.0 per
il progetto; MIT per Respiro-en/ONNX Runtime, LGPL-2.0 per LAME con sorgenti
corrispondenti inclusi, altre attribuzioni nei pacchetti.

**Limiti:** release candidate/beta Windows 10/11 x64, backend CPU. Nessun
pacchetto nativo macOS/Linux o GPU in questa release. Take con envelope,
section/reverse e registrazione attiva non sono supportate dall'editing diretto.
Le prove isolate su host Windows esistente non sostituiscono una macchina
Windows pulita. Ascolto professionale, DPI/temi e catene FX complesse richiedono
ancora collaudo umano. Nessun miglioramento di accuratezza rivendicato.

Nicola Lanci Voice Over · info@nicolalanci.com · https://www.nicolalanci.com
