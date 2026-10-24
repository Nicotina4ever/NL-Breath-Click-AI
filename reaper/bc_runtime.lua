-- SPDX-License-Identifier: MPL-2.0
-- ReaPack owns scripts; the verified release owns its optional private backend.
local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
local J=dofile(here..'/bc_json.lua')
local R={}
local binary='bc_studio_cli'..(package.config:sub(1,1)=='\\' and '.exe' or '')
local function manifest(folder)
    if R.manifest_folder==folder then return R.manifest end
    local value=J.read(folder..'/runtime-release.json')
    if type(value)~='table' or value.schema~=1 or type(value.version)~='string' or not value.version:match('^[%w%.%-]+$') then return nil end
    R.manifest_folder=folder;R.manifest=value;return value
end
function R.engine(folder)
    local direct=folder..'/bin/'..binary
    if reaper.file_exists(direct) then return direct end
    local m=manifest(folder)
    if m then
        local installed=folder..'/runtime/'..m.version..'/bin/'..binary
        local ready=J.read(folder..'/runtime/'..m.version..'/READY.json')
        if ready and ready.source_commit==m.source_commit and ready.archive_sha256==m.sha256 and reaper.file_exists(installed) then return installed end
    end
end
function R.install(folder)
    if R.installing or R.engine(folder) then return end
    assert(reaper.GetOS():match('Win'), 'Questa release del backend e\' per Windows x64')
    assert(manifest(folder),'Motore incluso nello ZIP REAPER. Per ReaPack: sincronizza il pacchetto completo, inclusa runtime-release.json.')
    local function quote(s) assert(not s:find('[\r\n"]'),'Percorso non valido');return '"'..s..'"' end
    R.attempt=reaper.genGuid():gsub('[{}]','')
    local command='powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File '..quote(folder..'/bc_runtime.ps1')..' -PackageDirectory '..quote(folder)..' -Attempt '..quote(R.attempt)
    reaper.ExecProcess(command,-2)
    R.installing=true;R.started=reaper.time_precise()
end
function R.tick(folder)
    if not R.installing then return end
    local result=J.read(folder..'/runtime/install-status.json')
    if result and result.attempt==R.attempt and result.status=='ready' and R.engine(folder) then R.installing=false;return 'Motore verificato e pronto. Puoi analizzare.' end
    if result and result.attempt==R.attempt and result.status=='failed' then R.installing=false;return 'Installazione motore: '..tostring(result.error) end
    if reaper.time_precise()-R.started>900 then R.installing=false;return 'Installazione non completata: riprova con Installa motore.' end
    return 'Installazione del motore verificato in corso... il pannello rimane utilizzabile.'
end
return R
