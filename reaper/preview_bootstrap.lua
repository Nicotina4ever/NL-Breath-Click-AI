-- SPDX-License-Identifier: MPL-2.0
-- Entry used only by start_preview.ps1 in the shared sacrificial REAPER profile.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local resource=reaper.GetResourcePath():gsub('\\','/')
assert(resource:lower():match('/build/reaper_sandbox/portable$'),'Use the dedicated preview profile')
local installed=resource..'/Scripts/NL Audio Tools/Breath Click Studio/BreathClickStudio.lua'
local panel=reaper.file_exists(installed) and installed or (here..'/BreathClickStudio.lua')
local action=reaper.AddRemoveReaScript(true,0,panel,true)
assert(action~=0,'Cannot register the panel in the preview profile')
reaper.Main_OnCommand(action,0)
