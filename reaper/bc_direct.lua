-- SPDX-License-Identifier: MPL-2.0
-- Sample coordinates always refer to the immutable prepared original. The
-- native renderer receives the full applied set once, never processed audio.
return function(X,H)
local J,check=X.J,H.check
local function clone(value) return J.decode(J.encode(value)) end
local function round(n) return math.floor(n+.5) end
local function same(a,b) return a:gsub('\\','/'):lower()==b:gsub('\\','/'):lower() end
local function path(take) return reaper.GetMediaSourceFileName(reaper.GetMediaItemTake_Source(take),'') end
local function absolute(rel)
    if rel:match('^%a:[/\\]') or rel:sub(1,1)=='/' then return rel end
    return X.project_path()..'/'..rel
end
X.media_path=absolute
local function identity(state)
    return {original_frames=state.frames,edited_frames=state.frames,
        time_map=J.array{{original_start=0,original_end=state.frames,edited_start=0,edited_end=state.frames}}}
end
local function mapping(ctx) return ctx.residual and identity(ctx.state) or ctx.processed and ctx.state.result.map or identity(ctx.state) end
local function forward(map,frame)
    if frame==map.original_frames then return map.edited_frames,false end
    return X.map_frame(map,frame)
end
local function inverse(map,frame,left)
    if frame>=map.edited_frames then return map.original_frames,false end
    if left then for _,s in ipairs(map.time_map) do if frame==s.edited_end then return s.original_end,false end end end
    for _,s in ipairs(map.time_map) do
        if frame>=s.edited_start and frame<s.edited_end then return s.original_start+frame-s.edited_start,false end
    end
    local best=0
    for _,s in ipairs(map.time_map) do if s.edited_end<=frame then best=math.max(best,s.original_end) end end
    return best,true
end
X.inverse_frame=inverse
local legacy_context=X.context
function X.context(item)
    local ctx=legacy_context(item); if not ctx or not ctx.state then return ctx end
    local st=ctx.state
    if st.schema~=2 then
        -- Import P1 preview review, keeping old take versions available to Undo.
        st.schema=2; st.source_path=path(ctx.take); st.rate=1
        st.user_markers=J.array(); st.next_marker=1
        for i=0,reaper.GetNumTakeMarkers(ctx.take)-1 do
            local p,n,c=reaper.GetTakeMarker(ctx.take,i)
            if not n:match('^BC%[') then st.user_markers[#st.user_markers+1]={frame=round((p-st.offset)*st.plan.sample_rate),name=n,color=c} end
        end
        if st.last_result then
            local take=reaper.GetMediaItemTakeByGUID(0,st.last_result.take_guid)
            local link=take and H.get(take,H.LINK)
            local report=link and J.read(absolute(link.report_rel))
            if report then
                local ok,rendered=pcall(J.decode,st.last_result.signature or '')
                check(ok and rendered.events,'La vecchia preview non contiene il piano applicato: scegli la take originale e rianalizza')
                local applied={}
                for _,e in ipairs(rendered.events) do if e.approved and e.enabled then applied[e.bcs_id]=true end end
                st.result={path=path(take),map=report}
                for _,e in ipairs(st.plan.events) do e.bcs_applied=applied[e.bcs_id] or false end
            end
        end
    end
    st.take_guid=H.take_guid(ctx.take) -- split items receive their own native GUID
    ctx.residual=st.residual and same(path(ctx.active),st.residual.path) or false
    ctx.processed=ctx.residual or st.result and same(path(ctx.active),st.result.path) or false
    check(ctx.processed or same(path(ctx.active),st.source_path),'Sorgente sostituita fuori da BreathClick: revisione conservata, analizza una nuova take')
    return ctx
end
function X.state(item) local ctx=X.context(item); return ctx and ctx.state,ctx end
function X.view(ctx)
    local st,sr=ctx.state,ctx.state.plan.sample_rate
    local off=reaper.GetMediaItemTakeInfo_Value(ctx.active,'D_STARTOFFS')
    local rate=reaper.GetMediaItemTakeInfo_Value(ctx.active,'D_PLAYRATE')
    local length=reaper.GetMediaItemInfo_Value(ctx.item,'D_LENGTH')
    local a,b
    if ctx.processed then
        local m=mapping(ctx)
        a=inverse(m,round(off*sr),false); b=inverse(m,round((off+length*rate)*sr),true)
        local v=st.view
        if v and math.abs(off-v.offset)<1/sr and math.abs(length*rate*sr-v.count)<.6 then a,b=v.first,v.last end
    else a=round((off-st.offset)*sr); b=a+round(length*rate*sr) end
    check(a>=0 and b>a and b<=st.frames+1,'Trim fuori dalla copia originale: espandi soltanto entro l\'intervallo analizzato')
    return a,math.min(st.frames,b),rate
end
function X.event_interval(ctx,event)
    local sr=ctx.state.plan.sample_rate
    local offset=reaper.GetMediaItemTakeInfo_Value(ctx.active,'D_STARTOFFS')
    local rate=reaper.GetMediaItemTakeInfo_Value(ctx.active,'D_PLAYRATE')
    local a,b=event.start,event['end']
    if ctx.processed then a=forward(mapping(ctx),a); b=forward(mapping(ctx),b)
    else offset=offset-ctx.state.offset end
    return (a/sr-offset)/rate,(b/sr-offset)/rate
end
local function from_timeline(ctx,time,left)
    local st,sr=ctx.state,ctx.state.plan.sample_rate
    local pos=reaper.GetMediaItemInfo_Value(ctx.item,'D_POSITION')
    local rate=reaper.GetMediaItemTakeInfo_Value(ctx.active,'D_PLAYRATE')
    local off=reaper.GetMediaItemTakeInfo_Value(ctx.active,'D_STARTOFFS')
    local f=round((off+(time-pos)*rate)*sr)
    if ctx.processed then return inverse(mapping(ctx),f,left) end
    return f-round(st.offset*sr)
end
local function short_ids(st)
    st.next_marker=st.next_marker or 1
    for _,e in ipairs(st.plan.events) do
        if not e.bcs_short then e.bcs_short=st.next_marker; st.next_marker=st.next_marker+1 end
    end
end
local function marker_key(name,st)
    local id,edge=name:match('^BC%[([%w%-]+)%]:([AB]) ')
    if id then return id,edge end
    local number; number,edge=name:match('^BC(%d+) ([AB])')
    if number then for _,e in ipairs(st.plan.events) do if e.bcs_short==tonumber(number) then return e.bcs_id,edge end end end
end
local function write_markers(ctx)
    local st,sr=ctx.state,ctx.state.plan.sample_rate; short_ids(st)
    -- All user markers are stored in original source samples and remapped too.
    for i=reaper.GetNumTakeMarkers(ctx.take)-1,0,-1 do reaper.DeleteTakeMarker(ctx.take,i) end
    local map=mapping(ctx)
    for _,m in ipairs(st.user_markers or {}) do
        if not ctx.processed or (m.frame>=0 and m.frame<=st.frames) then
            local f=ctx.processed and forward(map,m.frame) or m.frame
            local position=f/sr+(ctx.processed and 0 or st.offset)
            check(reaper.SetTakeMarker(ctx.take,-1,m.name,position,m.color)>=0,'Marcatore utente non ripristinato')
        end
    end
    for _,e in ipairs(st.plan.events) do
        if e.approved or e.bcs_applied then
            local a,b=e.start,e['end']
            if ctx.processed then a=forward(map,a); b=forward(map,b) end
            local base=ctx.processed and 0 or st.offset
            local suffix=e.bcs_applied and ' fatto' or ' proposto'
            local color=reaper.ColorToNative(e.bcs_applied and 80 or 235,e.bcs_applied and 190 or 195,70)|0x1000000
            check(reaper.SetTakeMarker(ctx.take,-1,string.format('BC%02d A',e.bcs_short)..suffix,base+a/sr,color)>=0)
            check(reaper.SetTakeMarker(ctx.take,-1,string.format('BC%02d B',e.bcs_short),base+b/sr,color)>=0)
        end
    end
end
function X.sync_markers(ctx,allow)
    if not ctx.state then return end
    local st,sr=ctx.state,ctx.state.plan.sample_rate; short_ids(st)
    local found,users={},J.array()
    local previous_users,matched=st.user_markers or {},{}
    local marker_take=ctx.take~=ctx.active and ctx.take or ctx.active
    local marker_processed=ctx.processed and marker_take==ctx.active
    for i=0,reaper.GetNumTakeMarkers(marker_take)-1 do
        local p,n,c=reaper.GetTakeMarker(marker_take,i); local id,edge=marker_key(n,st)
        if id then found[id]=found[id] or {}; check(not found[id][edge],'Bordo duplicato'); found[id][edge]=round((p-(marker_processed and 0 or st.offset))*sr)
        else
            local f=marker_processed and inverse(mapping(ctx),round(p*sr),false) or round((p-st.offset)*sr)
            -- A cue inside a cut maps to a seam. Keep its stable original
            -- anchor if it has not actually moved; inverse(seam) is ambiguous.
            for old_index,old in ipairs(previous_users) do
                local expected=marker_processed and forward(mapping(ctx),old.frame) or old.frame
                if not matched[old_index] and old.name==n and old.color==c and round((p-(marker_processed and 0 or st.offset))*sr)==expected then
                    f=old.frame; matched[old_index]=true; break
                end
            end
            users[#users+1]={frame=f,name=n,color=c}
        end
    end
    if marker_processed then
        for _,old in ipairs(previous_users) do
            if old.frame<0 or old.frame>st.frames then users[#users+1]=old end
        end
    end
    st.user_markers=users
    for _,e in ipairs(st.plan.events) do
        local pair=found[e.bcs_id]
        if pair and pair.A and pair.B then
            local expectedA,expectedB=e.start,e['end']
            if marker_processed then expectedA=forward(mapping(ctx),expectedA); expectedB=forward(mapping(ctx),expectedB) end
            -- Do not round-trip collapsed cuts through an ambiguous inverse.
            if pair.A~=expectedA or pair.B~=expectedB then
                local a,b=pair.A,pair.B
                if marker_processed then a=inverse(mapping(ctx),a,false); b=inverse(mapping(ctx),b,true) end
                check(a>=0 and b>a and b<=st.frames,'Bordi fuori item o invertiti')
                e.start=a; e['end']=b; e.bcs_edited=true
            end
        elseif e.approved or e.bcs_applied then
            if not allow then check(false,'Bordo mancante: usa Usa selezione o Bordi per ripristinarlo') end
            ctx.incomplete=ctx.incomplete or {}; ctx.incomplete[e.bcs_id]=true
        end
    end
end
local function context()
    check(not X.busy,'Attendi o annulla il lavoro corrente')
    local ctx=X.context(); check(ctx and ctx.state,'Analizza prima la take')
    X.view(ctx); X.sync_markers(ctx,true); return ctx
end
function X.review(mutator)
    local ctx=context()
    H.transaction('revisione respiri',{ctx},function()
        mutator(ctx.state,ctx); short_ids(ctx.state)
        H.put(ctx.take,H.KEY,ctx.state); write_markers(ctx)
    end)
end
local function selected_events(state,indices,fn)
    local count=0
    for i in pairs(indices) do local e=state.plan.events[i]; if e then fn(e); count=count+1 end end
    check(count>0,'Seleziona un respiro nella lista o con la selezione temporale')
end
local start_render
function X.include(indices,included)
    local ctx=context(); local st=clone(ctx.state); local rendered=false
    selected_events(st,indices,function(e)
        e.approved=included; e.bcs_edited=true
        if not included and e.bcs_applied then e.bcs_applied=false; rendered=true end
        -- An editorial decision never erases a separate explicit human label.
    end)
    if rendered then start_render({ctx},{st},'esclusione editoriale')
    else X.review(function(s) s.plan=st.plan end) end
end
function X.set_mode(indices,mode)
    X.review(function(st) selected_events(st,indices,function(e) e.mode=mode; e.bcs_edited=true end) end)
end
function X.apply_selected(indices,mode)
    local ctx=context(); local st=clone(ctx.state)
    selected_events(st,indices,function(e)
        check(({attenuate=true,silence=true,cut=true,halve=true,repair=true})[mode],'Modo non valido')
        local a,b=X.view(ctx); check(e.start>=a and e['end']<=b,'Intervento oltre il trim: correggi i bordi prima di applicare')
        check(e.kind~=1 or (mode~='cut' and mode~='halve'),'I click non accorciano la durata: usa Ripara click')
        if mode=='repair' then check(e.kind==1,'Seleziona un click');e.repair=true;e.manual_operation=false end
        if e.kind==1 and mode~='repair' then e.repair=false end
        e.mode=mode=='repair' and 'attenuate' or mode; e.approved=true; e.enabled=true; e.bcs_applied=true; e.bcs_edited=true
        e.bcs_declined=nil
        e.event_reduction_db=X.settings.reduction_db; e.fade_ms=X.settings.fade_ms
    end)
    start_render({ctx},{st},mode)
end
function X.add_manual(label,kind)
    local created=H.guid()
    X.review(function(st,ctx)
        local a,b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
        local pos,len=reaper.GetMediaItemInfo_Value(ctx.item,'D_POSITION'),reaper.GetMediaItemInfo_Value(ctx.item,'D_LENGTH')
        check(a>=pos and b>a and b<=pos+len+1e-9,'Seleziona un intervallo dentro l\'item')
        local first,last=from_timeline(ctx,a,false),from_timeline(ctx,b,true)
        check(last>first,'La selezione cade interamente in una pausa generata')
        st.plan.events[#st.plan.events+1]={kind=kind or 0,repair=kind==1,start=first,['end']=last,mode='attenuate',approved=true,enabled=true,
            state='manual',source='manual',classification='manual',manual_operation=label~='missed_breath' and label~='missed_click',bcs_manual=true,
            decision_source='user',training_eligible=label=='missed_breath' or label=='missed_click',learning_label=label or '',
            fade_ms=X.settings.fade_ms,keep_fraction=.5,bcs_id=created,bcs_edited=true,score=0}
    end)
    return created
end
function X.set_bounds(indices,a,b)
    local ctx=context(); local st=clone(ctx.state); local applied=false
    selected_events(st,indices,function(e)
        local first,last=X.view(ctx)
        check(a>=first and b>a and b<=last,'Bordi fuori dall\'item visibile')
        e.start=a; e['end']=b; e.bcs_edited=true; applied=applied or e.bcs_applied
    end)
    if applied then start_render({ctx},{st},'correzione bordi')
    else X.review(function(s) s.plan=st.plan end) end
end
function X.bounds_from_selection(indices)
    local n=0; for _ in pairs(indices) do n=n+1 end
    check(n==1,'Seleziona un solo respiro per correggerne i bordi')
    local ctx=context(); local a,b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
    check(b>a,'Trascina una selezione temporale in REAPER')
    X.set_bounds(indices,from_timeline(ctx,a,false),from_timeline(ctx,b,true))
end
function X.label(indices,label)
    local ctx=context(); local st=clone(ctx.state); local render=false
    selected_events(st,indices,function(e)
        local click=label=='click_confirmed' or label=='click_false_positive' or label=='missed_click' or label=='mouth_click_confirmed' or label=='vocal_transient'
        check(label=='editorial_exclusion' or (click and e.kind==1) or (not click and e.kind==0),'Etichetta incompatibile con la categoria selezionata')
        check((label~='missed_breath' and label~='missed_click') or e.bcs_manual or e.source=='manual','Per un mancato crea prima una regione dalla selezione')
        e.state=(label=='false_positive' or label=='click_false_positive' or label=='vocal_transient') and 'rejected' or label=='editorial_exclusion' and 'excluded' or 'confirmed'
        e.learning_label=label; e.training_eligible=label~='editorial_exclusion'; e.decision_source='user'; e.bcs_edited=true
        if label~='editorial_exclusion' then e.manual_operation=false end
        if label=='false_positive' or label=='click_false_positive' or label=='vocal_transient' or label=='editorial_exclusion' then
            e.approved=false; render=render or e.bcs_applied; e.bcs_applied=false
        else e.approved=true; e.enabled=true; e.manual_operation=false end
    end)
    if render then start_render({ctx},{st},'etichetta '..label)
    else X.review(function(s) s.plan=st.plan end) end
end
function X.repair_method(indices,method)
    check(method=='auto' or method=='linear' or method=='predictive','Algoritmo non valido')
    local ctx=context();local st=clone(ctx.state);local applied=false
    selected_events(st,indices,function(e)
        check(e.kind==1,'Seleziona un click');e.repair_method=method;e.repair=true;e.bcs_edited=true;applied=applied or e.bcs_applied
    end)
    if applied then start_render({ctx},{st},'algoritmo de-click') else X.review(function(s)s.plan=st.plan end) end
end
function X.select_event(index,play)
    local ctx=X.context(); local event=ctx and ctx.state and ctx.state.plan.events[index]; check(event,'Seleziona un respiro')
    local pos=reaper.GetMediaItemInfo_Value(ctx.item,'D_POSITION'); local a,b=X.event_interval(ctx,event)
    local length=reaper.GetMediaItemInfo_Value(ctx.item,'D_LENGTH')
    a=math.max(0,math.min(length,a)); b=math.max(a,math.min(length,b))
    reaper.GetSet_LoopTimeRange2(0,true,false,pos+a,pos+b,false)
    reaper.SetEditCurPos(math.max(pos,pos+a-.15),true,false)
    if play then
        X.preview_end=math.min(pos+length,pos+b+.2); X.preview_project=H.project()
        X.preview_repeat=reaper.GetSetRepeatEx(X.preview_project,-1); reaper.GetSetRepeatEx(X.preview_project,0)
        reaper.OnPlayButtonEx(X.preview_project)
    end
end
local function queue(entries,mode,commit,label)
    check(#entries>0,'Seleziona almeno un item audio')
    X.project=H.project(); X.entries=entries; X.index=1; X.mode=mode; X.cancelled=false; X.last_error=nil
    for _,entry in ipairs(entries) do entry.request.direct=true end
    X.on_complete=commit; X.commit_label=label
    X.completion_status=nil
    H.launch(entries[1]); X.busy=true; X.status=mode=='analyze' and 'Analisi in corso...' or 'Aggiornamento della take in corso...'
end
local function state_for(s,entry,plan)
    local old=entry.ctx.state
    if old then
        check(old.source_sha256==entry.result.source_sha256,'Originale cambiato: revisione conservata')
        H.merge_review(plan,old,old.offset,old.frames)
        for _,e in ipairs(plan.events) do if e.enabled==false then e.approved=false end end
        old.plan=plan; short_ids(old); return old
    end
    local st={schema=2,take_guid=s.take_guid,source_path=s.sorgente,source_sha256=entry.result.source_sha256,
        offset=s.off,rate=s.rate,length=s.len,frames=entry.result.frames,prepared_rel=H.relative(entry.result.prepared,entry.root),
        plan=plan,user_markers=J.array(),next_marker=1}
    for _,e in ipairs(plan.events) do
        e.bcs_id=H.guid(); e.approved=e.enabled~=false and e.classification~='speech_overlap' and e.classification~='mouth_click' and e.classification~='vocal_transient'; e.training_eligible=false; e.decision_source='automatic'
    end
    for i=0,reaper.GetNumTakeMarkers(s.take)-1 do
        local p,n,c=reaper.GetTakeMarker(s.take,i)
        st.user_markers[#st.user_markers+1]={frame=round((p-st.offset)*plan.sample_rate),name=n,color=c}
    end
    short_ids(st); return st
end
local function commit_analysis(entries)
    local contexts={}
    for _,entry in ipairs(entries) do
        local plan,err=J.read(entry.result.plan); check(plan,err)
        entry.ctx.state=state_for(entry.snapshot,entry,plan); contexts[#contexts+1]=entry.ctx
    end
    H.transaction('analisi e marcatori',contexts,function()
        for _,ctx in ipairs(contexts) do H.put(ctx.take,H.KEY,ctx.state); write_markers(ctx) end
    end)
end
local function validate_chunk(entry)
    local ok,chunk=reaper.GetItemStateChunk(entry.snapshot.item,'',false)
    check(ok and chunk==entry.snapshot.chunk_originale,'Item, FX o revisione cambiati durante il render: risultato non applicato')
end
local function install_source(ctx,sourcepath,processed,first,last,rate)
    local st,sr=ctx.state,ctx.state.plan.sample_rate
    local source=reaper.PCM_Source_CreateFromFile(sourcepath); check(source,'Sorgente non apribile')
    local previous=reaper.GetMediaItemTake_Source(ctx.take)
    if not reaper.SetMediaItemTake_Source(ctx.take,source) then reaper.PCM_Source_Destroy(source); error('Sostituzione sorgente fallita',0) end
    -- REAPER explicitly leaves the replaced PCM_source owned by the caller.
    -- Release only a detached source, never one referenced by another take.
    local referenced=false
    if previous then
        local p=0
        while true do
            local project=reaper.EnumProjects(p,''); if not project then break end
            for i=0,reaper.CountMediaItems(project)-1 do
                local item=reaper.GetMediaItem(project,i)
                for t=0,reaper.CountTakes(item)-1 do
                    if reaper.GetMediaItemTake_Source(reaper.GetTake(item,t))==previous then referenced=true end
                end
            end
            p=p+1
        end
        if not referenced then reaper.PCM_Source_Destroy(previous) end
    end
    ctx.active=ctx.take; ctx.processed=processed; reaper.SetActiveTake(ctx.take)
    local a,b=first,last
    if processed then a=forward(mapping(ctx),a); b=forward(mapping(ctx),b) end
    check(b>a,'L\'intervento eliminerebbe l\'intero item visibile')
    local off=(processed and 0 or st.offset)+a/sr
    check(reaper.SetMediaItemTakeInfo_Value(ctx.take,'D_STARTOFFS',off))
    check(reaper.SetMediaItemTakeInfo_Value(ctx.take,'D_PLAYRATE',rate))
    check(reaper.SetMediaItemInfo_Value(ctx.item,'D_LENGTH',(b-a)/(sr*rate)))
    st.view={first=first,last=last,offset=off,count=b-a}
    H.put(ctx.take,H.KEY,st); write_markers(ctx)
    reaper.UpdateItemInProject(ctx.item)
end
local function commit_render(entries)
    local contexts={};local declined=0
    for _,entry in ipairs(entries) do
        validate_chunk(entry)
        local report,err=J.read(entry.result.report); check(report,err)
        check(report.rejected_events==0 and report.edited_frames>0,'Il motore ha rifiutato il piano: take conservata')
        for _,op in ipairs(report.operations or {}) do
            if op.declined then
                for _,e in ipairs(entry.next_state.plan.events) do
                    if e.kind==op.kind and e.start==op.start and e['end']==op['end'] then e.bcs_applied=false;e.bcs_declined=op.note end
                end
            end
        end
        -- A declined auto repair remains a proposal, never a training decision.
        if (report.declined_events or 0)>0 then entry.next_state.signature=nil end
        declined=declined+(report.declined_events or 0)
        entry.next_state.result={path=entry.result.output,map=report}
        contexts[#contexts+1]=entry.ctx
    end
    H.transaction(X.commit_label or 'applica respiro',contexts,function()
        for _,entry in ipairs(entries) do
            entry.ctx.state=entry.next_state;entry.ctx.residual=false;entry.ctx.state.residual=nil
            install_source(entry.ctx,entry.result.output,true,entry.first,entry.last,entry.rate)
        end
    end)
    if declined>0 then X.completion_status=string.format('%d click conservati: previsione incerta. Altri interventi applicati; confronta le alternative in anteprima.',declined) end
end
start_render=function(contexts,states,label)
    local entries={}; local root=X.project_path()
    for i,ctx in ipairs(contexts) do
        local s=H.snapshot(ctx.item); local st=states[i]; local a,b,rate=X.view(ctx)
        local folder=H.new_job(root); local plan=clone(st.plan)
        plan.source=absolute(st.prepared_rel); plan.source_relative=''
        plan.preserve_duration=(st.timing or 'compact')=='sync'
        for _,e in ipairs(plan.events) do e.approved=e.bcs_applied==true; e.enabled=e.approved end
        local signature=J.encode(plan)
        check(not st.signature or st.signature~=signature,'Questo intervento e\' gia\' applicato; nessun secondo passaggio')
        st.signature=signature
        local planpath=folder..'/applied.json'; check(J.write(planpath,plan))
        entries[#entries+1]={snapshot=s,ctx=ctx,next_state=st,root=root,folder=folder,first=a,last=b,rate=rate,
            request={mode='render',exe=X.engine(),source=st.source_path,source_sha256=st.source_sha256,prepared=plan.source,plan=planpath}}
    end
    queue(entries,'render',commit_render,label)
end
function X.begin(mode,items)
    check(not X.busy and reaper.GetPlayState()==0,'Ferma il trasporto e attendi il worker')
    local root=X.project_path(); local entries,contexts,states={},{},{}
    for _,item in ipairs(items) do
        local ctx=X.context(item); local s=H.snapshot(item)
        if mode=='render' then
            check(ctx and ctx.state,'Analizza prima la take')
            X.sync_markers(ctx); local st=clone(ctx.state); local a,b=X.view(ctx)
            for _,e in ipairs(st.plan.events) do
                if e.start>=a and e['end']<=b then
                    e.bcs_applied=e.approved and e.enabled; e.bcs_edited=true
                    if e.bcs_applied then e.event_reduction_db=e.event_reduction_db or X.settings.reduction_db end
                end
            end
            contexts[#contexts+1]=ctx; states[#states+1]=st
        else
            check(mode=='analyze','Modo non valido')
            if ctx.state then X.sync_markers(ctx); X.view(ctx) end
            local st=ctx.state; local folder=H.new_job(root)
            local request={mode='analyze',exe=X.engine(),source=st and st.source_path or s.sorgente,
                offset=s.off,duration=s.len*s.rate,detector=X.settings.detector,reduction_db=X.settings.reduction_db,fade_ms=X.settings.fade_ms}
            if st then request.prepared=absolute(st.prepared_rel); request.source_sha256=st.source_sha256 end
            request.detect_breath=X.settings.detect_breath~=false
            request.detect_clicks=X.settings.detect_clicks==true
            request.click_sensitivity=X.settings.click_sensitivity or .5
            if X.settings.profile_active then request.profile=X.settings.profile_path end
            entries[#entries+1]={snapshot=s,ctx=ctx,root=root,folder=folder,request=request}
        end
    end
    if mode=='render' then start_render(contexts,states,'applica batch') else queue(entries,mode,commit_analysis) end
end
function X.set_timing(mode)
    check(mode=='compact' or mode=='sync','Modalita\' temporale non valida')
    local ctx=context(); local st=clone(ctx.state); st.timing=mode
    if st.result then start_render({ctx},{st},mode=='sync' and 'mantieni sincronizzazione' or 'compatta voce')
    else X.review(function(s) s.timing=mode end) end
end
function X.compare(processed)
    check(not X.busy,'Attendi il worker'); X.stop_preview()
    local ctx=context(); check(ctx.state.result,'Nessun intervento applicato')
    if ctx.processed==processed and not ctx.residual then return end
    local a,b,rate=X.view(ctx)
    H.transaction(processed and 'ascolta Elaborato' or 'ascolta Originale',{ctx},function()
        ctx.residual=false
        install_source(ctx,processed and ctx.state.result.path or ctx.state.source_path,processed,a,b,rate)
    end)
end
function X.difference()
    check(not X.busy and reaper.GetPlayState()==0,'Ferma il trasporto e attendi il worker')
    local ctx=context();check(ctx.state.result,'Applica prima un intervento')
    local root=X.project_path();local folder=H.new_job(root);local st=clone(ctx.state);local plan=clone(st.plan)
    local a,b,rate=X.view(ctx)
    plan.source=absolute(st.prepared_rel);plan.source_relative='';plan.preserve_duration=(st.timing or 'compact')=='sync'
    for _,e in ipairs(plan.events) do e.approved=e.bcs_applied==true;e.enabled=e.approved end
    local planpath=folder..'/difference.json';check(J.write(planpath,plan))
    local entry={snapshot=H.snapshot(ctx.item),ctx=ctx,folder=folder,root=root,
        request={mode='difference',exe=X.engine(),source=st.source_path,source_sha256=st.source_sha256,prepared=plan.source,plan=planpath}}
    queue({entry},'difference',function(entries)
        validate_chunk(entries[1])
        H.transaction('ascolta solo rimosso',{ctx},function()
            ctx.state=st;st.residual={path=entries[1].result.output};ctx.residual=true
            install_source(ctx,st.residual.path,true,a,b,rate)
        end)
        X.status='SOLO RIMOSSO: differenza allineata all originale; Originale/Elaborato per tornare'
    end)
end
function X.export_or_train(train)
    check(not X.busy,'Attendi il worker')
    local root=X.project_path(); local folder=H.new_job(root); local plans=J.array(); local seen={}
    local items=train and X.selected_items() or {reaper.GetSelectedMediaItem(0,0)}
    for _,item in ipairs(items) do
        local ctx=X.context(item); local st=ctx and ctx.state
        if st then
            local plan=clone(st.plan); plan.source=st.source_path; plan.source_relative=''; plan.source_sha256=st.source_sha256
            for _,e in ipairs(plan.events) do e.start=e.start+round(st.offset*plan.sample_rate); e['end']=e['end']+round(st.offset*plan.sample_rate) end
            local f=folder..'/labels_'..(#plans+1)..'.json'; check(J.write(f,plan)); plans[#plans+1]=f
        end
    end
    check(#plans>0,'Seleziona item analizzati con etichette esplicite')
    local output
    if train then
        output=folder..'/personal_profile.ini'
    else output=folder..'/labels.bcnstudio.json' end
    local entry={folder=folder,request={mode=train and 'profile-train' or 'labels-export',exe=X.engine(),plans=plans,output=output},
        snapshot=H.snapshot(items[1])}
    queue({entry},'learning',function()
        if train then X.settings.profile_path=output; X.settings.profile_active=false; X.save_settings() end
        X.status=train and ('Profilo validato: '..output..'. Attivalo con Profilo.') or ('Sessione etichette standalone: '..output)
    end)
end
function X.profile()
    if X.settings.profile_path and reaper.file_exists(X.settings.profile_path) then
        X.settings.profile_active=not X.settings.profile_active; X.save_settings()
        X.status=X.settings.profile_active and 'Profilo personale attivo nelle prossime analisi' or 'Rilevatore generale attivo'
    else X.choose_profile() end
end
function X.choose_profile()
    local ok,p=reaper.GetUserFileNameForRead(X.settings.profile_path or '','Apri personal_profile.ini della standalone','ini')
    if ok then X.settings.profile_path=p; X.settings.profile_active=true; X.save_settings() end
end
X.direct=true
end
