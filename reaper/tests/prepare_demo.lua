-- SPDX-License-Identifier: MPL-2.0
-- A small one-track demo derived only from the validated synthetic GUI fixture.
-- It shares existing media; no audio, model or REAPER executable is copied.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local X=dofile(here..'/../bc_studio_bridge.lua');local J=X.J
assert(BCS_TEST_ROOT and BCS_TEST_PROJECT and reaper.GetResourcePath():gsub('\\','/')==BCS_TEST_RESOURCE)
assert(BCS_TEST_PROJECT:gsub('\\','/'):match('/build/reaper_sandbox/runs/direct_%d+/BreathClick editingGUI%.rpp$'),'Only a synthetic integration fixture is accepted')
local target=BCS_TEST_PROJECT:gsub('\\','/'):gsub('BreathClick editingGUI%.rpp$','BreathClick editing diretto.rpp')
assert(not reaper.file_exists(target),'Existing demo is never overwritten')
reaper.Main_openProject('noprompt:'..BCS_TEST_PROJECT)
local item=reaper.GetMediaItem(0,0);assert(item)
reaper.SelectAllMediaItems(0,false);reaper.SetMediaItemSelected(item,true)
local ctx=X.context(item);assert(ctx.state and ctx.state.schema==2)
if ctx.processed then X.compare(false) end
X.review(function(st)
    st.result=nil;st.signature=nil;st.last_result=nil;st.timing='compact'
    local demo=J.array();local starts={[48000]=true,[168000]=true,[276000]=true}
    for _,e in ipairs(st.plan.events) do
        if starts[e.start] and e['end']==e.start+24000 and e.bcs_manual then demo[#demo+1]=e end
    end
    assert(#demo==3,'Expected the three original half-second synthetic breaths')
    table.sort(demo,function(a,b) return a.start<b.start end)
    st.plan.events=demo
    for i,e in ipairs(st.plan.events) do
        e.bcs_short=i
        e.bcs_applied=false;e.approved=true;e.enabled=true;e.mode='attenuate'
        e.learning_label='';e.training_eligible=false;e.decision_source='review_harness'
        e.manual_operation=e.source=='manual';e.event_reduction_db=nil
    end
end)
local voice=reaper.GetMediaItem_Track(item)
-- These are only generated control items/tracks, in a new demo copy.
for i=reaper.CountTracks(0)-1,0,-1 do local track=reaper.GetTrack(0,i);if track~=voice then reaper.DeleteTrack(track) end end
for i=reaper.CountTrackMediaItems(voice)-1,0,-1 do local other=reaper.GetTrackMediaItem(voice,i);if other~=item then assert(reaper.DeleteTrackMediaItem(voice,other)) end end
assert(reaper.GetSetMediaTrackInfo_String(voice,'P_NAME','Prova sintetica - tre respiri',true))
local state=X.state(item);local selected
for i,e in ipairs(state.plan.events) do if e.bcs_manual then selected=i;break end end
X.select_event(assert(selected),false)
reaper.Main_SaveProjectEx(0,target,8)
assert(reaper.file_exists(target) and reaper.CountTracks(0)==1 and reaper.CountTakes(item)==1 and reaper.CountMediaItems(0)==1)
assert(J.write(BCS_TEST_ROOT..'/result.json',{status='passed',project=target,tracks=1,takes=1,items=1,training_examples=0,source_sha256=state.source_sha256}))
reaper.defer(function() reaper.Main_OnCommand(40004,0) end)
