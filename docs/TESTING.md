# Verifiche e collaudo

## Suite automatica

I harness Windows condividono `editor/test_window.ps1`: identificano la
classe BreathClickEditorWindow nel PID figlio. MainWindowHandle puo' invece
indicare UAC Input Indicator o un'altra finestra IME e produrre falsi errori
di inizializzazione/controlli. Il riconoscimento non dipende dal titolo o
dalla prima finestra visibile. La verifica del pacchetto usa lo stesso criterio.

`ctest --test-dir build -C Release --output-on-failure`: 33 test sul tag RC2;
il numero RC3 dipende dalla configurazione (`ctest --test-dir build -N`). La CI headless registra
24 su Windows e 23 su Linux/macOS (il worker POSIX sostituisce PowerShell).
Nessun test registrato viene saltato; GUI e REAPER nativo non fanno parte
della matrice headless.

I test GUI di revisione, worker e RC2 riusano slot sotto
`build/test_work/managed`: eliminano i sintetici al successo e conservano
l'ultimo errore fino al retry. `BC_KEEP_TEST_OUTPUT=1` conserva gli output
per debug. Slot privi del marcatore di proprieta', link e slot occupati
sono rifiutati. Non mettere qui dati umani o preferenze d'ascolto.
REAPER senza `-WorkRoot` riusa uno slot per script e chiude solo le proprie
schede a successo; verifica tutte le sorgenti aperte prima della pulizia.
Quando il report contiene media, esegue anche `verify_audio.py` prima di
rimuoverli e conserva i numeri in `managed/last_reaper_<script>.json`.
`-KeepOpen` oppure `-WorkRoot` esplicito conservano progetto/media per ascolto.
`verify_distributions.py` senza `--work` usa un'area temporanea auto-rimossa,
inclusi TEMP e profili; `--work` richiede una cartella vuota da conservare.
Feedback, chiavi cieche e sessioni umane gia' in `build/rc3` restano esclusi
da questa politica, cosi' come portable, modelli e build di sviluppo.
Sono mantenuti controlli su PCM/durata/TimeMap, overlap, selezione e Undo,
preview/export, persistenza atomica, sessioni/progetti, prestazioni, Unicode,
WAV/MP3/FLAC, codec opzionale, Fusion e separazione training/validazione.
Le fixture sono sintetiche; non misurano accuratezza o qualita' percettiva.
Il test feature verifica anche il rifiuto dei pesi legacy privati assenti e
il caricamento esplicito dei coefficienti da un profilo.

REAPER reale: runner serializzato in `reaper/tests/run_isolated.ps1` con
`verify_audio.py` automatico negli slot gestiti (manuale con WorkRoot esplicito).
Tre dimezzamenti consecutivi, durata cumulativa, A/B
sulla stessa take, Undo/Redo, cue, altre tracce, offset, trim/split, rate,
FX JS reali, batch, cancel, rollback, labels e salvataggio/riapertura.
`ui_harness.lua` e `ui_driver.ps1` verificano mouse/tastiera e catture della
sola finestra del test, compresa la maniglia durante il trascinamento.

Il runner usa `host_ready.py` prima di avviare qualsiasi test: osserva il
dialogo di valutazione e preme normalmente Still Evaluating solo quando
abilitato. Poi richiede finestra principale ed event loop pronti; il driver
GUI richiede anche progetto, take e pannello utilizzabili. Timeout e modalita'
iniziali non superate sono errori di inizializzazione, non del motore.
Il gate Windows e' collaudato con countdown reale. macOS richiede accesso
Accessibility; l'adapter X11 rileva il dialogo ma si ferma se non puo'
provare lo stato del pulsante. Questi adapter nativi POSIX non sono collaudati
dalla CI headless; Wayland richiede un adapter dedicato.

Il driver controlla anche l'esito di GetCursorPos/SetCursorPos: accesso al
desktop negato e puntatore spostato sono condizioni diverse. Prima di ogni
clic verifica posizione e processo della finestra; non disabilitare questi
controlli per completare un test. Se il desktop o la finestra REAPER non sono
disponibili, registrare il collaudo mouse/tastiera come non eseguito. La prova
manuale residua e' mirata: nella portable isolata trascinare un bordo e
verificarne l'aggiornamento prima del rilascio, applicare Silenzia al solo
evento, annullare/ripristinare e provare Spazio per avvio/arresto anteprima.
I test nativi gia' superati non certificano da soli questa interazione fisica.

La portable di test usa `portable_host.lua` per eseguire richieste in istanze
ReaScript separate nello stesso processo. Il sender verifica risorsa, HWND e
PID prima dell'invio; nessun riavvio implicito tramite `-nonewinst` e nessun
inoltro al REAPER professionale. Se il dispatcher non risponde, e' un errore
di inizializzazione: non si crea una seconda istanza come ripiego.

`bc_mouth_click_engine` confronta linear/Burg su riferimento sintetico, tre
modalita' indipendenti, durata, overlap applicato una volta, residuo esatto,
rinuncia stereo atomica e separazione delle label click dal calibratore.
Include la regressione RC3 del vuoto vocale su 553 campioni a 48 kHz:
entrambe le strategie manuali rifiutano una ricostruzione non giustificata,
senza modifiche parziali stereo; un click ricostruibile della stessa durata
viene ancora riparato. La fixture e' sintetica, senza audio privato.

`scripts/review_click_artifact.py --study <confronto-votato-sotto-build>
--cli <CLI> --work <nuova-cartella-sotto-build>` riusa i piani e produce
ASCOLTA_CORREZIONE.html con prima/dopo e Solo rimosso. Controlla SHA di audio,
confronto e voti, PCM fuori regione, durata/residuo, e Auto identico su tutti
gli estratti. Non e' una nuova prova cieca: dopo il voto anche i gruppi
prima riservati sono sviluppo. Il file sorgente della riparazione e la
provenienza della build sono registrati nel report locale.

`mouth_click.lua` verifica le stesse operazioni in REAPER reale, inclusi
persistenza, Undo/Redo, Originale/Elaborato/Solo rimosso e rinuncia conservativa.
Con `run_isolated.ps1 -Script reaper/tests/mouth_click.lua -RunBoundsDialogTest`
il runner verifica anche il vero dialogo Bordi, con i due controlli visibili
e conferma normale OK, senza muovere il puntatore. `verify_audio.py result.json`
controlla i WAV realmente associati alle take, compreso il PCM conservato
quando Auto rinuncia. Il test GUI standalone verifica che il suo dialogo
confini non termini l'app e non arrotondi i click al millisecondo.

Confronto aggiuntivo esplicito, non training (download solo con --download):

```text
python tests/public_click_audio.py --download --work build/public-click --cli build/bin/Release/bc_studio_cli.exe
```

Lo script fissa URL, autori, licenze e SHA di quattro piccole registrazioni
pubbliche, conservate soltanto in build. Verifica p/t/k inalterati in automatico,
tre gruppi di click di lingua, e confronta lineare/Burg/Auto su tre mix
artificiali di click reale e voce reale. Auto deve rinunciare ai mix rischiosi.
Misure MSE e fixture isolate non attestano naturalezza percettiva, precisione
su saliva reale nel parlato o prestazioni su un corpus annotato indipendente.

Per estratti di registrazioni autorizzate, solo copie locali:

```text
python scripts/prepare_voice_trial.py --input <registrazione.wav> --work build/rc3/ascolto --cli build/bin/Release/bc_studio_cli.exe --seconds 8 --compare-methods
```

Il percorso di output deve essere nuovo e sotto build. Lo script controlla
SHA originali, durata, PCM fuori dagli eventi, residuo e identita' fra Auto
da piano e il vero comando process. Le alternative forzate includono anche
proposte incerte e sono confronti d'ascolto, non raccomandazioni o label.
ASCOLTA.html funziona localmente senza caricare audio online. Report e WAV
privati non entrano in Git o nei pacchetti. Mouse/tastiera in coda vanno
distinti dagli schiocchi della bocca durante la revisione umana.

Il verificatore delle due distribuzioni prova anche le tre modalita', un
impulso riparato e il residuo con la CLI estratta di ciascun prodotto.
`mouth_click.lua -PackageDirectory` tramite il runner usa e controlla il
backend del pacchetto, senza ricadere sul motore di sviluppo.

`voice_trial.lua` esegue anche importazione, intervento singolo, A/B/residuo,
Undo/Redo e riapertura in REAPER su una copia vocale reale. Il runner accetta
`-AudioFixture <copia-sotto-build>` e `-PackageDirectory <ZIP-estratto-sotto-build>`.
Questo test richiede una copia con una proposta impulsiva breve; non attribuisce
etichette umane ne' conferma che l'impulso sia saliva anziche' mouse/tastiera.
I progetti e i report restano privati sotto build.

Comandi di build, suite e pacchetto in BUILDING.md. Conservare i report
locali delle esecuzioni. Lo ZIP va verificato estratto con ambiente e
temporanei isolati: questa prova non equivale a Windows pulito.

## Prova professionale breve ancora necessaria

Collaudo completo locale: `standalone/tests/validate_professional_rc3.py`
accetta `--cli`, `--recordings`, `--annotations`, `--case` ripetibile e
`--work build/...` nuovo. Usa copie di registrazioni intere e CSV di punti:
tre modalita' in DSP/Fusion, eventi identici da soli/insieme, hash/durata/PCM
fuori intervento e confronti con diverse tolleranze. `--review-session`
accetta una revisione gia' copiata sotto build, con SHA sorgente coincidente;
unisce i negativi sovrapposti e distingue proposta da riparazione automatica.
CONTROLLO_RILEVAMENTO.html offre ascolto locale dei punti, senza training.
Il successo funzionale dello script non certifica precisione o copertura.

REAPER opt-in: `run_isolated.ps1 -Script reaper/tests/professional.lua
-AudioFixture <copia-intera-sotto-build> -PackageDirectory <pacchetto-estratto>`;
poi `verify_audio.py <run>/result.json`. Verifica take alternative, bordi,
tre modalita', riconciliazione fra categorie, batch misto/invariato, errore
sul secondo item con rollback di entrambi, Undo/Redo e riapertura. Non usa
il corpus privato nei test obbligatori o nella CI.

Le prove GUI rc2_workflow_test.ps1 e detection_settings_test.ps1 accettano
`-AudioFixture <copia-sotto-build>`; workflow_test.ps1 accetta
`-AudioFixtureDirectory <tre-copie-original.wav>`. Il test codec accetta
`--real-fixture <copia-sotto-build>`. Impostare TEMP/APPDATA e BC_TEST_BIN
del solo processo di prova in cartelle isolate. Le fixture nei CTest
rimangono sintetiche; non occorre distribuire registrazioni private.

Nuovo confronto dopo feedback: `scripts/compare_click_strategies.py --trial
<primo-trial> --cli <CLI-nuova> --baseline-cli <CLI-precedente> --holdout
<registrazioni-distinte> --work build/rc3/auto_selection/listening`.
Le approvazioni e gli intervalli sono identici per Auto/Lineare/Burg: non
confrontare un filtro prudente con alternative forzate su tutti i candidati.
La policy di ammissione va misurata separatamente dal selettore di riparazione.
Il generatore conserva gruppi SHA distinti, matrice e chiave casuale fuori
da Git. Non cambiare i criteri dopo aver letto i risultati dei gruppi tenuti
da parte. I voti locali/esportati sono preferenze d'ascolto, non label click.

Decodifica: `scripts/summarize_listening_feedback.py --study <confronto>
--votes <export.json> --out build/rc3/listening_feedback/verdetto.json`.
Controlla identita' studio/chiave/report, scelte e scale; non addestra o
importa etichette. Dopo aver usato i voti per cambiare il DSP, TUTTE le
registrazioni votate diventano sviluppo, anche quelle prima tenute da parte.
Per un confronto breve successivo il generatore accetta `--reuse-study
<confronto-votato> --case-id <id>` ripetibile e `--holdout <nuove-registrazioni>`.
I primi restano sviluppo; i nuovi gruppi SHA sono riservati prima del render,
senza cambiare i criteri dopo averne visto i risultati. Usare una nuova
directory sotto build: audio, ordine cieco e voti precedenti non si sovrascrivono.

Usare copie di voce e l'ambiente audio gia' configurato.

1. Importare uno spot; DSP/Fusion devono indicare il metodo effettivo o un
   errore esplicito. Correggere un bordo, aggiungere una regione manuale e
   fare una selezione multipla. Colori indicano tipo, non label di training.
2. Attenua/Silenzia: ascoltare consonanti, code, pause e stereo con A/B.
   Durata invariata. Taglia/Dimezza: ascoltare raccordi e ritmo del parlato.
3. Undo/Redo, salva/riapri, rianalizza ed esporta WAV/MP3. Eccezioni manuali
   e label devono restare, preview ed export devono coincidere percettivamente.
4. REAPER: BC01 -> Dimezza, BC02 -> Dimezza, BC03 -> Dimezza. Nella demo
   di 8 s le durate sono 7,75 / 7,50 / 7,25 s. Compatta anticipa solo il
   contenuto dell'item; Mantieni sync usa pausa e conserva tutti i tempi.
5. Valutare catene FX professionali complesse, DPI 125/150%, temi e avvio
   su Windows senza Visual Studio. Non cambiare impostazioni globali per
   ripetere verifiche gia' automatizzate.

Conferma respiro/mancato e Falso positivo sono etichette esplicite facoltative.
Escludi rimane editoriale. Il calibratore richiede classi sufficienti su
registrazioni indipendenti; copie/crop dello stesso PCM non sono validazione.
Non inventare label per raggiungere una soglia e non dichiarare miglioramenti
senza dati di confronto. Nessun audio privato deve essere inviato.

## Limiti attuali REAPER

Compatta opera entro l'item, senza ripple globale. Sync usa pausa con fade,
non genera room tone. Stretch marker, source section/reverse e take envelope
sono rifiutati senza appiattire il progetto. Rate 1 e 1,25, pitch e FX JS
statici sono verificati; altre catene richiedono ascolto effettivo.

## Registrazione intera: confronto fra host

Per una copia audio gia' presente sotto build (mai un originale), il collaudo
opzionale confronta nella GUI tutte le categorie e i confini con la CLI,
prova le tre modalita', l'anteprima e salva una schermata reale:

```powershell
powershell -File editor/recording_workflow_test.ps1 -AudioFixture "<build/copia.wav>" -WorkRoot "<build/cartella-vuota>"
powershell -File reaper/tests/run_isolated.ps1 -Script reaper/tests/band_limited_click.lua -AudioFixture "<build/copia-collaudo.wav>" -WorkRoot "<build/cartella-vuota>" -KeepOpen
```

Il secondo script usa la copia del collaudo RC3 con click a 1,047 s e verifica
anche filtro, sovrapposizione respiro/click, esito reale del trattamento e A/B.
Sono prove opzionali con dati locali, non dipendenze della suite CTest.
