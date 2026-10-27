-- SPDX-License-Identifier: MPL-2.0
-- Register a process-bound native action; no permanent polling/defer loop.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/../bc_json.lua')
local resource=reaper.GetResourcePath():gsub('\\','/')
assert(resource:lower():match('/build/reaper_sandbox/portable$'),'Test portable required')
local root=resource:match('^(.*)/portable$')
reaper.RecursiveCreateDirectory(root..'/host_requests',0)
local action=reaper.AddRemoveReaScript(true,0,here..'/portable_dispatch.lua',true)
assert(action>0 and action<65536,'Native dispatcher action ID unavailable')
os.remove(root..'/host.json') -- Ephemeral identity of the previous, closed host.
assert(J.write(root..'/host.json',{protocol=2,resource=resource,main_window=tostring(reaper.GetMainHwnd()),action=action}))
