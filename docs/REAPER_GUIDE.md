# Guida REAPER — NL Breath Click Studio RC2

[Presentazione e download](../README.md) · [Standalone](USER_GUIDE.md) · [English quick start](#english-quick-start)

L'integrazione permette di rivedere e trattare i respiri direttamente nel
progetto REAPER. Ogni intervento si applica alla stessa take, senza creare
tracce duplicate permanenti. I file audio originali sul disco restano intatti.

Serve **Windows 10/11 x64 e REAPER 7.x x64**. REAPER non è incluso;
la standalone e Python non sono necessari. Scegli una delle due installazioni seguenti.

## Installazione con ZIP

1. Scarica [REAPER Windows Beta 1.0.0-rc.2](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_REAPER_Windows_Beta_1.0.0-rc.2.zip) ed estrai tutto il contenuto.
2. In REAPER apri **Options → Show REAPER resource path**.
3. Copia la cartella estratta in **Scripts/NL Breath Click Studio** dentro la cartella risorse, conservando le sottocartelle. Se il pannello è già aperto, chiudilo prima.
4. Apri **Actions → Show action list → New action → Load ReaScript** e carica **BreathClickStudio.lua** dalla cartella appena copiata.
5. Esegui l'azione; puoi assegnarle una scorciatoia nell'elenco delle azioni.

Lo ZIP contiene il motore, i modelli e i componenti necessari: l'uso non
richiede download aggiuntivi. In alternativa alla copia manuale, il
`README.txt` incluso spiega come usare `Install_REAPER.ps1`.

## Installazione con ReaPack

Se usi già ReaPack:

1. Apri **Extensions → ReaPack → Import repositories** e incolla questo indirizzo:

   ```text
   https://raw.githubusercontent.com/Nicotina4ever/NL-Breath-Click-AI/main/index.xml
   ```

2. In **Browse packages** cerca **NL Breath Click Studio** e scegli esplicitamente la versione **1.0.0-rc.2**, indicata come pre-release.
3. Esegui l'azione installata. Al primo avvio il pannello scarica il motore ufficiale e ne controlla l'integrità. È necessaria una connessione Internet per questo passaggio.
4. Attendi il completamento prima di analizzare. Se il download fallisce, usa **Installa motore** per riprovare.

ReaPack gestisce gli aggiornamenti del pannello; chiudilo e riaprilo dopo
un aggiornamento. Il motore già installato viene riutilizzato se compatibile.
Non è necessario scaricare anche la standalone.

## Il primo intervento

1. Apri una copia del progetto, salvala e seleziona gli item vocali da analizzare.
2. Premi **Analizza** nel pannello.
3. Seleziona un respiro dalla lista o dalla timeline. Puoi usare **Ctrl/Shift** per più eventi.
4. Correggi i confini trascinando le maniglie gialle sulla forma d'onda del pannello. **Bordi** permette valori precisi; **Usa selezione** adotta l'intervallo temporale di REAPER.
5. Premi **Attenua**, **Silenzia**, **Taglia** o **Dimezza**: il trattamento riguarda soltanto gli eventi selezionati.
6. Confronta **Originale** ed **Elaborato** e ascolta anche le parole vicine.

Gli eventi proposti e quelli **APPLICATI** sono distinguibili nel pannello.
Puoi cambiare i confini anche dopo un intervento: il risultato viene aggiornato.
Per elaborare insieme le proposte incluse resta disponibile **Applica incluse**.
Non devi confermare o classificare individualmente tutti i respiri.

## Compattare o mantenere la sincronizzazione

| Modalità | Effetto di Taglia e Dimezza |
| --- | --- |
| **Compatta voce** | Accorcia il materiale dentro l'item: le parole e i respiri successivi nello stesso item vengono anticipati |
| **Mantieni sincronizzazione** | Sostituisce il tempo eliminato con una pausa, mantenendo durata e posizioni complessive |

Ad esempio, accorciando un respiro di 250 ms in modalità Compatta, il
materiale successivo dello stesso item arriva 250 ms prima. Altri
accorciamenti si sommano. Gli altri item e le altre tracce non vengono
spostati automaticamente: se devi conservare cue o sincronismo a video,
scegli Mantieni sincronizzazione.

Attenua e Silenzia conservano la durata. Ascolta i punti di raccordo,
soprattutto dove un respiro è vicino all'attacco della voce.

## Ascolto, Undo e salvataggio

**Originale/Elaborato** cambia l'ascolto sulla stessa take e traccia.
Non serve una seconda traccia per il confronto.
**Spazio** avvia o ferma l'ascolto del contesto.

Usa **Ctrl+Z/Ctrl+Y** o i comandi **Undo/Redo** del pannello per annullare
o ripetere un intervento completo. Se hai scelto Originale per confrontare,
torna a Elaborato prima di proseguire con l'ascolto del montaggio.

Salva normalmente il progetto REAPER. **Conserva BreathClick Media insieme
al progetto**: contiene i media generati necessari per riaprire il lavoro.
Gli originali non vengono riscritti; la cartella dei media elaborati non è
una copia da eliminare durante il montaggio.

## Correzioni e profilo personale

L'apprendimento è facoltativo e separato dalle scelte di montaggio:

- **Conferma respiro** registra un respiro riconosciuto correttamente.
- **Falso positivo** segnala una proposta che non è un respiro.
- **Mancato** conferma un evento manuale o crea un respiro dalla selezione temporale.
- **Escludi** toglie un intervento dall'elaborazione, senza chiamarlo falso positivo.

**Esporta etichette** salva le conferme in una sessione utilizzabile anche
nella standalone. **Crea profilo** usa le conferme degli item selezionati:
servono respiri e falsi positivi da registrazioni indipendenti. Copie e take
con offset della stessa registrazione non contano come audio diversi.
Se i dati non bastano, il programma spiega cosa manca.

**Carica profilo** apre anche un profilo della standalone; **Profilo**
lo attiva o disattiva per le analisi successive.
Il profilo non modifica automaticamente i pesi del modello AI.

## Aggiornamenti e limiti

Per l'installazione ZIP, chiudi il pannello e usa il nuovo pacchetto completo.
Con ReaPack, aggiorna dai suoi comandi e riapri il pannello.
Non è necessario installare entrambe le modalità.

La RC2 è una beta Windows con elaborazione CPU. L'editing diretto non
supporta take con envelope, stretch marker, sorgenti section/reverse o
registrazione in corso. Per questi casi lavora, se opportuno, su una copia
consolidata. Prova prima le tue catene FX e conserva una copia del progetto.
Il collaudo su Windows pulito e l'ascolto professionale restano da completare.

La riparazione avanzata degli schiocchi di saliva è ancora in sviluppo e non
fa parte di questo pacchetto. Non sono ancora disponibili pacchetti nativi
macOS o Linux.

Il comando **Motore** permette anche di scegliere un backend compatibile
già presente, ma questa condivisione è facoltativa: nessun'altra applicazione
Breath Click deve essere installata.

## English quick start

1. Choose either the self-contained REAPER ZIP or ReaPack; the standalone is not required.
2. ZIP: extract all files under **Scripts/NL Breath Click Studio** in REAPER's resource folder. Load **BreathClickStudio.lua** from **Actions → Show action list → New action → Load ReaScript**.
3. ReaPack: import the index above, select the **1.0.0-rc.2 pre-release**, then run the action. The engine downloads on first use; wait until installation finishes.
4. Save a copy of your project, select voice items and click **Analizza** (Analyze).
5. Select a breath, drag its boundaries or use **Bordi** (Bounds), then apply **Attenua**, **Silenzia**, **Taglia** or **Dimezza** (Attenuate, Silence, Cut, Halve).
6. **Compatta voce** shortens audio inside the item. **Mantieni sincronizzazione** preserves overall duration. Other items and tracks are not automatically moved.
7. Compare **Originale/Elaborato** (Original/Processed), use Undo/Redo as needed, and save. Keep **BreathClick Media** with the project.

See the ZIP's English `README.txt` for installation details.
This is a Windows beta; advanced saliva-click repair is not included.
Take envelopes, stretch markers, section/reverse sources and active recording
are unsupported. Test a copy of your project before using it for a delivery.

## Assistenza

**Informazioni** mostra versione e contatti.
[Segnala problemi o suggerimenti](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues)
specificando anche la versione di REAPER e il trattamento applicato.

Nicola Lanci Voice Over · [info@nicolalanci.com](mailto:info@nicolalanci.com) ·
[www.nicolalanci.com](https://www.nicolalanci.com)
