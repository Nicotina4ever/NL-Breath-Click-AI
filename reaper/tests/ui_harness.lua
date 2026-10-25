-- SPDX-License-Identifier: MPL-2.0
-- Real input test for the current direct editor, one defer loop and owned HWND.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
assert(BCS_TEST_ROOT and BCS_TEST_PROJECT and BCS_TEST_RESOURCE,'Isolated UI runner required')
reaper.Main_openProject('noprompt:'..BCS_TEST_PROJECT)
_G.BCS_PANEL_NO_RUN=true;local UI=dofile(here..'/../BreathClickStudio.lua');_G.BCS_PANEL_NO_RUN=nil
local X,J=UI.bridge,UI.bridge.J
local root=BCS_TEST_ROOT:gsub('\\','/')
local item=reaper.GetMediaItem(0,0)
reaper.SelectAllMediaItems(0,false);reaper.SetMediaItemSelected(item,true)
local state,ctx=X.state(item);assert(state and state.result,'Direct integration fixture required')
local id,index,old_start
for i,e in ipairs(state.plan.events) do if e.learning_label=='missed_breath' and not e.bcs_applied then id,index,old_start=e.bcs_id,i,e.start;break end end
assert(id,'Expected explicit missing breath not yet applied')
local previous_repeat=reaper.GetSetRepeat(-1);reaper.GetSetRepeat(1)
X.settings.fade_ms=0
local take_count=reaper.CountTakes(item);local take_guid=state.take_guid
local initial_length=reaper.GetMediaItemInfo_Value(item,'D_LENGTH')
local test_title='NL Breath Click Studio - GUI test '..(root:match('[^/]+$') or '')
gfx.init(test_title,1040,760,0)
UI.draw();UI.selected={[index]=true};UI.scroll=math.max(0,index-1);UI.draw()
reaper.atexit(function() X.cancel();X.stop_preview();UI.wave.close();gfx.quit() end)
local ready={title=test_title,buttons=UI.buttons,row={x=25,y=UI.list_top+(index-UI.scroll-1)*27+12},handles=UI.wave_handles}
ready.project=select(2,reaper.EnumProjects(-1,''));ready.take_ready=ctx~=nil and state.result~=nil;ready.host_gate=reaper.file_exists(root..'/host_ready')
assert(ready.host_gate and ready.take_ready,'Initialization: project/panel not ready')
assert(J.write(root..'/ui_ready.json',ready))
local phase,started,checks=0,reaper.time_precise(),J.array()
local last_trace
local function mark(number,label)
    checks[#checks+1]=label;assert(J.write(root..'/step_'..number..'.json',{passed=true,label=label}))
end
local function finish(status,err)
    local debug_state={status=X.status,busy=X.busy,phase=phase,mouse_x=gfx.mouse_x,mouse_y=gfx.mouse_y,mouse_cap=gfx.mouse_cap}
    X.stop_preview();reaper.GetSetRepeat(previous_repeat);UI.wave.close()
    assert(J.write(root..'/result.json',{status=status,error=err or '',checks=checks,resource=reaper.GetResourcePath(),project=BCS_TEST_PROJECT,version=reaper.GetAppVersion(),debug_state=debug_state}))
    reaper.Main_openProject('noprompt:'..BCS_TEST_PROJECT)
    gfx.quit();if not BCS_KEEP_OPEN then reaper.defer(function() reaper.Main_OnCommand(40004,0) end) end
end
local function tick()
    local ok,done=xpcall(function()
        assert(not reaper.file_exists(root..'/input_aborted'),'Native input driver aborted; inspect log and trace')
        assert(UI.draw(),'Panel closed');assert(reaper.time_precise()-started<110,'Timed out at phase '..phase)
        assert(not X.last_error,X.last_error)
        local current,context=X.state(item);local event
        for i,e in ipairs(current.plan.events) do if e.bcs_id==id then event=e;index=i;break end end
        assert(event,'Selected event lost')
        local selection={};for i in pairs(UI.selected) do selection[#selection+1]=i end
        local trace=J.encode({phase=phase,selected=selection,event_index=index,mode=event.mode,
            applied=event.bcs_applied or false,busy=X.busy or false,mouse=gfx.mouse_cap,
            x=gfx.mouse_x,y=gfx.mouse_y,cursor=reaper.GetCursorPosition(),status=X.status})
        if trace~=last_trace then
            local f=assert(io.open(root..'/input_trace.jsonl','ab'));f:write(trace,'\n');f:close();last_trace=trace
        end
        if phase==0 and ((gfx.mouse_cap&1)==1 or UI.wave.drag) then
            local f=assert(io.open(root..'/drag_trace.csv','ab'))
            f:write(string.format('%.5f,%d,%.2f,%.2f,%s,%s,%d\n',reaper.time_precise(),gfx.mouse_cap,gfx.mouse_x,gfx.mouse_y,
                UI.wave.drag and tostring(UI.wave.drag.edge) or '-',UI.wave.drag and tostring(UI.wave.drag.a) or '-',event.start));f:close()
        end
        assert(reaper.CountTakes(item)==take_count,'Direct editing added duplicate takes')
        assert(select(2,reaper.GetSetMediaItemTakeInfo_String(context.take,'GUID','',false))==take_guid,'Take identity changed')
        if phase==0 and UI.wave.drag and UI.wave.drag.a>old_start then
            assert(event.start==old_start,'Drag should commit on release, not every frame')
            mark('drag_live','Boundary handle visibly follows mouse while dragging');phase=1
        elseif phase==1 and not UI.wave.drag and event.start>old_start then mark('drag_saved','Mouse release commits corrected original sample bounds');phase=2
        elseif phase==2 and not X.busy and event.mode=='silence' and event.bcs_applied then
            assert(reaper.GetMediaItemInfo_Value(item,'D_LENGTH')==initial_length,'Silence changed duration')
            mark('silence','Mouse Silenzia immediately applies only this breath on the same take');phase=3
        elseif phase==3 and not event.bcs_applied and event.mode=='attenuate' then mark('undo_silence','Ctrl+Z restores source and proposal in one Undo');phase=4
        elseif phase==4 and not event.approved then mark('excluded','Checkbox performs an editorial exclusion');phase=5
        elseif phase==5 and event.approved then mark('undo_excluded','Ctrl+Z restores editorial inclusion');phase=6
        elseif phase==6 and event.learning_label=='false_positive' and event.training_eligible then mark('false_positive','Mouse explicitly labels a verified false positive');phase=7
        elseif phase==7 and event.learning_label=='missed_breath' then mark('undo_label','Ctrl+Z restores the previous explicit label');phase=8
        elseif phase==8 and event.learning_label=='breath_confirmed' then mark('confirmed','Mouse confirms a breath for optional learning');phase=9
        elseif phase==9 and event.learning_label=='missed_breath' then mark('undo_confirmed','Undo restores missing-breath label');phase=10
        elseif phase==10 and reaper.GetPlayState()==1 then mark('play','Space starts REAPER playback');phase=11
        elseif phase==11 and reaper.GetPlayState()==0 then assert(reaper.GetSetRepeat(-1)==1);mark('stop','Space stops playback and restores Repeat');phase=12
        elseif phase==12 and reaper.GetPlayState()==1 then mark('play_repeat','Audition starts with Repeat enabled');phase=13
        elseif phase==13 and reaper.GetPlayState()==0 then assert(reaper.GetSetRepeat(-1)==1);mark('auto_stop','Audition stops at context end and restores Repeat');phase=14
        elseif phase==14 and not X.busy and event.bcs_applied and event.mode=='halve' then
            assert(reaper.GetMediaItemInfo_Value(item,'D_LENGTH')<initial_length)
            mark('halve','Mouse Dimezza renders selected event immediately without a second take');phase=15
        elseif phase==15 and not X.busy and current.timing=='sync' then
            assert(math.abs(reaper.GetMediaItemInfo_Value(item,'D_LENGTH')-8)<1e-8)
            mark('sync','Mouse Mantieni sync restores original duration using pauses');phase=16
        elseif phase==16 and not context.processed then mark('original','Original A/B swaps source on same take');phase=17
        elseif phase==17 and context.processed then mark('processed','Processed A/B swaps source on same take');phase=18
        elseif phase==18 and X.busy then mark('analysis_started','Mouse Analizza starts a cancellable worker');phase=19
        elseif phase==19 and not X.busy then
            assert(event.bcs_applied and event.learning_label=='missed_breath')
            mark('analysis_done','Reanalysis preserves applied operation and explicit label');phase=20
        elseif phase==20 and reaper.file_exists(root..'/capture_done') then finish('passed');return true end
        return false
    end,debug.traceback)
    if not ok then finish('failed',tostring(done));return end
    if not done then reaper.defer(tick) end
end
reaper.defer(tick)
