NL Breath Click Studio - REAPER Windows Beta 1.0.0-rc.3 (local experimental trial)
Nicola Lanci Voice Over - info@nicolalanci.com - https://www.nicolalanci.com

Windows 10/11 x64, REAPER 7.x x64. REAPER itself is NOT included.
The standalone application is NOT required. CLI, CPU ONNX runtime, general
Respiro-en AI model and codec are included. No Python or SWS is required.

ZIP INSTALLATION (offline)
1. Extract this ZIP to its own folder, retaining every subfolder.
2. In REAPER: Options > Show REAPER resource path. Close the Breath Click panel.
3. Copy the extracted folder into Scripts/NL Breath Click Studio within that
   resource directory. Alternatively run:
   powershell -File Install_REAPER.ps1 -ResourceDirectory "<REAPER resource path>"
   The installer checks the complete package and changes only its Scripts folder.
4. Actions > Show action list > New action > Load ReaScript:
   Scripts/NL Breath Click Studio/BreathClickStudio.lua. Run the action.

REAPACK
The public ReaPack index still supplies RC2, not this experimental RC3.
For this listening trial use this complete ZIP in an isolated REAPER profile.
No new public release or ReaPack update has been published.

FIRST USE
Save a test project, select voice items and Analyze. Select a breath in the
panel or timeline; drag yellow waveform handles or use REAPER time selection.
Attenua, Silenzia, Taglia, Dimezza apply only selected breaths on the same take.
Compact mode shortens voice inside its item; Sync retains overall positions.
A/B uses the same track. Ctrl+Z/Y or panel Undo/Redo restore each operation.
Keep BreathClick Media with your project. Originals on disk are never rewritten.

BREATHS AND CLICKS
Rileva respiri and Rileva click are independent. For a selected click use
Ripara click, then Originale / Elaborato / Solo rimosso to compare.
Auto preserves the audio if reconstruction is uncertain; forced alternatives
need careful listening. See PROVA_RC3.md (Italian and English quick start).

OPTIONAL SHARING
Motore can explicitly select an existing compatible bc_studio_cli.exe, including
a future NL Audio Suite shared backend. No other application is mandatory.
ZIP-installed local backend takes precedence. ReaPack users may configure a
shared engine before first run; automatic backend installation is then skipped.

LIMITS
Release candidate/beta, Windows x64 CPU only. No native macOS/Linux/GPU package.
No take envelopes, stretch markers, section/reverse sources or active recording;
use a consolidated copy for unsupported take processing. Complex FX chains,
clean Windows and professional listening still need validation. Try a copy of
your project first. Saliva-click repair in RC3 is experimental and has not yet
been validated by professional listening. This is not the stable 1.0.0.

LICENSES / SOURCE
Project MPL-2.0, third parties retain their terms. See LICENSE, LICENSES and
THIRD_PARTY_NOTICES.md. Exact LAME source accompanies this ZIP in sources/lame.
BUILD_INFO.json identifies the public source commit; PACKAGE_MANIFEST.json
identifies every file. SHA-256 is supplied alongside the ZIP in GitHub Releases.
Public RC2 remains unchanged:
https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/tag/v1.0.0-rc.2
