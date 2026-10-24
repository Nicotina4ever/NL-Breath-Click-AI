# SPDX-License-Identifier: MPL-2.0
"""Strict data parsing and pure adapter contracts, using Lua 5.4, no DAW mocks as audio proof."""
import json
from pathlib import Path
from lupa.lua54 import LuaRuntime, LuaError

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
for file in root.glob('*.lua'):
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
assert panel.identity.version == '1.0.0-rc.2'
lua.globals()['panel'] = panel
lua.execute("""
local command
reaper.ExecProcess=function(text,timeout) command=text; assert(timeout==-2); return '' end
panel.action('Informazioni'); assert(panel.about_open)
panel.action('Sito web'); assert(command=='cmd.exe /d /c start "" "https://www.nicolalanci.com"')
panel.action('Email'); assert(command=='cmd.exe /d /c start "" "mailto:info@nicolalanci.com"')
panel.action('Chiudi info'); assert(not panel.about_open)
-- Esc closes only Information, preserving the editing panel.
panel.action('Informazioni')
gfx={getchar=function() return 27 end}
panel.bridge.tick=function() end
assert(panel.draw()==true and not panel.about_open)
""")
print('PANEL INFO OK: identity, contact destinations and Esc; no external application opened')

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
