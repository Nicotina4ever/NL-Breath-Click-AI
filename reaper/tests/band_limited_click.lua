-- SPDX-License-Identifier: MPL-2.0
-- Optional real-recording COPY: native markers, shared detector, actual panel.
-- No human labels or personal profiles are fabricated by this test.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local X=dofile(here..'/../bc_studio_bridge.lua');local J=X.J
local root=assert(BCS_TEST_ROOT):gsub('\\','/');local source=assert(BCS_TEST_AUDIO)
local checks=J.array();local heartbeat=0
local function test(v,name)assert(v,name);checks[#checks+1]=name end
local function wait()while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield()end;assert(not X.last_error,X.last_error)end
local function run()
    test(reaper.CountTracks(0)==0,'Fresh isolated project tab')
    reaper.InsertTrackAtIndex(0,false)
    local item=reaper.AddMediaItemToTrack(reaper.GetTrack(0,0));local take=reaper.AddTakeToMediaItem(item)
    local media=assert(reaper.PCM_Source_CreateFromFile(source));assert(reaper.SetMediaItemTake_Source(take,media))
    reaper.SetMediaItemInfo_Value(item,'D_LENGTH',reaper.GetMediaSourceLength(media))
    reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0);reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
    reaper.SetMediaItemSelected(item,true);reaper.Main_SaveProjectEx(0,root..'/Mouth review.rpp',8)
    X.settings.detector='fusion';X.settings.detect_breath=true;X.settings.detect_clicks=true
    X.settings.profile_active=false;X.settings.click_profile_active=false
    X.begin('analyze',{item});wait();local st=X.state(item);local target
    for i,e in ipairs(st.plan.events) do if e.classification=='band_limited_click' then
        test(not e.approved and not e.training_eligible,'New mouth hypothesis stays in review and outside training')
        if e.start<=50256 and e['end']>50256 then target=i end
    end end
    test(target~=nil,'Real missed click at 1.047 s is proposed by the native REAPER workflow')
    local quiet
    for i,e in ipairs(st.plan.events) do
        if e.classification=='isolated_click_review' and e.start/48000>20.7 and e.start/48000<20.75 then quiet=i end
    end
    test(quiet and not st.plan.events[quiet].approved,'Quiet broader-band click is visible as review-only in the native host')
    _G.BCS_PANEL_NO_RUN=true;local UI=dofile(here..'/../BreathClickStudio.lua');_G.BCS_PANEL_NO_RUN=nil
    local settings=X.settings;X=UI.bridge;X.settings=settings -- wait on the panel's actual worker
    gfx.init('NL Breath Click Studio - schiocchi morbidi',1040,760,0);UI.draw()
    UI.action('Filtro');UI.draw();UI.action('Filtro');UI.draw();UI.action('Tutte')
    local selected=0
    local onlyClicks=true
    for i in pairs(UI.selected) do selected=selected+1;onlyClicks=onlyClicks and st.plan.events[i].kind==1 end
    test(onlyClicks,'Filtered select-all never selects a hidden breath')
    test(selected>0,'Click filter has selectable native events')
    X.select_event(target,false);UI.selected={[target]=true};UI.draw()
    test(UI.selected[target] and next(UI.selected)==target,'Arrange synchronization preserves the exact selected click over a longer overlapping breath')
    test(UI.wave_handles~=nil,'Actual panel draws editable boundaries for the new mouth proposal')
    test(UI.wave_handles.b.x-UI.wave_handles.a.x>100,'Short mouth event is automatically enlarged enough to edit both edges')
    local beforeEnd=X.state(item).plan.events[target]['end']
    local handles=UI.wave_handles;local mx,my,cap=gfx.mouse_x,gfx.mouse_y,gfx.mouse_cap
    local context=X.context(item);local current=context.state.plan.events
    gfx.mouse_x=handles.b.x;gfx.mouse_y=handles.b.y;gfx.mouse_cap=1
    UI.wave.draw(context,current,true,283)
    gfx.mouse_x=handles.b.x+12;UI.wave.draw(context,current,false,283)
    gfx.mouse_cap=0;UI.wave.draw(context,current,false,283)
    gfx.mouse_x,gfx.mouse_y,gfx.mouse_cap=mx,my,cap
    test(X.state(item).plan.events[target]['end']>beforeEnd,'Dragging the end handle updates the real native event end, not its start')
    X.undo(false)
    test(X.state(item).plan.events[target]['end']==beforeEnd,'One Undo restores the waveform end-handle edit')
    local odd=X.state(item).plan.events[target]
    X.set_bounds({[target]=true},odd.start,odd['end']+1)
    context=X.context(item);UI.wave.draw(context,context.state.plan.events,false,283)
    test(UI.wave_handles~=nil,'Odd-sample click widths remain drawable at the adaptive zoom')
    X.undo(false)
    UI.filter=4;UI.order_events=nil;UI.draw()
    UI.selected={[target]=true};UI.anchor=target;UI.step(1)
    local following=next(UI.selected)
    test(following and UI.event_visible(X.state(item).plan.events[following]),'Next follows only undecided visible events')
    UI.step(-1);test(UI.selected[target],'Previous returns to the same undecided event')
    UI.filter=5;UI.order_events=nil;UI.draw();UI.action('Tutte')
    local isolated=true
    for i in pairs(UI.selected) do local e=X.state(item).plan.events[i];isolated=isolated and e.kind==1 and e.classification~='speech_overlap' end
    test(isolated and next(UI.selected)~=nil,'Impulse/mouth filter selects no hidden overlapping-voice hypothesis')
    UI.filter=0;UI.order_events=nil
    local original=X.state(item).plan.events[target]
    local newStart,newEnd=original.start+24000,original['end']+24000
    X.set_bounds({[target]=true},newStart,newEnd)
    UI.selected={[target]=true};UI.anchor=target;UI.step(1)
    local ordered=X.state(item).plan.events;local expected
    for i,e in ipairs(ordered) do
        if e.start>newStart and (not expected or e.start<ordered[expected].start) then expected=i end
    end
    test(expected and UI.selected[expected],'Navigation follows current edited positions rather than event insertion order')
    X.undo(false);st=X.state(item)
    test(st.plan.events[target].start==original.start,'Undo restores the visually moved boundary')
    UI.selected={[target]=true};UI.anchor=target;X.select_event(target,false);UI.draw()
    UI.action('Ripara click');wait();st=X.state(item);local e=st.plan.events[target]
    test(e.bcs_outcome=='repaired' or X.event_status(e)=='INTATTO: SICUREZZA' or X.event_status(e)=='RIPARATO','Actual repair outcome is explicit, including a safety refusal')
    test(not e.training_eligible and (e.learning_label or '')=='','Applying a proposal does not manufacture human truth')
    test(st.result.map.original_frames==st.result.map.edited_frames,'Click treatment preserves the complete take duration')
    test(reaper.CountTracks(0)==1 and reaper.CountTakes(item)==1,'No duplicate tracks or takes')
    local output=st.result.path;local prepared=X.media_path(st.prepared_rel);local outcome=X.event_status(e)
    UI.action('Originale');test(not X.context(item).processed,'Original comparison uses the same take')
    UI.action('Elaborato');wait();test(X.context(item).processed,'Processed comparison uses the same take')
    UI.action('Solo rimosso');wait()
    local residual=X.state(item).residual.path
    test(X.context(item).residual,'Removed-signal comparison uses the same take')
    UI.action('Elaborato');wait()
    X.undo(false);X.undo(true)
    test(reaper.CountTracks(0)==1 and reaper.CountTakes(item)==1,'Undo/Redo keeps the same track and take')
    reaper.Main_SaveProjectEx(0,root..'/Mouth review.rpp',8)
    UI.wave.close();gfx.quit()
    return {status='passed',checks=checks,heartbeat=heartbeat,outcome=outcome,source=source,professional=true,
        outputs=J.array{{source=prepared,output=output,residual=residual}}}
end
local task=coroutine.create(run)
local function frame()
    local ok,result=coroutine.resume(task)
    if ok and coroutine.status(task)~='dead' then reaper.defer(frame);return end
    if not ok then result={status='failed',error=tostring(result),traceback=debug.traceback(task),checks=checks}end
    assert(J.write(root..'/result.json',result))
end
frame()
