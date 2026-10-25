# Distribuzione — riferimento sviluppatori

Per scaricare e installare: [README](../README.md),
[guida standalone](USER_GUIDE.md), [guida REAPER e ReaPack](REAPER_GUIDE.md).
I due ZIP Windows sono indipendenti; ciascuno include il proprio backend.

## Provenienza dei pacchetti RC2

Il tag `v1.0.0-rc.2` identifica il sorgente esatto dei binari. Configurare
`BC_SOURCE_COMMIT` con quel commit e compilare Release; la CLI espone
`source_commit` in `capabilities`. Il packager rifiuta un commit diverso.
`BUILD_INFO.json`, `PACKAGE_MANIFEST.json` e gli SHA-256 identificano ogni asset.
Le correzioni alla documentazione su main non cambiano la provenienza dei binari.

L'indice ReaPack su main punta agli script del commit immutabile del tag
e al manifest dell'asset. ReaPack installa script e manifest; il worker
scarica lo ZIP REAPER, verifica SHA-256 e manifest, poi installa backend e licenze.
Non si presuppone che ReaPack estragga ZIP. Il backend è versionato e
riutilizzato se compatibile e integro; la condivisione esplicita tramite
**Motore** è facoltativa, senza dipendenze fra prodotti.

## Packaging e pubblicazione

Usare gli script e la versione del checkout che si intende distribuire:

```text
python scripts/package_reaper.py --standalone-zip <ZIP-standalone> --source-commit <commit>
python scripts/generate_reapack.py --commit <commit> --runtime-manifest dist/runtime-release.json
python scripts/verify_distributions.py --standalone <ZIP> --reaper <ZIP> --commit <commit> --work <vuota> --report <json>
```

`scripts/github_release.py` richiede `--publish` esplicito e usa le credenziali
Git configurate senza registrarle. Crea un tag annotato e una pre-release;
non sostituisce tag o asset pubblicati. Non eseguirlo per modificare soltanto
le note di una release esistente.

Prima di una nuova distribuzione occorrono controllo dei file pubblicati,
build Release, CTest, verifica del pacchetto estratto e collaudo REAPER del
pacchetto installato. [BUILDING.md](../BUILDING.md) descrive la preparazione;
[REDISTRIBUTION.md](REDISTRIBUTION.md) raccoglie licenze e modelli.
La RC2 non contiene applicazioni native macOS/Linux o backend GPU.
