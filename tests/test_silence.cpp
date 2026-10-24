// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  test_silence.cpp - Il rilevatore di silenzio con isteresi
//
//  Il bug che questo test blinda, trovato il 17/10:
//
//  detectSilence() lavora su result.frameLevelDb, che l'analizzatore riempie
//  SOLO se includeScores e' vero. Il comando "silence" del CLI non lo forzava,
//  quindi riceveva un vettore vuoto e rispondeva "zero silenzi" su qualunque
//  file. Sembrava un errore dell'algoritmo, era un dato mai calcolato.
//
//  Qui si verifica l'algoritmo in se', con un inviluppo di livello costruito a
//  mano: niente audio, niente analizzatore, solo la logica.
// ============================================================================
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/dsp/silence_gate.h"

namespace {

int g_eseguiti = 0;
int g_falliti = 0;

void check(bool condizione, const char* nome) {
    ++g_eseguiti;
    if (condizione) {
        std::printf("  [ OK ] %s\n", nome);
    } else {
        ++g_falliti;
        std::printf("  [  X  ] %s\n", nome);
    }
}

void section(const char* nome) {
    std::printf("\n%s\n", nome);
}

// Il tono: -20 dB. Il silenzio: -70 dB.
void aggiungi(std::vector<float>& livello, int frame, float db) {
    for (int i = 0; i < frame; ++i) livello.push_back(db);
}

bc::SilenceGateConfig configurazione() {
    bc::SilenceGateConfig config;
    config.thresholdDb = -48.0f;
    config.hysteresisDb = 6.0f;
    config.minSilenceMs = 200;
    return config;
}

// ---------------------------------------------------------------------------
//  Un silenzio singolo, della durata giusta
// ---------------------------------------------------------------------------
void testSilenzioSemplice() {
    section("Un silenzio singolo");

    const double sr = 16000.0;
    const int64_t hop = 160;   // 10 ms per frame

    std::vector<float> livello;
    aggiungi(livello, 100, -20.0f);   // 1,0 s di suono
    aggiungi(livello, 50, -70.0f);    // 0,5 s di silenzio
    aggiungi(livello, 100, -20.0f);   // 1,0 s di suono

    const std::vector<bc::SilenceRange> silenzi =
        bc::detectSilence(livello, hop, sr, configurazione());

    check(silenzi.size() == 1, "trovato un solo silenzio");
    if (silenzi.size() == 1) {
        const double inizio = silenzi[0].startSeconds(sr);
        const double fine = silenzi[0].endSeconds(sr);
        check(std::fabs(inizio - 1.0) < 0.02, "comincia dove comincia il silenzio");
        check(std::fabs(fine - 1.5) < 0.02, "finisce dove finisce il silenzio");
    }
}

// ---------------------------------------------------------------------------
//  L'isteresi: e' il motivo per cui esiste questo componente
//
//  Un respiro a -43 dB sta SOPRA la soglia di ingresso (-48) ma SOTTO quella
//  di uscita (-42). Con una soglia secca spezzerebbe la pausa; con l'isteresi
//  no. Ed e' esattamente il caso reale: il respiro sta dentro la pausa.
// ---------------------------------------------------------------------------
void testIsteresi() {
    section("L'isteresi non fa spezzare la pausa dal respiro");

    const double sr = 16000.0;
    const int64_t hop = 160;

    std::vector<float> livello;
    aggiungi(livello, 100, -20.0f);   // suono
    aggiungi(livello, 20, -70.0f);    // inizio pausa, 200 ms
    aggiungi(livello, 10, -43.0f);    // IL RESPIRO: sopra la soglia, sotto l'uscita
    aggiungi(livello, 20, -70.0f);    // resto della pausa
    aggiungi(livello, 100, -20.0f);   // suono

    const std::vector<bc::SilenceRange> silenzi =
        bc::detectSilence(livello, hop, sr, configurazione());

    check(silenzi.size() == 1, "la pausa resta una sola, non due");
    if (silenzi.size() == 1) {
        const double durata = silenzi[0].lengthMs(sr);
        check(durata > 400.0, "la pausa e' intera, respiro compreso");
    }
}

// ---------------------------------------------------------------------------
//  Filtri sulle durate
// ---------------------------------------------------------------------------
void testDurate() {
    section("Filtri sulle durate");

    const double sr = 16000.0;
    const int64_t hop = 160;
    const bc::SilenceGateConfig config = configurazione();

    // Silenzio di 50 ms: sotto il minimo di 200, va scartato.
    std::vector<float> corto;
    aggiungi(corto, 100, -20.0f);
    aggiungi(corto, 5, -70.0f);
    aggiungi(corto, 100, -20.0f);
    const std::vector<bc::SilenceRange> scartati =
        bc::detectSilence(corto, hop, sr, config);
    check(scartati.empty(), "un silenzio di 50 ms viene scartato");

    // Un click di 30 ms in mezzo alla pausa non deve spezzarla in due:
    // e' piu' corto del suono minimo (20 ms di default? no, 30 ms lo supera)
    // quindi qui si verifica il caso opposto con un click di 10 ms.
    std::vector<float> conClick;
    aggiungi(conClick, 100, -20.0f);
    aggiungi(conClick, 30, -70.0f);
    aggiungi(conClick, 1, -20.0f);    // 10 ms di suono
    aggiungi(conClick, 30, -70.0f);
    aggiungi(conClick, 100, -20.0f);
    const std::vector<bc::SilenceRange> uniti =
        bc::detectSilence(conClick, hop, sr, config);
    check(uniti.size() == 1, "un click di 10 ms non spezza la pausa");
}

// ---------------------------------------------------------------------------
//  Casi limite
// ---------------------------------------------------------------------------
void testCasiLimite() {
    section("Casi limite");

    const double sr = 16000.0;
    const bc::SilenceGateConfig config = configurazione();

    const std::vector<bc::SilenceRange> vuoto =
        bc::detectSilence({}, 160, sr, config);
    check(vuoto.empty(), "inviluppo vuoto: nessun silenzio, nessun crash");

    std::vector<float> tuttoSuono(500, -20.0f);
    const std::vector<bc::SilenceRange> suono =
        bc::detectSilence(tuttoSuono, 160, sr, config);
    check(suono.empty(), "solo suono: nessun silenzio");

    std::vector<float> tuttoSilenzio(500, -70.0f);
    const std::vector<bc::SilenceRange> silenzio =
        bc::detectSilence(tuttoSilenzio, 160, sr, config);
    check(silenzio.size() == 1, "tutto silenzio: un silenzio solo");

    // Hop nullo: non deve dividere per zero.
    const std::vector<bc::SilenceRange> rotto =
        bc::detectSilence(tuttoSuono, 0, sr, config);
    check(rotto.empty(), "hop nullo: nessun crash");
}

} // namespace

int main() {
    std::printf("Rilevamento del silenzio\n");
    std::printf("========================\n");

    testSilenzioSemplice();
    testIsteresi();
    testDurate();
    testCasiLimite();

    std::printf("\n============================================\n");
    if (g_falliti == 0) {
        std::printf("ESITO: SUPERATO (%d controlli)\n", g_eseguiti);
        return 0;
    }
    std::printf("ESITO: FALLITO (%d su %d)\n", g_falliti, g_eseguiti);
    return 1;
}
