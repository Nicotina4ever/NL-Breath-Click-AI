# SPDX-License-Identifier: MPL-2.0
"""Owned REAPER startup gate. Never enables controls or changes licensing.
Windows: native HWND state. macOS: Accessibility via System Events.
Linux: X11 xdotool; Wayland/permissions fail explicitly, never assume ready.
"""
import argparse
import ctypes
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

class InitializationError(RuntimeError):
    pass

def decision(snapshot):
    evaluations = [w for w in snapshot if w.get("evaluation")]
    if evaluations:
        buttons = [b for w in evaluations for b in w.get("buttons", []) if b.get("continue") and b.get("enabled")]
        return ("continue", buttons[0]["id"]) if buttons else ("countdown", None)
    return ("ready", None) if any(w.get("main") and w.get("enabled") for w in snapshot) else ("initializing", None)

def continue_caption(text):
    return bool(re.match(r"^(still evaluating|continua la valutazione|continua valutazione)(?:\s*\(\d+\))?$",text.replace("&","").lower().strip()))

class Windows:
    def __init__(self, pid):
        from ctypes import wintypes as W
        self.pid = pid
        self.u = ctypes.WinDLL("user32", use_last_error=True)
        self.callback = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        self.u.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
        self.u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
        self.u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
        self.u.IsWindowEnabled.argtypes = [W.HWND]
        self.u.IsWindowVisible.argtypes = [W.HWND]
        self.u.ShowWindow.argtypes = [W.HWND, ctypes.c_int]
        self.shown = False
        self.u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
        self.u.SendMessageW.restype = W.LPARAM
        self.u.EnumWindows.argtypes = [self.callback, W.LPARAM]
        self.u.EnumChildWindows.argtypes = [W.HWND, self.callback, W.LPARAM]
    def text(self, handle, cls=False):
        text = ctypes.create_unicode_buffer(4096)
        (self.u.GetClassNameW if cls else self.u.GetWindowTextW)(handle, text, len(text))
        return text.value
    def owned(self, handle):
        owner = ctypes.c_ulong()
        self.u.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        return owner.value == self.pid
    def snapshot(self):
        windows = []
        def read(handle, _):
            if not self.owned(handle):
                return True
            title = self.text(handle)
            if self.text(handle, True) == "REAPERwnd" and not self.shown:
                self.u.ShowWindow(handle, 5)
                self.shown = True
            children = []
            def child(h, _):
                children.append({"id": int(h), "text": self.text(h), "enabled": bool(self.u.IsWindowEnabled(h)),
                                 "continue": continue_caption(self.text(h))})
                return True
            self.u.EnumChildWindows(handle, self.callback(child), 0)
            evaluation = bool(self.u.IsWindowVisible(handle)) and (any(b["continue"] for b in children) or ("about reaper" in title.lower() and any("evaluat" in b["text"].lower() for b in children)))
            windows.append({"id": int(handle), "main": self.text(handle, True) == "REAPERwnd",
                            "enabled": bool(self.u.IsWindowEnabled(handle)), "evaluation": evaluation,
                            "buttons": children if evaluation else [], "title": title})
            return True
        self.u.EnumWindows(self.callback(read), 0)
        return windows
    def click(self, handle):
        # Recheck ownership and state at the moment of ordinary BM_CLICK.
        if not self.owned(handle) or not self.u.IsWindowEnabled(handle):
            return
        if not continue_caption(self.text(handle)):
            raise InitializationError("Evaluation control changed; no input sent")
        self.u.SendMessageW(handle, 0xF5, 0, 0)

class Mac:
    def __init__(self, pid):
        self.pid = pid
    def script(self, body):
        code = 'tell application "System Events"\nset p to first application process whose unix id is '+str(self.pid)+'\n'+body+'\nend tell'
        result = subprocess.run(["osascript", "-e", code], capture_output=True, text=True, timeout=10)
        if result.returncode:
            raise InitializationError("macOS window inspection unavailable (Accessibility permission required)")
        return result.stdout.strip()
    def snapshot(self):
        rows = self.script('set rows to ""\nrepeat with w in windows of p\nset ev to exists button "Still Evaluating" of w\nset en to false\nif ev then set en to enabled of button "Still Evaluating" of w\nset rows to rows & (id of w as text) & "|" & (ev as text) & "|" & (en as text) & linefeed\nend repeat\nreturn rows')
        return [{"id": int(i), "main": ev == "false", "enabled": True, "evaluation": ev == "true",
                 "buttons": [{"id": int(i), "continue": True, "enabled": en == "true"}]} for i, ev, en in (r.split("|") for r in rows.splitlines())]
    def click(self, window):
        self.script('tell window id '+str(window)+' of p\nif exists button "Still Evaluating" then\nif enabled of button "Still Evaluating" then click button "Still Evaluating"\nend if\nend tell')

class Linux:
    def __init__(self, pid):
        self.pid = pid
        if not os.environ.get("DISPLAY") or not shutil.which("xdotool"):
            raise InitializationError("Linux native startup inspection requires X11/xdotool; Wayland needs a native adapter")
    def command(self, *args):
        return subprocess.run(["xdotool", *map(str, args)], capture_output=True, text=True, timeout=10)
    def snapshot(self):
        ids = self.command("search", "--onlyvisible", "--pid", self.pid).stdout.split()
        windows = []
        for identifier in ids:
            title = self.command("getwindowname", identifier).stdout.strip()
            if "About REAPER" in title:
                # X11's opaque SWELL controls cannot safely prove enabled state.
                # Fail closed rather than sending guessed coordinates/keys.
                raise InitializationError("REAPER evaluation dialog found on X11: native enabled-button adapter still required")
            windows.append({"id": int(identifier), "main": "REAPER" in title, "enabled": True})
        return windows
    def click(self, _):
        raise InitializationError("No verified Linux evaluation control adapter")

def wait(adapter, timeout=90, probe=None, clock=time.monotonic, sleep=time.sleep):
    start = clock()
    stable = None
    observed = {"countdown_observed": False, "continue_clicks": 0, "polls": 0}
    while clock() - start < timeout:
        snapshot = adapter.snapshot()
        phase, control = decision(snapshot)
        observed["polls"] += 1
        if phase == "countdown":
            observed["countdown_observed"] = True
        if phase == "continue":
            adapter.click(control)
            observed["continue_clicks"] += 1
        if phase == "ready" and (probe is None or probe.exists()):
            stable = clock() if stable is None else stable
            if clock() - stable >= .3:
                return dict(observed, status="ready", elapsed_seconds=round(clock()-start,3))
        else:
            stable = None
        sleep(.1)
    raise InitializationError("REAPER initialization timeout: evaluation/main window not ready; BreathClick tests were not started")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pid", type=int, required=True)
    p.add_argument("--timeout", type=float, default=90)
    p.add_argument("--probe", type=Path)
    p.add_argument("--report", type=Path, required=True)
    a = p.parse_args()
    try:
        adapter = Windows(a.pid) if sys.platform == "win32" else Mac(a.pid) if sys.platform == "darwin" else Linux(a.pid)
        result = wait(adapter, a.timeout, a.probe)
    except Exception as error:
        result = {"status": "initialization_failed", "error": str(error)}
    a.report.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["status"] == "ready" else 2

if __name__ == "__main__":
    raise SystemExit(main())
