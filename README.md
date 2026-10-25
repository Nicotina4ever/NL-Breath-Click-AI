# NL Breath Click Studio

**Meno interventi ripetitivi sui respiri, più tempo per ascoltare la voce.**

[Italiano](README.md) · [English](README.en.md)

**Windows 10/11 a 64 bit — pre-release 1.0.0-rc.2**

- **[Scarica Standalone](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_Windows_Standalone_1.0.0-rc.2.zip)** — per lavorare senza REAPER.
- **[Scarica REAPER Windows Beta](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_REAPER_Windows_Beta_1.0.0-rc.2.zip)** — per lavorare dentro REAPER.
- [Pagina della release, note e checksum](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/tag/v1.0.0-rc.2)

**[Segnala un problema / Suggerisci una funzione](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues/new)**

Non servono competenze tecniche: racconta cosa stavi facendo e cosa vorresti ottenere.

## Un aiuto per l'editing della voce

NL Breath Click Studio è un programma gratuito e open source per doppiatori,
speaker e fonici. Individua i respiri nelle registrazioni e permette di
rivederli, attenuarli, silenziarli o accorciarli, mantenendo intatti i file originali.
L'elaborazione audio avviene sul computer, senza caricare le registrazioni online.

Su uno spot puoi abbassare un respiro troppo presente; su una lettura lunga
puoi rivedere le proposte e correggere soltanto le eccezioni. Il lavoro in
batch permette di trattare più file insieme. Il tempo risparmiato dipende
dalla registrazione e dalla revisione necessaria: l'ascolto resta parte del lavoro.

## Quale versione scegliere?

| | Standalone | REAPER Windows Beta |
| --- | --- | --- |
| Dove lavori | In un'applicazione dedicata | Nel tuo progetto REAPER |
| Come inizi | Estrai lo ZIP e apri `bc_studio.exe` | Installa il pannello tramite ZIP o ReaPack |
| Risultato | Esporti nuovi file WAV o MP3 | Applichi gli interventi alla take selezionata |
| Serve l'altra versione? | No | No; serve REAPER, che non è incluso |

**Le due distribuzioni sono indipendenti.** Ciascuno ZIP contiene tutto il
necessario per funzionare. Non occorre installare Python.
Con ReaPack i componenti per elaborare l'audio vengono scaricati al primo utilizzo.

## Cosa puoi fare nella RC2

- Individuare i respiri e correggerne i confini con il mouse o con valori precisi.
- Selezionare uno o più eventi, escludere quelli da conservare e aggiungere interventi manuali.
- Usare **Attenua**, **Silenzia**, **Taglia** e **Dimezza**, con confronto Originale/Elaborato e Undo/Redo.
- Nella standalone: importare WAV, MP3 e FLAC, salvare il progetto ed esportare WAV o MP3, anche in batch.
- In REAPER: intervenire sul singolo respiro nella stessa take, senza doppie tracce permanenti; scegliere se compattare la voce o mantenere la sincronizzazione.
- Creare facoltativamente un profilo personale dalle correzioni confermate. Escludere un evento dall'editing non significa classificarlo come errore di rilevamento.

La standalone include anche il rilevamento e il trattamento di click brevi.
**La riparazione avanzata degli schiocchi di saliva e i nuovi controlli
indipendenti respiri/click sono ancora in sviluppo e non fanno parte dei download RC2.**

## Inizia in pochi passaggi

### Standalone

1. Estrai **tutto** lo ZIP in una cartella scrivibile e apri `bc_studio.exe`.
2. Dal menu **Progetto**, apri una cartella di registrazioni e scegli **Analizza tutti i file**.
3. Ascolta e correggi le proposte: ad esempio, seleziona un respiro e scegli **Attenua** per abbassarlo mantenendone la durata.
4. Confronta **Originale** ed **Elaborato**, salva il progetto ed esporta in una cartella di destinazione.

[Guida standalone: comandi, selezioni, salvataggio ed esportazione](docs/USER_GUIDE.md)

### REAPER

1. Estrai lo ZIP REAPER e segui il `README.txt` incluso, oppure [installa con ReaPack](docs/REAPER_GUIDE.md#installazione-con-reapack).
2. Apri il pannello **NL Breath Click Studio** dall'elenco delle azioni e analizza gli item vocali selezionati.
3. Seleziona un respiro, correggi i bordi se necessario e applica il trattamento. **Compatta voce** accorcia il materiale dentro l'item; **Mantieni sincronizzazione** conserva la durata complessiva.

I file originali restano intatti. Conserva la cartella **BreathClick Media**
insieme al progetto REAPER per poterlo riaprire con tutti i media elaborati.

[Guida REAPER: installazione, ReaPack e lavoro quotidiano](docs/REAPER_GUIDE.md)

## Prima di usarlo su una consegna

La RC2 è una **release candidate**, con integrazione REAPER in beta.
Inizia da una copia del progetto e ascolta i raccordi, soprattutto dopo Taglia
o Dimezza. Nella standalone questi comandi accorciano il file; Attenua e
Silenzia ne conservano la durata.

In REAPER l'editing diretto non supporta take con envelope, stretch marker,
sorgenti section/reverse o registrazione in corso. Catene FX complesse e
configurazioni di lavoro diverse richiedono una prova sul proprio sistema.
Il collaudo su Windows pulito e l'ascolto professionale restano da completare.

I pacchetti disponibili sono per **Windows x64**. Le applicazioni macOS e
Linux non sono ancora disponibili; il lavoro sul motore multipiattaforma è separato.

## Gratuità, licenza e contatti

NL Breath Click Studio è gratuito e open source, con licenza
[Mozilla Public License 2.0](LICENSE). I componenti inclusi conservano le
proprie licenze: [attribuzioni e licenze di terze parti](THIRD_PARTY_NOTICES.md).
REAPER è un prodotto separato, con la propria licenza.

Per segnalare un problema o proporre un miglioramento, [apri una segnalazione su GitHub](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues).
Indica versione, standalone o REAPER, passaggi per riprodurre il problema e
messaggio visualizzato. Condividi audio soltanto se hai il diritto di renderlo pubblico.

**Nicola Lanci Voice Over** · [info@nicolalanci.com](mailto:info@nicolalanci.com) ·
[www.nicolalanci.com](https://www.nicolalanci.com)

Versione e contatti si trovano anche in **Aiuto → Informazioni** nella
standalone e in **Informazioni** nel pannello REAPER.

## Per chi sviluppa

[Documentazione sviluppatori](docs/DEVELOPING.md): compilazione, architettura,
SDK/CLI, test e distribuzione. Standalone, REAPER e la futura NL Audio Suite
condividono il motore; la suite completa non fa parte di questa release.
