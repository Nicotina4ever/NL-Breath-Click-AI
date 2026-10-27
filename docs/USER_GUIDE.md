# Guida standalone — NL Breath Click Studio RC2

[Presentazione e download](../README.md) · [English quick start](../README.en.md#get-started) · [Guida REAPER](REAPER_GUIDE.md)

**[Segnala un problema / Suggerisci una funzione](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues/new)**

## Aprire le registrazioni

**Build locale RC3:** nella barra principale puoi scegliere direttamente
**Rileva respiri** e **Rileva click / bocca**, insieme o separatamente.
Per i respiri, **Analisi combinata (voce)** usa modello vocale e analisi
acustica; e' la scelta iniziale se modello e runtime sono presenti. Le scelte
confermate vengono conservate. Dopo un cambio premi **Analizza**: le correzioni
manuali rimangono. Gli schiocchi morbidi dubbi appaiono tra gli eventi incerti.
L'analisi mostra l'avanzamento e un pulsante **Annulla**. Se la interrompi,
restano le regioni e le correzioni precedenti; puoi riprovare quando vuoi.
Nella build di sviluppo, **Precedente / Successivo** (P/N) inquadrano l'evento
allo zoom adatto anche dopo averne spostato i bordi. **Originale**, **Elaborato**
e **Solo rimosso** nel pannello ascoltano l'evento con 200 ms di contesto per
lato e si fermano da soli. Sui click trovi direttamente **Auto / Lineare /
Burg / Intatto**; la protezione della voce rimane attiva.
I filtri **Da decidere**, **Click: impulsi e schiocchi** e **Riparazioni
insoddisfacenti** aiutano a suddividere la revisione. **Segnala / apprendimento**
registra conferme, falsi positivi, eventi mancati sulla selezione temporale o
un giudizio sulla riparazione. Il pannello scorre per raggiungere i comandi
inferiori. I piani esportati conservano anche eventi esclusi e feedback.
Nel menu **Apprendimento** puoi creare e attivare separatamente un profilo
respiri e un profilo click usando correzioni esplicite salvate nei progetti.
Servono positivi e negativi da registrazioni indipendenti; nessun modello
viene aggiornato automaticamente.

Estrai tutto lo ZIP Standalone in una cartella scrivibile e avvia
`bc_studio.exe`. Non servono REAPER, Python o un account.

Dal menu **Progetto → Apri cartella di WAV…** scegli la cartella di lavoro:
sono accettati anche MP3 e FLAC. Il pannello a sinistra elenca i file e il loro
stato: da analizzare, analizzato, elaborato oppure errore. Doppio clic su una
riga, o **Ctrl+PagSu/PagGiù**, cambia registrazione.

Scegli **Progetto → Analizza tutti i file**. I respiri proposti sono già
inclusi nell'elaborazione: puoi concentrarti sulle eccezioni, senza confermarli
uno per uno.

## Scegliere il trattamento

| Comando | Quando usarlo | Durata |
| --- | --- | --- |
| **Attenua** | Un respiro deve restare naturale ma meno presente | Invariata |
| **Silenzia** | Vuoi azzerare il suono nell'intervallo | Invariata |
| **Taglia** | Vuoi eliminare una parte del respiro e compattare la lettura | Più breve |
| **Dimezza durata** | Vuoi accorciare il respiro conservandone una parte | Più breve |
| **Ripristina** | Vuoi togliere il trattamento manuale selezionato | Dipende dall'intervento annullato |

Attenua permette di scegliere riduzione in dB e raccordi. Dimezza offre anche
percentuali diverse dal 50%: la percentuale riguarda la parte interna
trattabile del respiro, con i margini protetti. Ascolta il risultato nel contesto.
Le impostazioni di attenuazione, fade e crossfade si trovano nel menu **Rilevamento**.

Nell'editor la forma d'onda resta nelle coordinate dell'originale. Taglia e
Dimezza accorciano invece l'anteprima elaborata e il file esportato.
Per materiale sincronizzato a video, usa Attenua o Silenzia se devi conservare
la durata del file.

## Rivedere e correggere gli eventi

Scegli lo strumento **REGIONI** per lavorare sui rilevamenti:

- Clic su un evento per selezionarlo.
- Trascina un bordo per correggere inizio o fine; trascina il centro per spostarlo.
- Usa **Modifica confini…** nel pannello contestuale per inserire tempi precisi.
- Usa **Ctrl+clic** o **Shift+clic** per selezionare più eventi.
- Premi **Canc** o **R** per escludere la selezione; **A** la include di nuovo.

Escludere conserva l'evento nel progetto e lascia intatto quel tratto audio.
L'eliminazione definitiva è un comando distinto nel menu Regioni.
I colori aiutano a orientarsi: giallo per i respiri, rosso per i click,
ciano per gli interventi manuali, grigio per gli esclusi.

Per intervenire dove non c'è un rilevamento, scegli **SELEZIONE TEMPO**,
trascina l'intervallo e applica il trattamento. I bordi restano modificabili;
**Confini selezione…** permette di inserire i tempi. **Esc** toglie la selezione.

Per rivedere molti eventi insieme, seleziona un intervallo e scegli
**Eventi nell'intervallo**. Includi/Escludi agiscono sul gruppo.
I filtri respiri, click, manuali, automatici, esclusi e incerti cambiano la vista:
non escludono da soli gli eventi dall'elaborazione. Per non trattare i click,
usa **Escludi tutti i click**; **Reincludi tutti i click** annulla questa scelta.

Applicare un altro trattamento allo stesso intervallo aggiorna quello esistente.
**Ctrl+Z/Ctrl+Y** annullano o ripetono le modifiche, compresi trascinamenti ed
esclusioni. **Invio** conferma un dialogo; **Esc** lo annulla.
Se due tagli sono incompatibili, correggi o ripristina uno degli interventi
segnalati prima di esportare.

Nella RC3 locale i raccordi di Taglia e Dimezza restano dentro la regione
approvata. Per sfumare un taglio, lascia spazio ai due lati usando il margine
interno; senza spazio il taglio resta esatto e il raccordo indicato e' 0 ms.
Il programma non prende campioni dalla parola vicina per creare una sfumatura.
Le nuove proposte di respiro breve o frammentato richiedono revisione e non
vengono attenuate automaticamente. Prima dell'ascolto elaborato viene sempre
ricontrollato il piano corrente, anche dopo modifiche senza aggiornamento
esplicito dell'anteprima.

## Ascolto e navigazione

Usa **Originale** e **Elaborato** per confrontare il risultato. Dopo una
modifica l'anteprima elaborata viene ricalcolata quando necessario.
Ascolta anche l'attacco della parola successiva e i raccordi, non soltanto il respiro.

Nella RC3 locale puoi scegliere **Auto, Lineare o Burg** per ogni click e
confrontarli in anteprima. Il pannello distingue **Click riparato**,
**Click intatto: protezione della voce** e **Rilevato: da rivedere**.
Se la ricostruzione rischia di danneggiare il parlato, anche Lineare e Burg
conservano l'originale. «Incluso» significa soltanto che l'intervento e'
richiesto: aggiorna l'anteprima per conoscerne l'esito. La scelta Nessuna
lascia l'audio intatto per tua decisione. Un giudizio insoddisfacente resta
separato dall'esito del trattamento e non modifica da solo l'audio.

| Tasto o comando | Azione |
| --- | --- |
| **Spazio** | Riproduci / pausa |
| **N / P** | Evento successivo / precedente |
| **L** | Ascolta il contesto dell'evento |
| **Ctrl+1 / Ctrl+2** | Originale / Elaborato |
| **G** | Vai all'evento selezionato |
| **+ / −**, rotella | Zoom orizzontale |
| **Ctrl+Z / Ctrl+Y** | Annulla / ripeti |
| **F1** | Aiuto sui comandi |
| **F2** | Impostazioni di rilevamento |

Lo zoom verticale ingrandisce la visualizzazione senza aumentare il volume.
**Vista → Segui cursore** mantiene visibile la riproduzione; spostare o
ingrandire manualmente la vista sospende il seguito. Puoi riattivarlo o usare
il comando per tornare al cursore.

La riproduzione usa l'uscita audio predefinita di Windows; la RC2 non offre
un selettore dedicato del dispositivo.

## Regolare il rilevamento

Apri **Rilevamento → Impostazioni…**. Parti da Conservativo, Bilanciato o
Alta sensibilità e prova l'**anteprima locale** su un passaggio di 10–20 secondi.
Confronta prima e dopo prima di analizzare l'intera registrazione.
Puoi salvare e ricaricare un preset.

**Applica** aggiorna le impostazioni, **OK** conferma e chiude, **Annulla**
scarta le modifiche non applicate. Le impostazioni confermate restano disponibili
al riavvio; aprire un progetto ripristina quelle salvate con quel progetto.

Una nuova analisi conserva gli interventi manuali e le esclusioni; eventuali
sovrapposizioni ambigue richiedono una revisione. Una proposta non è una certezza:
se contiene parlato o un attacco vocale, correggila o escludila.

## Salvare, riprendere ed esportare

Salva con **Ctrl+Shift+S** o dal menu Progetto. Conserva insieme:

- il progetto `.bcnproj.json`;
- la cartella `.breathclick_sessions` creata accanto al progetto;
- le registrazioni sorgenti.

Spostandoli insieme è più semplice riaprire il lavoro su un'altra cartella.
Dopo una chiusura imprevista, usa **Progetto → Riprendi autosalvataggio**,
se disponibile. L'autosalvataggio non sostituisce il salvataggio del progetto.

In **File → Formato esportazione** scegli WAV, MP3 oppure Stesso formato.
FLAC è supportato in ingresso; in uscita viene convertito in WAV.
Per MP3 puoi scegliere qualità CBR o VBR; ogni nuova codifica MP3 comporta
una ricompressione. Per conservare la qualità durante il lavoro, preferisci WAV.

Scegli **Progetto → Elabora ed esporta tutti i file…** e una cartella di
destinazione. Un riepilogo permette di controllare le operazioni prima di partire.
Gli originali non vengono sovrascritti. I nuovi file hanno il suffisso
`_breathclick`; report e piani accompagnano l'esportazione.
Se un file non contiene eventi inclusi, il risultato può essere invariato.

In caso di errore, leggi il messaggio e il report
`breathclick_export_report.json`. Se manca il codec MP3, puoi comunque
esportare in WAV; per ripristinare il codec usa il pacchetto ufficiale completo.

## Profilo personale, facoltativo

Nella RC3 locale puoi selezionare un intervallo sulla forma d'onda e usare
il tasto destro → **Respiro non rilevato** o **Click non rilevato**. Su un
evento puoi segnalare **Falso positivo** oppure **Riparazione insoddisfacente**.
Quest'ultimo giudizio conserva il trattamento valutato senza cambiare audio
o classificazione; puoi annullarlo o cancellarlo. Non avvia addestramenti.
Il profilo adattivo dei click non e' ancora disponibile.

Nel menu **Apprendimento** puoi confermare un respiro, segnalare un falso
positivo o un respiro mancato. **Escludi** è una scelta di montaggio e non
equivale a un falso positivo. Non occorre classificare ogni evento.

Con **Impara dalle correzioni** puoi creare un profilo usando queste
conferme. Servono esempi di entrambe le classi — almeno sei respiri e sei
falsi positivi — provenienti da almeno due registrazioni indipendenti.
Il programma controlla anche esempi separati da quelli usati per creare il profilo:
se non sono sufficienti, spiega perché non può procedere. Copie dello stesso
audio non contano come registrazioni diverse.

Puoi attivare il profilo o tornare a quello generale. Il profilo non modifica
automaticamente il modello AI e non sostituisce la revisione dei respiri mancati.

## Versione e assistenza

Questa guida riguarda la **1.0.0-rc.2 per Windows**. Il trattamento avanzato
degli schiocchi di saliva è ancora sperimentale e non è incluso nella RC2.

**Aiuto → Informazioni** mostra versione e contatti.
[Segnala un problema](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues)
indicando i passaggi e il messaggio ricevuto, senza allegare registrazioni private.

Nicola Lanci Voice Over · [info@nicolalanci.com](mailto:info@nicolalanci.com) ·
[www.nicolalanci.com](https://www.nicolalanci.com)
