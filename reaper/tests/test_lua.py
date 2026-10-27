# SPDX-License-Identifier: MPL-2.0
"""Strict data parsing and pure adapter contracts, using Lua 5.4, no DAW mocks as audio proof."""
import json
from pathlib import Path
from lupa.lua54 import LuaRuntime, LuaError

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
for file in sorted(root.rglob('*.lua')):
    compiled = lua.eval('loadfile')(file.as_posix())
    assert not isinstance(compiled, tuple), (file, compiled)
codec = lua.execute(f'return dofile({json.dumps((root / "bc_json.lua").as_posix())})')
for document in ['{}', '[]', ' [1, false, null, "caffè 🎙"] ', '{"x": {"k": -0.2e3}}',
                 '{"unicode":"\\ud83c\\udf99","quote":"a\\\"b\\\\c"}']:
    encoded = codec.encode(codec.decode(document))
    assert json.loads(encoded) == json.loads(document), (document, encoded)
for document in ['{"a":1,"a":2}', '[1,]', '{"x":}', '1.e2', '01', '1e999',
                 '"\\u0000"', '"\\ud800"', '"\\udc00"', '[true] trailing']:
    try:
        codec.decode(document)
    except LuaError:
        pass
    else:
        raise AssertionError(f'Invalid JSON accepted: {document}')
# Bridge module loads without opening windows or touching the DAW.
bridge = lua.execute(f'return dofile({json.dumps((root / "bc_studio_bridge.lua").as_posix())})')
lua.execute('reaper = {GetSetMediaItemTakeInfo_String=function() return false, "" end}')
assert bridge.B.guid_take(1, 1, 0) is None
report = codec.decode('{"time_map":[{"original_start":0,"original_end":10,"edited_start":0,"edited_end":10},{"original_start":15,"original_end":25,"edited_start":10,"edited_end":20}]}')
assert bridge.map_frame(report, 8) == (8, False)
assert bridge.map_frame(report, 12) == (10, True)
assert bridge.map_frame(report, 18) == (13, False)
assert bridge.map_frame(report, 25) == (20, True)  # native TimeMap endpoint convention
lua.globals()['adapter'] = bridge
lua.execute('''
local stopped, repeat_value = nil, 0
reaper = {
 EnumProjects=function(i) if i==0 then return 'projectA' elseif i==1 or i==-1 then return 'projectB' end end,
 GetPlayStateEx=function(p) assert(p=='projectA'); return 1 end,
 OnStopButtonEx=function(p) stopped=p end,
 GetSetRepeatEx=function(p,v) assert(p=='projectA'); if v~=-1 then repeat_value=v end; return repeat_value end
}
adapter.preview_project='projectA'; adapter.preview_repeat=1; adapter.preview_end=10
adapter.stop_preview()
assert(stopped=='projectA' and repeat_value==1 and adapter.preview_end==nil)
''')
print('LUA ADAPTER OK: syntax, strict JSON/Unicode, marker mapping; no REAPER/audio simulation claimed')

# Information screen uses the real panel module; contact launching is stubbed.
lua.execute('BCS_PANEL_NO_RUN=true')
panel = lua.execute(f'return dofile({json.dumps((root / "BreathClickStudio.lua").as_posix())})')
assert panel.identity.name == 'NL Breath Click Studio'
assert panel.identity.author == 'Nicola Lanci Voice Over'
assert panel.identity.email == 'info@nicolalanci.com'
assert panel.identity.website == 'www.nicolalanci.com'
assert panel.identity.version == '1.0.0-rc.3'
lua.globals()['panel'] = panel
lua.execute("""
local command
reaper.ExecProcess=function(text,timeout) command=text; assert(timeout==-2); return '' end
reaper.GetOS=function() return package.config:sub(1,1)=='\\\\' and 'Win64' or 'Linux' end
panel.action('Informazioni'); assert(panel.about_open)
panel.action('Sito web'); assert(command:find('https://www.nicolalanci.com',1,true))
panel.action('Email'); assert(command:find('mailto:info@nicolalanci.com',1,true))
panel.action('Chiudi info'); assert(not panel.about_open)
-- Esc closes only Information, preserving the editing panel.
panel.action('Informazioni')
gfx={getchar=function() return 27 end}
panel.bridge.tick=function() end
assert(panel.draw()==true and not panel.about_open)
""")
print('PANEL INFO OK: identity, contact destinations and Esc; no external application opened')
lua.execute('''
local saved=adapter.review
local event={kind=1,repair=true,repair_method='linear',learning_label='click_confirmed',training_eligible=true,approved=true,bcs_applied=true}
local state={plan={events={event}}}
adapter.review=function(fn) fn(state) end
adapter.repair_feedback({[1]=true},true)
assert(event.repair_feedback=='unsatisfactory' and event.repair_feedback_method=='linear')
assert(event.learning_label=='click_confirmed' and event.training_eligible and event.approved and event.bcs_applied)
event.repair_method='predictive'
assert(event.repair_feedback_method=='linear') -- snapshot remains the judged method
adapter.repair_feedback({[1]=true},false)
assert(event.repair_feedback==nil and event.repair_feedback_method==nil)
adapter.review=saved
''')
print('REPAIR FEEDBACK OK: metadata only, method snapshot, no detection-label changes')
lua.execute('''
local events={
 {kind=1,start=100,['end']=110,mode='attenuate',approved=true,enabled=true,repair=true,repair_method='linear',bcs_applied=true},
 {kind=1,start=200,['end']=210,mode='attenuate',approved=true,enabled=true,repair=true,repair_method='linear',bcs_applied=true},
 {kind=1,start=300,['end']=310,approved=false,enabled=true,repair=true},
 {kind=1,start=400,['end']=410,approved=true,enabled=true,repair=true,repair_method='none',bcs_applied=true}}
adapter.record_outcomes({plan={events=events}},{operations={
 {kind=1,start=100,['end']=110,applied=false,declined=true,note='Protezione della voce',click_strategy={selected='none'}},
 {kind=1,start=200,['end']=210,applied=true,click_strategy={selected='linear'}},
 {kind=1,start=400,['end']=410,applied=false,bypassed=true,click_strategy={selected='none'}}}})
assert(adapter.event_status(events[1])=='INTATTO: SICUREZZA' and not events[1].bcs_applied)
assert(adapter.event_status(events[2])=='RIPARATO' and events[2].bcs_applied)
assert(adapter.event_status(events[3])=='DA RIVEDERE')
events[3].bcs_edited=true;events[3].repair_method='linear'
assert(adapter.event_status(events[3])=='DA RIVEDERE') -- choosing a method is not editorial exclusion
events[3].decision_source='user'
assert(adapter.event_status(events[3])=='ESCLUSO')
assert(adapter.event_status(events[4])=='INTATTO: SCELTA' and not events[4].bcs_applied)
local restored=adapter.J.decode(adapter.J.encode(events))
assert(adapter.event_status(restored[1])=='INTATTO: SICUREZZA')
assert(adapter.event_status(restored[2])=='RIPARATO')
events[2].repair_feedback='unsatisfactory'
assert(adapter.event_status(events[2])=='RIPARATO') -- judgment never changes render truth
local shared={{kind=0,start=500,['end']=600,mode='cut',approved=true},{kind=0,start=500,['end']=600,mode='attenuate',approved=true}}
adapter.record_outcomes({plan={events=shared}},{operations={
 {kind=0,start=500,['end']=600,mode='attenuate',applied=false},
 {kind=0,start=500,['end']=600,mode='cut',applied=true}}})
assert(shared[1].bcs_applied and not shared[2].bcs_applied) -- identical bounds are not operation identity
''')
print('RENDER OUTCOMES OK: repair, safety refusal, pending review and bypass remain distinct after JSON restore')
lua.execute('''
-- Test the real reconciliation helper; no media or REAPER API simulation.
local helpers
for i=1,40 do local name,value=debug.getupvalue(adapter.begin,i);if not name then break end;if name=='H' then helpers=value end end
assert(helpers)
assert(helpers.automatic_approval({kind=1,enabled=true,classification='saliva_click'}))
for _,category in ipairs({'band_limited_click','isolated_click_review','speech_overlap','low_level_impulse','future_unknown_click'}) do
 assert(not helpers.automatic_approval({kind=1,enabled=true,classification=category}),category)
end
reaper.genGuid=function() return '{NEW-ID}' end
local function reconcile(feedback)
 local previous={offset=0,plan={sample_rate=48000,events={{kind=1,start=100,['end']=120,bcs_id='OLD',repair_feedback=feedback}}}}
 local plan={sample_rate=48000,events={{kind=1,start=100,['end']=120,enabled=true,classification='low_level_impulse'}}}
 helpers.merge_review(plan,previous,0,48000)
 return plan.events
end
local empty=reconcile('')
assert(#empty==1 and empty[1].bcs_id=='NEW-ID' and empty[1].approved==false)
local judged=reconcile('unsatisfactory')
assert(#judged==1 and judged[1].bcs_id=='OLD' and judged[1].repair_feedback=='unsatisfactory')
''')
print('RECONCILIATION OK: empty feedback is not a human review; real feedback survives')

lua.execute('''
panel.order={2,4};panel.selected={}
panel.action('Tutte')
assert(panel.selected[2] and panel.selected[4] and not panel.selected[1] and not panel.selected[3])
''')
print('FILTER SELECTION OK: select-all affects only events visible in the current filter')

# Undo remains compatible with already open/saved projects using the old label.
lua.execute("""
local description, calls = '', 0
reaper.Undo_CanUndo2=function() return description end
reaper.Undo_DoUndo2=function() calls=calls+1 end
adapter.busy=false
for _,label in ipairs({'BreathClick: Dimezza','NL Breath Click Studio: Dimezza'}) do
 description=label; adapter.undo(false)
end
assert(calls==2)
description='External editor operation'
assert(not pcall(adapter.undo,false) and calls==2)
""")
