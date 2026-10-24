# Licenze e attribuzioni - BreathClick

Il codice BreathClick e' offerto sotto la Mozilla Public License 2.0 (MPL-2.0) in LICENSE. Componenti di terze parti, runtime e
modelli conservano le loro licenze, elencate sotto e in LICENSES/.
Copyright (c) 2026 Nicotina4ever per il codice BreathClick.
La MPL si applica ai file del progetto; non sostituisce le licenze in third_party/.
I sorgenti delle versioni distribuite sono disponibili su
https://github.com/Nicotina4ever/NL-Breath-Click-AI.
LAME rimane una libreria separata, caricata dinamicamente, con sorgenti e
offerta di codice inclusi; non viene rilicenziata sotto MPL.

## kissfft

- Autore: Mark Borgerding.
- Licenza: BSD-3-Clause.
- Uso: FFT compilata dai sorgenti; nessuna libreria precompilata.
- Testo: `LICENSES/kissfft-BSD-3-Clause.txt`.

## ONNX Runtime

- Titolare: Microsoft Corporation e contributori.
- Licenza: MIT.
- Uso: inferenza CPU offline del modello Respiro-en; `onnxruntime.dll`.
- Testo: `LICENSES/ONNX-Runtime-MIT.txt`.

## Respiro-en

- Autori: Dong Yang, Tomoki Koriyama, Yuki Saito.
- Paper: “Frame-Wise Breath Detection with Self-Training”, INTERSPEECH 2024.
- Sorgente: https://huggingface.co/ydqmkkx/respiro-en
- Licenza dichiarata dal modello: MIT.
- Uso: `models/generale/respiro-en-dinamico.onnx` + `.onnx.data`, solo
  inferenza CPU offline. Il modello non è stato riaddestrato.
- Testo: `LICENSES/Respiro-en-MIT.txt`.
- Attribuzione: `models/generale/ATTRIBUZIONE.txt`.

## LAME 3.100 (encoder MP3)

- Progetto: LAME — https://lame.sourceforge.io/
- Licenza: GNU Library General Public License 2.0 (LGPL-2.0).
- Uso: `libmp3lame.dll`, caricata dinamicamente solo quando si esporta MP3.
  Il motore WAV e il riconoscimento non dipendono dalla DLL all'avvio.
- Sorgenti inclusi nel repository in `third_party/lame`; offerta di codice
  sorgente in `LICENSES/LAME-SOURCE-OFFER.txt`.
- Testo: `LICENSES/LGPL-2.0.txt`.

## dr_libs — dr_mp3 e dr_flac

- Autore: David Reid (https://github.com/mackron/dr_libs).
- Licenza: pubblico dominio (Unlicense) oppure MIT-0, a scelta.
- Uso: decodifica MP3 e FLAC incorporata; nessuna DLL di decodifica separata.
- Testo: `LICENSES/dr_libs-Unlicense-MIT-0.txt` e intestazioni in
  `third_party/dr_libs/`.

## Microsoft Visual C++ Runtime

- Titolare: Microsoft Corporation.
- File inclusi: `msvcp140*.dll`, `vcruntime140*.dll`, `vccorlib140.dll`,
  `concrt140.dll`.
- Condizioni: `LICENSES/Microsoft-VC-Runtime.txt`.
- I termini Microsoft prevalgono su qualsiasi riepilogo.

## Componenti di sviluppo non distribuiti

CMake, Visual Studio/MSVC, Python, NumPy, SoundFile e gli strumenti di test
non sono inclusi nel pacchetto standalone. Le dipendenze locali di build
binarie e il modello ONNX sono esclusi dal repository pubblico; le istruzioni
per ricostruire sono nella documentazione del progetto.

## Modelli e dati protetti

Non vengono distribuiti audio originale, annotazioni, dataset, pesi di
training o configurazioni locali. Il modello Respiro-en è distribuito solo
nella forma ONNX dichiarata sopra.
