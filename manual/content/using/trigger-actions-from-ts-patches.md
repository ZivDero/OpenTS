---
title: Trigger actions from Tiberian Sun patches and Vinifera
summary: The numbers a map written for CnCNet's Tiberian Sun patches or for Vinifera must give its trigger actions to run under OpenTS, and where those actions behave differently.
category: compatibility-migration
source_files:
  - code/taction.hh
  - code/taction.cpp
related:
  - type: internal
    id: trigger-action-registry
  - type: format
    id: scenario-objects
  - type: format
    id: save-games
---

OpenTS numbers the trigger actions beyond the stock set the way Vinifera does, so a map written for Vinifera keeps its numbers. A map written for CnCNet's Tiberian Sun patches numbers the same actions one lower and carries a Print Difficulty action that OpenTS does not have: move each of its action numbers from `105` through `116` up by one, and remove any action numbered `108`.

| Tiberian Sun patches | OpenTS and Vinifera | Action |
| --- | --- | --- |
| 105 | 106 | [Give Credits](/mapping/actions/taction-give-credits/) |
| 106 | 107 | [Enable Short Game](/mapping/actions/taction-enable-short-game/) |
| 107 | 108 | [Disable Short Game](/mapping/actions/taction-disable-short-game/) |
| 108 | none | Print Difficulty, dropped |
| none | 109 | [Create Building At](/mapping/actions/taction-create-building-at/) |
| 109 | 110 | [Destroy all of](/mapping/actions/taction-house-destroy-all/) |
| 110 | 111 | [Make Elite](/mapping/actions/taction-make-elite/) |
| 111 | 112 | [Enable Ally Reveal](/mapping/actions/taction-enable-ally-reveal/) |
| 112 | 113 | [Disable Ally Reveal](/mapping/actions/taction-disable-ally-reveal/) |
| 113 | 114 | [Create Autosave](/mapping/actions/taction-create-autosave/) |
| 114 | 115 | [Delete Attached Objects](/mapping/actions/taction-delete-object/) |
| 115 | 116 | [All Assign Mission](/mapping/actions/taction-all-assign-mission/) |
| 116 | 117 | [Make Ally (One-Way)](/mapping/actions/taction-make-ally-one-way/) |
| none | 118 | [Make Enemy (One-Way)](/mapping/actions/taction-make-enemy-one-way/) |

An action number OpenTS does not define, Vinifera's `119` and above included, does nothing when its trigger fires; the trigger's other actions still run.

## Where the actions differ

- **Give Credits** with a negative amount spends the credits like a purchase: the house's credits stop at zero and its stored Tiberium covers the rest, as under Vinifera. The Tiberian Sun patches subtracted the amount outright, so a house could go below zero.
- **All Assign Mission** reaches infantry, vehicles and aircraft, as under the Tiberian Sun patches. Vinifera also gives the mission to structures.
- **Create Autosave** writes `AUTOSAVE1.SAV` in a campaign or skirmish game, where the Tiberian Sun patches rotated through numbered slots. [Save games](/formats/save-games/) owns the details.
- **Spawn houses**. A house parameter of `50` through `57` names whoever starts at that position, as both patches resolve it. A position nobody holds names no house under OpenTS and Vinifera, so the action does nothing; the Tiberian Sun patches fell back to the Neutral house. [Scenario objects](/formats/scenario-objects/#spawn-houses) owns the spellings.
