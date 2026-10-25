# NL Breath Click Studio

**Spend less time on repetitive breath edits and more time listening to the voice.**

[Italiano](README.md) · [English](README.en.md)

**Windows 10/11, 64-bit — pre-release 1.0.0-rc.2**

- **[Download Standalone](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_Windows_Standalone_1.0.0-rc.2.zip)** — work without REAPER.
- **[Download REAPER Windows Beta](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_REAPER_Windows_Beta_1.0.0-rc.2.zip)** — work inside REAPER.
- [Release notes and checksums](https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/tag/v1.0.0-rc.2)

## A helping hand for voice editing

NL Breath Click Studio is a free, open source tool for voice actors,
narrators and audio engineers. It finds breaths in recordings so you can
review, lower, silence or shorten them while keeping the original files intact.
Audio processing happens on your computer, without uploading your recordings.

Lower a distracting breath in a commercial, or review the suggestions across
a long narration and correct the exceptions. Batch processing handles multiple
files together. The time saved depends on your material and the review it
needs; listening remains part of the process.

## Which version should I choose?

| | Standalone | REAPER Windows Beta |
| --- | --- | --- |
| Workspace | A dedicated application | Your REAPER project |
| Getting started | Extract the ZIP and open `bc_studio.exe` | Install the panel using the ZIP or ReaPack |
| Result | Export new WAV or MP3 files | Apply edits to the selected take |
| Requires the other version? | No | No; REAPER itself is required and is not included |

**The two distributions work independently.** Each ZIP includes its own
engine, models and required components. You do not need Python.
With ReaPack, the engine is downloaded and installed on first use.

## Available in RC2

- Detect breaths and adjust their boundaries with the mouse or precise time values.
- Select one or several events, exclude those you want to keep, and add manual edits.
- Attenuate, silence, cut or halve breaths, with Original/Processed comparison and Undo/Redo.
- In the standalone: import WAV, MP3 and FLAC, save projects and export WAV or MP3, including batches.
- In REAPER: edit one breath on the same take without permanent duplicate tracks; compact the voice or keep synchronization.
- Optionally create a personal profile from explicitly confirmed corrections. Excluding an edit is separate from reporting a detection mistake.

The standalone also includes detection and treatment of short clicks.
**Advanced saliva-click repair and the new independent breath/click controls
are still in development and are not included in the RC2 downloads.**

## Get started

### Standalone

1. Extract the **entire** ZIP into a writable folder and open `bc_studio.exe`.
2. In **Progetto** (Project), open a folder of recordings, then choose **Analizza tutti i file** (Analyze all files).
3. Listen and review the suggestions. For example, select a breath and choose **Attenua** (Attenuate) to lower it without changing its duration.
4. Compare **Originale** (Original) and **Elaborato** (Processed), save your project, then export to a destination folder.

[Standalone guide — Italian](docs/USER_GUIDE.md)

### REAPER

1. Extract the REAPER ZIP and follow its English `README.txt`, or [install through ReaPack](docs/REAPER_GUIDE.md#installazione-con-reapack).
2. Run **NL Breath Click Studio** from the action list and analyze the selected voice items.
3. Select a breath, adjust its boundaries if needed and apply a treatment. **Compatta voce** (Compact voice) shortens audio inside the item; **Mantieni sincronizzazione** (Keep synchronization) preserves the overall duration.

Original audio files remain intact. Keep **BreathClick Media** with your
REAPER project so the processed media is available when you reopen it.

[REAPER guide — Italian, with an English quick start](docs/REAPER_GUIDE.md)

## Before using it for a delivery

RC2 is a **release candidate**, and the REAPER integration is a beta.
Start with a copy of your project and listen to edit transitions, especially
after cutting or halving. In the standalone these operations shorten the
file; attenuation and silence preserve its duration.

REAPER direct editing does not support take envelopes, stretch markers,
section/reverse sources or active recording. Try complex FX chains and your
own workflow before relying on them for a delivery. Clean-Windows testing
and professional listening validation are still pending.

The downloads are for **Windows x64**. There are no macOS or Linux
applications available yet; cross-platform engine development is a separate effort.

## Free software, license and support

NL Breath Click Studio is free and open source under the
[Mozilla Public License 2.0](LICENSE). Included components retain their own
licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
REAPER is a separate product with its own license.

To report a problem or suggest an improvement, [open a GitHub issue](https://github.com/Nicotina4ever/NL-Breath-Click-AI/issues).
Include your version, standalone or REAPER, steps to reproduce and any error
message. Only share audio you have permission to make public.

**Nicola Lanci Voice Over** · [info@nicolalanci.com](mailto:info@nicolalanci.com) ·
[www.nicolalanci.com](https://www.nicolalanci.com)

Version and contact details are also under **Aiuto → Informazioni** (Help →
About) in the standalone, or **Informazioni** in the REAPER panel.

## For developers

[Developer documentation](docs/DEVELOPING.md): builds, architecture, SDK/CLI,
tests and distribution. Standalone, REAPER and the future NL Audio Suite
share the engine; the full suite is not part of this release.
