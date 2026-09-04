---
title: Add the trigger actions CnCNet maps use
category: feature
release: 0.2.0
targets:
- type: action
  id: TACTION_GIVE_CREDITS
  effect: added
- type: action
  id: TACTION_ENABLE_SHORT_GAME
  effect: added
- type: action
  id: TACTION_DISABLE_SHORT_GAME
  effect: added
- type: action
  id: TACTION_CREATE_BUILDING_AT
  effect: added
- type: action
  id: TACTION_HOUSE_DESTROY_ALL
  effect: added
- type: action
  id: TACTION_MAKE_ELITE
  effect: added
- type: action
  id: TACTION_ENABLE_ALLY_REVEAL
  effect: added
- type: action
  id: TACTION_DISABLE_ALLY_REVEAL
  effect: added
- type: action
  id: TACTION_CREATE_AUTOSAVE
  effect: added
- type: action
  id: TACTION_DELETE_OBJECT
  effect: added
- type: action
  id: TACTION_ALL_ASSIGN_MISSION
  effect: added
- type: action
  id: TACTION_MAKE_ALLY_ONE_WAY
  effect: added
- type: action
  id: TACTION_MAKE_ENEMY_ONE_WAY
  effect: added
credit: [ZivDero, Rampastring]
---

Maps gain the thirteen trigger actions that CnCNet's Tiberian Sun patches introduced and
Vinifera carries, numbered `106` through `118` as Vinifera numbers them: Give Credits, Enable
and Disable Short Game, Create Building At, Destroy all of, Make Elite, Enable and Disable Ally
Reveal, Create Autosave, Delete Attached Objects, All Assign Mission, and the one-way Make Ally
and Make Enemy. A map written for the Tiberian Sun patches numbers the same actions `105`
through `116` with a Print Difficulty action at `108`; each of its numbers moves up by one and
Print Difficulty is dropped, since `105` stays Talk Bubble. Rampastring wrote the Tiberian Sun
patches implementation these actions follow.
