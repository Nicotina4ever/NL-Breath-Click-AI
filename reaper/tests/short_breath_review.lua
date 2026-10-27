-- SPDX-License-Identifier: MPL-2.0
-- Optional real-recording COPY test. Proposals are not human training labels.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local X=dofile(here..'/../bc_studio_bridge.lua');local J=X.J
local root=assert(BCS_TEST_ROOT):gsub('\\','/');local source=assert(BCS_TEST_AUDIO)
local checks=J.array();local heartbeat=0
local function test(v,name)assert(v,name);checks[#checks+1]=name end
local function wait()while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield()end;assert(not X.last_error,X.last_error)end
local function run()
    test(reaper.CountTracks(0)==0,'Fresh isolated test tab')
    reaper.InsertTrackAtIndex(0,false)
    local item=reaper.AddMediaItemToTrack(reaper.GetTrack(0,0));local take=reaper.AddTakeToMediaItem(item)
    local media=assert(reaper.PCM_Source_CreateFromFile(source))
    assert(reaper.SetMediaItemTake_Source(take,media))
    reaper.SetMediaItemInfo_Value(item,'D_LENGTH',reaper.GetMediaSourceLength(media))
    reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0);reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
    reaper.SetMediaItemSelected(item,true)
    reaper.Main_SaveProjectEx(0,root..'/Short breath review.rpp',8)
    X.settings.detector='fusion';X.settings.detect_breath=true;X.settings.detect_clicks=false;X.settings.fade_ms=0
    X.begin('analyze',{item});wait();local st=X.state(item);local target
    for i,e in ipairs(st.plan.events) do if e.classification=='ai_short_review' then
        test(not e.approved and not e.training_eligible and X.event_status(e)=='DA RIVEDERE','Short AI proposal stays unapproved and outside training')
        if e.start<=300876 and e['end']>300876 then target=i end
    end end
    test(target~=nil,'Actual recording breath at 6.268 s is proposed in native REAPER')
    test(reaper.GetNumTakeMarkers(take)==2*#st.plan.events,'Short review proposal has visible native boundaries')
    _G.BCS_PANEL_NO_RUN=true;local UI=dofile(here..'/../BreathClickStudio.lua');_G.BCS_PANEL_NO_RUN=nil
    gfx.init('NL Breath Click Studio - revisione respiri brevi',1040,760,0)
    UI.draw() -- initialize the panel's current-item context before selection
    X.select_event(target,false);UI.selected={[target]=true};UI.draw()
    test(UI.wave_handles~=nil,'Real panel draws waveform and editable boundaries for the short breath')
    UI.wave.close();gfx.quit()
    local all={};for i in ipairs(st.plan.events) do all[i]=true end;X.include(all,false)
    X.apply_selected({[target]=true},'attenuate');wait();st=X.state(item)
    test(st.plan.events[target].bcs_applied and not st.plan.events[target].training_eligible,'Editorial application does not become a verified learning example')
    test(st.result.map.applied_events==1 and st.result.map.original_frames==st.result.map.edited_frames,'Only selected breath applied, duration preserved')
    test(reaper.CountTracks(0)==1 and reaper.CountTakes(item)==1,'No duplicate track or take')
    return {status='passed',checks=checks,heartbeat=heartbeat,professional=true,source=source,
        outputs=J.array{{source=X.media_path(st.prepared_rel),output=st.result.path}}}
end
local task=coroutine.create(run)
local function frame()
    local ok,result=coroutine.resume(task)
    if ok and coroutine.status(task)~='dead' then reaper.defer(frame);return end
    if not ok then result={status='failed',error=tostring(result),traceback=debug.traceback(task),checks=checks}end
    assert(J.write(root..'/result.json',result))
end
frame()
