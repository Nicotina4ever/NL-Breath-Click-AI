# Verifiche e collaudo

## Suite automatica

`ctest --test-dir build -C Release --output-on-failure`: 33 test obbligatori.
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
