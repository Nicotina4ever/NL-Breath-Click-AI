# Motore condiviso per NL Audio Suite

Standalone, REAPER e suite usano lo stesso Core/StudioEngine. Nessun secondo
algoritmo, modello o motore viene mantenuto per l'integrazione.

## C++ nello stesso processo

```
cmake --build build --config Release
cmake --install build --config Release --prefix <sdk>
```

Il consumer usa find_package(NLBreathClick 1 CONFIG REQUIRED), collega
NL::BreathClickEngine e include studio_engine.h. Il test tests/sdk_consumer
compila da sole intestazioni e librerie installate e verifica TimeMap.
I tipi pubblici comprendono Settings, EditPlan, TimeMap, sessioni e progetti.
Usare lo stesso compilatore/CRT per il collegamento C++; non e' promessa
un'ABI binaria fra compilatori. La CLI e' il confine stabile per altri host.
`analyzeFile` accetta un `AnalysisControl` opzionale: `cancelRequested` atomico
e callback di avanzamento sul thread chiamante. Il risultato viene sostituito
solo al completamento; annullamento ed errori conservano quello precedente.
Il chiamante mantiene il controllo in vita fino al ritorno della funzione.
SDK con coefficienti privati non viene esportato e i tre header di pesi sono
sempre esclusi dall'installazione. Nessun modello viene incorporato nel SDK.

## CLI fuori processo e JSON

bc_studio_cli capabilities identifica protocol nl.breathclick.cli, versione 1,
versione del prodotto, commit compilato, backend CPU, schema EditPlan 2 e
contratti REAPER. Comandi esistenti: plan, edit, slice, verify-source,
labels-export, profile-train e profile-filter. Non richiedono una GUI o REAPER.
EditPlan/report e TimeMap usano campioni originali stabili e versioni esplicite.

Il worker legge un envelope JSON schema 1 in una directory nuova per job.
Windows mantiene bc_job.ps1 senza richiedere Python. Il port POSIX usa
python3 bc_job.py --request <request.json>, solo libreria standard Python 3.
Stessi comandi nativi e medesimo motore: non viene rielaborato l'audio in Python.
Il client continua a usare il suo event loop, legge result.json scritto
atomicamente e richiede annullamento creando cancel. Il worker interrompe
soltanto il proprio processo figlio; hash prima/dopo proteggono gli originali.

## De-click RC3: selezione e diagnostica

`capabilities` espone `click_auto_selector: 3` e `click_repair_strategies`
(`auto`, `linear`, `predictive`, `none`), senza cambiare il protocollo CLI 1
o lo schema EditPlan 2. `none` e' un bypass esplicito: non equivale ad attenuare
e non cambia le label di rilevamento. I client devono controllare le capability
prima di inviare nuove strategie a un backend precedente.

Le operazioni di edit espongono `bypassed` e `click_strategy`: metodo richiesto,
metodo effettivo, motivo, durata/contesto, errore sui campioni adiacenti,
impatto locale normalizzato della Lineare, accordo delle previsioni,
evidenza impulsiva, banda alta e cambio di livello. `-1` indica una metrica
non calcolata. Il report di `process` espone anche `click_decisions`, con
`review_required` per proposte non ammesse automaticamente.

Nella RC3 locale i report aggiungono `outcome` alle operazioni e alle
`click_decisions`: `repaired` (ricostruzione effettuata), `preserved_safety`
(rinuncia: nessuna ricostruzione), `bypassed` (scelta esplicita Nessuna),
`review` (proposta da rivedere), `excluded`, `applied` (altro intervento) o
`not_applied`. Il report di edit include anche `event_outcomes`, uno per
evento del piano con `event_index` a base zero, kind e confini originali:
cosi' anche le proposte non elaborate rimangono visibili. `pending` indica
un evento incluso senza un esito di rendering corrispondente; non e' un
successo. Sono campi additivi nello schema 2, separati dalle label umane.
`repaired` descrive l'esecuzione del trattamento, non certifica la qualita'
percettiva o l'eliminazione del difetto. Un contatore di rilevamenti o di
eventi inclusi non deve essere presentato come numero di click eliminati.

L'impatto locale e' un'euristica di selezione, non un punteggio di qualita'
percettiva. Nessun allargamento dell'intervallo o guadagno aggiuntivo in Auto.
I canali vengono modificati insieme solo dopo la decisione. Predittiva non
ripiega silenziosamente su Lineare: se manca un contesto valido conserva
l'evento, con un motivo esplicito. I report non diventano esempi umani.

Il selettore 3 confronta le due ricostruzioni bidirezionali sugli stessi
quattro intervalli intatti nascosti ai predittori, due per lato. Mantiene il
budget d'impatto 0,10; preferisce Burg solo se passa i controlli e, quando
entrambe le strategie sono ammesse, riduce il rischio stimato di almeno il
20%. Il cambio di livello che blocca gli attacchi si misura presso i confini
(almeno 2 ms), anziche' su sillabe distanti entro il contesto di 25 ms.
Campi aggiuntivi: `predictive_bridge_error`, `predictive_voice_risk`,
`boundary_level_ratio`. `predictive_validation_error` conserva il vecchio
significato di previsione monodirezionale per diagnostica, non guida piu'
da solo la scelta. L'accordo fra i lati e' pesato per la durata sul contesto.
Restano controlli impulsivi, banda alta, stabilita' numerica e commit atomico
multicanale. Il protocollo e le strategie manuali rimangono invariati.

## Condivisione facoltativa nell'installer modulare futuro

- NL_AUDIO_ENGINE: percorso esplicito della CLI per l'adapter di sviluppo.
- NL_AUDIO_RUNTIME_DIR: directory di librerie native compatibili con l'host.
- NL_AUDIO_MODEL_DIR: directory con respiro-en-dinamico.onnx e relativa data.

Sono override di processo, senza cambiare PATH o configurazioni globali.
I percorsi espliciti non ripiegano silenziosamente su modelli diversi.
I due ZIP Windows della prima pre-release rimangono autonomi; la condivisione
non e' un prerequisito. La suite potra' installare un backend compatibile una
sola volta e passare questi percorsi ai moduli. Nessuna suite viene costruita qui.
CPU e' il solo backend verificato/distribuito; provider GPU richiederanno
compatibilita', selezione esplicita, licenze e collaudi propri.

## Feedback e futura estensione spettrale

La RC3 locale aggiunge `ai_short_review` e `dsp_fragment_review` alle
classificazioni di respiro. Sono proposte non approvate, escluse dal training
e dal trattamento automatico, anche se lo score e' alto. I client non devono
approvare ogni evento solo perche' `kind == 0`. La prima usa run AI di 80–200 ms
con picco almeno 0,2 e superiore alla soglia AI, distanti dai bordi del file;
la seconda raggruppa
frammenti DSP separati da <=10 ms, con attivita' complessiva >= durata minima,
span <=400 ms, gate pausa e corroborazione AI. Nessuna fusione con regioni
gia' accettate. Il diagnostico CLI opt-in `analyze --trace-ai-probabilities`
aggiunge `ai_probabilities` e `ai_probability_hop_s` al JSON, senza cambiare
l'output ordinario o il modello. Raccordi/crossfade possono usare soltanto
campioni interni agli intervalli approvati; `fade_frames` indica il raccordo
effettivo di timeline in modalita' compatta. Un taglio senza margini non
aggiunge rimozioni o fade esterni.

Regioni, Event, sessioni e piani conservano i campi opzionali
`repair_feedback` (`unsatisfactory` oppure vuoto) e
`repair_feedback_method` (trattamento richiesto quando e' stato espresso il
giudizio). Non cambiano `learning_label`, `training_eligible`, inclusione o
audio e non diventano automaticamente dati di training. I client possono
ignorarli per il rendering ma devono conservarli durante salvataggi e nuove
analisi. Le vecchie versioni non ne garantiscono la conservazione. I dati
restano nei documenti dell'utente, senza copie di registrazioni o modelli.

Il rilevatore click condiviso include `band_limited_click`: proposte a due
scale, con contesto bilaterale e controllo di periodicita', sempre da rivedere.
Non alterano automaticamente l'audio. `click_profiles:1` dichiara il supporto
al calibratore separato: `profile-train --kind click --plan ... --out ...`.
Il default resta `--kind breath`; `profile-filter` legge la categoria dal
profilo e, se specificato, verifica `--kind`. I profili respiro schema 2 restano
compatibili; quelli click usano schema 3 e `category=click`. Mancati, negativi
e split per registrazione seguono le stesse regole; nessun training implicito.
Un filtro non modifica decisioni editoriali umane o eventi dell'altra categoria.

**NL Spectral S Repair e' un modulo futuro, non implementato o esposto fra
le capability attuali.** Potra' usare il confine C++/CLI versionato esistente:
audio sorgente immutabile, selezioni in campioni originali, annotazioni
fonetiche/proposte indipendenti, piano di interventi e risultato con residuo
allineato. Non codificare sibilanti come falsi click o respiri. I nuovi tipi
di operazione richiederanno un contratto/capability esplicito e rifiuto da
parte di host incompatibili, senza forzare oggi lo schema BreathClick.

Rilevamento fonetico, trattamento spettrale selettivo e ricostruzione neurale
saranno componenti sostituibili dietro lo stesso host. Modelli e provider
compatibili potranno essere risolti dal runtime condiviso della suite, con
hash, versione, licenze e CPU/GPU espliciti. Nessuna duplicazione obbligatoria
fra standalone, REAPER e suite; nessuna promessa oggi di riparare sibilanti
o voci compresse senza perdita timbrica. Consonanti intatte, ascolto e
confronto su registrazioni indipendenti saranno criteri di ammissione propri,
senza ritardare la 1.0 di BreathClick.

Riferimento di interazione: [IA De-Esser, guida del produttore](https://immanentaudio.com/de-esser/user-guide/),
per rilevamento relativo alla sorgente, controllo manuale e parametri
effettivi visibili. La guida descrive filtraggio dinamico adattivo: non e'
una specifica di ricostruzione neurale o di apprendimento personale persistente.
Nessun codice, binario o modello di quel prodotto e' stato incorporato.
