-- SPDX-License-Identifier: MPL-2.0
-- ============================================================================
--  BreathClick Next - configurazione (P1)
--
--  Questo e' l'unico file che un utente puo' modificare. I percorsi non
--  servono: tutti i moduli stanno accanto a questo file.
-- ============================================================================

local C = {}

-- Versione mostrata nel pannello e nella diagnostica.
C.versione = "2026-10-21-p1"

-- Prefisso delle regioni che rappresentano un respiro.
-- Le regioni si creano a mano in REAPER (tasto destro -> Insert region) oppure
-- con il pulsante "Annota fiato" del pannello. Il nome deve iniziare cosi'.
C.prefisso = "BCN: "
C.nome_fiato = "fiato"

-- Attenuazione predefinita e fade.
C.attenua_db = -12.0
C.fade_ms = 15.0
C.fade_min_ms = 5.0
C.fade_max_ms = 50.0

-- Motore di attenuazione:
--   "take"  = volume envelope della take (preferito, non tocca la traccia)
--   "track" = volume envelope della traccia (ripiego, piu' invasivo)
C.motore_envelope = "take"

-- Comandi nativi REAPER usati per creare l'envelope quando non esiste.
-- Sono ID stabili di REAPER 7.x; non richiedono SWS.
--   40406 = Traccia: Envelope volume traccia visibile
--   40693 = Presa: Envelope della presa volume. On/off
C.azione_track_volume = 40406
C.azione_take_volume = 40693

-- Tolleranze.
C.eps = 0.001            -- secondi, confronti temporali
C.min_durata_ms = 10.0   -- sotto questa durata la regione non e' un respiro
C.tolleranza_db = 0.05   -- dB, verifica dei punti scritti
C.tolleranza_tempo = 0.002
C.tolleranza_punti = 0.0005 -- secondi, confronto punti del nostro envelope
C.fade_min_sec = C.fade_min_ms / 1000.0
C.fade_max_sec = C.fade_max_ms / 1000.0

-- Coordinate dei punti del take envelope. In P1 si assume il tempo relativo
-- all'inizio dell'item; il test REAPER verifica che i punti siano dove sono
-- stati scritti. Se il probe mostrasse un'altra coordinata, il motore take
-- viene bloccato e si usa il track envelope.
C.take_envelope_coordinate = "item"

-- Se true, il pannello mostra dettagli tecnici (punti, GUID, motore).
C.diagnostica = false

-- Non usare SWS: tutte le funzioni sono API native di REAPER 7.x.
C.richiede_sws = false

return C
