# Modelli e profili

`generale/` documenta Respiro-en e identifica esattamente ONNX e dati esterni
tramite manifest/SHA-256. I due file modello sono esterni a Git. Il runtime
ONNX e' un ulteriore artefatto opzionale; vedere ../BUILDING.md.

`base/`, `esempio_trattato/` e `addestrato/manifest.txt` sono configurazioni
piccole usate anche dai test. Quest'ultimo e' solo un riferimento a pesi locali:
nessun pesi.json e' distribuito. Pesi e dataset dell'utente rimangono privati.
I tre header legacy generati in locale sono ignorati da Git e abilitabili
solo con BC_PRIVATE_MODEL_DIR. Il default pubblico e' senza pesi privati.
