# Componenti della prima pre-release Windows

Verifica del 25 ottobre 2026; hash esatti in DEPENDENCIES.lock.json.

| Componente | Termini e distribuzione |
|---|---|
| Codice NL Breath Click Studio | MPL-2.0, testo LICENSE e sorgente del tag pubblico |
| Respiro-en generale ONNX e external data | Repository ydqmkkx/respiro-en, commit ea478afd15a27ec4f24598cb77dc8199a9bcf35e: card license: mit, pubblico e non gated; trasformazione dinamica per inferenza, nessun fine-tuning. Attribuzione e MIT inclusi |
| ONNX Runtime CPU 1.31.0 | MIT; DLL invariata, testo MIT e avvisi completi ThirdPartyNotices della distribuzione 1.31.0 inclusi |
| kissfft | BSD-3-Clause, notice inclusa; compilata nel motore |
| dr_mp3 / dr_flac | MIT-0 oppure Unlicense, intestazioni e testo inclusi |
| LAME 3.100 | LGPL-2.0, DLL separata sostituibile, testo e sorgenti corrispondenti completi in sources/lame in entrambi gli ZIP |
| Microsoft VC143 CRT x64 | DLL inalterate dalla cartella Redist del toolchain VS installato, nei termini della Distributable List Microsoft; avviso incluso |

Fonti: https://huggingface.co/ydqmkkx/respiro-en/blob/ea478afd15a27ec4f24598cb77dc8199a9bcf35e/README.md
https://github.com/microsoft/onnxruntime/blob/main/LICENSE
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution

Solo il modello generale con i due hash dichiarati viene distribuito.
Nessun peso personale, dataset, voce privata o REAPER incluso. ReaPack stesso
non e' incluso nei prodotti. Il suo runtime ufficiale, se necessario al test,
resta esclusivamente nella portable isolata, con licenza upstream separata.
La release usa CPU; CUDA, cuDNN, provider GPU e relativi termini non sono
inclusi o implicitamente autorizzati da questa distribuzione.
