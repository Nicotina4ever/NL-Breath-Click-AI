-- SPDX-License-Identifier: MPL-2.0
-- Original waveform handles remain editable even when a cut is collapsed on
-- the project timeline. Only the mouse release commits; dragging is a preview.
return function(UI,X,invoke,text,color,inside)
local V={}
function V.close()
    if V.source then
        if V.building then reaper.PCM_Source_BuildPeaks(V.source,2) end
        reaper.PCM_Source_Destroy(V.source); V.source=nil
    end
    V.building=nil
end
function V.draw(ctx,events,clicked,top)
    local index; for i in pairs(UI.selected) do if events[i] then index=index and math.min(index,i) or i end end
    local e=index and events[index]
    color(.055,.075,.09); gfx.rect(14,top,gfx.w-28,145,1)
    if not e then text(24,top+14,'Seleziona un respiro: trascina i bordi qui, oppure usa la selezione temporale di REAPER.'); return end
    local sr=ctx.state.plan.sample_rate
    local valid,first,last=pcall(X.view,ctx)
    if not valid then text(24,top+15,tostring(first),1,.65,.55); return end
    local a,b=e.start,e['end']
    local pad=math.floor((e.kind==1 and math.max(sr*.015,(b-a)*1.5) or math.max(sr*.25,(b-a)*.4))+.5)
    local lo,hi=math.max(0,a-pad),math.min(ctx.state.frames,b+pad)
    if V.drag then lo,hi=V.drag.lo,V.drag.hi; a,b=V.drag.a,V.drag.b end
    local left,right,y,height=28,gfx.w-28,top+38,77
    local function xpos(f) return left+(f-lo)/(hi-lo)*(right-left) end
    local filepath=X.media_path(ctx.state.prepared_rel)
    if V.path~=filepath then
        V.close(); V.path=filepath; V.source=reaper.PCM_Source_CreateFromFile(filepath); V.cache=nil
        if V.source then V.building=reaper.PCM_Source_BuildPeaks(V.source,0)~=0 end
    end
    if V.building and reaper.PCM_Source_BuildPeaks(V.source,1)==0 then
        reaper.PCM_Source_BuildPeaks(V.source,2); V.building=nil; V.cache_key=nil
    end
    local key=string.format('%s:%d:%d:%d',filepath,lo,hi,right-left)
    if V.source and V.cache_key~=key then
        local n=math.max(1,math.floor(right-left)); local buffer=reaper.new_array(n*2)
        local returned=reaper.PCM_Source_GetPeaks(V.source,n*sr/(hi-lo),lo/sr,1,n,0,buffer)
        local count=type(returned)=='number' and returned&0xfffff or 0
        V.cache={buffer=buffer,n=n,count=count}; V.cache_key=key
        if count==0 then V.cache_key=nil end -- a not-yet-built source is retried next frame
    end
    if V.cache then
        color(.32,.51,.59)
        for i=1,math.min(V.cache.count,V.cache.n) do
            local max,min=V.cache.buffer[i],V.cache.buffer[V.cache.n+i]
            gfx.line(left+i,y+height*.5-max*height*.45,left+i,y+height*.5-min*height*.45)
        end
    end
    color(.15,.45,.55,.35); gfx.rect(xpos(a),y,math.max(1,xpos(b)-xpos(a)),height,1)
    color(.98,.76,.35); gfx.line(xpos(a),y-7,xpos(a),y+height+5); gfx.line(xpos(b),y-7,xpos(b),y+height+5)
    gfx.rect(xpos(a)-5,y-7,10,11,1); gfx.rect(xpos(b)-5,y+height-4,10,11,1)
    local hint=(a>=first and b<=last) and 'trascina le maniglie gialle' or 'fuori dal trim attuale: sola lettura'
    text(24,top+10,string.format('Bordi sull\'originale · %.3f – %.3f s · %s',a/sr,b/sr,hint),.97,.82,.51)
    local start,finish=X.event_interval(ctx,e)
    text(24,top+121,string.format('Nell\'item attuale: %.3f – %.3f s · %s',start,finish,e.bcs_applied and 'applicato' or 'proposto'),.65,.76,.81)
    UI.wave_handles={a={x=xpos(a),y=y+16},b={x=xpos(b),y=y+height-16},left=left,right=right,lo=lo,hi=hi,top=y,height=height}
    if clicked and not X.busy and a>=first and b<=last and inside(14,top,gfx.w-28,145) then
        local da,db=math.abs(gfx.mouse_x-xpos(a)),math.abs(gfx.mouse_x-xpos(b))
        local edge=da<12 and da<=db and 'a' or (db<12 and 'b')
        if edge then V.drag={edge=edge,index=index,a=a,b=b,lo=lo,hi=hi,first=first,last=last} end
    end
    if V.drag then
        local d=V.drag; local f=math.floor(lo+(gfx.mouse_x-left)/(right-left)*(hi-lo)+.5)
        if (gfx.mouse_cap&1)==1 then
            if d.edge=='a' then d.a=math.max(d.first,math.min(d.b-1,f)) else d.b=math.min(d.last,math.max(d.a+1,f)) end
        else
            V.drag=nil; invoke(function() X.set_bounds({[d.index]=true},d.a,d.b) end)
        end
    end
end
return V
end
