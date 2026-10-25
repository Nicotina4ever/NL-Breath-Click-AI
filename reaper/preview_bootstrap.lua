-- SPDX-License-Identifier: MPL-2.0
-- Entry used only by start_preview.ps1 in the shared sacrificial REAPER profile.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local resource=reaper.GetResourcePath():gsub('\\','/')
assert(resource:lower():match('/build/reaper_sandbox/portable$'),'Use the dedicated preview profile')
if BCS_PREVIEW_DEVELOPMENT then
    local root=assert(BCS_PREVIEW_DIRECTORY):gsub('\\','/')
    assert(root:lower()==(resource:match('^(.*)/portable$')..'/experimental'):lower(),'Invalid experimental project directory')
    local project=root..'/Breath and Mouth RC3.rpp'
    local opened=false
    for i=0,1000 do
        local p,name=reaper.EnumProjects(i,'');if not p then break end
        if name:gsub('\\','/'):lower()==project:lower() then reaper.SelectProjectInstance(p);opened=true;break end
    end
    if not opened then
        reaper.Main_OnCommand(40859,0) -- New test tab, preserving every existing tab.
        if reaper.file_exists(project) then reaper.Main_openProject(project)
        else
            reaper.InsertTrackAtIndex(0,false);local track=reaper.GetTrack(0,0)
            reaper.GetSetMediaTrackInfo_String(track,'P_NAME','Breath & Mouth - prova RC3',true)
            local files={{root..'/fixture.wav','Voce/respiri/click sintetici'}}
            local public=here..'/../build/rc3/public_audio/'
            for _,f in ipairs{{'mouth.wav','Click di lingua reali - CC0 Xemptful'},{'mixed_voice/t_vowel.wav','Mix artificiale click reale / vocale reale'}}do
                if reaper.file_exists(public..f[1])then files[#files+1]={public..f[1],f[2]}end
            end
            local position=0
            for _,f in ipairs(files) do
                local source=assert(reaper.PCM_Source_CreateFromFile(f[1]));local duration=reaper.GetMediaSourceLength(source)
                local item=reaper.AddMediaItemToTrack(track);local take=reaper.AddTakeToMediaItem(item)
                assert(reaper.SetMediaItemTake_Source(take,source));reaper.SetMediaItemInfo_Value(item,'D_POSITION',position)
                reaper.SetMediaItemInfo_Value(item,'D_LENGTH',duration);reaper.GetSetMediaItemTakeInfo_String(take,'P_NAME',f[2],true)
                if position==0 then reaper.SetMediaItemSelected(item,true)end
                position=position+duration+.5
            end
            reaper.Main_SaveProjectEx(0,project,8);reaper.UpdateArrange()
        end
    end
    local engine=here..'/../build/bin/Release/bc_studio_cli.exe';assert(reaper.file_exists(engine),'Compile the RC3 engine first')
    reaper.SetExtState('BreathClickStudio','engine',engine,false)
    -- Only the test profile is affected. Do not write these preferences globally.
    _G.BCS_PANEL_NO_RUN=true;local UI=dofile(here..'/BreathClickStudio.lua');_G.BCS_PANEL_NO_RUN=nil
    UI.open('NL Breath Click Studio - RC3 sperimentale',true)
    assert(UI.bridge.J.write(root..'/ready.json',{status='ready',version=UI.identity.version,engine=UI.bridge.engine(),project=project}))
    return
end
local installed=resource..'/Scripts/NL Audio Tools/Breath Click Studio/BreathClickStudio.lua'
local panel=reaper.file_exists(installed) and installed or (here..'/BreathClickStudio.lua')
local action=reaper.AddRemoveReaScript(true,0,panel,true)
assert(action~=0,'Cannot register the panel in the preview profile')
reaper.Main_OnCommand(action,0)
