-- SPDX-License-Identifier: MPL-2.0
-- Called only by WM_COMMAND on the verified portable's own HWND.
reaper.set_action_options(3)
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/../bc_json.lua')
local resource=reaper.GetResourcePath():gsub('\\','/')
assert(resource:lower():match('/build/reaper_sandbox/portable$'),'Test portable required')
local root=resource:match('^(.*)/portable$')
local repo=(here:gsub('\\','/')):match('^(.*)/reaper/tests$')
local pointer=J.read(root..'/host_request.json')
assert(pointer and type(pointer.name)=='string' and pointer.name:match('^[%x]+%.request%.json$'),'Invalid test request')
local path=root..'/host_requests/'..pointer.name
local request=J.read(path)
if not request then return end -- Already consumed, never apply the same request twice.
assert(os.rename(path,path..'.received'))
local action
local ok,err=xpcall(function()
    local script=assert(request.script):gsub('\\','/')
    local managed=script:match('^(.*)/bootstrap%.lua$')
    managed=managed and managed:lower():match('^'..(repo..'/build/test_work/managed/reaper_'):lower():gsub('([^%w])','%%%1')..'[a-z0-9_]+$') and
        J.read(script:match('^(.*)/bootstrap%.lua$')..'/.nlbc-test-workspace.json')
    managed=managed and managed.owner=='nlbc-synthetic-tests-v1'
    assert(not script:find('/%.%./') and (script:sub(1,#root+1):lower()==(root..'/'):lower() or
        script:sub(1,#repo+8):lower()==(repo..'/reaper/'):lower() or managed),'Request outside test/source perimeter')
    action=reaper.AddRemoveReaScript(true,0,script,true);assert(action~=0,'Cannot register requested script')
end,debug.traceback)
assert(J.write(root..'/host_requests/'..pointer.name:gsub('%.request%.json$','.result.json'),
    {status=ok and 'dispatched' or 'initialization_failed',error=ok and '' or tostring(err),main_window=tostring(reaper.GetMainHwnd())}))
if ok then reaper.Main_OnCommand(action,0)end
