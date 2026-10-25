-- SPDX-License-Identifier: MPL-2.0
-- Real DAW regression with synthetic reference media, never a human training sample.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local X=dofile(here..'/../bc_studio_bridge.lua');local J=X.J
assert(BCS_TEST_ROOT and reaper.CountTracks(0)==0,'Fresh isolated test tab required')
local root=BCS_TEST_ROOT:gsub('\\','/');local checks=J.array();local heartbeat=0
local function test(ok,name)assert(ok,name);checks[#checks+1]=name end
local function wait()while X.busy do X.tick();heartbeat=heartbeat+1;coroutine.yield() end;assert(not X.last_error,X.last_error)end
local function fixture(path,click,uncertain)
    local pcm={};local rate=48000;local random=73
    for i=0,rate*2-1 do
        local x=.08*math.sin(2*math.pi*223*i/rate)+.025*math.sin(2*math.pi*446*i/rate)
        if uncertain then random=(1103515245*random+12345)&0x7fffffff;x=x+.02*(random/0x7fffffff-.5) end
        if click and i==16800 then x=x+.6 end
        pcm[#pcm+1]=string.pack('<f',x)
    end
    local data=table.concat(pcm);local f=assert(io.open(path,'wb'))
    f:write('RIFF',string.pack('<I4',36+#data),'WAVEfmt ',string.pack('<I4I2I2I4I4I2I2',16,3,1,rate,rate*4,4,32),'data',string.pack('<I4',#data),data);f:close()
end
local function run()
    local source=root..'/click.wav';local clean=root..'/clean.wav';fixture(source,true);fixture(clean,false)
    reaper.InsertTrackAtIndex(0,false);local track=reaper.GetTrack(0,0)
    local function new_item(position,file)
        local item=reaper.AddMediaItemToTrack(track);local take=reaper.AddTakeToMediaItem(item)
        assert(reaper.SetMediaItemTake_Source(take,assert(reaper.PCM_Source_CreateFromFile(file or source))))
        reaper.SetMediaItemInfo_Value(item,'D_POSITION',position);reaper.SetMediaItemInfo_Value(item,'D_LENGTH',2)
        reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0);reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
        reaper.SelectAllMediaItems(0,false);reaper.SetMediaItemSelected(item,true);return item,take
    end
    reaper.Main_SaveProjectEx(0,root..'/Click editing.rpp',8)
    local item,take=new_item(0)
    X.settings.detect_breath=false;X.settings.detect_clicks=true;X.settings.detector='fusion'
    X.settings.click_sensitivity=.5;X.settings.fade_ms=0
    X.begin('analyze',{item});wait();local st=X.state(item)
    test(st.plan.ai_inference_frames==0,'Click-only does not invoke breath AI')
    local index
    for i,e in ipairs(st.plan.events) do test(e.kind==1,'Click-only has a distinct category');if e.start<=16800 and e['end']>16800 then index=i end end
    test(index~=nil,'Digital impulse detected in actual REAPER item')
    X.apply_selected({[index]=true},'repair');wait();st=X.state(item)
    local output=st.result.path
    test(st.plan.events[index].bcs_applied and st.plan.events[index].repair,'Single click repaired immediately')
    test(st.result.map.edited_frames==96000 and reaper.GetMediaItemInfo_Value(item,'D_LENGTH')==2,'Click repair preserves item duration and TimeMap')
    test(reaper.CountTracks(0)==1 and reaper.CountTakes(item)==1,'No duplicate track or take')
    X.undo(false);test(not X.state(item).result,'One Undo restores original click/source')
    X.undo(true);test(X.state(item).result~=nil,'One Redo restores repaired source')
    X.difference();wait();local _,ctx=X.state(item)
    test(ctx.residual and reaper.GetMediaItemInfo_Value(item,'D_LENGTH')==2,'Removed signal is available on same take')
    local residual=ctx.state.residual.path
    X.compare(false);local _,original=X.state(item);test(not original.processed,'Original A/B restores disk original')
    X.compare(true);local _,processed=X.state(item);test(processed.processed and not processed.residual,'Processed A/B leaves residual mode')
    X.apply_selected({[index]=true},'attenuate');wait();st=X.state(item);local attenuated=st.result.path
    test(not st.plan.events[index].repair,'Explicit Attenua does not secretly reconstruct a click')
    X.undo(false);test(X.state(item).plan.events[index].repair,'Undo restores the dedicated click-repair choice')
    X.label({[index]=true},'click_false_positive');wait();st=X.state(item)
    test(st.plan.events[index].learning_label=='click_false_positive' and not st.plan.events[index].bcs_applied,'Verified click false positive undoes its applied repair')
    X.label({[index]=true},'click_confirmed');test(X.state(item).plan.events[index].training_eligible,'Click truth is explicit, separate from breath profile')
    X.repair_method({[index]=true},'linear');X.apply_selected({[index]=true},'repair');wait()
    test(X.state(item).plan.events[index].repair_method=='linear','Per-event alternative algorithm persists')
    local second=new_item(3);X.settings.detect_breath=true;X.settings.detect_clicks=false
    X.begin('analyze',{second});wait();local breath=X.state(second)
    for _,e in ipairs(breath.plan.events) do test(e.kind==0,'Breath-only never adds click candidates')end
    test(breath.plan.ai_inference_frames>0,'Breath-only retains actual AI inference')
    local third=new_item(6);X.settings.detect_clicks=true
    X.begin('analyze',{third});wait();local both=X.state(third);local found=false
    for _,e in ipairs(both.plan.events) do if e.kind==1 then found=true end end
    test(found and both.plan.ai_inference_frames>0,'Breaths and clicks run independently together with Fusion')
    local uncertain=root..'/uncertain.wav';fixture(uncertain,true,true)
    local fourth=new_item(9,uncertain);X.settings.detector='dsp';X.settings.detect_breath=false
    X.begin('analyze',{fourth});wait()
    reaper.GetSet_LoopTimeRange2(0,true,false,9.8,9.803,false)
    local manual=X.add_manual(nil,1);local manual_index
    for i,e in ipairs(X.state(fourth).plan.events)do if e.bcs_id==manual then manual_index=i end end
    X.apply_selected({[manual_index]=true},'repair');wait();local cautious=X.state(fourth)
    test(not cautious.plan.events[manual_index].bcs_applied and cautious.plan.events[manual_index].bcs_declined and cautious.result.map.declined_events==1,'Unsafe auto reconstruction is visibly declined on the actual take')
    test(not cautious.plan.events[manual_index].training_eligible and reaper.CountTracks(0)==1 and reaper.CountTakes(fourth)==1,'Automatic refusal creates no training truth or duplicate take')
    X.undo(false);X.undo(true)
    test(X.state(fourth).plan.events[manual_index].bcs_declined~=nil,'Undo/Redo preserves the declined reconstruction state')
    reaper.Main_SaveProjectEx(0,root..'/Click editing.rpp',8)
    reaper.Main_openProject('noprompt:'..root..'/Click editing.rpp')
    local restored=X.state(reaper.GetMediaItem(0,0));local labelled=false
    for _,e in ipairs(restored.plan.events)do if e.learning_label=='click_confirmed' and e.repair_method=='linear' then labelled=true end end
    test(labelled,'Save/reopen preserves click truth and algorithm')
    test(X.state(reaper.GetMediaItem(0,3)).plan.events[manual_index].bcs_declined~=nil,'Save/reopen preserves conservative auto decision')
    return {status='passed',checks=checks,heartbeat=heartbeat,source=source,clean=clean,output=output,residual=residual,attenuated=attenuated,project=root..'/Click editing.rpp'}
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
