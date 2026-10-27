-- SPDX-License-Identifier: MPL-2.0
-- @description NL Breath Click Studio - editing diretto, TimeMap e apprendimento
-- @version 1.0.0-rc.3
-- @author Nicola Lanci Voice Over
-- @link https://www.nicolalanci.com
-- @about Independent Windows x64 beta. ReaPack installs scripts; verified backend installs on first use.
-- @provides
--   [main] BreathClickStudio.lua
--   [nomain] bc_studio_bridge.lua
--   [nomain] bc_direct.lua
--   [nomain] bc_direct_view.lua
--   [nomain] bcnext_core.lua
--   [nomain] BreathClickNext_config.lua
--   [nomain] bc_json.lua
--   [nomain] bc_job.ps1
--   [nomain] bc_runtime.lua
--   [nomain] bc_runtime.ps1
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local X=dofile(here..'/bc_studio_bridge.lua')
local UI={identity={name='NL Breath Click Studio',version='1.0.0-rc.3',adapter_version='1.0.0-rc.3-beta',author='Nicola Lanci Voice Over',email='info@nicolalanci.com',website='www.nicolalanci.com'},bridge=X,selected={},scroll=0,anchor=1,buttons={},last_mouse=0,refresh=-1}
function UI.event_visible(e)
    local f=UI.filter or 0
    if f==0 then return true end
    if f==1 then return e.kind==0 end
    if f==2 then return e.kind==1 end
    if f==4 then return e.enabled~=false and not e.approved and e.decision_source~='user' end
    if f==5 then return e.kind==1 and e.classification~='speech_overlap' and e.classification~='vocal_transient' end
    if f==6 then return e.repair_feedback and e.repair_feedback~='' end
    return e.classification=='ai_short_review' or e.classification=='dsp_fragment_review' or
        e.classification=='speech_overlap' or e.classification=='mouth_click' or
        e.classification=='vocal_transient' or e.classification=='low_level_impulse' or e.classification=='band_limited_click' or e.classification=='isolated_click_review'
end
function UI.step(delta)
    assert(not X.busy,'Attendi il lavoro in corso')
    local ctx=X.context();local events=ctx and ctx.state and ctx.state.plan.events or {};local order={}
    local function less(a,b)
        if events[a].start~=events[b].start then return events[a].start<events[b].start end
        if events[a]['end']~=events[b]['end'] then return events[a]['end']<events[b]['end'] end
        return a<b
    end
    for i,e in ipairs(events) do if UI.event_visible(e) then order[#order+1]=i end end
    table.sort(order,less)
    if #order==0 then X.status='Nessun evento nel filtro corrente';return end
    local current=UI.selected[UI.anchor] and UI.anchor or nil
    if not current then for i in pairs(UI.selected) do if events[i] and (not current or less(i,current)) then current=i end end end
    local index=delta>0 and order[1] or order[#order]
    if current and events[current] then
        if delta>0 then for _,i in ipairs(order) do if less(current,i) then index=i;break end end
        else for n=#order,1,-1 do local i=order[n];if less(i,current) then index=i;break end end end
    end
    if X.preview_end then X.stop_preview() end
    UI.selected={[index]=true};UI.anchor=index;UI.reveal_index=index
    X.select_event(index,false)
    UI.cursor=reaper.GetCursorPosition()
    UI.time_a,UI.time_b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
end
local function invoke(fn)
    local previous=X.status; X.last_error=nil
    local ok,err=xpcall(fn,debug.traceback)
    if not ok then X.status=tostring(err); X.last_error=tostring(err) end
    if ok and X.status==previous then X.status='Pronto. Revisione e stato aggiornati.' end
    UI.refresh=-1
    UI.order_events=nil
end
function UI.action(name)
    if name=='Installa motore' then X.runtime.install(here)
    elseif name=='Informazioni' then UI.about_open=true
    elseif name=='Chiudi info' then UI.about_open=false
    elseif name=='Sito web' or name=='Email' then
        local target=name=='Email' and 'mailto:info@nicolalanci.com' or 'https://www.nicolalanci.com'
        if reaper.CF_ShellExecute then reaper.CF_ShellExecute(target)
        elseif package.config:sub(1,1)=='\\' then reaper.ExecProcess('cmd.exe /d /c start "" "'..target..'"',-2)
        elseif reaper.GetOS():match('OSX') then reaper.ExecProcess('open "'..target..'"',-2)
        else reaper.ExecProcess('xdg-open "'..target..'"',-2) end
    elseif name=='Rileva respiri' then X.settings.detect_breath=X.settings.detect_breath==false;X.save_settings()
    elseif name=='Rileva click' then X.settings.detect_clicks=not X.settings.detect_clicks;X.save_settings()
    elseif name=='Filtro' then UI.filter=((UI.filter or 0)+1)%7;UI.order_events=nil;UI.scroll=0;UI.selected={}
    elseif name=='Precedente' then UI.step(-1)
    elseif name=='Successivo' then UI.step(1)
    elseif name=='Ripara click' then X.apply_selected(UI.selected,'repair')
    elseif name=='Click confermato' then X.label(UI.selected,'click_confirmed')
    elseif name=='Falso click' then X.label(UI.selected,'click_false_positive')
    elseif name=='Schiocco' then X.label(UI.selected,'mouth_click_confirmed')
    elseif name=='Preserva voce' then X.label(UI.selected,'vocal_transient')
    elseif name=='Click mancato' then UI.reveal_id=X.add_manual('missed_click',1)
    elseif name=='Segnala' then
        local choice=gfx.showmenu('Respiro non rilevato|Click non rilevato|Falso positivo|Riparazione insoddisfacente|Cancella giudizio riparazione')
        if choice==1 then UI.reveal_id=X.add_manual('missed_breath')
        elseif choice==2 then UI.reveal_id=X.add_manual('missed_click',1)
        elseif choice==3 then
            local ctx=X.context();local click=false
            for i in pairs(UI.selected) do click=ctx.state.plan.events[i].kind==1;break end
            X.label(UI.selected,click and 'click_false_positive' or 'false_positive')
        elseif choice==4 or choice==5 then X.repair_feedback(UI.selected,choice==4) end
    elseif name=='Algoritmo' then
        local choice=gfx.showmenu('Auto|Lineare|Predittiva (Burg)|Nessuna riparazione')
        if choice>0 then X.repair_method(UI.selected,({'auto','linear','predictive','none'})[choice]) end
    elseif name=='Analizza' then X.begin('analyze',X.selected_items())
    elseif name=='Applica incluse' then X.begin('render',X.selected_items())
    elseif name=='Annulla lavoro' then X.cancel()
    elseif name=='Da selezione' then UI.reveal_id=X.add_manual()
    elseif name=='Includi' then X.include(UI.selected,true)
    elseif name=='Escludi' then X.label(UI.selected,'editorial_exclusion')
    elseif name=='Conferma respiro' then X.label(UI.selected,'breath_confirmed')
    elseif name=='Falso positivo' then X.label(UI.selected,'false_positive')
    elseif name=='Mancato' then
        local ctx=X.context(); local manual
        for i in pairs(UI.selected) do local e=ctx and ctx.state and ctx.state.plan.events[i]; if e and (e.bcs_manual or e.source=='manual') then manual=true end end
        if manual then X.label(UI.selected,'missed_breath') else UI.reveal_id=X.add_manual('missed_breath') end
    elseif name=='Usa selezione' then X.bounds_from_selection(UI.selected)
    elseif name=='Compatta voce' then X.set_timing('compact')
    elseif name=='Mantieni sync' then X.set_timing('sync')
    elseif name=='Esporta etichette' then X.export_or_train(false)
    elseif name=='Crea profilo' then
        local choice=gfx.showmenu('Profilo respiri|Profilo click e schiocchi')
        if choice>0 then X.export_or_train(true,choice==2 and 'click' or 'breath') end
    elseif name=='Profilo' then
        local choice=gfx.showmenu((X.settings.profile_active and '!' or '')..'Profilo respiri|'..(X.settings.click_profile_active and '!' or '')..'Profilo click')
        if choice>0 then X.profile(choice==2 and 'click' or 'breath') end
    elseif name=='Carica profilo' then
        local choice=gfx.showmenu('Carica profilo respiri|Carica profilo click')
        if choice>0 then X.choose_profile(choice==2 and 'click' or 'breath') end
    elseif name=='Tutte' then
        UI.selected={}; for _,i in ipairs(UI.order or {}) do UI.selected[i]=true end
    elseif name=='Attenua' then X.apply_selected(UI.selected,'attenuate')
    elseif name=='Silenzia' then X.apply_selected(UI.selected,'silence')
    elseif name=='Taglia' then X.apply_selected(UI.selected,'cut')
    elseif name=='Dimezza' then X.apply_selected(UI.selected,'halve')
    elseif name=='Originale' then X.compare(false)
    elseif name=='Elaborato' then X.compare(true)
    elseif name=='Solo rimosso' then X.difference()
    elseif name=='Undo' then X.undo(false)
    elseif name=='Redo' then X.undo(true)
    elseif name=='Motore' then X.configure_engine()
    elseif name=='Fusion / DSP' then X.settings.detector=X.settings.detector=='fusion' and 'dsp' or 'fusion'; X.save_settings()
    elseif name=='Impostazioni' then
        local ok,text=reaper.GetUserInputs('NL Breath Click Studio - trattamento',3,'Attenuazione dB,Fade ms,Sensibilita click 0-1',tostring(X.settings.reduction_db)..','..tostring(X.settings.fade_ms)..','..tostring(X.settings.click_sensitivity or .5))
        if ok then
            local a,b,c=text:match('^([^,]+),([^,]+),([^,]+)$'); a=tonumber(a); b=tonumber(b);c=tonumber(c)
            assert(c and c>=0 and c<=1 and a and b and a==a and b==b and a>=-120 and a<=0 and b>=0 and b<=500,'Usa attenuazione da -120 a 0 dB e fade da 0 a 500 ms')
            X.settings.reduction_db=a; X.settings.fade_ms=b;X.settings.click_sensitivity=c
            X.save_settings()
        end
    elseif name=='Ascolta' then
        assert((reaper.GetPlayState()&4)==0,'Registrazione in corso: usa il trasporto REAPER')
        if reaper.GetPlayState()~=0 then
            if X.preview_end then X.stop_preview() else reaper.OnStopButton() end
            return
        end
        local index; for i in pairs(UI.selected) do index=index and math.min(index,i) or i end
        X.select_event(index,true)
        UI.cursor=reaper.GetCursorPosition()
        UI.time_a,UI.time_b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
    elseif name=='Bordi...' then
        local index; for i in pairs(UI.selected) do index=index and math.min(index,i) or i end
        local ctx=X.context(); local e=ctx and ctx.state and ctx.state.plan.events[index]
        assert(e,'Seleziona un respiro')
        local sr=ctx.state.plan.sample_rate
        local ok,text=reaper.GetUserInputs('Bordi evento - coordinate originali in secondi',2,'Inizio,Fine',string.format('%.6f,%.6f',e.start/sr,e['end']/sr))
        if ok then
            local a,b=text:match('^([^,]+),([^,]+)$'); a=tonumber(a); b=tonumber(b)
            assert(a and b and a>=0 and b>a and b<=ctx.state.frames/sr,'Intervallo non valido')
            X.set_bounds({[index]=true},math.floor(a*sr+.5),math.floor(b*sr+.5))
        end
    elseif name=='Aiuto' then
        reaper.ShowMessageBox('Seleziona un evento nella lista o con una selezione temporale in REAPER.\nTrascina le maniglie gialle per correggere i bordi, oppure premi Usa selezione.\nAttenua / Silenzia / Taglia / Dimezza applicano subito SOLO gli eventi selezionati, sulla stessa take e traccia. Applica incluse rimane il batch.\n\nCompatta voce accorcia l\'audio dentro l\'item. Mantieni sync riempie il tempo liberato con pausa e conserva la durata. Gli altri item e cue non si spostano.\nOriginale / Elaborato cambiano sorgente sulla stessa take; Ctrl+Z/Y annulla/ripete. Conserva BreathClick Media con il progetto.\n\nApprendimento facoltativo: Conferma respiro, Falso positivo, Mancato. Escludi e\' solo editoriale. Esporta etichette crea una sessione standalone. Crea profilo usa solo etichette esplicite degli item selezionati e richiede registrazioni indipendenti. Profilo attiva o carica lo stesso calibratore .ini della standalone.','NL Breath Click Studio - editing diretto',0)
    end
end
local function color(r,g,b,a) gfx.set(r,g,b,a or 1) end
local function text(x,y,value,r,g,b)
    color(r or .9,g or .91,b or .93); gfx.x=x; gfx.y=y; gfx.drawstr(value)
end
local function inside(x,y,w,h) return gfx.mouse_x>=x and gfx.mouse_x<x+w and gfx.mouse_y>=y and gfx.mouse_y<y+h end
local function button(name,x,y,w,clicked)
    local hover=inside(x,y,w,29)
    color(hover and .24 or .16,hover and .34 or .23,hover and .43 or .29)
    gfx.rect(x,y,w,29,1); text(x+8,y+6,name)
    UI.buttons[name]={x=x,y=y,w=w,h=29}
    if clicked and hover then invoke(function() UI.action(name) end) end
end
UI.wave=dofile(here..'/bc_direct_view.lua')(UI,X,invoke,text,color,inside)
function UI.draw()
    local key=gfx.getchar() -- update native mouse/key events before reading their state
    local runtime_status=X.runtime.tick(here)
    if runtime_status then X.status=runtime_status end
    X.tick()
    if UI.about_open then
        if key==27 then UI.about_open=false; return true end
        local clicked=(gfx.mouse_cap&1)==1 and (UI.last_mouse&1)==0
        UI.last_mouse=gfx.mouse_cap
        color(.075,.095,.12); gfx.rect(0,0,gfx.w,gfx.h,1)
        gfx.setfont(1,'Segoe UI',20)
        text(30,35,UI.identity.name,.55,.87,.95)
        gfx.setfont(1,'Segoe UI',17)
        text(30,78,'Versione '..UI.identity.version..' · REAPER adapter '..UI.identity.adapter_version)
        text(30,112,UI.identity.author)
        text(30,146,UI.identity.email)
        text(30,176,UI.identity.website)
        text(30,215,'Mozilla Public License 2.0 (MPL-2.0)',.65,.75,.8)
        button('Sito web',30,260,110,clicked)
        button('Email',150,260,100,clicked)
        button('Chiudi info',260,260,125,clicked)
        gfx.update()
        return key>=0
    end
    local changed=reaper.GetProjectStateChangeCount(0)
    local item=reaper.GetSelectedMediaItem(0,0)
    if changed~=UI.refresh or item~=UI.item or UI.was_busy~=X.busy then
        local selected_ids={}
        if item==UI.item and UI.ctx and UI.ctx.state then
            for i in pairs(UI.selected) do local e=UI.ctx.state.plan.events[i]; if e then selected_ids[e.bcs_id]=true end end
        end
        if item~=UI.item then UI.selected={}; UI.anchor=1; UI.scroll=0 end
        UI.refresh=changed; UI.item=item; UI.was_busy=X.busy
        local ok,ctx=pcall(X.context,item); UI.ctx=ok and ctx or nil
        if not ok then X.status='Revisione non leggibile: '..tostring(ctx); X.last_error=X.status end
        if UI.ctx and UI.ctx.state then
            if X.status=='Seleziona una take audio.' then X.status='Seleziona un respiro e scegli il trattamento; trascina i bordi gialli per correggerlo.' end
            UI.selected={}; for i,e in ipairs(UI.ctx.state.plan.events) do if selected_ids[e.bcs_id] then UI.selected[i]=true end end
        end
        if UI.ctx and UI.ctx.state then
            local valid,err=pcall(X.sync_markers,UI.ctx); if not valid then X.status=err end
        end
    end
    -- Selecting a breath in REAPER's arrange selects its row as well.
    local ta,tb=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
    local cursor=reaper.GetCursorPosition()
    local time_changed=ta~=UI.time_a or tb~=UI.time_b
    local cursor_changed=cursor~=UI.cursor and reaper.GetPlayState()==0
    UI.cursor=cursor
    if not X.busy and UI.ctx and UI.ctx.state and (time_changed or cursor_changed) then
        UI.time_a,UI.time_b=ta,tb
        local best,overlap=nil,0; local pos=reaper.GetMediaItemInfo_Value(UI.ctx.item,'D_POSITION')
        for i,e in ipairs(UI.ctx.state.plan.events) do if UI.event_visible(e) then
            local a,b=X.event_interval(UI.ctx,e)
            local n
            if time_changed and tb>ta then
                local intersection=math.max(0,math.min(tb,pos+b)-math.max(ta,pos+a))
                n=intersection/math.max(1e-9,(tb-ta)+(b-a)-intersection)
            else n=(cursor>=pos+a and cursor<=pos+b) and 1/math.max(1e-6,b-a) or 0 end
            if n>overlap then best,overlap=i,n end
        end end
        if best then UI.selected={[best]=true}; UI.anchor=best; UI.reveal_index=best
        elseif tb>ta and ta>=pos and tb<=pos+reaper.GetMediaItemInfo_Value(UI.ctx.item,'D_LENGTH') then UI.selected={} end
    end
    local clicked=(gfx.mouse_cap&1)==1 and (UI.last_mouse&1)==0
    UI.last_mouse=gfx.mouse_cap
    color(.075,.095,.12); gfx.rect(0,0,gfx.w,gfx.h,1)
    gfx.setfont(1,'Segoe UI',17)
    text(14,10,'NL Breath Click Studio · editing diretto',.55,.87,.95)
    text(14,34,string.format('%s  |  %.1f dB · fade %.0f ms',X.settings.detector:upper(),X.settings.reduction_db,X.settings.fade_ms))
    local rows={
        {{'Analizza',90},{'Applica incluse',125},{'Annulla lavoro',122},{'Da selezione',113},{'Usa selezione',122},{'Bordi...',76},{'Impostazioni',110}},
        {{'Attenua',85},{'Silenzia',85},{'Taglia',76},{'Dimezza',89},{'Compatta voce',137},{'Mantieni sync',137}},
        {{'Tutte',63},{'Includi',78},{'Escludi',78},{'Conferma respiro',153},{'Falso positivo',135},{'Mancato',87},{'Segnala',90}},
        {{'Originale',98},{'Elaborato',98},{'Ascolta',90},{'Undo',73},{'Redo',73},{'Fusion / DSP',119},{'Motore',82},{'Aiuto',70},{'Precedente',100},{'Successivo',100}},
        {{'Esporta etichette',158},{'Crea profilo',120},{'Profilo',84},{'Carica profilo',139},{'Informazioni',125},{'Installa motore',139}},
    }
    for row,buttons in ipairs(rows) do local x=14; for _,b in ipairs(buttons) do button(b[1],x,54+(row-1)*35,b[2],clicked); x=x+b[2]+6 end end
    -- A/B and Undo can replace the source while a button is being drawn.
    -- The waveform below must use the new source/view in this same frame.
    if UI.refresh==-1 then
        local ok,ctx=pcall(X.context,UI.item); UI.ctx=ok and ctx or nil
        if not ok then X.status=tostring(ctx); X.last_error=X.status end
    end
    local ctx=UI.ctx; local events=ctx and ctx.state and ctx.state.plan.events or {}
    if UI.order_events~=events then
        UI.order_events=events; UI.order={}
        for i,e in ipairs(events) do
            if UI.event_visible(e) then UI.order[#UI.order+1]=i end
        end
        table.sort(UI.order,function(a,b) return events[a].start<events[b].start end)
    end
    if UI.reveal_id then
        for i,e in ipairs(events) do if e.bcs_id==UI.reveal_id then UI.selected={[i]=true}; UI.anchor=i; UI.reveal_index=i end end
        UI.reveal_id=nil; UI.time_a,UI.time_b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
    end
    local name=ctx and reaper.GetTakeName(ctx.active) or 'Nessuna take selezionata'
    local included=0; for _,e in ipairs(events) do if e.approved then included=included+1 end end
    local timing=ctx and ctx.state and ctx.state.timing=='sync' and 'Mantieni sincronizzazione' or 'Compatta voce nell\'item'
    text(14,235,name:sub(1,45)..' · '..timing..' · profili R:'..(X.settings.profile_active and 'on' or 'off')..' C:'..(X.settings.click_profile_active and 'on' or 'off'))
    local outside=ctx and ctx.state and #(ctx.state.outside_review or {}) or 0
    local applied=0; for _,e in ipairs(events) do if e.bcs_applied then applied=applied+1 end end
    local undecided=0;for _,e in ipairs(events) do if e.enabled~=false and not e.approved and e.decision_source~='user' then undecided=undecided+1 end end
    local filter_names={'Tutti','Respiri','Click','Incerti / voce','Da decidere','Impulsi e schiocchi','Riparazioni da rivedere'}
    text(14,258,string.format('%d eventi · %d applicati · %d inclusi · %d da decidere · filtro: %s%s',#events,applied,included,undecided,filter_names[(UI.filter or 0)+1],
        outside>0 and (' · '..outside..' correzioni fuori item conservate') or ''),.65,.75,.8)
    local tools={{'Rileva respiri',130},{'Rileva click',120},{'Filtro',65},{'Ripara click',105},{'Algoritmo',95}}
    local tx=14;for _,b in ipairs(tools) do button(b[1],tx,gfx.h-106,b[2],clicked);tx=tx+b[2]+6 end
    text(tx,gfx.h-102,(X.settings.detect_breath~=false and 'Respiri ON' or 'Respiri OFF')..' · '..(X.settings.detect_clicks and 'Click ON' or 'Click OFF'),.5,.9,.8)
    local labels_click={{'Click confermato',145},{'Falso click',105},{'Click mancato',125},{'Schiocco',95},{'Preserva voce',130},{'Solo rimosso',125}}
    tx=14;for _,b in ipairs(labels_click) do button(b[1],tx,gfx.h-71,b[2],clicked);tx=tx+b[2]+6 end
    UI.wave.draw(ctx,events,clicked,283)
    local top,bottom,height=439,gfx.h-110,27
    UI.list_top=top
    local visible=math.max(1,math.floor((bottom-top)/height))
    if UI.reveal_index then
        for row,i in ipairs(UI.order) do
            if i==UI.reveal_index and (row<=UI.scroll or row>UI.scroll+visible) then UI.scroll=math.max(0,row-visible) end
        end
        UI.reveal_index=nil
    end
    if gfx.mouse_wheel~=0 then UI.scroll=UI.scroll-math.floor(gfx.mouse_wheel/120)*3; gfx.mouse_wheel=0 end
    UI.scroll=math.max(0,math.min(UI.scroll,math.max(0,#UI.order-visible)))
    local modes={attenuate='Attenua',silence='Silenzia',cut='Taglia',halve='Dimezza'}
    local kinds={digital_click='Click digitale',saliva_click='Saliva breve',mouth_click='Schiocco',speech_overlap='Click/voce incerto',vocal_transient='Transiente vocale',low_level_impulse='Impulso debole: rivedere',band_limited_click='Schiocco morbido: rivedere',isolated_click_review='Impulso isolato: rivedere'}
    for row=1,visible do
        local index=UI.order[row+UI.scroll]; local e=events[index]; if not e then break end
        local y=top+(row-1)*height
        if UI.selected[index] then color(.12,.30,.41); gfx.rect(12,y,gfx.w-24,height-2,1) end
        local sr=ctx.state.plan.sample_rate
        local a,b=X.event_interval(ctx,e)
        local labels={breath_confirmed='confermato',false_positive='falso positivo',missed_breath='mancato confermato',editorial_exclusion='escluso editoriale',click_confirmed='click confermato',click_false_positive='falso click',missed_click='click mancato',mouth_click_confirmed='schiocco confermato',vocal_transient='voce preservata'}
        local cr,cg,cb=.91,.86,.65;if e.kind==1 then cr,cg,cb=.98,.57,.43 end;if not e.approved then cr,cg,cb=.48,.52,.56 end
        text(19,y+3,(e.approved and '[x]' or '[ ]')..string.format(' BC%02d   %7.3f → %7.3f    %-8s  %s · %s%s',e.bcs_short or index,a,b,modes[e.mode] or e.mode,e.kind==1 and (kinds[e.classification] or 'Click') or 'Respiro',X.event_status(e),e.repair_feedback=='unsatisfactory' and ' · riparazione da rivedere' or labels[e.learning_label] and (' · '..labels[e.learning_label]) or ''),cr,cg,cb)
        if clicked and inside(12,y,gfx.w-24,height) then
            if (gfx.mouse_cap&4)~=0 then UI.selected[index]=not UI.selected[index] or nil
            elseif (gfx.mouse_cap&8)~=0 then
                local anchor=1; for n,i in ipairs(UI.order) do if i==UI.anchor then anchor=n end end
                for n=math.min(anchor,row+UI.scroll),math.max(anchor,row+UI.scroll) do UI.selected[UI.order[n]]=true end
            else UI.selected={[index]=true}; UI.anchor=index end
            if gfx.mouse_x<50 then invoke(function() X.include({[index]=true},not e.approved) end)
            else
                invoke(function() X.select_event(index,false) end)
                UI.time_a,UI.time_b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
                UI.cursor=reaper.GetCursorPosition()
            end
        end
    end
    local status=X.status:match('^[^\r\n]*') or X.status
    text(14,gfx.h-37,status:sub(1,125),X.last_error and 1 or .6,X.last_error and .55 or .84,.63)
    text(14,gfx.h-17,'Ctrl/Shift: selezione multipla · Spazio: ascolta · Canc: escludi · Ctrl+Z/Y: Undo/Redo',.5,.6,.67)
    if key==32 then invoke(function() UI.action('Ascolta') end)
    elseif key==110 or key==78 then invoke(function() UI.step(1) end)
    elseif key==112 or key==80 then invoke(function() UI.step(-1) end)
    elseif key==6579564 or key==127 then invoke(function() UI.action('Escludi') end)
    elseif key==26 then invoke(function() UI.action('Undo') end)
    elseif key==25 then invoke(function() UI.action('Redo') end) end
    gfx.update()
    return key>=0 and key~=27
end
function UI.open(title,development)
    X.load_settings()
    if development then X.settings.detect_breath=true;X.settings.detect_clicks=true end
    local engine_ready=pcall(X.engine)
    if not engine_ready and reaper.file_exists(here..'/runtime-release.json') then X.runtime.install(here) end
    gfx.init(title or 'NL Breath Click Studio - REAPER preview',1040,760,0)
    reaper.atexit(function() X.cancel(); X.stop_preview(); UI.wave.close(); gfx.quit() end)
    local function frame()
        local ok,keep=xpcall(UI.draw,debug.traceback)
        if ok and keep then reaper.defer(frame)
        elseif not ok then X.cancel(); reaper.ShowMessageBox(tostring(keep),'NL Breath Click Studio',0) end
    end
    reaper.defer(frame) -- Let the native gfx window finish initialization first.
end
if _G.BCS_PANEL_NO_RUN then return UI end
UI.open()
