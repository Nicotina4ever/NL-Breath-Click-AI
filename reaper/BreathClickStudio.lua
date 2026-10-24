-- SPDX-License-Identifier: MPL-2.0
-- @description NL Breath Click Studio - editing diretto, TimeMap e apprendimento
-- @version 1.0.0-rc.2
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
local UI={identity={name='NL Breath Click Studio',version='1.0.0-rc.2',adapter_version='1.0.0-rc.2-beta',author='Nicola Lanci Voice Over',email='info@nicolalanci.com',website='www.nicolalanci.com'},bridge=X,selected={},scroll=0,anchor=1,buttons={},last_mouse=0,refresh=-1}
local function invoke(fn)
    local previous=X.status; X.last_error=nil
    local ok,err=xpcall(fn,debug.traceback)
    if not ok then X.status=tostring(err); X.last_error=tostring(err) end
    if ok and X.status==previous then X.status='Pronto. Revisione e stato aggiornati.' end
    UI.refresh=-1
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
    elseif name=='Crea profilo' then X.export_or_train(true)
    elseif name=='Profilo' then X.profile()
    elseif name=='Carica profilo' then X.choose_profile()
    elseif name=='Tutte' then
        UI.selected={}; if UI.ctx and UI.ctx.state then for i in ipairs(UI.ctx.state.plan.events) do UI.selected[i]=true end end
    elseif name=='Attenua' then X.apply_selected(UI.selected,'attenuate')
    elseif name=='Silenzia' then X.apply_selected(UI.selected,'silence')
    elseif name=='Taglia' then X.apply_selected(UI.selected,'cut')
    elseif name=='Dimezza' then X.apply_selected(UI.selected,'halve')
    elseif name=='Originale' then X.compare(false)
    elseif name=='Elaborato' then X.compare(true)
    elseif name=='Undo' then X.undo(false)
    elseif name=='Redo' then X.undo(true)
    elseif name=='Motore' then X.configure_engine()
    elseif name=='Fusion / DSP' then X.settings.detector=X.settings.detector=='fusion' and 'dsp' or 'fusion'; X.save_settings()
    elseif name=='Impostazioni' then
        local ok,text=reaper.GetUserInputs('NL Breath Click Studio - trattamento',2,'Attenuazione dB,Fade ms',tostring(X.settings.reduction_db)..','..tostring(X.settings.fade_ms))
        if ok then
            local a,b=text:match('^([^,]+),([^,]+)$'); a=tonumber(a); b=tonumber(b)
            assert(a and b and a==a and b==b and a>=-120 and a<=0 and b>=0 and b<=500,'Usa attenuazione da -120 a 0 dB e fade da 0 a 500 ms')
            X.settings.reduction_db=a; X.settings.fade_ms=b
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
        local ok,text=reaper.GetUserInputs('Bordi respiro - coordinate originali in secondi',2,'Inizio,Fine',string.format('%.6f,%.6f',e.start/sr,e['end']/sr))
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
        for i,e in ipairs(UI.ctx.state.plan.events) do
            local a,b=X.event_interval(UI.ctx,e)
            local n
            if time_changed and tb>ta then n=math.max(0,math.min(tb,pos+b)-math.max(ta,pos+a))
            else n=(cursor>=pos+a and cursor<=pos+b) and 1/math.max(1e-6,b-a) or 0 end
            if n>overlap then best,overlap=i,n end
        end
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
        {{'Tutte',63},{'Includi',78},{'Escludi',78},{'Conferma respiro',153},{'Falso positivo',135},{'Mancato',87}},
        {{'Originale',98},{'Elaborato',98},{'Ascolta',90},{'Undo',73},{'Redo',73},{'Fusion / DSP',119},{'Motore',82},{'Aiuto',70}},
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
        for i in ipairs(events) do UI.order[#UI.order+1]=i end
        table.sort(UI.order,function(a,b) return events[a].start<events[b].start end)
    end
    if UI.reveal_id then
        for i,e in ipairs(events) do if e.bcs_id==UI.reveal_id then UI.selected={[i]=true}; UI.anchor=i; UI.reveal_index=i end end
        UI.reveal_id=nil; UI.time_a,UI.time_b=reaper.GetSet_LoopTimeRange2(0,false,false,0,0,false)
    end
    local name=ctx and reaper.GetTakeName(ctx.active) or 'Nessuna take selezionata'
    local included=0; for _,e in ipairs(events) do if e.approved then included=included+1 end end
    local timing=ctx and ctx.state and ctx.state.timing=='sync' and 'Mantieni sincronizzazione' or 'Compatta voce nell\'item'
    text(14,235,name:sub(1,55)..' · '..timing..' · profilo '..(X.settings.profile_active and 'attivo' or 'off'))
    local outside=ctx and ctx.state and #(ctx.state.outside_review or {}) or 0
    local applied=0; for _,e in ipairs(events) do if e.bcs_applied then applied=applied+1 end end
    text(14,258,string.format('%d eventi · %d applicati · %d inclusi · %d esclusi%s',#events,applied,included,#events-included,
        outside>0 and (' · '..outside..' correzioni fuori item conservate') or ''),.65,.75,.8)
    UI.wave.draw(ctx,events,clicked,283)
    local top,bottom,height=439,gfx.h-67,27
    UI.list_top=top
    local visible=math.max(1,math.floor((bottom-top)/height))
    if UI.reveal_index then
        for row,i in ipairs(UI.order) do
            if i==UI.reveal_index and (row<=UI.scroll or row>UI.scroll+visible) then UI.scroll=math.max(0,row-visible) end
        end
        UI.reveal_index=nil
    end
    if gfx.mouse_wheel~=0 then UI.scroll=UI.scroll-math.floor(gfx.mouse_wheel/120)*3; gfx.mouse_wheel=0 end
    UI.scroll=math.max(0,math.min(UI.scroll,math.max(0,#events-visible)))
    local modes={attenuate='Attenua',silence='Silenzia',cut='Taglia',halve='Dimezza'}
    for row=1,visible do
        local index=UI.order[row+UI.scroll]; local e=events[index]; if not e then break end
        local y=top+(row-1)*height
        if UI.selected[index] then color(.12,.30,.41); gfx.rect(12,y,gfx.w-24,height-2,1) end
        local sr=ctx.state.plan.sample_rate
        local a,b=X.event_interval(ctx,e)
        local labels={breath_confirmed='confermato',false_positive='falso positivo',missed_breath='mancato confermato',editorial_exclusion='escluso editoriale'}
        text(19,y+3,(e.approved and '[x]' or '[ ]')..string.format(' BC%02d   %7.3f → %7.3f    %-8s  %s · %s%s',e.bcs_short or index,a,b,modes[e.mode] or e.mode,e.kind==1 and 'Click' or 'Respiro',e.bcs_applied and 'APPLICATO' or 'proposto',labels[e.learning_label] and (' · '..labels[e.learning_label]) or ''),e.approved and .91 or .48,e.approved and .86 or .52,e.approved and .65 or .56)
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
    text(14,gfx.h-50,status:sub(1,125),X.last_error and 1 or .6,X.last_error and .55 or .84,.63)
    text(14,gfx.h-27,'Ctrl/Shift: selezione multipla · Spazio: ascolta · Canc: escludi · Ctrl+Z/Y: Undo/Redo',.5,.6,.67)
    if key==32 then invoke(function() UI.action('Ascolta') end)
    elseif key==6579564 or key==127 then invoke(function() UI.action('Escludi') end)
    elseif key==26 then invoke(function() UI.action('Undo') end)
    elseif key==25 then invoke(function() UI.action('Redo') end) end
    gfx.update()
    return key>=0 and key~=27
end
function UI.open()
    X.load_settings()
    local engine_ready=pcall(X.engine)
    if not engine_ready and reaper.file_exists(here..'/runtime-release.json') then X.runtime.install(here) end
    gfx.init('NL Breath Click Studio - REAPER preview',1040,760,0)
    reaper.atexit(function() X.cancel(); X.stop_preview(); UI.wave.close(); gfx.quit() end)
    local function frame()
        local ok,keep=xpcall(UI.draw,debug.traceback)
        if ok and keep then reaper.defer(frame)
        elseif not ok then X.cancel(); reaper.ShowMessageBox(tostring(keep),'NL Breath Click Studio',0) end
    end
    frame()
end
if _G.BCS_PANEL_NO_RUN then return UI end
UI.open()
