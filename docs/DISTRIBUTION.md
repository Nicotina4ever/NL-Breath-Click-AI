# Distribuzioni Windows indipendenti

Release: https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/tag/v1.0.0-rc.2

- **Windows Standalone**: estrarre lo ZIP e avviare bc_studio.exe. REAPER e
  Python non sono necessari. Include runtime, modello generale e codec.
- **REAPER Windows Beta**: estrarre lo ZIP completo e seguire README.txt.
  Copiare in Scripts/NL Breath Click Studio nella risorsa REAPER oppure usare
  Install_REAPER.ps1 con ResourceDirectory esplicita. Caricare BreathClickStudio.lua
  dall'elenco Actions. La standalone e' assente e non e' necessaria.
- **ReaPack**: importare l'indice seguente, cercare NL Breath Click Studio e
  scegliere esplicitamente la pre-release 1.0.0-rc.2. ReaPack installa script e
  manifest con checksum. Al primo avvio il worker scarica lo ZIP REAPER
  ufficiale, verifica SHA-256 e manifest e installa solo backend e licenze.
  Il pannello rimane reattivo; Installa motore consente di riprovare.

```
https://raw.githubusercontent.com/Nicotina4ever/NL-Breath-Click-AI/main/index.xml
```

Gli aggiornamenti ReaPack gestiscono gli script; il backend e' versionato,
verificato e riutilizzato se gia' integro. Chiudere/riaprire il pannello dopo
l'aggiornamento. Nessuna estrazione ZIP implicita di ReaPack. Nessun installer
interviene su configurazioni audio, progetti o registrazioni. Per condividere
facoltativamente un backend esistente usare Motore prima del primo avvio;
non esiste una dipendenza obbligatoria fra i prodotti.

## Provenienza e riproduzione

Il tag v1.0.0-rc.2 identifica il sorgente esatto dei binari. Configurare
BC_SOURCE_COMMIT con quel commit e compilare Release; la CLI espone
source_commit in capabilities. Il packager rifiuta un commit diverso.
BUILD_INFO.json, PACKAGE_MANIFEST.json e gli SHA-256 identificano ogni asset.
L'indice ReaPack su main puo' essere pubblicato dopo il tag: punta agli script
del commit immutabile del tag e al manifest dell'asset verificato.

```
python scripts/package_reaper.py --standalone-zip <ZIP-standalone> --source-commit <commit>
python scripts/generate_reapack.py --commit <commit> --runtime-manifest dist/runtime-release.json
python scripts/verify_distributions.py --standalone <ZIP> --reaper <ZIP> --commit <commit> --work <vuota> --report <json>
```

scripts/github_release.py richiede --publish esplicito, usa le credenziali
Git configurate senza registrarle, crea un tag annotato e una pre-release.
Non sostituisce tag o asset pubblicati. Pubblicare solo dopo gate privacy,
Release, CTest, verifica estratto e collaudo REAPER del pacchetto installato.
Licenze e redistribuzione: REDISTRIBUTION.md. Nessuna build macOS/Linux o GPU
viene dichiarata disponibile in questa pre-release Windows.
