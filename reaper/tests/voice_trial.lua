-- SPDX-License-Identifier: MPL-2.0
-- Functional checks on an authorized local voice COPY, not a quality rating.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local package=assert(BCS_TEST_PACKAGE_DIR,'Independent installed package required')
local source=assert(BCS_TEST_AUDIO,'Build-local voice copy required'):gsub('\\','/')
reaper.DeleteExtState('BreathClickStudio','engine',false)
local X=dofile(package..'/bc_studio_bridge.lua');local J=X.J
local root=assert(BCS_TEST_ROOT):gsub('\\','/')
local checks=J.array();local heartbeat=0
local function test(ok,name)assert(ok,name);checks[#checks+1]=name end
local function wait()while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield() end;assert(not X.last_error,X.last_error)end
local function run()
    test(reaper.CountTracks(0)==0,'Fresh sacrificial project tab')
    test(X.engine():sub(1,#package)==package,'Uses independent packaged engine')
    reaper.InsertTrackAtIndex(0,false);local track=reaper.GetTrack(0,0)
    local item=reaper.AddMediaItemToTrack(track);local take=reaper.AddTakeToMediaItem(item)
    local media=assert(reaper.PCM_Source_CreateFromFile(source))
    local duration=reaper.GetMediaSourceLength(media)
    reaper.SetMediaItemTake_Source(take,media)
    reaper.SetMediaItemInfo_Value(item,'D_LENGTH',duration)
    reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0);reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
    reaper.SelectAllMediaItems(0,false);reaper.SetMediaItemSelected(item,true)
    reaper.Main_SaveProjectEx(0,root..'/Private voice trial.rpp',8)
    X.settings.detect_breath=false;X.settings.detect_clicks=true;X.settings.detector='dsp'
    X.begin('analyze',{item});wait();local st=X.state(item)
    test(#st.plan.events>0,'Real voice copy produces review proposals')
    local index
    for i,e in ipairs(st.plan.events)do
        test(e.kind==1 and not e.training_eligible,'Click-only proposals are not human training labels')
        if e.classification=='digital_click' then index=index or i end
    end
    test(index~=nil,'Trial contains a short impulse proposal to exercise editing')
    X.apply_selected({[index]=true},'repair');wait();st=X.state(item)
    local output=st.result.path
    test(math.abs(reaper.GetMediaItemInfo_Value(item,'D_LENGTH')-duration)<1e-8,'Real voice click edit preserves duration')
    test(st.plan.events[index].bcs_applied or st.plan.events[index].bcs_declined,'Single-event outcome is explicit')
    X.undo(false);test(not X.state(item).result,'Undo restores the real voice source')
    X.undo(true);test(X.state(item).result~=nil,'Redo restores the processed real voice')
    X.difference();wait();local _,difference=X.state(item);local residual=difference.state.residual.path
    test(difference.residual,'Removed signal is attached to the same take')
    X.compare(false);local _,original=X.state(item);test(not original.processed,'Original A/B works with real voice')
    X.compare(true);local _,processed=X.state(item);test(processed.processed and not processed.residual,'Processed A/B restores edited source')
    test(reaper.CountTracks(0)==1 and reaper.CountTakes(item)==1,'No duplicate voice track or take')
    reaper.Main_SaveProjectEx(0,root..'/Private voice trial.rpp',8)
    reaper.Main_openProject('noprompt:'..root..'/Private voice trial.rpp')
    test(X.state(reaper.GetMediaItem(0,0)).result~=nil,'Reopen preserves real voice editing state')
    return {status='passed',checks=checks,heartbeat=heartbeat,source=source,output=output,residual=residual,
        project=root..'/Private voice trial.rpp',scope='Functional voice-copy trial; events are unannotated, no saliva or perceptual quality claim'}
end
local task=coroutine.create(run)
local function frame()
    local ok,value=coroutine.resume(task)
    if ok and coroutine.status(task)~='dead' then reaper.defer(frame);return end
    if not ok then value={status='failed',error=tostring(value),traceback=debug.traceback(task),checks=checks}end
    assert(J.write(root..'/result.json',value))
    if not BCS_KEEP_OPEN then reaper.defer(function()reaper.Main_OnCommand(40004,0)end)end
end
frame()
