/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/


#pragma once

#include "house.hh"


/*
 * A house-at reference names whoever starts at one of the eight numbered start positions.
 * No house exists for it: a scenario spells it "Spawn1".."Spawn8" or "<Player @ A>".."<Player @ H>",
 * a numeric parameter 50..57 or 4475..4482, and it resolves to a live house only while
 * somebody holds that position.
 */
constexpr int HOUSE_AT_COUNT = 8;

// Either spelling, exact and case-insensitive, to its position 0..7; -1 for anything else.
int House_At_Slot_From_Name(char const * name);

// Either numeric family to its position 0..7; -1 for anything else, 58..60 included.
int House_At_Slot(HousesType house);

bool Is_House_At(HousesType house);

// The spellings written back: 50 + slot and "Spawn<slot + 1>".
HousesType House_At_Index(int slot);
char const * House_At_Name(int slot);
