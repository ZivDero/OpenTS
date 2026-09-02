---
title: Write an out-of-sync report with bounded histories
category: feature
release: 0.2.0
targets:
- type: system
  id: developer-mode
  effect: changed
credit:
- ZivDero
---

When a multiplayer game goes out of sync the engine now writes its diagnostic report into the
`Debug` folder beside the executable, named for the local player and the frame, rather than a
`SYNC<n>.TXT` in the working directory. The report is written once per frame and names every
player whose checksum disagreed, records the session identity and seed so two players' reports
can be lined up, keys each object to its stable identifier, and no longer draws from the shared
random generator while it is written. It also carries bounded, newest-first histories of the
recent random draws, target assignments, mission changes, facing changes, animation creations
and events, so a divergence can be traced to the point it began. The out-of-sync message box now
reads "The game is out of sync." instead of "Reconnection Error!".
