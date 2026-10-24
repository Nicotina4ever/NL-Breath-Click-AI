-- SPDX-License-Identifier: MPL-2.0
-- REAPER adapter. Core/StudioEngine own detection, sample edits and TimeMap.
-- Persistent review belongs to a take; generated media belongs to the project.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/bc_json.lua')
local B=dofile(here..'/bcnext_core.lua')
local X={J=J,B=B,status='Seleziona una take audio.',settings={detector='fusion',reduction_db=-12,fade_ms=15},busy=false}
local KEY='P_EXT:BreathClickStudio'
local LINK='P_EXT:BreathClickStudioResult'
local function check(ok,err) if not ok then error(err or 'REAPER operation failed',0) end; return ok end
local function guid() return reaper.genGuid():gsub('[{}]','') end
local function take_guid(take)
    local ok,value=reaper.GetSetMediaItemTakeInfo_String(take,'GUID','',false)
    check(ok and value~='','GUID take non disponibile'); return value
end
local function get(take,key)
    local ok,value=reaper.GetSetMediaItemTakeInfo_String(take,key,'',false)
    if not ok or value=='' then return nil end
    local parsed,err=pcall(J.decode,value); check(parsed,err); return err
end
local function put(take,key,value)
    if key==KEY and value.plan then
        local origin=math.floor(value.offset*value.plan.sample_rate+0.5)
        for _,event in ipairs(value.plan.events) do
            if event.bcs_edited then event.bcs_source_start=origin+event.start; event.bcs_source_end=origin+event['end'] end
        end
    end
    check(reaper.GetSetMediaItemTakeInfo_String(take,key,J.encode(value),true),'Salvataggio metadati take fallito')
end
local function current_project() return reaper.EnumProjects(-1,'') end
function X.project_path()
    local _,filename=reaper.EnumProjects(-1,'')
    check(filename and filename~='','Salva il progetto prima di analizzare: i media elaborati devono restare recuperabili.')
    return filename:match('^(.*)[/\\]')
end
function X.engine()
    local configured=reaper.GetExtState('BreathClickStudio','engine')
    if configured~='' and reaper.file_exists(configured) then return configured end
    local built=here..'/../build/bin/Release/bc_studio_cli.exe'
    if reaper.file_exists(built) then return built end
    error('Seleziona bc_studio_cli.exe con il pulsante Motore.',0)
end
function X.configure_engine()
    local ok,path=reaper.GetUserFileNameForRead('','Seleziona bc_studio_cli.exe','exe')
    if ok then check(path:match('[/\\]bc_studio_cli%.exe$'),'Scegli bc_studio_cli.exe'); reaper.SetExtState('BreathClickStudio','engine',path,true) end
end
function X.load_settings()
    local saved=reaper.GetExtState('BreathClickStudio','settings')
    if saved=='' then return end
    local ok,data=pcall(J.decode,saved)
    if ok and type(data)=='table' and (data.detector=='fusion' or data.detector=='dsp')
        and type(data.reduction_db)=='number' and data.reduction_db>=-120 and data.reduction_db<=0
        and type(data.fade_ms)=='number' and data.fade_ms>=0 and data.fade_ms<=500 then X.settings=data end
end
function X.save_settings() reaper.SetExtState('BreathClickStudio','settings',J.encode(X.settings),true) end
function X.context(item)
    item=item or reaper.GetSelectedMediaItem(0,0)
    if not item then return nil end
    local take=reaper.GetActiveTake(item); if not take then return nil end
    local link=get(take,LINK)
    local owner=link and reaper.GetMediaItemTakeByGUID(0,link.owner_guid) or take
    check(owner and reaper.GetMediaItemTake_Item(owner)==item,'La take originale non e\' piu\' disponibile')
    local state=get(owner,KEY)
    return {item=item,take=owner,active=take,state=state,link=link}
end
function X.state(item) local ctx=X.context(item); return ctx and ctx.state,ctx end
local function snapshot(item)
    local s,err=B.snapshot(item,{allow_rate=true}); check(s,err)
    check(s.rate>0 and s.rate<100 and s.rate==s.rate,'Playback rate non valido')
    check(reaper.GetMediaItemInfo_Value(item,'C_LOCK')==0,'Item bloccato: nessuna modifica')
    check(reaper.GetMediaItemInfo_Value(item,'B_ALLTAKESPLAY')==0,'Disattiva Riproduci tutte le take su questo item')
    local source=reaper.GetMediaItemTake_Source(s.take)
    check(not reaper.GetMediaSourceParent(source),'Sorgenti section/reverse: prepara una take WAV dedicata')
    check(reaper.CountTakeEnvelopes(s.take)==0,'Take con envelope: usa una copia consolidata; l\'automazione non viene appiattita')
    return s
end
local function validate(s)
    check(current_project()==X.project,'Il progetto attivo e\' cambiato: risultato non applicato')
    local ok,err=B.verifica_snapshot(s); check(ok,err)
    for _,field in ipairs{{'D_POSITION','pos'},{'D_LENGTH','len'}} do
        check(math.abs(reaper.GetMediaItemInfo_Value(s.item,field[1])-s[field[2]])<1e-9,'Item modificato durante il lavoro')
    end
    for _,field in ipairs{{'D_STARTOFFS','off'},{'D_PLAYRATE','rate'}} do
        check(math.abs(reaper.GetMediaItemTakeInfo_Value(s.take,field[1])-s[field[2]])<1e-9,'Take modificata durante il lavoro')
    end
    snapshot(s.item)
end
local function marker_name(event,edge)
    return 'BC['..event.bcs_id..']:'..edge..' '..(event.kind==1 and 'Click' or 'Respiro')..(event.approved and '' or ' (escluso)')
end
local function marker_id(name) return name:match('^BC%[([%w%-]+)%]:([AB]) ') end
function X.sync_markers(ctx,allow_incomplete)
    local state=ctx.state; if not state then return end
    local found={}
    for i=0,reaper.GetNumTakeMarkers(ctx.take)-1 do
        local position,name=reaper.GetTakeMarker(ctx.take,i)
        local id,edge=marker_id(name)
        if id then
            found[id]=found[id] or {}; check(not found[id][edge],'Marcatore BreathClick duplicato')
            found[id][edge]=position
        end
    end
    local sr=state.plan.sample_rate
    for _,e in ipairs(state.plan.events) do
        local pair=found[e.bcs_id]
        if pair and (not pair.A or not pair.B) then
            if e.approved then
                check(allow_incomplete,'Manca un bordo del respiro: ripristina il marcatore o escludi l\'evento dal pannello')
                ctx.incomplete=ctx.incomplete or {}; ctx.incomplete[e.bcs_id]=true
            end
        elseif pair then
            local a=math.floor((pair.A-state.offset)*sr+0.5)
            local b=math.floor((pair.B-state.offset)*sr+0.5)
            check(a>=0 and b>a and b<=state.frames,'Bordi del respiro fuori take o invertiti')
            if a~=e.start or b~=e['end'] then e.bcs_edited=true end
            e.start=a; e['end']=b
            if e.bcs_edited then
                local origin=math.floor(state.offset*sr+0.5)
                e.bcs_source_start=origin+a; e.bcs_source_end=origin+b
            end
        elseif e.approved then
            -- Deleting both native markers excludes the event, preserving metadata.
            e.approved=false; e.bcs_edited=true; e.decision_source='user'
        end
    end
end
local function write_markers(ctx)
    for i=reaper.GetNumTakeMarkers(ctx.take)-1,0,-1 do
        local _,name=reaper.GetTakeMarker(ctx.take,i)
        if marker_id(name) then reaper.DeleteTakeMarker(ctx.take,i) end
    end
    for _,event in ipairs(ctx.state.plan.events) do
        if event.approved then
            local color=reaper.ColorToNative(event.kind==1 and 240 or 235,event.kind==1 and 80 or 195,60)|0x1000000
            local sr=ctx.state.plan.sample_rate
            check(reaper.SetTakeMarker(ctx.take,-1,marker_name(event,'A'),ctx.state.offset+event.start/sr,color)>=0,'Creazione marcatore fallita')
            check(reaper.SetTakeMarker(ctx.take,-1,marker_name(event,'B'),ctx.state.offset+event['end']/sr,color)>=0,'Creazione bordo fallita')
        end
    end
end
local function transaction(label,contexts,fn)
    check(reaper.GetPlayState()==0,'Ferma il trasporto prima di modificare le take')
    local chunks={}
    for _,ctx in ipairs(contexts) do local ok,chunk=reaper.GetItemStateChunk(ctx.item,'',false); check(ok,'Snapshot item fallito'); chunks[#chunks+1]={ctx.item,chunk} end
    reaper.Undo_BeginBlock2(0); reaper.PreventUIRefresh(1)
    local ok,err=xpcall(fn,debug.traceback)
    if not ok then
        local restored=true
        for _,entry in ipairs(chunks) do local worked,value=pcall(reaper.SetItemStateChunk,entry[1],entry[2],false); restored=restored and worked and value end
        reaper.PreventUIRefresh(-1)
        reaper.Undo_EndBlock2(0,'NL Breath Click Studio: operazione ripristinata',-1)
        reaper.UpdateArrange()
        error(tostring(err)..(restored and '\nItem ripristinati.' or '\nRipristino incompleto: usa Ctrl+Z.'),0)
    end
    reaper.PreventUIRefresh(-1); reaper.Undo_EndBlock2(0,'NL Breath Click Studio: '..label,-1); reaper.UpdateArrange()
end
function X.review(mutator)
    check(not X.busy,'Attendi o annulla il lavoro corrente')
    local ctx=X.context(); check(ctx and ctx.state,'Analizza prima la take')
    check(ctx.active==ctx.take,'Passa a Originale per correggere i respiri')
    check(math.abs(ctx.state.offset-reaper.GetMediaItemTakeInfo_Value(ctx.take,'D_STARTOFFS'))<1e-9 and
        math.abs(ctx.state.length-reaper.GetMediaItemInfo_Value(ctx.item,'D_LENGTH'))<1e-9,'Offset/durata modificati: rianalizza per riallineare le correzioni')
    X.sync_markers(ctx,true)
    transaction('revisione respiri',{ctx},function()
        mutator(ctx.state,ctx)
        for _,e in ipairs(ctx.state.plan.events) do
            check(not (ctx.incomplete and ctx.incomplete[e.bcs_id] and e.approved),'Escludi prima l\'evento con il bordo mancante')
        end
        put(ctx.take,KEY,ctx.state); write_markers(ctx)
    end)
end
function X.include(indices,included)
    X.review(function(state)
        for index in pairs(indices) do local e=state.plan.events[index]; if e then e.approved=included; e.bcs_edited=true; e.decision_source='user'; e.training_eligible=false end end
    end)
end
function X.set_mode(indices,mode)
    check(({attenuate=true,silence=true,cut=true,halve=true})[mode],'Modo non valido')
    X.review(function(state)
        for index in pairs(indices) do local e=state.plan.events[index]; if e then e.mode=mode; e.bcs_edited=true; e.decision_source='user'; e.training_eligible=false end end
    end)
end
function X.add_manual()
    X.review(function(state,ctx)
        local a,b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
        local pos=reaper.GetMediaItemInfo_Value(ctx.item,'D_POSITION'); local sr=state.plan.sample_rate
        local first,last=math.floor((a-pos)*sr+0.5),math.floor((b-pos)*sr+0.5)
        check(first>=0 and last>first and last<=state.frames,'Seleziona un intervallo dentro l\'item originale')
        state.plan.events[#state.plan.events+1]={kind=0,start=first,['end']=last,mode='attenuate',approved=true,enabled=true,
            state='manual',source='manual',classification='manual',manual_operation=true,decision_source='user',training_eligible=false,
            fade_ms=state.plan.fade_ms,keep_fraction=0.5,bcs_id=guid(),bcs_edited=true,score=1}
    end)
end
function X.stop_preview()
    local project=X.preview_project
    if project then
        local i=0
        while true do
            local candidate=reaper.EnumProjects(i,''); if not candidate then break end
            if candidate==project then
                local state=reaper.GetPlayStateEx(project)
                if (state&4)==0 then
                    if state~=0 then reaper.OnStopButtonEx(project) end
                    if X.preview_repeat~=nil and reaper.GetSetRepeatEx(project,-1)==0 then
                        reaper.GetSetRepeatEx(project,X.preview_repeat)
                    end
                end
                break
            end
            i=i+1
        end
    end
    X.preview_end=nil; X.preview_project=nil; X.preview_repeat=nil
end
function X.select_event(index,play)
    local ctx=X.context(); check(ctx and ctx.state,'Nessuna analisi')
    local event=ctx.state.plan.events[index]; check(event,'Seleziona un evento')
    check(ctx.active==ctx.take,'Passa a Originale per ascoltare l\'intervallo originale')
    local pos=reaper.GetMediaItemInfo_Value(ctx.item,'D_POSITION'); local sr=ctx.state.plan.sample_rate
    local a,b=pos+event.start/sr,pos+event['end']/sr
    reaper.GetSet_LoopTimeRange2(0,true,false,a,b,false)
    reaper.SetEditCurPos(math.max(pos,a-0.15),true,false)
    if play then
        X.preview_end=b+0.15; X.preview_project=current_project()
        X.preview_repeat=reaper.GetSetRepeatEx(X.preview_project,-1)
        reaper.GetSetRepeatEx(X.preview_project,0)
        reaper.OnPlayButtonEx(X.preview_project)
    end
end
local function quote(value)
    check(not value:find('[\r\n"]'),'Percorso non valido per il launcher')
    return '"'..value..'"'
end
local function relative(path,root)
    path=path:gsub('\\','/'); root=root:gsub('\\','/'):gsub('/+$','')
    check(path:sub(1,#root+1):lower()==root:lower()..'/','Percorso cache non valido')
    return path:sub(#root+2)
end
local function new_job(root)
    local folder=root..'/BreathClick Media/'..guid()
    reaper.RecursiveCreateDirectory(folder,0) -- request write below verifies the directory
    return folder
end
local function launch(entry)
    check(J.write(entry.folder..'/request.json',entry.request))
    local command='powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File '..quote(here..'/bc_job.ps1')..' -Request '..quote(entry.folder..'/request.json')
    local started=reaper.ExecProcess(command,-2)
    check(started~=nil,'Avvio worker fallito')
    entry.started=reaper.time_precise()
end
local function merge_review(plan,old,offset,frames)
    local edited,outside=J.array(),J.array()
    local seen={}; local origin=math.floor(offset*plan.sample_rate+0.5)
    local function retain(source)
        if not source.bcs_edited or seen[source.bcs_id] then return end
        seen[source.bcs_id]=true
        local e=J.decode(J.encode(source))
        e.bcs_source_start=e.bcs_source_start or math.floor(old.offset*old.plan.sample_rate+0.5)+e.start
        e.bcs_source_end=e.bcs_source_end or math.floor(old.offset*old.plan.sample_rate+0.5)+e['end']
        e.start=e.bcs_source_start-origin; e['end']=e.bcs_source_end-origin
        if e.start>=0 and e['end']<=frames then edited[#edited+1]=e else outside[#outside+1]=e end
    end
    if old then
        check(old.plan.sample_rate==plan.sample_rate,'Sample rate sorgente cambiato: revisione conservata')
        for _,e in ipairs(old.plan.events) do retain(e) end
        for _,e in ipairs(old.outside_review or {}) do retain(e) end
    end
    local fresh=J.array()
    for _,e in ipairs(plan.events) do
        local overlaps=false
        for _,kept in ipairs(edited) do if e.start<kept['end'] and e['end']>kept.start then overlaps=true; break end end
        for _,kept in ipairs(outside) do if e.start<kept['end'] and e['end']>kept.start then overlaps=true; break end end
        if not overlaps then
            e.bcs_id=guid(); e.approved=true; e.decision_source='automatic'; e.training_eligible=false
            fresh[#fresh+1]=e
        end
    end
    for _,e in ipairs(edited) do fresh[#fresh+1]=e end
    table.sort(fresh,function(a,b) return a.start<b.start end)
    plan.events=fresh
    return outside
end
function X.begin(mode,items)
    check(not X.busy,'Lavoro gia\' in corso')
    X.last_error=nil
    check(reaper.GetPlayState()==0,'Ferma il trasporto prima di avviare il lavoro')
    local root=X.project_path(); local exe=X.engine(); local entries={}
    for _,item in ipairs(items) do
        local s=snapshot(item); local ctx=X.context(item)
        check(ctx.active==ctx.take,'Passa a Originale prima di analizzare o applicare')
        local old=ctx.state
        if old then
            check(mode~='render' or (math.abs(old.offset-s.off)<1e-9 and math.abs(old.length-s.len)<1e-9),'Offset/durata modificati: rianalizza per riallineare le correzioni')
            X.sync_markers(ctx)
        end
        local folder=new_job(root)
        local request={mode=mode,exe=exe,source=s.sorgente}
        if mode=='analyze' then
            request.offset=s.off; request.duration=s.len; request.detector=X.settings.detector
            request.reduction_db=X.settings.reduction_db; request.fade_ms=X.settings.fade_ms
        elseif mode=='render' then
            check(old,'Analizza prima tutte le take selezionate')
            local included=0; for _,e in ipairs(old.plan.events) do if e.approved and e.enabled then included=included+1 end end
            check(included>0,'Nessun intervento incluso nella take')
            -- Work from immutable prepared source on every pass, never a rendered take.
            request.prepared=root..'/'..old.prepared_rel
            check(reaper.file_exists(request.prepared),'Manca la copia di analisi: ripristina la cartella BreathClick Media o analizza di nuovo')
            request.source_sha256=old.source_sha256
            local plan=J.decode(J.encode(old.plan))
            plan.source=request.prepared; plan.source_relative=''; plan.marker_scope='project'
            plan.reduction_db=X.settings.reduction_db; plan.fade_ms=X.settings.fade_ms
            for _,e in ipairs(plan.events) do e.fade_ms=X.settings.fade_ms end
            request.plan=folder..'/reviewed.json'; check(J.write(request.plan,plan))
            local signature=J.encode(plan)
            check(not old.last_result or old.last_result.signature~=signature,'Questa revisione e\' gia\' applicata: usa Elaborato per il confronto')
            ctx.render_plan=plan; ctx.signature=signature
        else error('Modo worker non valido',0) end
        entries[#entries+1]={folder=folder,request=request,snapshot=s,ctx=ctx,root=root}
    end
    check(#entries>0,'Seleziona almeno un item audio')
    X.project=current_project(); X.entries=entries; X.index=1; X.mode=mode; X.cancelled=false
    launch(entries[1]); X.busy=true; X.status=mode=='analyze' and 'Analisi in corso...' or 'Elaborazione in corso...'
end
function X.selected_items()
    local items={}; for i=0,reaper.CountSelectedMediaItems(0)-1 do items[#items+1]=reaper.GetSelectedMediaItem(0,i) end
    return items
end
local function commit_analysis(entries)
    local contexts={}
    for _,entry in ipairs(entries) do
        local plan,err=J.read(entry.result.plan); check(plan,err)
        local previous=entry.ctx.state
        check(not previous or previous.source_sha256==entry.result.source_sha256,'La sorgente e\' cambiata: revisione precedente conservata, usa una nuova take')
        if previous then X.sync_markers(entry.ctx) end
        local s=entry.snapshot
        local outside=merge_review(plan,previous,s.off,entry.result.frames)
        entry.ctx.state={schema=1,take_guid=s.take_guid,source_sha256=entry.result.source_sha256,
            offset=s.off,length=s.len,frames=entry.result.frames,
            prepared_rel=relative(entry.result.prepared,entry.root),plan=plan,outside_review=outside}
        contexts[#contexts+1]=entry.ctx
    end
    transaction('analisi e marcatori take',contexts,function()
        for _,ctx in ipairs(contexts) do put(ctx.take,KEY,ctx.state); write_markers(ctx) end
    end)
end
local function copy_take_attributes(from,to)
    for _,name in ipairs{'D_VOL','D_PAN','D_PANLAW','D_PITCH','B_PPITCH','I_CHANMODE','I_PITCHMODE','I_CUSTOMCOLOR'} do
        check(reaper.SetMediaItemTakeInfo_Value(to,name,reaper.GetMediaItemTakeInfo_Value(from,name)),'Copia proprieta take fallita')
    end
    for i=0,reaper.TakeFX_GetCount(from)-1 do reaper.TakeFX_CopyToTake(from,i,to,i,false) end
    check(reaper.TakeFX_GetCount(to)==reaper.TakeFX_GetCount(from),'Copia FX incompleta')
end
local function map_frame(report,frame)
    for _,seg in ipairs(report.time_map) do
        if frame>=seg.original_start and frame<seg.original_end then
            return seg.edited_start+frame-seg.original_start,false
        end
    end
    local last=0
    for _,seg in ipairs(report.time_map) do if seg.original_end<=frame then last=math.max(last,seg.edited_end) end end
    return last,true
end
X.map_frame=map_frame
local function commit_render(entries)
    local contexts={}
    for _,entry in ipairs(entries) do
        local report,err=J.read(entry.result.report); check(report,err)
        check(report.rejected_events==0,'Il motore ha rifiutato alcuni interventi: nessuna take applicata')
        check(report.edited_frames>0,'L\'intervento eliminerebbe l\'intera take')
        entry.report=report; contexts[#contexts+1]=entry.ctx
        local source=reaper.PCM_Source_CreateFromFile(entry.result.output)
        check(source,'Il WAV elaborato non e\' apribile in REAPER')
        entry.media_source=source
    end
    transaction('applica interventi su nuove take',contexts,function()
        for _,entry in ipairs(entries) do
            local ctx,state,report=entry.ctx,entry.ctx.state,entry.report
            local take=reaper.AddTakeToMediaItem(ctx.item); check(take,'Creazione nuova take fallita')
            check(reaper.SetMediaItemTake_Source(take,entry.media_source),'Assegnazione WAV fallita')
            entry.media_source=nil -- owned by the take now
            copy_take_attributes(ctx.take,take)
            check(reaper.SetMediaItemTakeInfo_Value(take,'D_STARTOFFS',0))
            check(reaper.SetMediaItemTakeInfo_Value(take,'D_PLAYRATE',1))
            local sr=state.plan.sample_rate; local length=report.edited_frames/sr
            check(reaper.SetMediaItemInfo_Value(ctx.item,'D_LENGTH',length))
            local _,name=reaper.GetSetMediaItemTakeInfo_String(ctx.take,'P_NAME','',false)
            check(reaper.GetSetMediaItemTakeInfo_String(take,'P_NAME',(name or 'Voce')..' [NL Breath Click Studio]',true))
            for i=0,reaper.GetNumTakeMarkers(ctx.take)-1 do
                local pos,label,color=reaper.GetTakeMarker(ctx.take,i)
                -- User take markers follow TimeMap; source time becomes segment time.
                if not marker_id(label) and pos>=state.offset and pos<=state.offset+state.length then
                    local frame,removed=map_frame(report,math.floor((pos-state.offset)*sr+0.5))
                    check(reaper.SetTakeMarker(take,-1,label..(removed and ' [dentro taglio]' or ''),frame/sr,color)>=0)
                end
            end
            local link={owner_guid=state.take_guid,original_length=state.length,edited_length=length,report_rel=relative(entry.result.report,entry.root)}
            put(take,LINK,link)
            state.last_result={take_guid=take_guid(take),length=length,signature=ctx.signature}
            -- Persist the exact editorial plan used by the shared engine.
            state.plan=ctx.render_plan
            put(ctx.take,KEY,state)
            reaper.SetActiveTake(take)
        end
    end)
end
function X.cancel()
    if not X.busy then return end
    X.cancelled=true
    local entry=X.entries[X.index]
    local f=io.open(entry.folder..'/cancel','wb'); if f then f:write('cancel'); f:close() end
    X.status='Annullamento in corso...'
end
function X.tick()
    if X.preview_end then
        if current_project()~=X.preview_project or reaper.GetPlayState()==0 or
            reaper.GetPlayPosition()>=X.preview_end then X.stop_preview() end
    end
    if not X.busy then return end
    local ok,err=xpcall(function()
        if current_project()~=X.project then X.cancel() end
        local entry=X.entries[X.index]
        if reaper.time_precise()-entry.started>930 then X.cancel(); error('Worker non risponde: progetto non modificato') end
        if not reaper.file_exists(entry.folder..'/result.json') then return end
        local result,msg=J.read(entry.folder..'/result.json'); check(result,msg)
        if X.cancelled or result.status=='cancelled' then X.busy=false; X.status='Annullato. Take e audio originali intatti.'; return end
        check(result.status=='complete',result.error or 'Worker fallito')
        validate(entry.snapshot); entry.result=result
        if X.index<#X.entries then
            X.index=X.index+1; launch(X.entries[X.index]); X.status=string.format('%s %d/%d...',X.mode=='analyze' and 'Analisi' or 'Elaborazione',X.index,#X.entries)
        else
            for _,e in ipairs(X.entries) do validate(e.snapshot) end
            if X.on_complete then X.on_complete(X.entries)
            elseif X.mode=='analyze' then commit_analysis(X.entries) else commit_render(X.entries) end
            X.busy=false
            if X.mode~='learning' then X.status=X.mode=='analyze' and 'Analisi pronta. Correggi i bordi con mouse o selezione temporale.' or
                'Intervento applicato sulla stessa take. Originale/Elaborato per A/B; Ctrl+Z annulla.' end
        end
    end,debug.traceback)
    if not ok then
        for _,e in ipairs(X.entries or {}) do if e.media_source then reaper.PCM_Source_Destroy(e.media_source); e.media_source=nil end end
        X.busy=false; X.status=tostring(err); X.last_error=tostring(err)
    end
end
function X.compare(processed)
    check(not X.busy,'Attendi il lavoro corrente')
    if X.preview_end then X.stop_preview() end
    local ctx=X.context(); check(ctx and ctx.state and ctx.state.last_result,'Nessun elaborato disponibile')
    local target=processed and reaper.GetMediaItemTakeByGUID(0,ctx.state.last_result.take_guid) or ctx.take
    check(target and reaper.GetMediaItemTake_Item(target)==ctx.item,'Take confronto mancante')
    transaction(processed and 'ascolta Elaborato' or 'ascolta Originale',{ctx},function()
        reaper.SetActiveTake(target)
        check(reaper.SetMediaItemInfo_Value(ctx.item,'D_LENGTH',processed and ctx.state.last_result.length or ctx.state.length))
    end)
end
function X.undo(redo)
    check(not X.busy,'Attendi o annulla il worker')
    local desc=redo and reaper.Undo_CanRedo2(0) or reaper.Undo_CanUndo2(0)
    local prefix='NL Breath Click Studio:'
    check(desc and (desc:sub(1,#prefix)==prefix or desc:sub(1,12)=='BreathClick:'),'L\'ultima operazione non e\' di NL Breath Click Studio: usa Undo/Redo di REAPER')
    if redo then reaper.Undo_DoRedo2(0) else reaper.Undo_DoUndo2(0) end
end
-- Direct editing extends the existing jobs, rollback, preflight and transport.
dofile(here..'/bc_direct.lua')(X,{get=get,put=put,check=check,guid=guid,take_guid=take_guid,
    snapshot=snapshot,transaction=transaction,new_job=new_job,launch=launch,relative=relative,
    merge_review=merge_review,project=current_project,KEY=KEY,LINK=LINK})
return X
