/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Pins the two spellings of a house-at reference, with no engine and no game data: which names
// and numbers name a start position, which are refused, and what is written back.

#include <cstdio>
#include <cstring>

#include "houseat.h"

namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-64s %s\n", what, condition ? "ok" : "FAILED");

	if (!condition) {
		Failures++;
	}
}


constexpr char const * const SpawnSpelling[HOUSE_AT_COUNT] = {
	"Spawn1", "Spawn2", "Spawn3", "Spawn4", "Spawn5", "Spawn6", "Spawn7", "Spawn8"
};

constexpr char const * const PlayerAtSpelling[HOUSE_AT_COUNT] = {
	"<Player @ A>", "<Player @ B>", "<Player @ C>", "<Player @ D>",
	"<Player @ E>", "<Player @ F>", "<Player @ G>", "<Player @ H>"
};

}


int main(void)
{
	/*
	 * Both spellings, in either case, name the same eight positions.
	 */
	{
		bool names = true;
		bool numbers = true;
		for (int slot = 0; slot < HOUSE_AT_COUNT; slot++) {
			names = names && House_At_Slot_From_Name(SpawnSpelling[slot]) == slot;
			names = names && House_At_Slot_From_Name(PlayerAtSpelling[slot]) == slot;
			numbers = numbers && House_At_Slot(static_cast<HousesType>(50 + slot)) == slot;
			numbers = numbers && House_At_Slot(static_cast<HousesType>(4475 + slot)) == slot;
		}
		Check(names, "Spawn1..Spawn8 and <Player @ A>..<Player @ H> name positions 0..7");
		Check(numbers, "50..57 and 4475..4482 number positions 0..7");
		Check(House_At_Slot_From_Name("SPAWN3") == 2 && House_At_Slot_From_Name("<player @ c>") == 2,
			"either spelling is matched without regard to case");
		Check(Is_House_At(HOUSE_SPAWN_FIRST) && Is_House_At(HOUSE_SPAWN_LAST) &&
			Is_House_At(HOUSE_PLAYER_AT_FIRST) && Is_House_At(HOUSE_PLAYER_AT_LAST),
			"the ends of both numeric ranges are house-at references");
	}

	/*
	 * Anything else is refused, the numbers just past each range included.
	 */
	{
		bool refused = true;
		char const * const rejected[] = {
			"Spawn0", "Spawn9", "Spawn10", "Spawn 1", "Spawn1x", "Spawn", "<Player @ I>",
			"<Player @ A", "Player @ A", "<Player @A>", "GDI", "Neutral", ""
		};
		for (char const * name : rejected) {
			refused = refused && House_At_Slot_From_Name(name) == -1;
		}
		Check(refused && House_At_Slot_From_Name(nullptr) == -1,
			"a name that is not one of the sixteen spellings names nothing");

		bool outside = true;
		int const numbers[] = { -1, 0, 1, 3, 49, 58, 59, 60, 4474, 4483 };
		for (int number : numbers) {
			outside = outside && House_At_Slot(static_cast<HousesType>(number)) == -1;
			outside = outside && !Is_House_At(static_cast<HousesType>(number));
		}
		Check(outside, "49, 58..60, 4474 and 4483 name nothing, as does a country");
	}

	/*
	 * A position is written back in the Tiberian Sun spelling and reads back as itself.
	 */
	{
		bool round = true;
		for (int slot = 0; slot < HOUSE_AT_COUNT; slot++) {
			round = round && House_At_Index(slot) == static_cast<HousesType>(50 + slot);
			round = round && House_At_Slot(House_At_Index(slot)) == slot;
			round = round && House_At_Slot_From_Name(House_At_Name(slot)) == slot;
		}
		Check(round, "a position round trips through 50 + slot and its Spawn name");
		Check(std::strcmp(House_At_Name(0), "Spawn1") == 0 && std::strcmp(House_At_Name(7), "Spawn8") == 0,
			"the written name is Spawn<slot + 1>");
		Check(House_At_Index(-1) == HOUSE_NONE && House_At_Index(HOUSE_AT_COUNT) == HOUSE_NONE &&
			House_At_Name(-1) == nullptr && House_At_Name(HOUSE_AT_COUNT) == nullptr,
			"a slot outside 0..7 has no index and no name");
	}

	return Failures == 0 ? 0 : 1;
}
