-- SPDX-License-Identifier: MPL-2.0
-- Real REAPER integration, exclusively in run_isolated.ps1's sacrificial profile.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local package=BCS_TEST_PACKAGE_DIR or (here..'/..')
if BCS_TEST_PACKAGE_DIR then reaper.DeleteExtState('BreathClickStudio','engine',false) end
local X=dofile(package..'/bc_studio_bridge.lua')
local J=X.J
assert(BCS_TEST_ROOT and reaper.CountTracks(0)==0 and reaper.CountMediaItems(0)==0,'Fresh isolated project required')
local root=BCS_TEST_ROOT:gsub('\\','/')
local tests,heartbeat=J.array(),0
local outputs=J.array()
local function test(value,name)
    assert(value,name); tests[#tests+1]=name
end
local function near(a,b,eps) return math.abs(a-b)<(eps or 1e-8) end
local function wait()
    while X.busy do X.tick(); heartbeat=heartbeat+1; coroutine.yield() end
    assert(not X.last_error,X.last_error)
end
local function selected(items)
    reaper.SelectAllMediaItems(0,false)
    for _,item in ipairs(items) do reaper.SetMediaItemSelected(item,true) end
end
local function fixture(path,seconds)
    local rate=48000; local frames=math.floor(rate*seconds); local parts={}
    math.randomseed(142)
    for i=0,frames-1 do
        local t=i/rate; local sample=.08*math.sin(2*math.pi*220*t)+.02*math.sin(2*math.pi*440*t)
        if (t>1.8 and t<2.2) or (t>4.1 and t<4.5) then sample=.035*(math.random()*2-1) end
        parts[#parts+1]=string.pack('<f',sample)
    end
    local data=table.concat(parts)
    local f=assert(io.open(path,'wb'))
    f:write('RIFF',string.pack('<I4',36+#data),'WAVEfmt ',string.pack('<I4I2I2I4I4I2I2',16,3,1,rate,rate*4,4,32),'data',string.pack('<I4',#data),data)
    f:close()
end
local function item(track,source,pos,length,offset)
    local it=reaper.AddMediaItemToTrack(track); local take=reaper.AddTakeToMediaItem(it)
    assert(reaper.SetMediaItemTake_Source(take,assert(reaper.PCM_Source_CreateFromFile(source))))
    reaper.SetMediaItemInfo_Value(it,'D_POSITION',pos); reaper.SetMediaItemInfo_Value(it,'D_LENGTH',length)
    reaper.SetMediaItemInfo_Value(it,'D_FADEINLEN',0); reaper.SetMediaItemInfo_Value(it,'D_FADEOUTLEN',0)
    reaper.SetMediaItemTakeInfo_Value(take,'D_STARTOFFS',offset)
    reaper.SetMediaItemTakeInfo_Value(take,'D_PLAYRATE',1)
    reaper.GetSetMediaItemTakeInfo_String(take,'P_NAME','Voce prova',true)
    reaper.SetActiveTake(take); return it,take
end
local function run()
    test(reaper.GetResourcePath():gsub('\\','/')==BCS_TEST_RESOURCE,'One existing portable and isolated profile')
    if BCS_TEST_PACKAGE_DIR then test(X.engine():sub(1,#package)==package,'Independent installed backend; no standalone/development engine') end
    local source=root..'/source.wav'; fixture(source,10)
    reaper.InsertTrackAtIndex(0,false); local voice=reaper.GetTrack(0,0)
    reaper.InsertTrackAtIndex(1,false); local backing=reaper.GetTrack(0,1)
    local a,ta=item(voice,source,10,8,.75)
    local b,tb=item(voice,source,25,3,.5)
    local cue,tc=item(backing,source,10,9,0)
    local _,cue_chunk=reaper.GetItemStateChunk(cue,'',false)
    local fx=reaper.TakeFX_AddByName(ta,'JS: BreathClickTestGain',1)
    local tfx=reaper.TrackFX_AddByName(voice,'JS: BreathClickTestGain',false,1)
    test(fx>=0 and tfx>=0,'Real JSFX loaded on take and track, no system VST scan')
    reaper.TakeFX_SetParam(ta,fx,0,-9); reaper.TrackFX_SetParam(voice,tfx,0,-3)
    local fx_guid=reaper.TakeFX_GetFXGUID(ta,fx); local track_fx_guid=reaper.TrackFX_GetFXGUID(voice,tfx)
    local owner=select(2,reaper.GetSetMediaItemTakeInfo_String(ta,'GUID','',false))
    reaper.SetMediaItemTakeInfo_Value(ta,'D_PAN',.23)
    reaper.SetTakeMarker(ta,-1,'Cue voce',.75+7,0)
    reaper.SetTakeMarker(ta,-1,'Cue nel respiro',.75+3.75,0)
    reaper.SetTakeMarker(ta,-1,'Cue fuori trim',.1,0)
    reaper.AddProjectMarker2(0,false,16,0,'Cue backing',101,0)
    reaper.Main_SaveProjectEx(0,root..'/BreathClick diretto.rpp',8)
    X.settings.detector='fusion'; X.settings.fade_ms=0
    selected{a}; X.begin('analyze',{a}); wait()
    local st=X.state(a)
    test(st.schema==2 and st.frames==384000,'Prepared immutable segment includes offset sample-exactly')
    test(st.plan.ai_inference_frames>0,'Fusion inference actually runs')
    local prepared=st.prepared_rel
    local all={};for i in ipairs(st.plan.events) do all[i]=true end
    X.include(all,false)
    local ids,indices={},{}
    for _,span in ipairs{{1,1.5},{3.5,4},{5.75,6.25}} do
        reaper.GetSet_LoopTimeRange2(0,true,false,10+span[1],10+span[2],false); X.add_manual()
        st=X.state(a); indices[#indices+1]=#st.plan.events; ids[#ids+1]=st.plan.events[#st.plan.events].bcs_id
    end
    for i=0,reaper.GetNumTakeMarkers(ta)-1 do local _,n=reaper.GetTakeMarker(ta,i); test(#n<32 and not n:find(owner,1,true),'Short readable take marker labels') end
    local function invariants()
        local _,chunk=reaper.GetItemStateChunk(cue,'',false)
        test(chunk==cue_chunk,'Backing item and source unchanged')
        test(reaper.CountTracks(0)==2 and reaper.CountTakes(a)==1,'No new tracks or takes are created')
        test(reaper.GetActiveTake(a)==ta,'Editing keeps the original take identity')
        test(reaper.TakeFX_GetFXGUID(ta,fx)==fx_guid and near(reaper.TakeFX_GetParam(ta,fx,0),-9),'Existing take FX identity and parameter preserved')
        test(reaper.TrackFX_GetFXGUID(voice,tfx)==track_fx_guid and near(reaper.TrackFX_GetParam(voice,tfx,0),-3),'Track FX unchanged')
        test(near(reaper.GetMediaItemInfo_Value(b,'D_POSITION'),25),'Other voice item retains its item boundary and position')
        local found=false
        for i=0,select(2,reaper.CountProjectMarkers(0))+select(3,reaper.CountProjectMarkers(0))-1 do local _,_,p,_,n=reaper.EnumProjectMarkers3(0,i);if n=='Cue backing' then found=near(p,16) end end
        test(found,'Other-track project cue remains fixed')
        local stable=false
        for _,m in ipairs(X.state(a).user_markers) do if m.name=='Cue nel respiro' then stable=m.frame==180000 end end
        test(stable,'A take cue inside a removed span retains its original anchor across cumulative renders')
    end
    for k,index in ipairs(indices) do
        X.apply_selected({[index]=true},'halve'); wait()
        st=X.state(a)
        test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),8-k*.25),'Successive halve '..k..' has cumulative sample-exact duration')
        local count=0;for _,e in ipairs(st.plan.events) do if e.bcs_applied then count=count+1 end end
        test(count==k,'Only selected breaths are applied, proposed events remain pending')
        test(st.result.map.applied_events==k,'Full immutable-source render applies every selected event exactly once')
        test(st.prepared_rel==prepared,'Immutable prepared audio reused on every render')
        outputs[#outputs+1]={mode='halve_'..k,path=st.result.path,report=st.result.map}
        if k<3 then local later=st.plan.events[indices[k+1]];local p=X.event_interval(X.context(a),later);test(near(p,({3.5,5.75})[k]-k*.25),'Later breath follows cumulative TimeMap '..k) end
        local mapped=false
        for i=0,reaper.GetNumTakeMarkers(ta)-1 do local p,n=reaper.GetTakeMarker(ta,i);if n=='Cue voce' then mapped=near(p,7-k*.25) end end
        test(mapped,'User take cue follows cumulative sample map '..k)
        invariants()
    end
    test(not pcall(X.apply_selected,{[indices[3]]=true},'halve'),'Identical application is refused instead of halving twice')
    X.undo(false);test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),7.5),'Undo restores second cumulative render')
    X.undo(true);test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),7.25),'Redo restores third cumulative render')
    X.apply_selected({[indices[2]]=true},'cut');wait();st=X.state(a)
    test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),7),'Changing halve to cut replaces the original operation, no double edit')
    outputs[#outputs+1]={mode='replace_cut',path=st.result.path,report=st.result.map}
    local third=X.event_interval(X.context(a),st.plan.events[indices[3]])
    test(near(third,5),'Third breath reflects .25 + .50 seconds already removed')
    X.set_timing('sync');wait();st=X.state(a)
    test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),8),'Maintain-sync restores original duration with pauses')
    local second=X.event_interval(X.context(a),st.plan.events[indices[2]])
    test(near(second,3.5),'Maintain-sync keeps later breath at its original cue')
    outputs[#outputs+1]={mode='sync',path=st.result.path,report=st.result.map}
    X.compare(false);test(near(reaper.GetMediaItemTakeInfo_Value(ta,'D_STARTOFFS'),.75) and reaper.CountTakes(a)==1,'Original A/B restores source on the same take')
    local outside=false
    for i=0,reaper.GetNumTakeMarkers(ta)-1 do local p,n=reaper.GetTakeMarker(ta,i);if n=='Cue fuori trim' then outside=near(p,.1) end end
    test(outside,'Original A/B restores take markers outside the initial analyzed offset')
    X.compare(true);test(reaper.CountTakes(a)==1 and X.context(a).processed,'Processed A/B uses same take and same FX')
    local hidden=true
    for i=0,reaper.GetNumTakeMarkers(ta)-1 do local _,n=reaper.GetTakeMarker(ta,i);if n=='Cue fuori trim' then hidden=false end end
    test(hidden,'Out-of-segment cue is preserved as metadata rather than moved visibly to frame zero')
    X.set_timing('compact');wait()
    X.label({[indices[1]]=true},'breath_confirmed')
    test(X.state(a).plan.events[indices[1]].training_eligible,'Explicit human confirmation is a verified positive')
    X.label({[indices[2]]=true},'false_positive');wait();st=X.state(a)
    test(st.plan.events[indices[2]].learning_label=='false_positive' and st.plan.events[indices[2]].training_eligible,'False positive stored independently of editorial approval')
    test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),7.5),'False-positive command restores an already cut breath')
    X.undo(false);test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),7),'One Undo restores label and audio together')
    X.undo(true)
    X.label({[indices[3]]=true},'editorial_exclusion');wait();st=X.state(a)
    test(not st.plan.events[indices[3]].training_eligible and st.plan.events[indices[3]].learning_label=='editorial_exclusion','Editorial exclusion never becomes a negative training example')
    test(near(reaper.GetMediaItemInfo_Value(a,'D_LENGTH'),7.75),'Editorial exclusion restores only its own previous edit')
    reaper.GetSet_LoopTimeRange2(0,true,false,16.4,16.6,false);X.add_manual('missed_breath')
    st=X.state(a);local missed=#st.plan.events
    test(st.plan.events[missed].learning_label=='missed_breath' and st.plan.events[missed].training_eligible,'Explicit missing breath can be added from compacted time selection')
    test(st.plan.events[missed].start==round_for_test(6.65*48000),'Missing-breath coordinates map back to original samples')
    reaper.GetSet_LoopTimeRange2(0,true,false,16.42,16.62,false);X.bounds_from_selection({[missed]=true})
    st=X.state(a);test(st.plan.events[missed].start==round_for_test(6.67*48000),'REAPER time selection directly corrects event boundaries')
    X.export_or_train(false);wait()
    local exported=X.entries[1].result.output;local session=assert(J.read(exported))
    test(#session.regions==3,'Standalone session exports only three explicit learning examples')
    test(session.source_sha256==st.source_sha256,'Learning export refers to full original recording, not rendered cache')
    X.export_or_train(true)
    while X.busy do X.tick();coroutine.yield() end
    test(X.last_error~=nil,'Personal calibrator refuses one recording as independent validation')
    X.last_error=nil
    local legacy_state=X.state(a);local legacy_template=J.decode(J.encode(legacy_state))
    reaper.Main_SaveProjectEx(0,root..'/BreathClick editingGUI.rpp',0)
    local ctx=X.context(a);st=ctx.state
    local start=X.map_frame(st.result.map,round_for_test(1.8*48000));local finish=X.map_frame(st.result.map,round_for_test(7.5*48000))
    reaper.SetMediaItemTakeInfo_Value(ta,'D_STARTOFFS',start/48000);reaper.SetMediaItemInfo_Value(a,'D_LENGTH',(finish-start)/48000)
    test(not pcall(X.apply_selected,{[indices[1]]=true},'cut'),'An operation crossing item trim is blocked before any source change')
    X.apply_selected({[missed]=true},'silence');wait()
    local first,last=X.view(X.context(a));test(first==86400 and last==360000,'Trim remains anchored to original frames after a later render')
    local right=reaper.SplitMediaItem(a,reaper.GetMediaItemInfo_Value(a,'D_POSITION')+1)
    test(right~=nil,'Real native split creates a second bounded view of the same original')
    selected{right};local rt=reaper.GetActiveTake(right)
    local rs=X.state(right)
    test(rs.take_guid~=owner,'Split take identity is independent, original sample anchors retained')
    local _,left_chunk=reaper.GetItemStateChunk(a,'',false)
    local rp=reaper.GetMediaItemInfo_Value(right,'D_POSITION')
    reaper.GetSet_LoopTimeRange2(0,true,false,rp+.2,rp+.4,false);X.add_manual();rs=X.state(right)
    local length_before=reaper.GetMediaItemInfo_Value(right,'D_LENGTH')
    X.apply_selected({[#rs.plan.events]=true},'cut');wait()
    test(near(reaper.GetMediaItemInfo_Value(right,'D_LENGTH'),length_before-.2),'Editing a split/trimmed item changes only its own visible interval')
    local _,left_after=reaper.GetItemStateChunk(a,'',false);test(left_after==left_chunk,'Left split item is unchanged by editing right split')
    selected{b};reaper.SetMediaItemTakeInfo_Value(tb,'D_PLAYRATE',1.25);reaper.SetMediaItemTakeInfo_Value(tb,'B_PPITCH',1);reaper.SetMediaItemTakeInfo_Value(tb,'D_PITCH',2)
    X.begin('analyze',{b});wait();local bs=X.state(b)
    test(bs.frames==180000,'Non-unit rate slices duration times rate from immutable source')
    local none={};for i in ipairs(bs.plan.events) do none[i]=true end;if next(none) then X.include(none,false) end
    reaper.GetSet_LoopTimeRange2(0,true,false,25.4,25.8,false);X.add_manual();bs=X.state(b)
    local ri=#bs.plan.events
    X.apply_selected({[ri]=true},'halve');wait()
    test(near(reaper.GetMediaItemInfo_Value(b,'D_LENGTH'),2.8),'Playback rate 1.25 converts removed source samples to correct project duration')
    test(near(reaper.GetMediaItemTakeInfo_Value(tb,'D_PLAYRATE'),1.25) and near(reaper.GetMediaItemTakeInfo_Value(tb,'D_PITCH'),2),'Rate, pitch and native resampling remain on the existing take')
    X.compare(false);test(near(reaper.GetMediaItemInfo_Value(b,'D_LENGTH'),3) and near(reaper.GetMediaItemTakeInfo_Value(tb,'D_STARTOFFS'),.5),'A/B restores source offset at non-unit rate')
    X.compare(true)
    local real_set=reaper.SetMediaItemTake_Source
    local _,before=reaper.GetItemStateChunk(b,'',false)
    X.apply_selected({[ri]=true},'cut');reaper.SetMediaItemTake_Source=function() return false end
    while X.busy do X.tick();coroutine.yield() end;reaper.SetMediaItemTake_Source=real_set
    local _,after=reaper.GetItemStateChunk(b,'',false)
    test(X.last_error~=nil and before==after,'Injected source attachment failure restores full item, FX and metadata')
    X.last_error=nil
    tb=reaper.GetMediaItemTakeByGUID(0,bs.take_guid)
    selected{b};X.apply_selected({[ri]=true},'cut');reaper.SetMediaItemInfo_Value(b,'D_POSITION',25.1)
    while X.busy do X.tick();coroutine.yield() end
    test(X.last_error~=nil,'Moving an item during rendering rejects the stale result')
    X.last_error=nil;reaper.SetMediaItemInfo_Value(b,'D_POSITION',25)
    X.apply_selected({[ri]=true},'cut');X.cancel()
    while X.busy do X.tick();coroutine.yield() end
    test(not X.last_error,'Cancellation leaves the previously applied source intact')
    reaper.GetSet_LoopTimeRange2(0,true,false,25.1,25.2,false);X.add_manual()
    bs=X.state(b);local batch_index=#bs.plan.events
    X.begin('render',{b});wait();bs=X.state(b)
    test(bs.plan.events[batch_index].bcs_applied and bs.result.map.applied_events==2,'Batch remains available and combines pending event with one existing edit once')
    test(reaper.CountTakes(b)==1 and near(reaper.GetMediaItemInfo_Value(b,'D_LENGTH'),2.8),'Batch preserves take count and cumulative duration')
    selected{a};X.begin('analyze',{a});wait();st=X.state(a)
    local preserved=0;for _,e in ipairs(st.plan.events) do if e.learning_label=='breath_confirmed' or e.learning_label=='false_positive' or e.learning_label=='missed_breath' then preserved=preserved+1 end end
    test(preserved==3 and st.prepared_rel==prepared,'Reanalysis preserves applied decisions, labels and cached original after trim/split')
    reaper.Main_SaveProjectEx(0,root..'/BreathClick diretto.rpp',8)
    reaper.Main_openProject('noprompt:'..root..'/BreathClick diretto.rpp')
    ta=reaper.GetMediaItemTakeByGUID(0,owner);a=reaper.GetMediaItemTake_Item(ta);selected{a}
    st=X.state(a)
    local confirmed=false
    for _,e in ipairs(st.plan.events) do if e.bcs_id==ids[1] and e.learning_label=='breath_confirmed' then confirmed=true end end
    test(st.schema==2 and st.result and confirmed,'Save/reopen persists cumulative map and explicit learning labels')
    test(reaper.CountTakes(a)==1 and reaper.TakeFX_GetCount(ta)==1 and reaper.TrackFX_GetCount(reaper.GetMediaItem_Track(a))==1,'Save/reopen keeps existing FX and no duplicate takes')
    local legacy,original_legacy=item(reaper.GetMediaItem_Track(a),source,40,8,.75)
    local prior=reaper.AddTakeToMediaItem(legacy)
    assert(reaper.SetMediaItemTake_Source(prior,assert(reaper.PCM_Source_CreateFromFile(legacy_template.result.path))))
    reaper.SetMediaItemTakeInfo_Value(prior,'D_STARTOFFS',0);reaper.SetMediaItemInfo_Value(legacy,'D_LENGTH',7.75)
    local original_id=select(2,reaper.GetSetMediaItemTakeInfo_String(original_legacy,'GUID','',false))
    local prior_id=select(2,reaper.GetSetMediaItemTakeInfo_String(prior,'GUID','',false))
    local old_plan=J.decode(J.encode(legacy_template.plan))
    for _,e in ipairs(old_plan.events) do e.approved=e.bcs_applied==true;e.enabled=e.approved end
    legacy_template.last_result={take_guid=prior_id,length=7.75,signature=J.encode(old_plan)}
    local old_output=legacy_template.result.path
    legacy_template.schema=1;legacy_template.take_guid=original_id;legacy_template.result=nil
    legacy_template.source_path=nil;legacy_template.signature=nil;legacy_template.view=nil
    assert(reaper.GetSetMediaItemTakeInfo_String(original_legacy,'P_EXT:BreathClickStudio',J.encode(legacy_template),true))
    assert(reaper.GetSetMediaItemTakeInfo_String(prior,'P_EXT:BreathClickStudioResult',J.encode({owner_guid=original_id,report_rel=old_output..'.edit.json'}),true))
    for _,e in ipairs(legacy_template.plan.events) do if e.approved then
        reaper.SetTakeMarker(original_legacy,-1,'BC['..e.bcs_id..']:A Respiro',.75+e.start/48000,0)
        reaper.SetTakeMarker(original_legacy,-1,'BC['..e.bcs_id..']:B Respiro',.75+e['end']/48000,0)
    end end
    reaper.SetActiveTake(prior);selected{legacy}
    local migrated=X.state(legacy);local pending
    for i,e in ipairs(migrated.plan.events) do if e.learning_label=='missed_breath' then pending=i;test(not e.bcs_applied,'Migration uses actual old render signature, pending proposals stay pending') end end
    X.apply_selected({[pending]=true},'silence');wait()
    test(X.state(legacy).schema==2 and reaper.CountTakes(legacy)==2 and reaper.GetActiveTake(legacy)==original_legacy,'Old preview migrates to direct source editing without another take or track')
    _G.BCS_PANEL_NO_RUN=true;local UI=dofile(package..'/BreathClickStudio.lua');_G.BCS_PANEL_NO_RUN=nil
    local rate_item=reaper.GetMediaItemTake_Item(reaper.GetMediaItemTakeByGUID(0,bs.take_guid));selected{rate_item}
    gfx.init('NL Breath Click Studio - REAPER preview',1040,760,0);UI.draw()
    reaper.SetEditCurPos(25.5,false,false);UI.draw()
    test(UI.selected[ri],'Native REAPER cursor inside a breath selects the corresponding panel event')
    reaper.GetSet_LoopTimeRange2(0,true,false,25.42,25.55,false);UI.draw()
    test(UI.selected[ri],'Native REAPER time selection selects the corresponding breath')
    test(UI.wave_handles~=nil,'Actual panel renders original waveform and draggable boundary handles')
    UI.action('Informazioni');UI.draw()
    test(UI.about_open and UI.identity.name=='NL Breath Click Studio' and UI.identity.author=='Nicola Lanci Voice Over','Native panel renders Information with current product identity')
    UI.action('Chiudi info');UI.draw()
    test(not UI.about_open and UI.selected[ri],'Closing Information preserves the selected breath in the native panel')
    UI.wave.close();gfx.quit()
    return {status='passed',version=reaper.GetAppVersion(),resource=reaper.GetResourcePath(),checks=tests,heartbeat=heartbeat,
        outputs=outputs,source=source,prepared=X.media_path(st.prepared_rel),project=root..'/BreathClick diretto.rpp',gui_project=root..'/BreathClick editingGUI.rpp',learning_session=exported}
end
function round_for_test(x) return math.floor(x+.5) end
local task=coroutine.create(run)
local function frame()
    local ok,result=coroutine.resume(task)
    if ok and coroutine.status(task)~='dead' then reaper.defer(frame);return end
    if not ok then result={status='failed',error=tostring(result),traceback=debug.traceback(task),checks=tests} end
    reaper.Main_SaveProjectEx(0,root..'/BreathClick diretto.rpp',8)
    assert(J.write(root..'/result.json',result))
    if not BCS_KEEP_OPEN then reaper.defer(function() reaper.Main_OnCommand(40004,0) end) end
end
frame()
