# NL Breath Click Studio RC3 — prova locale

Questa build e' destinata al collaudo: non e' la 1.0 stabile e non modifica
la RC2 pubblicata. Standalone e REAPER funzionano indipendentemente.

## Come provarla

- Nel workspace apri **editor/Prova_RC3.cmd**, oppure estrai interamente lo
  ZIP standalone aggiornato e avvia **bc_studio.exe**.
- Per REAPER usa **reaper/Prova_RC3.cmd**: riutilizza la singola portable di
  prova. Lo ZIP REAPER contiene il proprio motore, senza includere REAPER;
  le istruzioni d'installazione sono in README.txt.
- Apri una registrazione completa, scegli **Rileva respiri**, **Rileva click /
  bocca** o entrambi e premi **Analizza**. Nella standalone i comandi sono
  direttamente nella barra principale. **Analisi combinata (voce)** e' la
  scelta iniziale quando sono presenti modello vocale e runtime.
  L'avanzamento dell'analisi resta visibile; **Annulla** interrompe il lavoro
  conservando le regioni e le correzioni precedenti.
- Correggi i bordi trascinando le regioni. Usa Attenua, Silenzia, Taglia o
  Dimezza sui respiri; sui click scegli Ripara e confronta Auto, Lineare e
  Predittiva (Burg). Gli schiocchi morbidi dubbi restano da rivedere.
- Confronta **Originale**, **Elaborato** e **Solo rimosso**. Se senti parti
  della voce nel rimosso, correggi i confini, cambia metodo o annulla.
  **Intatto: sicurezza** significa che il click NON e' stato riparato.
- In REAPER **Tutte** agisce soltanto sugli eventi del filtro visibile.
  Compatta voce accorcia la take; Mantieni sync conserva la durata.
  Gli altri item e le tracce con cue restano fermi.
- Salva il progetto per conservare confini, inclusioni e classificazioni.
  Nel menu **Apprendimento** della standalone e in **Crea profilo** in REAPER
  puoi creare profili separati per respiri e click. Servono esempi confermati
  e falsi positivi da registrazioni indipendenti. Escludere un evento non lo
  trasforma in un falso positivo e nessuna rete viene riaddestrata da sola.

## Limiti attuali

Il rilevamento non e' esaustivo e richiede revisione. Le nuove proposte di
schiocchi a banda limitata non vengono riparate automaticamente. Anche una
scelta manuale conserva l'originale se la ricostruzione e' troppo rischiosa;
non significa che il disturbo sia scomparso. La riparazione dei click non
accorcia la registrazione e tratta al massimo 45 ms per evento.

L'ascolto professionale, soprattutto sugli schiocchi sovrapposti alla voce,
resta decisivo. Sono ancora necessari il collaudo su Windows pulito e le
verifiche ad alto DPI. macOS e Linux non sono applicazioni GUI distribuite.
Non inviare audio privato su GitHub.

### English quick trial

Extract the entire standalone ZIP and run **bc_studio.exe**, or follow
README.txt in the independent REAPER ZIP. Enable breaths, clicks or both,
analyze a full recording and review the proposed boundaries. Compare Original,
Processed and Removed signal before exporting. A safety refusal preserves the
original: it does not mean the click was removed. Uncertain soft mouth clicks
remain review proposals. Both hosts support separate optional breath/click
personal profiles based on explicit labels and independent recordings.
This is a local candidate; RC2 remains unchanged and no stable release is
being published.
