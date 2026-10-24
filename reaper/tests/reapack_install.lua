-- SPDX-License-Identifier: MPL-2.0
-- Actual ReaPack downloads, registry and first-use backend in the shared portable.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/../bc_json.lua')
assert(BCS_TEST_ROOT and BCS_TEST_RESOURCE==reaper.GetResourcePath():gsub('\\','/'),'Isolated runner required')
assert(reaper.ReaPack_AddSetRepository,'Official ReaPack extension not loaded')
local url='https://raw.githubusercontent.com/Nicotina4ever/NL-Breath-Click-AI/main/index.xml'
local ok,err=reaper.ReaPack_AddSetRepository('ReaPack','',false,0);assert(ok,err)
ok,err=reaper.ReaPack_AddSetRepository('NL Audio Tools',url,true,1);assert(ok,err)
reaper.ReaPack_ProcessQueue(true)
local sync=reaper.NamedCommandLookup('_REAPACK_SYNC');assert(sync~=0,'ReaPack synchronize action missing')
reaper.Main_OnCommand(sync,0)
local folder=reaper.GetResourcePath():gsub('\\','/')..'/Scripts/NL Audio Tools/Breath Click Studio'
local script=folder..'/BreathClickStudio.lua'
local manifest_before=J.read(folder..'/runtime-release.json')
local ready_before=manifest_before and J.read(folder..'/runtime/'..manifest_before.version..'/READY.json')
local started=reaper.time_precise();local heartbeat=0;local UI;local resynced=false
local function fail(message)
    J.write(BCS_TEST_ROOT..'/result.json',{status='failed',error=message,heartbeat=heartbeat})
    reaper.defer(function()reaper.Main_OnCommand(40004,0)end)
end
local function frame()
    heartbeat=heartbeat+1
    if reaper.time_precise()-started>210 then fail('ReaPack first-use timeout: '..folder);return end
    local success,error=xpcall(function()
        if not reaper.file_exists(script) or not reaper.file_exists(folder..'/runtime-release.json') then reaper.defer(frame);return end
        if not UI then
            local entry,message=reaper.ReaPack_GetOwner(script)
            if not entry then reaper.defer(frame);return end
            assert(reaper.ReaPack_FreeEntry(entry))
            _G.BCS_PANEL_NO_RUN=true;UI=dofile(script);_G.BCS_PANEL_NO_RUN=nil
            assert(not reaper.file_exists(folder..'/bin/bc_studio_cli.exe'),'First use must exercise downloaded backend')
            UI.bridge.runtime.install(folder)
        end
        UI.bridge.runtime.tick(folder)
        local engine=UI.bridge.runtime.engine(folder)
        if not engine then
            local state=J.read(folder..'/runtime/install-status.json')
            assert(not state or state.status~='failed',state and state.error)
            reaper.defer(frame);return
        end
        assert(not reaper.file_exists(folder..'/bc_studio.exe') and not reaper.file_exists(folder..'/bin/bc_studio.exe'),'Standalone must not be installed')
        local m=assert(J.read(folder..'/runtime-release.json'))
        local ready=assert(J.read(folder..'/runtime/'..m.version..'/READY.json'))
        assert(ready.source_commit==m.source_commit and ready.archive_sha256==m.sha256)
        assert(J.write(BCS_TEST_ROOT..'/reapack-install.json',{status='passed',repository=url,source_commit=m.source_commit,archive_sha256=m.sha256,engine=engine,registry_verified=true,resynchronized=ready_before~=nil,first_use_download=ready_before==nil,heartbeat=heartbeat}))
        assert(J.write(BCS_TEST_ROOT..'/result.json',{status='passed',checks={'Actual ReaPack registry and downloaded scripts','Verified first-use HTTPS backend, without standalone'},source_commit=m.source_commit}))
        reaper.defer(function()reaper.Main_OnCommand(40004,0)end)
    end,debug.traceback)
    if not success then fail(tostring(error)) end
end
reaper.defer(frame)
