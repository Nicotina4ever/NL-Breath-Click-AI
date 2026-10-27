-- SPDX-License-Identifier: MPL-2.0
-- Multi-item/take and edge regression on a build-local full recording COPY.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local package=BCS_TEST_PACKAGE_DIR or (here..'/..')
if BCS_TEST_PACKAGE_DIR then reaper.DeleteExtState('BreathClickStudio','engine',false) end
local X=dofile(package..'/bc_studio_bridge.lua');local J=X.J
local root=assert(BCS_TEST_ROOT):gsub('\\','/')
local source=assert(BCS_TEST_AUDIO):gsub('\\','/')
local checks=J.array();local heartbeat=0;local outputs=J.array()
local function test(ok,name)assert(ok,name);checks[#checks+1]=name end
local function wait()while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield() end;assert(not X.last_error,X.last_error)end
local function select(item)reaper.SelectAllMediaItems(0,false);reaper.SetMediaItemSelected(item,true)end
local function item_chunk(item)local ok,s=reaper.GetItemStateChunk(item,'',false);assert(ok);return s end
local function take_snapshot(take)
    local state={path=reaper.GetMediaSourceFileName(reaper.GetMediaItemTake_Source(take),'')}
    for _,field in ipairs{'D_STARTOFFS','D_PLAYRATE','D_PITCH','B_PPITCH','D_VOL','D_PAN'}do state[field]=reaper.GetMediaItemTakeInfo_Value(take,field)end
    state.markers=J.array();for i=0,reaper.GetNumTakeMarkers(take)-1 do local p,n,c=reaper.GetTakeMarker(take,i);state.markers[#state.markers+1]={p,n,c}end
    state.fx=J.array();for i=0,reaper.TakeFX_GetCount(take)-1 do state.fx[#state.fx+1]=reaper.TakeFX_GetFXGUID(take,i)end
    state.ext=({reaper.GetSetMediaItemTakeInfo_String(take,'P_EXT:BreathClickStudio','',false)})[2]
    return J.encode(state)
end
local function run()
    test(reaper.CountTracks(0)==0,'Fresh sacrificial project')
    if BCS_TEST_PACKAGE_DIR then test(X.engine():sub(1,#package)==package,'Uses independent packaged engine')end
    reaper.InsertTrackAtIndex(0,false);reaper.InsertTrackAtIndex(1,false)
    local voice=reaper.GetTrack(0,0);local backing=reaper.GetTrack(0,1)
    local function make(track,pos,off)
        local item=reaper.AddMediaItemToTrack(track);local take=reaper.AddTakeToMediaItem(item)
        assert(reaper.SetMediaItemTake_Source(take,assert(reaper.PCM_Source_CreateFromFile(source))))
        reaper.SetMediaItemInfo_Value(item,'D_POSITION',pos);reaper.SetMediaItemInfo_Value(item,'D_LENGTH',4)
        reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0);reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
        reaper.SetMediaItemTakeInfo_Value(take,'D_STARTOFFS',off)
        return item,take
    end
    local a,ta=make(voice,10,.75);local b,tb=make(voice,20,1.25);local cue=make(backing,10,0)
    local alternative=reaper.AddTakeToMediaItem(a)
    assert(reaper.SetMediaItemTake_Source(alternative,assert(reaper.PCM_Source_CreateFromFile(source))))
    reaper.SetMediaItemTakeInfo_Value(alternative,'D_STARTOFFS',2)
    reaper.SetMediaItemTakeInfo_Value(alternative,'D_PAN',-.3)
    reaper.SetMediaItemTakeInfo_Value(alternative,'D_PITCH',2)
    reaper.SetTakeMarker(alternative,-1,'Alternative cue',2.5,0)
    local fx=reaper.TakeFX_AddByName(ta,'JS: BreathClickTestGain',1)
    test(fx>=0,'Existing real JS take FX loaded')
    reaper.SetTakeMarker(ta,-1,'Edge cue',.75+3.9,0)
    reaper.SetActiveTake(ta);select(a)
    reaper.AddProjectMarker2(0,false,11,0,'Unselected backing cue',100,0)
    reaper.Main_SaveProjectEx(0,root..'/Professional.rpp',8)
    local untouched=take_snapshot(alternative);local cue_chunk=item_chunk(cue)
    X.settings.detector='fusion';X.settings.fade_ms=0
    local mode_events={}
    for _,mode in ipairs{{'breaths',true,false},{'clicks',false,true},{'both',true,true}}do
        X.settings.detect_breath=mode[2];X.settings.detect_clicks=mode[3]
        X.begin('analyze',{a,b});wait()
        local st=X.state(a);local events=J.array()
        for _,e in ipairs(st.plan.events)do
            test((mode[2] or e.kind~=0) and (mode[3] or e.kind~=1),'Mode '..mode[1]..' category isolation')
            events[#events+1]={kind=e.kind,start=e.start,finish=e['end'],classification=e.classification}
            if e.kind==1 and (e.classification=='speech_overlap' or e.classification=='vocal_transient' or e.classification=='mouth_click')then
                test(not e.approved and not e.training_eligible,'Uncertain click remains review-only')
            end
        end
        mode_events[mode[1]]=events
        test(mode[1]~='clicks' or st.plan.ai_inference_frames==0,'Click-only bypasses breath inference')
        test(take_snapshot(alternative)==untouched and item_chunk(cue)==cue_chunk,'Analysis preserves inactive take and unselected cue item')
    end
    for kind,name in pairs{[0]='breaths',[1]='clicks'}do
        local subset=J.array();for _,e in ipairs(mode_events.both)do if e.kind==kind then subset[#subset+1]=e end end
        test(J.encode(subset)==J.encode(mode_events[name]),'Native '..name..' proposals identical alone/together')
    end
    local reference_click
    for _,e in ipairs(X.state(a).plan.events)do if e.kind==1 then reference_click=e;break end end
    test(reference_click~=nil,'Real copy has click proposals for cross-category reconciliation')
    select(a)
    reaper.GetSet_LoopTimeRange2(0,true,false,10+reference_click.start/48000,10+reference_click['end']/48000,false)
    local breath_id=X.add_manual(nil,0)
    X.begin('analyze',{a});wait()
    local retained_click,retained_breath=false,false
    for _,e in ipairs(X.state(a).plan.events)do
        if e.kind==1 and e.start==reference_click.start and e['end']==reference_click['end'] then retained_click=true end
        if e.bcs_id==breath_id then retained_breath=true end
    end
    test(retained_click and retained_breath,'Manual breath does not suppress a newly detected overlapping click')
    local function exclude(item)
        select(item);local all={};for i in ipairs(X.state(item).plan.events)do all[i]=true end
        if next(all) then X.include(all,false)end
    end
    exclude(a);exclude(b)
    local function add(item,first,last,kind)
        select(item);local pos=reaper.GetMediaItemInfo_Value(item,'D_POSITION')
        reaper.GetSet_LoopTimeRange2(0,true,false,pos+first/48000,pos+last/48000,false)
        local id=X.add_manual(nil,kind)
        for i,e in ipairs(X.state(item).plan.events)do if e.bcs_id==id then return i end end
        error('Manual event missing')
    end
    local left=add(a,1,6,1);local right=add(a,192000-6,192000-1,1)
    X.repair_method({[left]=true,[right]=true},'linear');wait()
    local b_before=item_chunk(b)
    X.apply_selected({[left]=true},'repair');wait()
    test(X.state(a).result.map.applied_events==1 and not X.state(a).plan.events[right].bcs_applied,'Single left-edge event does not apply pending right-edge event')
    test(item_chunk(b)==b_before,'Single edit leaves another selected-capable item byte-identical')
    X.apply_selected({[right]=true},'repair');wait()
    local processed=X.state(a).result.path
    test(X.state(a).result.map.applied_events==2 and reaper.GetMediaItemInfo_Value(a,'D_LENGTH')==4,'Successive edge repairs apply once and preserve length')
    test(reaper.CountTakes(a)==2 and take_snapshot(alternative)==untouched,'Two pre-existing takes remain two; inactive take unchanged')
    test(reaper.TakeFX_GetFXGUID(ta,fx)~=nil and reaper.CountTracks(0)==2,'Existing FX and track count preserved')
    X.undo(false);test(X.state(a).result.map.applied_events==1,'Undo removes only second edge edit')
    X.undo(true);test(X.state(a).result.map.applied_events==2,'Redo restores both edge edits once')
    X.difference();wait();local _,ctx=X.state(a);local residual=ctx.state.residual.path
    X.compare(false);test(X.state(a).source_path==source,'Original restores disk source on same take')
    X.compare(true)
    outputs[#outputs+1]={source=X.media_path(X.state(a).prepared_rel),output=processed,residual=residual}
    reaper.SetActiveTake(alternative)
    test(not X.state(a),'Unanalysed alternative does not inherit another take review')
    test(not pcall(X.apply_selected,{[left]=true},'repair'),'Applying another take events to alternative is blocked')
    reaper.SetActiveTake(ta)
    local mid=add(b,24000,26400,0);X.set_mode({[mid]=true},'silence')
    reaper.SetMediaItemSelected(a,true)
    local a_before=item_chunk(a)
    X.begin('render',{a,b});wait()
    test(X.state(b).result and X.state(b).plan.events[mid].bcs_applied,'Mixed batch applies pending item when another item is already processed')
    test(item_chunk(a)==a_before,'Mixed batch does not rewrite already processed item')
    test(X.state(b).result.map.applied_events==1,'Batch applies only one selected pending interval')
    test(item_chunk(cue)==cue_chunk and take_snapshot(alternative)==untouched,'Batch preserves unselected cue and inactive take')
    X.undo(false);test(not X.state(b).result,'One batch Undo restores pending item original')
    X.undo(true);test(X.state(b).result~=nil,'One batch Redo restores pending item edit')
    local stale=add(b,48000,49200,0)
    local previous=X.state(b).result.path
    X.apply_selected({[stale]=true},'silence');reaper.SetActiveTake(alternative)
    -- Switching a DIFFERENT item is harmless; the active source being edited is unchanged.
    wait();test(X.state(b).result.path~=previous,'Changing another item active take does not invalidate independent render')
    reaper.SetActiveTake(ta)
    test(item_chunk(cue)==cue_chunk and take_snapshot(alternative)==untouched,'Unselected media still intact after asynchronous edit')
    select(a);local previous_source=reaper.GetMediaSourceFileName(reaper.GetMediaItemTake_Source(ta),'')
    X.set_bounds({[left]=true},1,7);reaper.SetActiveTake(alternative)
    while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield()end
    test(X.last_error and reaper.GetMediaSourceFileName(reaper.GetMediaItemTake_Source(ta),'')==previous_source,'Switching edited active take rejects stale render without source changes')
    X.last_error=nil;reaper.SetActiveTake(ta)
    test(X.state(a).plan.events[left]['end']==6,'Rejected boundary change preserves previous review')
    local extra_a=add(a,96000,98400,0);X.set_mode({[extra_a]=true},'silence')
    local extra_b=add(b,72000,74400,0);X.set_mode({[extra_b]=true},'silence')
    reaper.SetMediaItemSelected(a,true)
    local before_a,before_b=item_chunk(a),item_chunk(b)
    local real_set=reaper.SetMediaItemTake_Source
    X.begin('render',{a,b})
    reaper.SetMediaItemTake_Source=function(take,media)if take==tb then return false end;return real_set(take,media)end
    while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield()end
    reaper.SetMediaItemTake_Source=real_set
    test(X.last_error and item_chunk(a)==before_a and item_chunk(b)==before_b,'Second-item attachment failure rolls back both items atomically')
    X.last_error=nil;X.begin('render',{a,b});wait()
    test(X.state(a).plan.events[extra_a].bcs_applied and X.state(b).plan.events[extra_b].bcs_applied,'Retry applies both pending items once')
    X.undo(false);test(item_chunk(a)==before_a and item_chunk(b)==before_b,'One multi-item Undo restores both previous items')
    X.undo(true);test(X.state(a).plan.events[extra_a].bcs_applied and X.state(b).plan.events[extra_b].bcs_applied,'One multi-item Redo restores both edits')
    local final_a,final_b=item_chunk(a),item_chunk(b)
    local undo_before=reaper.Undo_CanUndo2(0)
    X.begin('render',{a,b})
    test(not X.busy and item_chunk(a)==final_a and item_chunk(b)==final_b and reaper.Undo_CanUndo2(0)==undo_before,'Fully applied batch is a no-op with no extra Undo or render')
    local final_state=X.state(b)
    outputs[#outputs+1]={source=X.media_path(final_state.prepared_rel),output=final_state.result.path}
    reaper.Main_SaveProjectEx(0,root..'/Professional.rpp',8)
    reaper.Main_openProject('noprompt:'..root..'/Professional.rpp')
    test(reaper.CountTracks(0)==2 and reaper.CountTakes(reaper.GetMediaItem(0,0))==2,'Reopen preserves exact original item/take counts')
    return {status='passed',professional=true,checks=checks,heartbeat=heartbeat,outputs=outputs,source=source,scope='Functional real-copy checks, not perceptual ratings'}
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
