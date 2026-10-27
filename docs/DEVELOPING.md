# Documentazione sviluppatori

Per installare e usare il programma: [README](../README.md),
[guida standalone](USER_GUIDE.md) e [guida REAPER](REAPER_GUIDE.md).
Questa pagina raccoglie i riferimenti tecnici, mantenuti separati dalle guide d'uso.

## Compilazione e manutenzione

| Documento | Contenuto |
| --- | --- |
| [BUILDING.md](../BUILDING.md) | Toolchain, dipendenze, build DSP e AI/Fusion, test e packaging |
| [Test e collaudo](TESTING.md) | Suite automatica, harness GUI/REAPER e verifiche manuali ancora necessarie |
| [Criteri 1.0](ACCEPTANCE_V1.md) | Requisiti per la versione definitiva |
| [Distribuzione](DISTRIBUTION.md) | Provenienza dei binari, manifest, checksum e pubblicazione |
| [Redistribuzione](REDISTRIBUTION.md) | Modelli, runtime e vincoli delle dipendenze |
| [Attribuzioni](../THIRD_PARTY_NOTICES.md) | Componenti, autori e licenze |

Il [tag v1.0.0-rc.2](https://github.com/Nicotina4ever/NL-Breath-Click-AI/tree/v1.0.0-rc.2)
identifica il sorgente dei pacchetti RC2. La documentazione su main può ricevere
correzioni senza modificare quel tag o i binari pubblicati.
Le note tecniche RC2 sono nel [changelog di sviluppo](development/RC2_CHANGELOG.md);
le [note per gli utilizzatori](RELEASE_1.0.0-rc.2.md) descrivono installazione e limiti.

## Architettura e integrazione

| Percorso | Responsabilità |
| --- | --- |
| `core/` | Audio I/O, DSP, documento, regioni, apprendimento e persistenza |
| `standalone/engine/` | Motore condiviso, ONNX, EditPlan, TimeMap, sessioni e progetti |
| `standalone/`, `editor/` | CLI e interfaccia Windows |
| `reaper/` | Pannello Lua, bridge del motore e test d'integrazione |
| `scripts/` | Dipendenze, packaging e strumenti di verifica |
| `third_party/` | Sorgenti delle dipendenze e relative licenze |

[SDK C++ e protocollo CLI/JSON](INTEGRATION_API.md) documenta le interfacce
per altri host e per la futura NL Audio Suite. Il motore è condiviso;
GUI standalone e adattatore REAPER rimangono indipendenti.

## Adattamento e feedback: stato reale

| Capacita' | Disponibile | Ancora da realizzare |
| --- | --- | --- |
| Etichette verificate | Respiri/click confermati, falsi positivi, mancati, esclusione editoriale separata; sessioni, Undo e metadata take | Raccolta unificata di esempi locali con revisione e provenienza completa |
| Profilo personale | Calibratori leggeri separati per respiri e click, attivabili/disattivabili in entrambi gli host | Generazione personale dei mancati, soglie dei generatori e preferenze di riparazione |
| Sicurezza del calibratore | Split per identita' PCM della registrazione; soglia scelta solo sul training; rifiuto se peggiorano miss o falsi positivi sulla registrazione separata | Registro di candidati/versioni e confronto anche col profilo personale precedente; insieme di test finale mai usato per le scelte |
| Preferenze d'ascolto | Confronti locali, chiave, export e decodifica; giudizio per evento sulla riparazione separato dalle label | Adattatore personale delle strategie sicure, valutato su nuovi dati; provenienza del metodo effettivo/versione al momento del giudizio |
| Modelli supervisionati | Inferenza Respiro-en generale e caricamento di profili; strumenti Python legacy locali esterni al repository | Pipeline curata e riproducibile di aggiornamento dei rilevatori; nessun fine-tuning ONNX automatico oggi |
| Ripristino | Undo/Redo, originali intatti, profilo disattivabile, scritture atomiche | Registro dei modelli con promozione candidato/attivo e ripristino della precedente versione personale |

I calibratori Core usano soltanto esempi espliciti della propria categoria;
click e respiri non si contaminano. Un evento mancato contribuisce alle
metriche ma il filtro non puo' generarlo. Le versioni respiro schema 2 restano
compatibili; i nuovi profili click schema 3 hanno una categoria esplicita. Il confronto di qualita' della riparazione non misura la
copertura del rilevamento. Un feedback negativo sulla riparazione non dice
che l'evento fosse un falso positivo, ne' disattiva da solo un intervento.

Prima di supervisionare un nuovo rilevatore occorrono intervalli verificati,
negativi (incluse consonanti/attacchi), file e campioni originali, categoria,
autore/provenienza della decisione e versione delle feature. Usare manifest
con riferimenti e hash, senza duplicare audio o pesi. Separare registrazioni
di sviluppo, validazione e collaudo finale; il manifest deve mantenere copie
e crop della stessa registrazione nello stesso gruppo. L'hash del PCM di
un file intero non riconosce da solo una porzione salvata con un altro nome:
per i crop serve anche la provenienza dalla registrazione originale.
Gli estratti votati usati per
correggere il software diventano sviluppo, anche se prima erano riservati.

La promozione futura deve confrontare generale, personale attivo e candidato
con miss e falsi positivi separati, oltre a danni alla voce, durata, residuo
e ascolto. Un guadagno medio non autorizza regressioni sulle consonanti.
Salvare la decisione e gli hash, mantenere una versione attiva ripristinabile
e attivare esplicitamente il candidato accettato. Non scrivere nei dataset o
riaddestrare in risposta a ogni correzione; nessun caricamento online implicito.

Il lavoro RC3 immediato resta copertura dei mancati e riparazione sicura.
Non e' richiesta una nuova rete per consegnare la 1.0.

Ricognizione RC3: riusato Respiro-en per proposte brevi/frammentate da
rivedere, senza nuovi pesi. Benchmark locale di FFmpeg adeclick (anche con
100 ms di contesto aggiunto): ripara impulsi sintetici, ma modifica anche
p/t/k di controllo; nessuna integrazione o sostituzione di Auto. Report in
build/rc3/consolidation/ffmpeg_benchmark. Il candidato fonetico
[wav2vec2-lv-60-espeak-cv-ft](https://huggingface.co/facebook/wav2vec2-lv-60-espeak-cv-ft/tree/main)
ha pesi da 1,26 GB: esaminati metadata, non scaricato o provato; nessun
beneficio ancora dimostrato sul compito. La guida IA De-Esser e' un riferimento
per l'estensione futura in INTEGRATION_API.md, non un modulo implementato.

[Portabilità](PORTABILITY.md) distingue il motore senza interfaccia grafica
dalle applicazioni distribuite. Una build CI Linux/macOS del motore non è
un'applicazione standalone o un pacchetto REAPER disponibile per quelle piattaforme.

## Lingue e manutenzione dei testi

La presentazione ha due pagine, [italiana](../README.md) e [inglese](../README.en.md).
Aggiornare insieme download, funzioni disponibili e limiti. Le guide complete
sono condivise, con avvio rapido inglese nel README e nella guida REAPER.
Le note della release contengono entrambe le lingue.

Registri di lavoro, checkpoint e istruzioni per agenti appartengono alla
documentazione interna; non sono istruzioni per installare il prodotto.
