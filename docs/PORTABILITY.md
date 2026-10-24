# Portabilita' dopo la prima pre-release Windows

Il tag v1.0.0-rc.2 e i suoi asset rimangono immutati. Gli sviluppi successivi
su main non dichiarano un nuovo pacchetto macOS/Linux gia' disponibile.

Core/StudioEngine e CLI sono separati dalle GUI Win32 e compilabili headless:

```
cmake -S . -B build -DBC_BUILD_EDITOR=OFF -DBC_BUILD_STUDIO=OFF -DBC_BUILD_STUDIO_CLI=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Windows usa BCrypt e il LAME vendorizzato. Linux/macOS usano OpenSSL Crypto
per SHA-256 e LAME di sistema (Ubuntu: libssl-dev/libmp3lame-dev;
Homebrew: openssl@3/lame). Il codec viene copiato accanto ai binari dei test.
Nessuna dipendenza Win32 nel loader portabile: dlopen, percorso eseguibile
nativo e percorsi UTF-8. ONNX Runtime puo' essere caricato come .so/.dylib;
la sua inferenza richiede un runtime nativo compatibile e il modello generale.
Una build DSP senza runtime AI non costituisce un test di inferenza AI POSIX.
La CI prepara esplicitamente modello pubblico e runtime nativo 1.31.0 con
scripts/prepare_ci_runtime.py: ZIP della release e wheel ufficiali PyPI con
SHA-256 fissati. Il test pipeline richiede inferenza AI e Fusion reale;
assenza di questi artefatti fallisce il test, senza salti o fallback DSP.

CI: .github/workflows/portable.yml esegue build/test headless e consumer SDK
su Linux, macOS e Windows. Non esegue GUI Win32 o una DAW su quei runner.
Le prove GUI/audio Windows e i test REAPER reali nella portable sono separati.
L'adapter riconosce CLI e quoting POSIX e ha un worker Python 3 cancellabile
con test audio reali del bridge; il REAPER nativo macOS/Linux richiede ancora
collaudo manuale prima di una distribuzione su quei sistemi.

GUI: la versione Windows completa rimane il frontend di produzione. Il motore,
le sessioni, TimeMap e il modello di editing sono gia' riutilizzabili. Per il
frontend comune il candidato e' Qt 6 Widgets (QPainter per la waveform,
model/view per le regioni e backend audio separato), da verificare con una
matrice di parita' prima di sostituire controlli Win32. Nessuna GUI ridotta viene
promossa come sostituta: selezione/bordi, Undo, etichette, A/B, batch e progetti
restano requisiti. I moduli Qt necessari dovranno essere scelti verificando
LGPL/commerciale e distribuzione: Qt non viene introdotto nei pacchetti attuali.

Riferimenti di licenza per le scelte successive:
https://doc.qt.io/qt-6/licensing.html (verificare i singoli moduli, alcuni GPL-only);
https://openssl-library.org/source/license/ (OpenSSL >=3: Apache-2.0).
Questi componenti non sono stati aggiunti ai pacchetti Windows del tag RC2.
