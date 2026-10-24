-- SPDX-License-Identifier: MPL-2.0
local path=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(path..'/../bc_json.lua')
assert(BCS_TEST_ROOT and reaper.CountMediaItems(0)==0 and reaper.CountTracks(0)==0,'Only an empty sandbox')
assert(J.write(BCS_TEST_ROOT..'/result.json',{status='passed',version=reaper.GetAppVersion(),resource=reaper.GetResourcePath()}))
if not BCS_KEEP_OPEN then reaper.defer(function() reaper.Main_OnCommand(40004,0) end) end
