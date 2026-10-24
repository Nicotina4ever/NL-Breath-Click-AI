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
