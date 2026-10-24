-- SPDX-License-Identifier: MPL-2.0
-- Preserve every open test tab before giving the single portable to the tests.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/../bc_json.lua')
local resource=reaper.GetResourcePath():gsub('\\','/')
assert(resource:lower():match('/build/reaper_sandbox/portable$'),'Not the dedicated portable; no operation performed')
local root=resource:match('^(.*)/portable$')..'/recovery_'..reaper.genGuid():gsub('[{}]','')
local projects={}
for i=0,1000 do
    local p,name=reaper.EnumProjects(i,''); if not p then break end
    assert((reaper.GetPlayStateEx(p)&4)==0,'Recording in progress; portable left untouched')
    projects[#projects+1]={project=p,filename=name}
end
assert(reaper.RecursiveCreateDirectory(root,0)~=0)
for i,entry in ipairs(projects) do
    reaper.OnStopButtonEx(entry.project)
    local base=entry.filename:match('^(.*)[/\\]')
    -- REAPER rebases media filenames on Save As, but custom P_EXT paths are
    -- application data. Make those references absolute in the recovery copy.
    if base then
        for n=0,reaper.CountMediaItems(entry.project)-1 do
            local item=reaper.GetMediaItem(entry.project,n)
            for t=0,reaper.CountTakes(item)-1 do
                local take=reaper.GetTake(item,t)
                for _,key in ipairs{'P_EXT:BreathClickStudio','P_EXT:BreathClickStudioResult'} do
                    local ok,value=reaper.GetSetMediaItemTakeInfo_String(take,key,'',false)
                    if ok and value~='' then
                        local parsed,data=pcall(J.decode,value)
                        if parsed then
                            for _,field in ipairs{'prepared_rel','report_rel'} do
                                if data[field] and not data[field]:match('^%a:[/\\]') and data[field]:sub(1,1)~='/' then data[field]=base..'/'..data[field] end
                            end
                            assert(reaper.GetSetMediaItemTakeInfo_String(take,key,J.encode(data),true))
                        end
                    end
                end
            end
        end
    end
    entry.saved=root..'/project_'..i..'.rpp'
    reaper.Main_SaveProjectEx(entry.project,entry.saved,8)
    assert(reaper.file_exists(entry.saved),'Recovery save failed; portable remains open')
    entry.project=nil
end
assert(J.write(root..'/manifest.json',{projects=projects,resource=resource}))
local f=assert(io.open(resource..'/park_result.json','wb')); f:write(J.encode({recovery=root,status='saved'})); f:close()
reaper.defer(function() reaper.Main_OnCommand(40004,0) end)
