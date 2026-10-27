-- SPDX-License-Identifier: MPL-2.0
-- Only the managed harness's own tabs; the portable and other tabs stay open.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/../bc_json.lua')
local resource=reaper.GetResourcePath():gsub('\\','/')
assert(resource:lower():match('/build/reaper_sandbox/portable$'))
local request=assert(J.read(resource:match('^(.*)/portable$')..'/workspace_finish_request.json'))
local root=assert(request.workspace):gsub('\\','/')
assert(root:lower():match('/build/test_work/managed/reaper_[a-z0-9_]+$'))
assert(reaper.file_exists(root..'/.nlbc-test-workspace.json'))
local identity=assert(J.read(root..'/managed_project.json'))
local owned={}
local index=0
while true do
    local project,path=reaper.EnumProjects(index,'')
    if not project then break end
    path=path:gsub('\\','/')
    if path:lower():sub(1,#root+1)==root:lower()..'/' or
       (tostring(project)==identity.owned and path=='') then
        owned[#owned+1]={project=project,path=path~='' and path or root..'/test.rpp'}
    end
    index=index+1
end
for _,entry in ipairs(owned) do
    reaper.SelectProjectInstance(entry.project)
    reaper.Main_SaveProjectEx(entry.project,entry.path,0)
    -- File: Close current project tab. Save first, never dismiss a prompt.
    reaper.Main_OnCommand(40860,0)
end
if identity.previous then
    local i=0
    while true do
        local project=reaper.EnumProjects(i,'')
        if not project then break end
        if tostring(project)==identity.previous then reaper.SelectProjectInstance(project);break end
        i=i+1
    end
end
-- The runner checks ALL live sources after closure before removing scratch.
dofile(here..'/workspace_inventory.lua')
assert(J.write(root..'/workspace_finished.json',{status='closed',tabs=#owned}))
