# BreathClick Studio 1.0.0-rc.2

Release candidata per Windows x64, con workflow multi-file per cartelle di
spot. Il WAV originale non viene mai sovrascritto.


## Novità della candidata 1.0

- Selezione molto più visibile: riempimento distinto, bordo luminoso, maniglie,
  stato INCLUSO/ESCLUSO e colore diverso per la selezione multipla.
- Undo/Redo reali e atomici: un trascinamento è **una sola** operazione, non
  centinaia; funzionano anche per regioni manuali, esclusioni e modalità.
- Regioni manuali complete: aggiunta, spostamento, confini, esclusione,
  reinclusione ed eliminazione, con la stessa logica delle automatiche.
- **Segui cursore** durante la riproduzione, con ON/OFF e blocco quando si
  sposta la vista a mano.
- Impostazioni di rilevamento persistenti con OK/Applica/Annulla, Predefiniti,
  Salva/Carica preset e profili Conservativo/Bilanciato/Alta sensibilità.
- **Anteprima locale** del rilevamento (10–20 s) con prima/dopo, conteggi
  mantenuti/aggiunti/rimossi, ascolto della sola finestra e cache delle feature in memoria e cancellazione dei lavori superati.
- Riconciliazione sicura: una nuova analisi conserva manuali ed esclusioni;
  i conflitti ambigui vengono mostrati e non sovrascritti.
- Prima versione reale di **apprendimento dalle correzioni**: etichette
  esplicite, profilo personale con validazione indipendente e rollback.
  I pesi ONNX di Respiro-en **non** vengono aggiornati automaticamente.

## Contenuto

```text
bc_studio.exe                  applicazione GUI
bc_studio_cli.exe              CLI offline
libmp3lame.dll                 encoder MP3 opzionale (caricato on demand)
onnxruntime.dll                runtime CPU
msvcp140*.dll, vcruntime*.dll  runtime Visual C++
models\generale\               modello Respiro-en e attribuzione
LICENSES\                      testi delle licenze di terze parti
README.md                      questo file
GUIDA_STUDIO.md                guida operativa
LEGGIMI_AVVIO.txt              avvio rapido
TESTING.md          protocollo e checklist di collaudo
CHANGELOG.md                   note della release candidate
THIRD_PARTY_NOTICES.md         attribuzioni
BreathClickStudio_1.0.0-rc.2.zip.sha256                 hash del pacchetto (fornito accanto allo ZIP)
```

## Requisiti

- Windows 10/11 x64.
- Nessuna installazione aggiuntiva richiesta.
- Il pacchetto è autosufficiente: estrarre lo ZIP in una cartella scrivibile.
- WAV, MP3 e FLAC in ingresso; esportazione WAV PCM o MP3.
- Se `libmp3lame.dll` viene rimossa, la GUI si avvia ancora e importa MP3;
  solo l'esportazione MP3 mostra un singolo errore, mentre il WAV resta usabile.

## Avvio

1. Estrarre lo ZIP in una cartella dedicata.
2. Aprire `bc_studio.exe`.
3. **Progetto → Apri cartella di WAV/MP3/FLAC...** per importare gli spot.
4. Verificare la CLI aprendo un prompt nella cartella e digitando:

   ```powershell
   .\bc_studio_cli.exe selftest
   ```

5. Tenere una copia dell'audio originale in una cartella separata.

## Flusso consigliato

```text
Apri cartella (dialogo moderno di Windows)
  -> Analizza tutti (tutti gli eventi restano inclusi)
  -> Correggi solo le eccezioni con Canc/A, mouse, pannello contestuale
  -> Scegli Attenua / Taglia / Dimezza, per evento o modalita' globale
  -> Scegli formato esportazione (Stesso formato / WAV / MP3) e qualita'
  -> Impostazioni (anteprima locale Prima/Dopo) e profili rapidi
  -> Zoom orizzontale/verticale, segui cursore e anteprima
  -> Salva progetto
  -> Elabora ed esporta tutti (un solo riepilogo globale)
  -> Controlla breathclick_export_report.json
```

Il progetto `.bcnproj.json` contiene le sessioni per file e le decisioni
editoriali. L'autosalvataggio è in `%TEMP%\BreathClick` e si riprende dal menu
**Progetto → Riprendi autosalvataggio**.

## Sicurezza dei dati

- Il WAV originale **non viene sovrascritto**.
- Le esportazioni vanno sempre in una cartella scelta dall'utente e sono
  scritte in modo atomico.
- Sessioni e piani sono file separati (`.bcnstudio.json`, `.plan.json`,
  `.edit.json`).
- Prima di un collaudo su materiale reale, conservare una copia verificata
  degli originali.

## Stato

Versione **1.0.0-rc.2**: workflow multi-file e progetto, inclusione automatica
degli eventi con correzione delle sole eccezioni, importazione WAV/MP3/FLAC ed
esportazione WAV o MP3 (LAME, CBR/VBR), selezione visibile, Undo/Redo atomici,
regioni manuali complete, segui cursore, impostazioni persistenti con anteprima
locale e riconciliazione sicura, prima versione del profilo personale.
La suite di release registra **31 CTest**: automazione Win32 della GUI,
600 eventi, cronologia, errori di salvataggio, calibratore e audio sintetico
WAV/MP3/FLAC. Gli esiti effettivi sono nel changelog e nel registro progetto.
Restano da completare Windows pulito, DPI/tema, uscita audio reale e ascolto.
Il collaudo breve e' in `TESTING.md`.

Vedere `TESTING.md` per il piano di prova e i criteri di uscita.

## Licenze

Il codice BreathClick Studio è privato. Componenti di terze parti:

- ONNX Runtime — MIT (Microsoft)
- Respiro-en — MIT (Dong Yang, Tomoki Koriyama, Yuki Saito)
- kissfft — BSD-3-Clause (Mark Borgerding)
- Visual C++ Runtime — termini Microsoft, vedere
  `LICENSES/Microsoft-VC-Runtime.txt`

I testi completi sono in `LICENSES/`; i termini Microsoft prevalgono su
qualsiasi riepilogo.
