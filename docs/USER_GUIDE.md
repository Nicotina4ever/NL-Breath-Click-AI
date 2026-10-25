# Guida operativa — NL Breath Click Studio 1.0.0-rc.2

Questa guida descrive la versione consegnata. Non copre REAPER, training o
strumenti di sviluppo. Il WAV originale non viene mai modificato.

## 1. Avvio e progetto

1. Estrarre lo ZIP in una cartella scrivibile.
2. Avviare `bc_studio.exe`.
3. Menu **Progetto → Apri cartella di WAV...**: si apre il dialogo moderno di
   Windows, con barra del percorso modificabile, ricerca, dischi e cartelle
   sincronizzate. Sono accettati anche file indicati sulla riga di comando.
4. Il pannello a sinistra mostra i file, lo stato e il numero di eventi.

Stati visibili:

- `da analizzare`: file importato;
- `analizzato`: eventi rilevati e **già inclusi** nel piano, salvo eccezioni;
- `elaborato`: file già esportato;
- `errore`: ultima operazione fallita; il messaggio è nel report.

Doppio clic su una riga, oppure **Ctrl+PagSu / Ctrl+PagGiù**, cambia file.
`Ctrl+Shift+O` apre una nuova cartella; `Ctrl+Shift+S` salva il progetto.

## 2. Analizzare tutti

**Progetto → Analizza tutti i file** analizza ogni WAV in sequenza. Il titolo
mostra l'avanzamento. Tutti gli eventi rilevati sono inclusi automaticamente:
non serve approvarli uno per uno. Le proposte automatiche **non sono**
etichette di training (`training_eligible = false`).

## 3. Correggere le eccezioni

Scegli **REGIONI** per gli eventi o **SELEZIONE TEMPO** per trascinare ovunque.
Con lo strumento temporale la fascia resta visibile dopo il rilascio: i bordi
sono trascinabili e **Confini selezione...** permette la correzione numerica.
Esc cancella la selezione senza modificare l'audio. Pulsanti e menu contestuale:
Attenua (dB/fade), Silenzia, Taglia, Dimezza durata, Ripristina. Gli interventi
sono nel piano, annullabili e salvati nel progetto; la waveform resta sulla
timeline originale. Attenua/Silenzia mantengono la durata; Taglia/Dimezza la
ricompattano e usano la TimeMap per il playback.
Applicando un altro trattamento allo stesso intervallo si modifica l'intervento
esistente, con un solo Undo. Invio conferma i dialoghi; Esc annulla. Le etichette
mostrano ATTENUA/SILENZIA/TAGLIA/DIMEZZA. Waveform e curva restano visibili nelle
regioni: la curva rappresenta il gain sulla sorgente, i tagli cambiano la durata
nell'anteprima e non sono disegnati come attenuazioni.

Per molti eventi usa Shift+clic, oppure seleziona un intervallo e premi
**Eventi nell'intervallo**. Includi/Escludi e Undo agiscono sul gruppo. Il filtro
cambia solo la vista: respiri, click, manuali, automatici, esclusi, incerti.
Per disattivare i click usa **Escludi tutti i click**; il menu Regione offre
anche **Reincludi tutti i click**. L'indicatore di possibile parlato segnala
proposte da verificare, non label confermate.

Giallo = respiro, rosso = click, ciano = manuale, grigio = escluso, blu/viola =
selezione. Originale (Ctrl+1) usa il sorgente; Elaborato (Ctrl+2) genera il piano
corrente se necessario. Le modifiche invalidano l'anteprima. Le attenuazioni
sovrapposte non si sommano. Conflitti fra interventi di timeline sono segnalati
prima dell'export: correggere o ripristinare uno degli interventi.

La forma d'onda risponde al mouse:

- **clic** su una regione: la seleziona e aggiorna il pannello contestuale;
- **trascina il centro** della regione: sposta l'intero evento mantenendo la
  durata;
- **trascina un bordo**: modifica solo inizio o fine; il cursore cambia;
- **Ctrl+clic**: selezione multipla;
- **Canc**: esclude l'evento dal piano ma non lo cancella dal progetto;
- **A** / **R**: reincludi / escludi l'evento o il gruppo;
- **Elimina definitivamente** esiste solo come comando esplicito nel menu
  Regioni.

Scorciatoie attive anche con il fuoco sulla forma d'onda:

| Tasto | Azione |
|---|---|
| `N` / `P` | evento successivo / precedente |
| `A` / `R` | includi / escludi |
| `L` | ascolta il contesto |
| `Canc` | esclude (non elimina) |
| `G` | vai al respiro selezionato |
| `Spazio` | riproduci / pausa |
| `Ctrl+Z` / `Ctrl+Y` | annulla / ripristina |
| `+` / `-` | zoom orizzontale |
| `Ctrl+rotella` o rotella | zoom centrato sul puntatore |
| `F1` | elenco completo dei tasti |

Il pannello contestuale mostra subito `Respiro selezionato`, stato
Incluso/Escluso, inizio e fine in secondi, durata in millisecondi, intervento,
fade e margine. **Modifica confini...** apre un dialogo numerico per correggere
inizio e fine senza trascinare.


### 3bis. Annulla, ripristina e regioni manuali

- **Ctrl+Z** / **Ctrl+Y** oppure menu **Modifica → Annulla/Ripristina**.
- Un trascinamento completo (centro o bordo) è **una sola** operazione da
  annullare; non genera centinaia di passi.
- Undo/Redo aggiornano forma d'onda, pannello, contatori, selezione e anteprima;
  non modificano l'audio originale.
- Una regione manuale si crea trascinando un intervallo libero e scegliendo
  **Regione → Aggiungi sulla selezione**. Poi si sposta, si ridimensiona, si
  esclude/reinclude, si elimina o riceve Attenua/Taglia/Dimezza come una
  regione automatica.
- L'inclusione/esclusione editoriale è una decisione di montaggio; **non** è
  automaticamente un'etichetta di training.

## 4. Pannello contestuale e legenda

Il pannello a destra contiene:

- contatori `Rilevati`, `Inclusi`, `Esclusi`, `Manuali`;
- pulsanti **Includi**, **Escludi**, **Modifica confini...**;
- modalità **Attenua**, **Taglia**, **Dimezza** per l'evento;
- ampiezza verticale `1×/2×/4×/8×`, pulsanti `Zoom −`, `Adatta`, `Zoom +`.

La legenda in basso è sempre visibile. I tooltip spiegano i comandi principali;
**F1** apre la finestra completa degli aiuti.

## 5. Modalità e parametri

- **Attenua**: abbassa di −12 dB con fade; la durata non cambia.
- **Taglia**: rimuove il nucleo e ricompatta la timeline.
- **Dimezza**: conserva una percentuale del nucleo tagliabile (25/50/75% o
  valore scelto). La percentuale si riferisce al nucleo, non all'intero
  respiro.

La **modalità globale** del menu Regioni si applica a tutti gli eventi inclusi;
le eccezioni restano. Una modalità assegnata a un evento escluso viene
conservata e verrà applicata se l'evento torna incluso.

Menu **Rilevamento → Attenuazione e fade...** imposta attenuazione, fade e
**crossfade Taglia/Dimezza**. Il crossfade è un overlap-add: la durata cala dei
campioni di sovrapposizione e il report espone `crossfade_frames` e
`removed_samples`. Impostare 0 per il vecchio raccordo a rampa.
Gain e fade globali aggiornano i respiri esistenti; i trattamenti manuali e
dei click conservano i loro valori. Il crossfade viene salvato nella sessione.
I Taglia/Dimezza manuali mantengono la durata esatta dell'intervento richiesto,
con raccordi a rampa invece dell'ulteriore accorciamento da overlap.

## 6. Zoom orizzontale e verticale

- Rotella + `Ctrl` (o rotella) per lo zoom orizzontale centrato sul puntatore;
  pulsanti `+` / `−` e **Adatta** per lo zoom ancorato alla regione selezionata.
- Zoom verticale indipendente `1×/2×/4×/8×`: cambia solo il disegno, non il
  guadagno audio. Il valore `1×` è il ripristino.
- **Vai al respiro selezionato** (`G`) porta la regione in vista.

## 6bis. Segui cursore

- Menu **Vista → Segui il cursore durante la riproduzione** attiva/disattiva
  l'inseguimento. Quando è attivo, la vista scorre per mantenere visibile la
  testina; se si sposta o zooma la vista a mano durante la riproduzione,
  l'inseguimento si disattiva per non combattere con l'utente.
- **Vista → Vai alla posizione di riproduzione** riporta subito la testina al
  centro della vista.

## 6ter. Impostazioni di rilevamento e anteprima locale

Aprire **Rilevamento → Impostazioni di rilevamento...** (oppure `F2`).

- **Conservativo / Bilanciato / Alta sensibilità** sono tre insiemi reali di
  parametri, non etichette: modificano soglie, sensibilità e durata minima.
- I parametri avanzati restano modificabili e i preset si possono salvare e
  caricare come file `.bcpreset`.
- **Anteprima locale**: analizza solo una finestra di 10–20 secondi intorno
  alla vista, non l'intero file. Mostra la waveform con gli eventi proposti,
  il confronto **Prima/Dopo** e i conteggi mantenuti/aggiunti/rimossi.
  **Ascolta anteprima** riproduce la finestra locale senza toccare il progetto;
  lo stesso pulsante la ferma.
- L'analisi locale gira in background; cambi rapidi annullano i risultati
  superati. La cache delle feature resta in memoria finche' il dialogo e' aperto.
  Ascolta Prima riproduce l'originale; Ascolta Dopo applica attenuazione e
  riparazione click correnti alle proposte DSP. Per Taglia/Dimezza usa l'anteprima
  del risultato nel workflow principale.
- Finché non si preme **OK** o **Applica**, parametri e anteprima sono
  temporanei e il progetto non cambia. **Annulla** riporta alla configurazione
  confermata in precedenza.
- **OK** conferma e chiude; **Applica** conferma senza chiudere; **Predefiniti**
  riporta i valori di base. Le impostazioni confermate vengono salvate e
  ricaricate al riavvio.
- Una nuova analisi usa la riconciliazione: conserva regioni manuali,
  esclusioni e label verificate, rimuove solo le proposte automatiche non più
  presenti, aggiunge le nuove e segnala le sovrapposizioni ambigue.

## 6quater. Apprendimento dalle correzioni (prima versione reale)

Il menu **Apprendimento** assegna etichette **esplicite**:

- **Conferma respiro selezionato per il training**: vero positivo verificato;
- **Segna come respiro mancato confermato**: regione manuale confermata;
- **Segna come falso positivo confermato**: errore verificato;
- **Segna esclusione editoriale**: decisione di montaggio, **non** training;
- **Riporta a non verificato**.

Poi **Impara dalle correzioni...** raccoglie solo gli esempi esplicitamente
confermati. Servono almeno 6 respiri e 6 falsi positivi da almeno **due
registrazioni distinte**; una parte dei file viene tenuta fuori dal training
per la validazione. Se il profilo non supera la validazione indipendente non
viene creato e il programma spiega il motivo.

Un profilo valido può essere **attivato** dal menu o ripristinato al modello
generale. Il profilo è un piccolo calibratore su score del rilevatore e feature
DSP; **non allena e non modifica i pesi ONNX di Respiro-en**. Non viene attivato
automaticamente e non viene presentato come "training" ciò che è solo una
soglia. Il training PyTorch completo resta un lavoro futuro separato: l'uso
normale del programma non richiede Python.

## 7. Anteprima, riepilogo ed esportazione

- **Anteprima del risultato** calcola il WAV di prova senza scrivere file.
- **Anteprima del rilevamento** (dentro le impostazioni) è locale e non modifica il progetto.
- Prima di esportare compare un unico riepilogo globale: file, eventi rilevati,
  inclusi, esclusi, aggiunti manualmente, interventi che accorciano e durata
  tagliata stimata. Una sola conferma; nessuna conferma per evento.
- **Progetto → Elabora ed esporta tutti i file...** elabora tutti gli eventi
  inclusi. Ogni uscita è scritta su un file temporaneo e sostituita in modo
  atomico; l'originale non viene mai sovrascritto.
- Ogni file riceve `*_breathclick.wav`, `.edit.json` e `.plan.json`; nella
  cartella compare `breathclick_export_report.json`.

Un file senza eventi inclusi puo' essere esportato invariato; il report indica zero interventi.

## 8. Salvataggio, riapertura e recupero

- **Progetto → Salva progetto** salva un `.bcnproj.json`. Accanto compare
  `.breathclick_sessions/` con una sessione per file (regioni, inclusioni,
  modalità, correzioni).
- **Progetto → Apri progetto** riapre file e decisioni.
- L'autosalvataggio viene scritto periodicamente in
  `%TEMP%\BreathClick\recovery.bcnproj.json`. **Progetto → Riprendi
  autosalvataggio** lo riapre dopo un crash.

Il progetto contiene percorsi; se i WAV vengono spostati insieme al file di
progetto, i percorsi relativi restano validi.

## 9. Formati MP3 e FLAC

- In ingresso: **WAV, MP3 e FLAC**, anche in cartelle miste. Il formato viene
  riconosciuto dal contenuto, quindi un file con estensione sbagliata viene
  letto correttamente quando il contenuto è valido.
- MP3 viene decodificato in PCM float all'apertura; analisi, waveform,
  anteprima, revisione, Attenua, Taglia e Dimezza sono identici al WAV.
- In uscita: **Stesso formato dell'originale** (predefinito), **WAV PCM** o
  **MP3** con bitrate 128/192/256/320 kbps oppure VBR Q0–Q9. Il menu
  **File → Formato esportazione** imposta la scelta.
- Un MP3 riesportato in MP3 subisce una **nuova codifica lossy**: non è
  un'esportazione lossless. Il programma lo ricorda nel riepilogo.
- FLAC è supportato in **importazione**; l'esportazione FLAC non è inclusa,
  quindi il default per un FLAC è WAV PCM. La scelta esplicita MP3 resta
  disponibile.
- I tag ID3 comuni (titolo, artista, album, anno, genere, traccia, commento)
  vengono letti e riscritti per MP3. I tag non trasferibili sono indicati nel
  report; i chunk BWF/bext non hanno equivalente completo in MP3.
- `libmp3lame.dll` è l'encoder MP3, caricato **solo quando serve esportare**.
  Se manca, la GUI e il WAV funzionano ancora; l'esportazione MP3 mostra un
  solo errore e non blocca l'applicazione.
- La DLL inclusa è x64 e redistribuibile sotto LGPL 2.0; i sorgenti sono in
  `third_party/lame` e le licenze in `LICENSES/`.

## 10. Formati, originali e audio

WAV PCM 16/24/32 bit, float 32, mono/stereo e WAVE_FORMAT_EXTENSIBLE. Sample
rate e canali vengono mantenuti; i chunk RIFF/BWF compatibili vengono
preservati e quelli non più validi vengono rimossi con nota nel report.
Verificare sempre l'hash dell'originale prima e dopo il lavoro.

L'ascolto usa il dispositivo audio predefinito di Windows. ASIO, Apollo e
REAPER non vengono aperti né modificati.

## 11. Errori e limiti reali

- Il pacchetto non è un installer: è uno ZIP da estrarre.
- La fusione AI/DSP resta sperimentale.
- Il crossfade è un overlap-add lineare/coseno tra i bordi; va giudicato in
  ascolto.
- Un evento fuori dai limiti o una sovrapposizione di interventi di timeline
  viene rifiutato e riportato; non viene scritto un risultato parziale.
- Non sono dichiarate prove su una macchina Windows pulita senza Visual
  Studio: il collaudo finale va fatto dall'utente.
- La selezione esplicita del dispositivo di uscita non è ancora esposta.
- L'integrazione REAPER e' una distribuzione indipendente dalla standalone.

## 12. Buone pratiche

- Lavorare su copie; conservare gli originali in sola lettura.
- Ascoltare i raccordi Taglia/Dimezza in cuffia e su monitor.
- Salvare il progetto prima di un batch lungo.
- Controllare `breathclick_export_report.json` e i `.edit.json`.
- Annotare gli errori con numero di file, tasto, stato e messaggio.

Le impostazioni confermate aggiornano progetto e default globali; riaprire un
progetto o una sessione ripristina i suoi valori senza riscrivere i default.
Il calibratore usa un file separato per valutare miss e falsi positivi; non
recupera automaticamente respiri mancati. Le copie dello stesso audio non
valgono come registrazioni indipendenti. Le generazioni di sessione precedenti
restano nella cartella .breathclick_sessions: non sono audio originali.

## Informazioni e contatti

Aiuto > Informazioni mostra versione e licenza, Nicola Lanci Voice Over,
info@nicolalanci.com e www.nicolalanci.com. I due pulsanti aprono il programma
email e il sito nel browser. Nel pannello REAPER usare Informazioni; Esc
chiude questa schermata senza chiudere il pannello o modificare il progetto.

## Motore Breath & Mouth Click — sperimentale RC3 locale

La RC2 pubblicata rimane invariata. Nelle impostazioni della standalone e nel
pannello REAPER, **Rileva respiri** e **Rileva click** sono indipendenti. Il
rilevamento dei click usa DSP, anche senza caricare il modello dei respiri.
Sensibilita', filtri e classificazioni rimangono distinti; i nomi digitali,
saliva e schiocco sono ipotesi da rivedere, non diagnosi o etichette umane.

Per un click selezionato scegliere **De-click / Ripara click**: Auto, Lineare
o Predittiva (Burg bidirezionale). Auto controlla la prevedibilita' sui campioni
adiacenti e l'accordo delle due ricostruzioni; quando il rischio e' alto,
conserva l'audio e lo dichiara nel report/anteprima. In REAPER compare
**AUTO: CONSERVATO**. Gli eventi incerti non vengono approvati automaticamente.
Le alternative esplicite servono per il confronto: possono danneggiare la
voce. Non estrapolare intervalli lunghi; il limite e' 45 ms. I click non
modificano la durata e non usano Taglia/Dimezza.

Usare **Originale**, **Elaborato** e **Solo rimosso** per confrontare il PCM
con lo stesso motore. Il rimosso e' allineato alle coordinate originali anche
in presenza di tagli dei respiri. In REAPER cambia la sorgente della stessa
take, senza seconda traccia; Undo/Redo e l'originale su disco restano disponibili.

Click confermato, Falso click, Click mancato, Schiocco e Preserva voce sono
annotazioni distinte da quelle dei respiri. Escludi resta editoriale. Le
annotazioni click sono persistenti ma **non addestrano il calibratore respiri**;
non viene dichiarato un nuovo modello appreso per i click.

Nel checkout Windows compilato, `reaper/Prova_RC3.cmd` apre la singola portable
di prova, il motore RC3 e un progetto sintetico separato. Non installa nulla
nel REAPER professionale. Se sono presenti le piccole fixture pubbliche in
build, la demo comprende anche click di lingua e un mix artificiale con voce.
`editor/Prova_RC3.cmd` avvia la standalone con preferenze locali di prova,
senza sovrascrivere quelle della versione stabile; accetta il percorso audio
come argomento. Il binario e' `build/bin/Release/bc_studio.exe`.
Il collaudo percettivo su veri schiocchi di saliva sovrapposti al parlato
rimane necessario prima di distribuire RC3.
