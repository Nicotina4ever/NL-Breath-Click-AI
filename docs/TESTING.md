# Verifiche e collaudo

## Suite automatica

`ctest --test-dir build -C Release --output-on-failure`: 33 test sul tag RC2;
37 sul ramo sperimentale RC3 con GUI Windows completa. La CI headless registra
24 su Windows e 23 su Linux/macOS (il worker POSIX sostituisce PowerShell).
Nessun test registrato viene saltato; GUI e REAPER nativo non fanno parte
della matrice headless.
Sono mantenuti controlli su PCM/durata/TimeMap, overlap, selezione e Undo,
preview/export, persistenza atomica, sessioni/progetti, prestazioni, Unicode,
WAV/MP3/FLAC, codec opzionale, Fusion e separazione training/validazione.
Le fixture sono sintetiche; non misurano accuratezza o qualita' percettiva.
Il test feature verifica anche il rifiuto dei pesi legacy privati assenti e
il caricamento esplicito dei coefficienti da un profilo.

REAPER reale: runner serializzato in `reaper/tests/run_isolated.ps1`, poi
`verify_audio.py`. Tre dimezzamenti consecutivi, durata cumulativa, A/B
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

`bc_mouth_click_engine` confronta linear/Burg su riferimento sintetico, tre
modalita' indipendenti, durata, overlap applicato una volta, residuo esatto,
rinuncia stereo atomica e separazione delle label click dal calibratore.
`mouth_click.lua` verifica le stesse operazioni in REAPER reale, inclusi
persistenza, Undo/Redo, Originale/Elaborato/Solo rimosso e rinuncia conservativa.

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

Comandi di build, suite e pacchetto in BUILDING.md. Conservare i report
locali delle esecuzioni. Lo ZIP va verificato estratto con ambiente e
temporanei isolati: questa prova non equivale a Windows pulito.

## Prova professionale breve ancora necessaria

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
