# NL Breath Click Studio 1.0.0-rc.2

Candidate Windows x64 successiva alla RC1. Non e' la 1.0 definitiva:
restano l'ascolto professionale dell'utente e il collaudo su Windows pulito.

- Import/export audio e temporanei atomici compatibili con percorsi Windows
  lunghi e Unicode, senza cambiare le policy di sistema. Le due CLI ricevono
  argomenti UTF-8; sessioni, piani e progetti decodificano gli escape Unicode
  JSON. Regressioni su WAV/MP3/FLAC e sostituzione fallita senza perdere file.
- Opzioni del compilatore applicate prima dei sottoprogetti: la codifica UTF-8
  dei sorgenti e i controlli MSVC dichiarati sono ora effettivi.
- Aggiornamento editing: sulla stessa selezione un nuovo trattamento sostituisce
  il precedente con un Undo; non accumula Silenzia/Attenua o tagli incompatibili.
  Regioni rilevate, label verificate e sovrapposizioni parziali restano separate.
- Dialoghi Attenuazione e Percentuale conservata confermabili da tastiera,
  senza chiudere l'editor. Crossfade presente nelle impostazioni globali e
  salvato nelle sessioni; impostazioni globali di gain/fade applicate ai respiri
  esistenti, conservando i trattamenti manuali e quelli dei click.
- Waveform e curva restano visibili sopra gli sfondi delle regioni. Curva
  Attenua/Silenzia con le stesse rampe del motore, etichette manuali esplicite
  e leggibili. Taglia/Dimezza indicano cambi di durata, senza finti avvallamenti
  di gain; la vista mantiene le coordinate della sorgente.
- Riapertura: eventi manuali e rilevati con gli stessi estremi ripristinati
  su regioni distinte, senza sovrascrivere il trattamento del primo evento.
- Due strumenti espliciti: REGIONI e SELEZIONE TEMPO. Fascia visibile durante
  e dopo il drag, confini correggibili, tempi/durata, capture e autoscroll.
- Interventi manuali Attenua, Silenzia, Taglia, Dimezza durata e Ripristina,
  anche senza rilevamenti. Piano non distruttivo, un Undo per operazione,
  salvataggio/riapertura e stessa elaborazione per anteprima ed export.
  Taglia/Dimezza manuali usano raccordi senza accorciamenti aggiuntivi da
  overlap del crossfade; il 50% e' arrotondato al campione.
- Selezione multipla e per intervallo, filtri respiri/click/manuali/automatici/
  esclusi/incerti e click in blocco. I filtri cambiano la vista; Includi/Escludi
  cambiano il piano. Nessuna approvazione individuale obbligatoria.
- Rosso indica click, giallo respiro, ciano intervento manuale, grigio escluso.
  Contatori distinti, esito del motore e riepilogo delle label di apprendimento.
- Originale ed Elaborato mantengono buffer distinti; il timer dell'editor
  precedente non puo' sostituirli. Una modifica invalida l'anteprima.
- Sovrapposizioni di gain: prevale la riduzione maggiore, senza sommarle.
  Click sovrapposti riparati una volta; interventi di timeline incompatibili
  vengono segnalati e non applicati parzialmente.
- Provenienza e score AI conservati attraverso detector, documento, sessione
  e piano. Metodo effettivo/frame di inferenza registrati; sessioni legacy
  senza tale prova indicate come non documentate. Un modello esplicito
  inesistente genera errore senza sostituzioni silenziose.
- Includi/Escludi non cancellano le label umane verificate. I negativi restano
  in sessione anche se esclusi dal piano di applicazione. Label opposte con
  intervalli sovrapposti impediscono la creazione del calibratore. L'export
  non altera provenienza o cronologia delle decisioni.

## Integrazione e sorgente corrente

Editing diretto REAPER sulla stessa take, TimeMap cumulativa, Compatta/Sync,
correzione confini, etichette e calibratore Core. Vedere reaper/LEGGIMI.txt.
Sorgenti delle dipendenze inclusi, pesi privati legacy opzionali ed esclusi;
packaging senza copie storiche automatiche, sorgenti LAME nel pacchetto.
Verifiche correnti e limiti: TESTING.md.
