-- SPDX-License-Identifier: MPL-2.0
-- ============================================================================
--  bcnext_core.lua - identificazione di item/take, regioni e tempo
--
--  Modulo condiviso dal pannello e dai test. Non disegna nulla e non modifica
--  nulla: legge lo stato del progetto e prepara le strutture dati.
-- ============================================================================

local function cartella_di(percorso)
    return (percorso or ""):match("^(.*)[/\\][^/\\]*$") or "."
end

local qui = cartella_di(debug.getinfo(1, "S").source:sub(2))
local C = dofile(qui .. "/BreathClickNext_config.lua")

local B = {
    config = C,
    versione = C.versione,
}

-- ---------------------------------------------------------------------------
--  utilita'
-- ---------------------------------------------------------------------------
function B.arrotonda(valore, cifre)
    local fattore = 10 ^ (cifre or 3)
    return math.floor(valore * fattore + 0.5) / fattore
end

function B.limita(valore, minimo, massimo)
    if valore < minimo then return minimo end
    if valore > massimo then return massimo end
    return valore
end

function B.db_a_guadagno(db)
    return 10 ^ (db / 20.0)
end

function B.guadagno_a_db(guadagno)
    if not guadagno or guadagno <= 0 then return -160.0 end
    return 20.0 * math.log(guadagno, 10)
end

function B.vicino(a, b, tolleranza)
    if type(a) ~= "number" or type(b) ~= "number" then return false end
    return math.abs(a - b) <= (tolleranza or C.eps)
end

function B.nome_fiato(nome)
    if type(nome) ~= "string" then return false end
    if nome:sub(1, #C.prefisso) ~= C.prefisso then return false end
    local resto = nome:sub(#C.prefisso + 1):lower()
    return resto:find(C.nome_fiato, 1, true) ~= nil
end

function B.formatta_tempo(secondi)
    local ms = math.floor((secondi or 0) * 1000 + 0.5)
    local h = math.floor(ms / 3600000)
    local m = math.floor(ms / 60000) % 60
    local s = math.floor(ms / 1000) % 60
    local r = ms % 1000
    if h > 0 then return string.format("%d:%02d:%02d.%03d", h, m, s, r) end
    return string.format("%d:%02d.%03d", m, s, r)
end

function B.log(messaggio)
    if C.diagnostica then
        reaper.ShowConsoleMsg("BCN: " .. tostring(messaggio) .. "\n")
    end
end

-- ---------------------------------------------------------------------------
--  Chunk: lettura di IGUID e GUID delle take
--
--  L'API nativa non espone il GUID dell'item. Leggerlo dal chunk e' l'unico
--  modo senza dipendere da SWS. Il GUID della take si puo' anche risolvere
--  con GetMediaItemTakeByGUID: qui lo conserviamo per la verifica.
-- ---------------------------------------------------------------------------
local function righe_di(testo)
    testo = (testo or ""):gsub("\r\n", "\n"):gsub("\n+$", "")
    local righe = {}
    for riga in (testo .. "\n"):gmatch("(.-)\n") do
        righe[#righe + 1] = riga
    end
    return righe
end

-- Trova i blocchi <NOME ...> ... >, rispettando l'annidamento.
local function blocchi(testo, nome)
    local righe = righe_di(testo)
    local trovati = {}
    local i = 1
    while i <= #righe do
        local riga = righe[i]
        local apre = riga:match("^%s*<" .. nome .. "%s*$") or
                     riga:match("^%s*<" .. nome .. "%s")
        if apre then
            local profondita = 0
            local chiusura = nil
            for j = i, #righe do
                local aperti = select(2, righe[j]:gsub("<", ""))
                local chiusi = select(2, righe[j]:gsub(">", ""))
                profondita = profondita + aperti - chiusi
                if profondita <= 0 then
                    chiusura = j
                    break
                end
            end
            if chiusura then
                local testo_blocco = table.concat(righe, "\n", i, chiusura)
                trovati[#trovati + 1] = {
                    riga_inizio = i,
                    riga_fine = chiusura,
                    testo = testo_blocco,
                }
                i = chiusura + 1
            else
                i = i + 1
            end
        else
            i = i + 1
        end
    end
    return trovati, righe
end

function B.leggi_chunk_item(item)
    local a, b = reaper.GetItemStateChunk(item, "", false)
    local chunk = type(a) == "string" and a or b
    if type(chunk) ~= "string" or chunk == "" then
        return nil, "chunk dell'item non leggibile"
    end
    return chunk
end

function B.leggi_chunk_traccia(traccia)
    local a, b = reaper.GetTrackStateChunk(traccia, "", false)
    local chunk = type(a) == "string" and a or b
    if type(chunk) ~= "string" or chunk == "" then
        return nil, "chunk della traccia non leggibile"
    end
    return chunk
end

function B.guid_da_chunk(chunk)
    if type(chunk) ~= "string" then return nil end
    return chunk:match("IGUID%s+({[^}]+})")
end

local function guid_di_take(testo)
    -- Si cerca una riga che inizia con GUID, non IGUID.
    for riga in (testo .. "\n"):gmatch("(.-)\r?\n") do
        local guid = riga:match("^%s*GUID%s+({[^}]+})")
        if guid then return guid end
    end
    return nil
end

function B.info_take_da_chunk(chunk)
    local guids, righe = {}, nil
    local trovati, tutte = blocchi(chunk, "TAKE")
    righe = tutte
    if #trovati > 0 then
        for _, blocco in ipairs(trovati) do
            guids[#guids + 1] = {
                guid = guid_di_take(blocco.testo),
                riga_inizio = blocco.riga_inizio,
                riga_fine = blocco.riga_fine,
                con_wrapper = true,
            }
        end
    else
        -- Con una sola take REAPER non scrive il blocco <TAKE>: i campi
        -- (GUID, SOURCE, VOLENV2) stanno direttamente nell'item. In questo
        -- caso l'intero item chunk e' il blocco della take.
        guids[1] = {
            guid = guid_di_take(chunk),
            riga_inizio = 1,
            riga_fine = #righe,
            con_wrapper = false,
        }
    end
    return guids, righe
end

function B.indice_take_attiva(item)
    local attiva = reaper.GetActiveTake(item)
    if not attiva then return nil end
    local n = reaper.GetMediaItemNumTakes(item)
    for i = 0, n - 1 do
        if reaper.GetMediaItemTake(item, i) == attiva then return i end
    end
    return nil
end

-- Native GUIDs include the first take of multi-take items (whose chunk has
-- no TAKE wrapper). Keep the historical parser only for old API mocks.
function B.guid_take(take, item, indice)
    if reaper.GetSetMediaItemTakeInfo_String then
        local ok, guid = reaper.GetSetMediaItemTakeInfo_String(take, 'GUID', '', false)
        if ok and guid and guid ~= '' then return guid end
        return nil -- a failed native identity read must never guess from a chunk
    end
    local chunk = B.leggi_chunk_item(item)
    local info = chunk and B.info_take_da_chunk(chunk) or {}
    return info[indice + 1] and info[indice + 1].guid or nil
end

function B.percorso_sorgente(take)
    if not take then return nil end
    local sorgente = reaper.GetMediaItemTake_Source(take)
    if not sorgente then return nil end
    local nome = reaper.GetMediaSourceFileName(sorgente, "")
    if type(nome) ~= "string" or nome == "" then return nil end
    return nome
end

-- ---------------------------------------------------------------------------
--  Snapshot di un item: tutto cio' che serve per verificare, al momento di
--  Applica, che l'item sia ancora quello dell'anteprima.
-- ---------------------------------------------------------------------------
function B.snapshot(item, options)
    if not item then return nil, "item non valido" end
    if reaper.ValidatePtr2 and not reaper.ValidatePtr2(0, item, "MediaItem*") then
        return nil, "item non valido"
    end

    local take = reaper.GetActiveTake(item)
    if not take then return nil, "l'item non ha una take audio" end
    if reaper.TakeIsMIDI(take) then return nil, "l'item e' MIDI: non supportato" end

    local sorgente = B.percorso_sorgente(take)
    if not sorgente then return nil, "sorgente audio dell'item non disponibile" end

    local pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
    local lun = reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
    local off = reaper.GetMediaItemTakeInfo_Value(take, "D_STARTOFFS")
    local rate = reaper.GetMediaItemTakeInfo_Value(take, "D_PLAYRATE")
    if type(pos) ~= "number" or type(lun) ~= "number" or
       type(off) ~= "number" or type(rate) ~= "number" or lun <= 0 then
        return nil, "posizione, durata, offset o rate non validi"
    end

    if reaper.GetTakeNumStretchMarkers and reaper.GetTakeNumStretchMarkers(take) > 0 then
        return nil, "take con stretch marker: non supportata in P1"
    end
    if (not options or not options.allow_rate) and math.abs(rate - 1.0) > 0.0005 then
        return nil, string.format(
            "item con playback rate %.3f: non supportato in P1 (portalo a 1.0)",
            rate)
    end

    local chunk, errore = B.leggi_chunk_item(item)
    if not chunk then return nil, errore end
    local item_guid = B.guid_da_chunk(chunk)
    if not item_guid then
        return nil, "GUID dell'item non leggibile: P1 non puo' garantire l'identita'"
    end
    local take_info = B.info_take_da_chunk(chunk)
    local indice = B.indice_take_attiva(item)
    if not indice then return nil, "take attiva non determinabile" end
    local take_guid = B.guid_take(take, item, indice)
    if not take_guid then
        return nil, "GUID della take attiva non leggibile: P1 non puo' garantire l'identita'"
    end

    local attiva = reaper.GetActiveTake(item)
    if not attiva or attiva ~= take then
        return nil, "la take attiva e' cambiata durante la lettura"
    end

    local traccia = reaper.GetMediaItemTake_Track(take)
    if not traccia then return nil, "traccia dell'item non trovata" end
    local traccia_guid = reaper.GetTrackGUID(traccia)
    if not traccia_guid then return nil, "GUID della traccia non leggibile" end

    local s = {
        item = item,
        take = take,
        traccia = traccia,
        item_guid = item_guid,
        take_guid = take_guid,
        indice_take = indice,
        n_take = reaper.GetMediaItemNumTakes(item),
        traccia_guid = traccia_guid,
        pos = pos,
        len = lun,
        off = off,
        rate = rate,
        sorgente = sorgente,
        fine = pos + lun,
        chunk_originale = chunk,
    }
    return s
end

function B.risolvi_take(s)
    local take = nil
    if s.take_guid then
        take = reaper.GetMediaItemTakeByGUID(0, s.take_guid)
    end
    if not take and s.take and reaper.ValidatePtr2 and
       reaper.ValidatePtr2(0, s.take, "MediaItem_Take*") then
        take = s.take
    end
    return take
end

-- Verifica che l'item/take siano ancora quelli dell'anteprima.
function B.verifica_snapshot(s)
    local take = B.risolvi_take(s)
    if not take then return false, "la take e' stata eliminata o sostituita" end
    if not s.take_guid then
        return false, "snapshot senza GUID della take: rifai l'anteprima"
    end

    local item = reaper.GetMediaItemTake_Item(take)
    if not item then return false, "l'item della take non esiste piu'" end
    if item ~= s.item then
        return false, "la take punta a un item diverso: rifai l'anteprima"
    end

    local attiva = reaper.GetActiveTake(item)
    if not attiva or attiva ~= take then
        return false, "la take attiva e' cambiata: rifai l'anteprima"
    end

    local indice = B.indice_take_attiva(item)
    if indice ~= s.indice_take then
        return false, "l'indice della take attiva e' cambiato: rifai l'anteprima"
    end

    local traccia = reaper.GetMediaItemTake_Track(take)
    if not traccia then return false, "la traccia dell'item non esiste piu'" end
    if s.traccia_guid and reaper.GetTrackGUID(traccia) ~= s.traccia_guid then
        return false, "l'item e' stato spostato su un'altra traccia"
    end

    local pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
    local lun = reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
    local off = reaper.GetMediaItemTakeInfo_Value(take, "D_STARTOFFS")
    local rate = reaper.GetMediaItemTakeInfo_Value(take, "D_PLAYRATE")
    if not B.vicino(pos, s.pos) or not B.vicino(lun, s.len) or
       not B.vicino(off, s.off) or not B.vicino(rate, s.rate) then
        return false, "posizione, durata, offset o rate dell'item sono cambiati: rifai l'anteprima"
    end

    local sorgente = B.percorso_sorgente(take)
    if sorgente ~= s.sorgente then
        return false, "la sorgente audio dell'item e' cambiata: rifai l'anteprima"
    end

    local chunk = B.leggi_chunk_item(item)
    if not chunk then return false, "chunk dell'item non leggibile" end
    if B.guid_da_chunk(chunk) ~= s.item_guid then
        return false, "l'item e' stato sostituito: rifai l'anteprima"
    end
    local info = B.info_take_da_chunk(chunk)
    local guid_corrente = B.guid_take(take, item, indice)
    if guid_corrente ~= s.take_guid then
        return false, "la take attiva e' cambiata: rifai l'anteprima"
    end

    return true
end

-- ---------------------------------------------------------------------------
--  Selezione degli item bersaglio
-- ---------------------------------------------------------------------------
function B.item_bersaglio()
    local elenco, visti = {}, {}
    local function chiave(item)
        -- Non si usa l'userdata come chiave di tabella: in alcuni host
        -- (e nei test con Lupa) gli oggetti Python non sono chiavi affidabili.
        return tostring(item)
    end
    local function aggiungi(item)
        if item then
            local k = chiave(item)
            if not visti[k] then
                visti[k] = true
                elenco[#elenco + 1] = item
            end
        end
    end

    local n = reaper.CountSelectedMediaItems(0)
    if n > 0 then
        for i = 0, n - 1 do aggiungi(reaper.GetSelectedMediaItem(0, i)) end
        return elenco
    end

    n = reaper.CountSelectedTracks(0)
    if n > 0 then
        for t = 0, n - 1 do
            local traccia = reaper.GetSelectedTrack(0, t)
            for i = 0, reaper.CountTrackMediaItems(traccia) - 1 do
                aggiungi(reaper.GetTrackMediaItem(traccia, i))
            end
        end
        return elenco
    end

    local cursore = reaper.GetCursorPosition()
    for t = 0, reaper.CountTracks(0) - 1 do
        local traccia = reaper.GetTrack(0, t)
        for i = 0, reaper.CountTrackMediaItems(traccia) - 1 do
            local item = reaper.GetTrackMediaItem(traccia, i)
            local pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
            local lun = reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
            if cursore >= pos and cursore < pos + lun then aggiungi(item) end
        end
    end

    if #elenco > 1 then
        return {}, "piu' item sotto il cursore: seleziona gli item o la traccia"
    end
    if #elenco == 0 then
        return {}, "seleziona almeno un item o una traccia audio"
    end
    return elenco
end

-- ---------------------------------------------------------------------------
--  Regioni come eventi
--
--  L'utente crea regioni chiamate "BCN: fiato". Ogni regione appartiene
--  all'item che la contiene per intero. Se una regione e' fuori item o
--  ambigua, si rifiuta: non si indovina.
-- ---------------------------------------------------------------------------
local function enumera_regioni()
    local regioni = {}
    local _, marker, reg = reaper.CountProjectMarkers(0)
    local totale = (marker or 0) + (reg or 0)
    for i = 0, totale - 1 do
        local ok, isrgn, pos, fine, nome, id = reaper.EnumProjectMarkers3(0, i)
        if ok and isrgn then
            regioni[#regioni + 1] = {
                isrgn = true,
                inizio = pos,
                fine = fine,
                nome = nome or "",
                id = id,
                indice = i,
            }
        end
    end
    return regioni
end

function B.eventi_da_regioni(snapshots)
    local errori = {}
    local regioni = enumera_regioni()
    local eventi = {}

    for _, regione in ipairs(regioni) do
        if B.nome_fiato(regione.nome) then
            local dentro = {}
            local sovrapposti = {}
            for _, s in ipairs(snapshots) do
                local sovrappone = regione.inizio < s.fine - C.eps and
                                   regione.fine > s.pos + C.eps
                if sovrappone then
                    sovrapposti[#sovrapposti + 1] = s
                    if regione.inizio >= s.pos - C.eps and
                       regione.fine <= s.fine + C.eps then
                        dentro[#dentro + 1] = s
                    end
                end
            end

            if #sovrapposti > 0 and #dentro == 0 then
                errori[#errori + 1] = string.format(
                    "regione '%s' (%.3f-%.3f) esce dall'item: correggi i bordi",
                    regione.nome, regione.inizio, regione.fine)
            elseif #dentro > 1 then
                errori[#errori + 1] = string.format(
                    "regione '%s' (%.3f-%.3f) cade in piu' item: seleziona un solo item",
                    regione.nome, regione.inizio, regione.fine)
            elseif #dentro == 0 then
                -- Una regione BCN fuori dagli item scelti non e' un errore:
                -- puo' appartenere a un'altra traccia. Si ignora.
            else
                local s = dentro[1]
                local durata = regione.fine - regione.inizio
                if durata * 1000.0 < C.min_durata_ms then
                    errori[#errori + 1] = string.format(
                        "regione '%s' troppo breve (%.1f ms, minimo %.0f ms)",
                        regione.nome, durata * 1000.0, C.min_durata_ms)
                elseif regione.inizio < s.pos - C.eps or regione.fine > s.fine + C.eps then
                    errori[#errori + 1] = string.format(
                        "regione '%s' esce dall'item (%.3f-%.3f contro %.3f-%.3f)",
                        regione.nome, regione.inizio, regione.fine, s.pos, s.fine)
                else
                    eventi[#eventi + 1] = {
                        id = #eventi + 1,
                        regione_id = regione.id,
                        nome = regione.nome,
                        inizio = regione.inizio,
                        fine = regione.fine,
                        durata = durata,
                        snapshot = s,
                    }
                end
            end
        end
    end

    table.sort(eventi, function(a, b)
        if a.inizio == b.inizio then return a.fine < b.fine end
        return a.inizio < b.inizio
    end)
    for i, e in ipairs(eventi) do e.id = i end

    if #errori > 0 then
        return nil, table.concat(errori, "\n")
    end
    if #eventi == 0 then
        return nil, "nessuna regione '" .. C.prefisso .. C.nome_fiato ..
                    "' negli item scelti. Creane una con 'Annota fiato'."
    end
    return eventi
end

-- Tempo dell'envelope: per una take envelope i punti sono relativi
-- all'inizio dell'item (documentazione SWS: "counted from take position").
-- Per una track envelope sono tempo di progetto.
function B.tempo_envelope(s, tempo_timeline, motore)
    if motore == "track" then return tempo_timeline end
    return tempo_timeline - s.pos
end

function B.chiave_evento(evento)
    local s = evento.snapshot
    local a = B.arrotonda(evento.inizio - s.pos, 3)
    local b = B.arrotonda(evento.fine - s.pos, 3)
    local guid = s.take_guid or tostring(s.take)
    return string.format("%s|%.3f|%.3f", guid, a, b)
end

-- Fade effettivo, limitato ai bordi dell'item e alla durata del respiro.
-- Restituisce nil, errore se non c'e' spazio per un fade positivo.
function B.fade_effettivo(evento, fade_secondi)
    local s = evento.snapshot
    local disponibile_prima = evento.inizio - s.pos
    local disponibile_dopo = s.fine - evento.fine
    local fade = fade_secondi
    if fade > disponibile_prima then fade = disponibile_prima end
    if fade > disponibile_dopo then fade = disponibile_dopo end
    if fade < 0 then fade = 0 end
    return fade
end

-- Limiti del fade per un evento: nessun punto fuori dall'item.
function B.limiti_fade(evento, fade)
    local s = evento.snapshot
    local fade_eff = B.fade_effettivo(evento, fade)
    if fade_eff < C.fade_min_sec then
        return nil, string.format(
            "fade troppo vicino al bordo dell'item " ..
            "(disponibile %.1f ms, minimo %.1f ms)",
            fade_eff * 1000.0, C.fade_min_sec * 1000.0)
    end
    local pre = evento.inizio - fade_eff
    local post = evento.fine + fade_eff
    if pre < s.pos - C.eps or post > s.fine + C.eps then
        return nil, "i punti di fade uscirebbero dall'item"
    end
    return fade_eff, nil
end

return B
