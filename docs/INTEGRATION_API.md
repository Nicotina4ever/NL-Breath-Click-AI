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
