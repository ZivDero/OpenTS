/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/


#include "houseat.h"

#include <cstring>

static_assert(HOUSE_SPAWN_LAST - HOUSE_SPAWN_FIRST + 1 == HOUSE_AT_COUNT, "one Spawn alias per start position");
static_assert(HOUSE_PLAYER_AT_LAST - HOUSE_PLAYER_AT_FIRST + 1 == HOUSE_AT_COUNT, "one Player @ alias per start position");

namespace {

constexpr char const * const SpawnNames[HOUSE_AT_COUNT] = {
	"Spawn1", "Spawn2", "Spawn3", "Spawn4", "Spawn5", "Spawn6", "Spawn7", "Spawn8"
};

constexpr char const * const PlayerAtNames[HOUSE_AT_COUNT] = {
	"<Player @ A>", "<Player @ B>", "<Player @ C>", "<Player @ D>",
	"<Player @ E>", "<Player @ F>", "<Player @ G>", "<Player @ H>"
};

}


int House_At_Slot_From_Name(char const * name)
{
	if (name == nullptr) {
		return -1;
	}

	for (int slot = 0; slot < HOUSE_AT_COUNT; slot++) {
		if (_stricmp(name, SpawnNames[slot]) == 0 || _stricmp(name, PlayerAtNames[slot]) == 0) {
			return slot;
		}
	}
	return -1;
}


int House_At_Slot(HousesType house)
{
	if (house >= HOUSE_SPAWN_FIRST && house <= HOUSE_SPAWN_LAST) {
		return house - HOUSE_SPAWN_FIRST;
	}
	if (house >= HOUSE_PLAYER_AT_FIRST && house <= HOUSE_PLAYER_AT_LAST) {
		return house - HOUSE_PLAYER_AT_FIRST;
	}
	return -1;
}


bool Is_House_At(HousesType house)
{
	return House_At_Slot(house) != -1;
}


HousesType House_At_Index(int slot)
{
	if (slot < 0 || slot >= HOUSE_AT_COUNT) {
		return HOUSE_NONE;
	}
	return static_cast<HousesType>(HOUSE_SPAWN_FIRST + slot);
}


char const * House_At_Name(int slot)
{
	if (slot < 0 || slot >= HOUSE_AT_COUNT) {
		return nullptr;
	}
	return SpawnNames[slot];
}
