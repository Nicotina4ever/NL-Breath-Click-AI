# SPDX-License-Identifier: MPL-2.0
"""Percorsi condivisi dei test NL Breath Click Studio.

Preferisce variabili d'ambiente, altrimenti deduce il workspace dalla posizione
del repository: nessun percorso K: obbligatorio.
"""
import os
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = PROJECT_ROOT.parent
DEFAULT_WORK = PROJECT_ROOT / "build" / "test_work"
WORK = Path(os.environ.get("BC_STUDIO_WORK", DEFAULT_WORK))
INPUT = Path(os.environ.get("BC_STUDIO_INPUT", WORK / "input"))
CLI = Path(os.environ.get("BC_STUDIO_CLI", Path(os.environ.get("BC_TEST_BIN", PROJECT_ROOT / "build" / "bin" / "Release")) / "bc_studio_cli.exe"))

BIN = Path(os.environ.get("BC_TEST_BIN", PROJECT_ROOT / "build" / "bin" / "Release"))
GEN = Path(os.environ.get("BC_STUDIO_GEN", BIN / "bc_cli.exe"))
