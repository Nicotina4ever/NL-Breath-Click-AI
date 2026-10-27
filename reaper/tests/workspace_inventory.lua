-- SPDX-License-Identifier: MPL-2.0
-- Read-only inventory: never close tabs or change the project to reclaim space.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/../bc_json.lua')
local resource=reaper.GetResourcePath():gsub('\\','/')
assert(resource:lower():match('/build/reaper_sandbox/portable$'),'Isolated portable required')
local paths=J.array()
local index=0
while true do
    local project,path=reaper.EnumProjects(index,'')
    if not project then break end
    paths[#paths+1]=path
    for itemIndex=0,reaper.CountMediaItems(project)-1 do
        local item=reaper.GetMediaItem(project,itemIndex)
        for takeIndex=0,reaper.CountTakes(item)-1 do
            local source=reaper.GetMediaItemTake_Source(reaper.GetTake(item,takeIndex))
            if source then paths[#paths+1]=reaper.GetMediaSourceFileName(source,'') end
        end
    end
    index=index+1
end
local report=resource:match('^(.*)/portable$')..'/workspace_inventory.json'
os.remove(report) -- Owned ephemeral report, never a project or a user file.
assert(J.write(report,{
    paths=paths,process=reaper.GetAppVersion(),time=os.time()}))
