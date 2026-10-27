-- SPDX-License-Identifier: MPL-2.0
-- Real native marker edits without notifying the adapter. Synthetic input only.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local X=dofile(here..'/../bc_studio_bridge.lua');local J=X.J
local root=assert(BCS_TEST_ROOT):gsub('\\','/')
local source=assert(BCS_TEST_AUDIO):gsub('\\','/')
local checks=J.array();local heartbeat=0
local function test(value,name) assert(value,name);checks[#checks+1]=name end
local function wait() while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield() end;assert(not X.last_error,X.last_error) end
local function run()
    test(reaper.CountTracks(0)==0,'Fresh isolated project')
    reaper.InsertTrackAtIndex(0,false);reaper.InsertTrackAtIndex(1,false)
    local item=reaper.AddMediaItemToTrack(reaper.GetTrack(0,0));local take=reaper.AddTakeToMediaItem(item)
    assert(reaper.SetMediaItemTake_Source(take,assert(reaper.PCM_Source_CreateFromFile(source))))
    reaper.SetMediaItemInfo_Value(item,'D_POSITION',10);reaper.SetMediaItemInfo_Value(item,'D_LENGTH',1.5)
    reaper.SetMediaItemTakeInfo_Value(take,'D_STARTOFFS',.125)
    reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0);reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
    reaper.SetMediaItemSelected(item,true)
    local cue=reaper.AddMediaItemToTrack(reaper.GetTrack(0,1));reaper.SetMediaItemInfo_Value(cue,'D_POSITION',12)
    local _,cue_before=reaper.GetItemStateChunk(cue,'',false)
    reaper.Main_SaveProjectEx(0,root..'/Current regions.rpp',8)
    X.settings.detector='dsp';X.settings.detect_breath=false;X.settings.detect_clicks=true;X.settings.fade_ms=0
    X.begin('analyze',{item});wait()
    local st=X.state(item);local all={};for i in ipairs(st.plan.events) do all[i]=true end
    test(reaper.GetNumTakeMarkers(take)==2*#st.plan.events,'Every proposal has visible native boundaries before treatment')
    if next(all) then X.include(all,false) end
    local function add(a,b)
        reaper.GetSet_LoopTimeRange2(0,true,false,10+a,10+b,false)
        local id=X.add_manual(nil,0)
        for i,e in ipairs(X.state(item).plan.events) do if e.bcs_id==id then return i end end
    end
    local first=add(.5,.52);local ambiguous=add(.8,.82);local last=add(1,1.02)
    X.apply_selected({[first]=true},'silence');wait()
    local old=X.state(item).result.path
    local function marker(index,edge)
        local short=X.state(item).plan.events[index].bcs_short
        for i=0,reaper.GetNumTakeMarkers(take)-1 do
            local p,n,c=reaper.GetTakeMarker(take,i)
            if n:match('^BC'..string.format('%02d',short)..' '..edge) then return i,p,n,c end
        end
        error('Marker not found')
    end
    local m,_,name,color=marker(first,'B')
    reaper.SetTakeMarker(take,m,name,.55,color) -- no BreathClick notification
    X.compare(true);test(X.busy,'Processed comparison detects native marker movement instead of reusing cached audio');wait()
    st=X.state(item)
    test(st.result.path~=old and st.plan.events[first]['end']==26400,'Fresh render uses the current native boundary')
    test(not st.plan.events[first].bcs_render_stale,'Successful render clears the stale flag')
    X.label({[first]=true,[ambiguous]=true},'breath_confirmed') -- synthetic contract labels only
    local border,_,label,shade=marker(first,'B')
    reaper.SetTakeMarker(take,border,label,.56,shade)
    X.export_or_train(false);wait()
    local labels=assert(J.read(X.entries[1].result.output))
    local current_label=false
    for _,r in ipairs(labels.regions) do if r.start==30000 then current_label=r['end']==32880 end end
    test(current_label,'Learning export rereads native boundaries and maps them to the original offset')
    reaper.SetTakeMarker(take,border,label,.55,shade)
    local current=st.result.path
    local missing=marker(ambiguous,'B');reaper.DeleteTakeMarker(take,missing)
    X.export_or_train(false);wait()
    local filtered=assert(J.read(X.entries[1].result.output))
    test(#filtered.regions==1 and X.status:find('1 esempi',1,true),'Ambiguous label is skipped explicitly while other verified labels remain exportable')
    test(not pcall(X.apply_selected,{[ambiguous]=true},'silence') and not X.busy,'Missing selected border cannot apply old coordinates')
    test(X.state(item).result.path==current,'Invalid selected proposal preserves the installed audio')
    X.apply_selected({[last]=true},'silence');wait();st=X.state(item)
    test(st.plan.events[last].bcs_applied and not st.plan.events[ambiguous].bcs_applied,'Ambiguous unapplied event does not block another valid event')
    test(st.plan.events[ambiguous].bcs_review_conflict~=nil,'Only the ambiguous event requests border review')
    X.set_bounds({[ambiguous]=true},38400,39360)
    X.apply_selected({[ambiguous]=true},'silence');wait()
    test(X.state(item).plan.events[ambiguous].bcs_applied,'Explicit border correction resolves the affected event')
    X.undo(false);X.undo(true)
    test(X.state(item).plan.events[ambiguous].bcs_applied,'Undo/Redo preserves current-coordinate rendering')
    reaper.SetMediaItemInfo_Value(item,'D_POSITION',14)
    reaper.SetMediaItemTakeInfo_Value(take,'D_PLAYRATE',1.25)
    reaper.SetMediaItemInfo_Value(item,'D_LENGTH',1.2)
    local a,b=X.event_interval(X.context(item),X.state(item).plan.events[first])
    test(math.abs(a-.4)<1e-8 and math.abs(b-.44)<1e-8,'Native position/rate changes map current source bounds correctly')
    local _,cue_after=reaper.GetItemStateChunk(cue,'',false)
    test(cue_before==cue_after and reaper.CountTracks(0)==2 and reaper.CountTakes(item)==1,'Other track cue and original take count unchanged')
    local output=X.state(item).result.path
    reaper.Main_SaveProjectEx(0,root..'/Current regions.rpp',8)
    reaper.Main_openProject('noprompt:'..root..'/Current regions.rpp')
    test(X.state(reaper.GetMediaItem(0,0)).plan.events[first]['end']==26400,'Save/reopen preserves corrected original-sample boundary')
    return {status='passed',checks=checks,heartbeat=heartbeat,source=source,prepared=X.media_path(st.prepared_rel),output=output}
end
local task=coroutine.create(run)
local function frame()
    local ok,value=coroutine.resume(task)
    if ok and coroutine.status(task)~='dead' then reaper.defer(frame);return end
    if not ok then value={status='failed',error=tostring(value),traceback=debug.traceback(task),checks=checks} end
    assert(J.write(root..'/result.json',value))
end
frame()
