-- SPDX-License-Identifier: MPL-2.0
-- Small strict JSON codec. Data only: never load()/evaluate project or job data.
local J = {null = {}}
local array_mt = {__json_array=true}
function J.array(t) return setmetatable(t or {}, array_mt) end
local function quote(s)
    return '"' .. s:gsub('[%z\1-\31\\"]', function(c)
        local escapes = {['"']='\\"', ['\\']='\\\\', ['\n']='\\n', ['\r']='\\r', ['\t']='\\t'}
        return escapes[c] or string.format('\\u%04x', c:byte())
    end) .. '"'
end
function J.encode(value, depth)
    depth=(depth or 0)+1; assert(depth<=64, 'JSON nesting limit')
    if value==J.null then return 'null' end
    local kind=type(value)
    if kind=='string' then return quote(value) end
    if kind=='number' then
        assert(value==value and math.abs(value)<math.huge, 'Non-finite JSON number')
        return string.format('%.17g',value):gsub(',', '.')
    end
    if kind=='boolean' then return tostring(value) end
    assert(kind=='table','Unsupported JSON value: '..kind)
    local out={}
    local is_array=(getmetatable(value)==array_mt) or #value>0
    if is_array then
        for i=1,#value do out[i]=J.encode(value[i],depth) end
        return '['..table.concat(out,',')..']'
    end
    local keys={}; for k in pairs(value) do assert(type(k)=='string'); keys[#keys+1]=k end
    table.sort(keys)
    for _,k in ipairs(keys) do out[#out+1]=quote(k)..':'..J.encode(value[k],depth) end
    return '{'..table.concat(out,',')..'}'
end
function J.decode(text)
    assert(type(text)=='string' and #text<64*1024*1024,'Invalid JSON input')
    text=text:gsub('^\239\187\191','')
    local p,n=1,#text
    local function ws() local _,last=text:find('^[ \t\r\n]*',p); p=(last or p-1)+1 end
    local function fail(s) error(s..' at JSON byte '..p,0) end
    local parse
    local function str()
        assert(text:sub(p,p)=='"'); p=p+1; local out={}
        while p<=n do
            local c=text:sub(p,p); p=p+1
            if c=='"' then return table.concat(out) end
            if c=='\\' then
                local e=text:sub(p,p); p=p+1
                local escapes={['"']='"',['\\']='\\',['/']='/',b='\b',f='\f',n='\n',r='\r',t='\t'}
                if e=='u' then
                    local hex=text:sub(p,p+3); if not hex:match('^%x%x%x%x$') then fail('Invalid Unicode') end
                    local cp=tonumber(hex,16); p=p+4
                    if cp>=0xd800 and cp<=0xdbff then
                        if text:sub(p,p+1)~='\\u' then fail('Missing low surrogate') end
                        local low=text:sub(p+2,p+5); if not low:match('^%x%x%x%x$') then fail('Invalid surrogate') end
                        low=tonumber(low,16); if low<0xdc00 or low>0xdfff then fail('Invalid low surrogate') end
                        p=p+6; cp=0x10000+(cp-0xd800)*1024+low-0xdc00
                    elseif cp>=0xdc00 and cp<=0xdfff then fail('Unpaired surrogate') end
                    if cp==0 then fail('NUL not allowed') end
                    out[#out+1]=utf8.char(cp)
                elseif escapes[e] then out[#out+1]=escapes[e] else fail('Invalid escape') end
            else
                if c:byte()<32 then fail('Control character') end
                out[#out+1]=c
            end
        end
        fail('Unterminated string')
    end
    parse=function(depth)
        if depth>64 then fail('JSON nesting limit') end
        ws(); local c=text:sub(p,p)
        if c=='"' then return str() end
        if c=='{' or c=='[' then
            p=p+1; ws(); local object=c=='{'; local out=object and {} or J.array()
            local close=object and '}' or ']'
            if text:sub(p,p)==close then p=p+1; return out end
            while true do
                local key
                if object then
                    if text:sub(p,p)~='"' then fail('Expected object key') end
                    key=str(); if out[key]~=nil then fail('Duplicate key') end
                    ws(); if text:sub(p,p)~=':' then fail('Expected colon') end; p=p+1
                end
                local val=parse(depth+1)
                if object then out[key]=val else out[#out+1]=val end
                ws(); local sep=text:sub(p,p); p=p+1
                if sep==close then return out end
                if sep~=',' then fail('Expected separator') end; ws()
            end
        end
        for literal,val in pairs({['true']=true,['false']=false,['null']=J.null}) do
            if text:sub(p,p+#literal-1)==literal then p=p+#literal; return val end
        end
        local number=text:match('^-?%d+%.%d+[eE][+-]?%d+',p) or text:match('^-?%d+[eE][+-]?%d+',p)
            or text:match('^-?%d+%.%d+',p) or text:match('^-?%d+',p)
        if not number or number:match('^-?0%d') then fail('Invalid number') end
        local val=tonumber(number); if not val or val~=val or math.abs(val)==math.huge then fail('Invalid number') end
        p=p+#number; return val
    end
    local value=parse(1); ws(); if p<=n then fail('Trailing JSON data') end
    return value
end
function J.read(path)
    local f,err=io.open(path,'rb'); if not f then return nil,err end
    local data=f:read('*a'); f:close()
    local ok,value=pcall(J.decode,data); if not ok then return nil,value end
    return value
end
function J.write(path,value)
    local existing=io.open(path,'rb'); if existing then existing:close(); return nil,'Destination already exists' end
    local pending=path..'.pending'
    local f,err=io.open(pending,'wb'); if not f then return nil,err end
    local ok,msg=f:write(J.encode(value)); f:close(); if not ok then return nil,msg end
    return os.rename(pending,path)
end
return J
