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
