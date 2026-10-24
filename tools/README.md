# Strumenti locali

Questa directory raccoglie toolchain e utilità locali che non vengono
committate (pesanti e rigenerabili). Non sono dipendenze globali.

- `cmake-3.28.6-windows-x86_64/` — CMake locale usato da `build.ps1`.
  È escluso da Git; se manca, lo script cerca CMake nel PATH o nei percorsi
  standard.

Il codice e le build non devono dipendere da una posizione assoluta: usare
`build.ps1`, che risolve il percorso rispetto alla radice del progetto.
