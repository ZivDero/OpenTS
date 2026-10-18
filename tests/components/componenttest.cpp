/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises component storage, its save block and its checksum without the engine.

#include "component.h"
#include "crc.h"
#include "saveload.h"
#include "savestream.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


unsigned int ExpectedGameVersion = 1;


namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-64s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition) {
		Failures++;
	}
}


struct HealthComponent
{
	int Strength = 0;
	short Regen = 0;

	void Serialize(SaveStreamClass & stream)
	{
		stream.Serialize(Strength);
		stream.Serialize(Regen);
	}

	void Compute_CRC(CRCEngine & crc) const
	{
		crc(Strength);
		crc(Regen);
	}
};


struct TagComponent
{
	int Value = 0;

	void Serialize(SaveStreamClass & stream)
	{
		stream.Serialize(Value);
	}

	void Compute_CRC(CRCEngine & crc) const
	{
		crc(Value);
	}
};


// Its Serialize writes four bytes and reads two, so a load of it always ends short of its body.
struct LopsidedComponent
{
	int Value = 0;

	void Serialize(SaveStreamClass & stream)
	{
		if (stream.Is_Saving()) {
			stream.Serialize(Value);
		} else {
			short half = 0;
			stream.Serialize(half);
			Value = half;
		}
	}

	void Compute_CRC(CRCEngine & crc) const
	{
		crc(Value);
	}
};


std::vector<unsigned char> Save(std::uint64_t & slot)
{
	std::vector<unsigned char> buffer;
	SaveStreamClass stream(buffer, SaveStreamClass::MODE_SAVE);
	Serialize_Components(slot, stream);
	return(buffer);
}


// Loads the buffer into the slot and reports whether the block read whole and exactly.
bool Load(std::vector<unsigned char> & buffer, std::uint64_t & slot)
{
	SaveStreamClass stream(buffer, SaveStreamClass::MODE_LOAD);
	Serialize_Components(slot, stream);
	return(!stream.Was_Error() && stream.Offset() == stream.Size());
}


void Put_Int(std::vector<unsigned char> & buffer, int value)
{
	unsigned char bytes[sizeof(value)];
	std::memcpy(bytes, &value, sizeof(value));
	buffer.insert(buffer.end(), bytes, bytes + sizeof(value));
}


void Put_Name(std::vector<unsigned char> & buffer, char const * name)
{
	unsigned short length = (unsigned short)std::strlen(name);
	unsigned char bytes[sizeof(length)];
	std::memcpy(bytes, &length, sizeof(length));
	buffer.insert(buffer.end(), bytes, bytes + sizeof(length));
	buffer.insert(buffer.end(), name, name + length);
}


int Checksum(std::uint64_t slot)
{
	CRCEngine crc;
	crc(12345);
	Compute_Component_CRC(slot, crc);
	return(crc());
}

}	// namespace


int main(void)
{
	Register_Component<HealthComponent>("Health");
	Register_Component<TagComponent>("Tag");
	Register_Component<LopsidedComponent>("Lopsided");

	/*
	 * An object without components has no entity and writes a count of zero.
	 */
	{
		std::uint64_t slot = 0;
		Check(Get_Component<HealthComponent>(slot) == nullptr, "an empty slot has no component");
		std::vector<unsigned char> buffer = Save(slot);
		Check(buffer.size() == sizeof(int), "an empty slot writes only its count");

		std::uint64_t loaded = 0;
		Check(Load(buffer, loaded), "an empty block loads");
		Check(loaded == 0, "an empty block creates no entity");
	}

	/*
	 * Components come back with their values, and the record is read exactly.
	 */
	{
		std::uint64_t slot = 0;
		Add_Component<TagComponent>(slot).Value = 77;
		HealthComponent & health = Add_Component<HealthComponent>(slot);
		health.Strength = 300;
		health.Regen = 4;
		Check(slot != 0, "adding a component creates the entity");

		std::vector<unsigned char> buffer = Save(slot);

		std::uint64_t loaded = 0;
		Check(Load(buffer, loaded), "a saved block loads whole");
		HealthComponent const * back = Get_Component<HealthComponent>(loaded);
		TagComponent const * tag = Get_Component<TagComponent>(loaded);
		Check(back != nullptr && back->Strength == 300 && back->Regen == 4, "the health component comes back");
		Check(tag != nullptr && tag->Value == 77, "the tag component comes back");
		Check(Get_Component<LopsidedComponent>(loaded) == nullptr, "no component appears that was not saved");

		// Health was registered first, so its name follows the count although it was added second.
		Check(buffer.size() > 10 && std::memcmp(&buffer[6], "Health", 6) == 0, "components are written in registration order");

		Destroy_Components(slot);
		Destroy_Components(loaded);
	}

	/*
	 * A load replaces the components the object had before.
	 */
	{
		std::uint64_t source = 0;
		Add_Component<TagComponent>(source).Value = 5;
		std::vector<unsigned char> buffer = Save(source);

		std::uint64_t target = 0;
		Add_Component<HealthComponent>(target).Strength = 9;
		Check(Load(buffer, target), "a block loads over existing components");
		Check(Get_Component<HealthComponent>(target) == nullptr, "a component the block lacks is gone");
		Check(Get_Component<TagComponent>(target) != nullptr, "the block's component is present");

		Destroy_Components(source);
		Destroy_Components(target);
	}

	/*
	 * A removed component is not written.
	 */
	{
		std::uint64_t slot = 0;
		Add_Component<TagComponent>(slot);
		Remove_Component<TagComponent>(slot);
		Check(Get_Component<TagComponent>(slot) == nullptr, "a removed component is gone");
		Check(Save(slot).size() == sizeof(int), "a slot whose components were removed writes a count of zero");
		Destroy_Components(slot);
	}

	/*
	 * Destroying the entity empties the slot, and a handle to it stops being alive.
	 */
	{
		std::uint64_t slot = 0;
		Add_Component<TagComponent>(slot);
		std::uint64_t const handle = slot;
		Destroy_Components(slot);
		Check(slot == 0, "destroying empties the slot");
		Check(!Component_World().is_alive(handle), "a handle to a destroyed entity is not alive");

		std::uint64_t reused = 0;
		Add_Component<TagComponent>(reused);
		Check(!Component_World().is_alive(handle), "a recycled index does not revive the old handle");
		Destroy_Components(reused);
	}

	/*
	 * The loader refuses what this build could not have written.
	 */
	{
		std::vector<unsigned char> buffer;
		Put_Int(buffer, 1);
		Put_Name(buffer, "Unknown");
		Put_Int(buffer, 4);
		Put_Int(buffer, 0);
		std::uint64_t slot = 0;
		Check(!Load(buffer, slot), "an unregistered component fails the load");
		Destroy_Components(slot);
	}
	{
		std::vector<unsigned char> buffer;
		Put_Int(buffer, 2);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 4);
		Put_Int(buffer, 1);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 4);
		Put_Int(buffer, 2);
		std::uint64_t slot = 0;
		Check(!Load(buffer, slot), "a component named twice fails the load");
		Destroy_Components(slot);
	}
	{
		std::uint64_t source = 0;
		Add_Component<LopsidedComponent>(source).Value = 3;
		std::vector<unsigned char> buffer = Save(source);
		std::uint64_t slot = 0;
		Check(!Load(buffer, slot), "a body read short fails the load");
		Destroy_Components(source);
		Destroy_Components(slot);
	}
	{
		std::vector<unsigned char> buffer;
		Put_Int(buffer, 1);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 2);
		Put_Int(buffer, 1);
		std::uint64_t slot = 0;
		Check(!Load(buffer, slot), "a body read long fails the load");
		Destroy_Components(slot);
	}
	{
		std::vector<unsigned char> buffer;
		Put_Int(buffer, 1000000);
		std::uint64_t slot = 0;
		Check(!Load(buffer, slot), "a count the stream cannot hold fails the load");
		Check(slot == 0, "a refused count creates no entity");
	}
	{
		std::vector<unsigned char> buffer;
		Put_Int(buffer, 1);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 100);
		Put_Int(buffer, 1);
		std::uint64_t slot = 0;
		Check(!Load(buffer, slot), "a body length past the stream fails the load");
		Destroy_Components(slot);
	}

	/*
	 * The checksum ignores an object without components and the order components were added.
	 */
	{
		std::uint64_t empty = 0;
		CRCEngine plain;
		plain(12345);
		Check(Checksum(empty) == plain(), "an object without components leaves the checksum alone");

		std::uint64_t first = 0;
		Add_Component<TagComponent>(first).Value = 1;
		Add_Component<HealthComponent>(first).Strength = 2;

		std::uint64_t second = 0;
		Add_Component<HealthComponent>(second).Strength = 2;
		Add_Component<TagComponent>(second).Value = 1;

		Check(Checksum(first) == Checksum(second), "the checksum follows registration order, not addition order");
		Check(Checksum(first) != plain(), "components change the checksum");

		Get_Component<TagComponent>(second)->Value = 8;
		Check(Checksum(first) != Checksum(second), "a changed value changes the checksum");

		Destroy_Components(first);
		Destroy_Components(second);
	}

	std::printf("\n%d failure(s)\n", Failures);
	return(Failures == 0 ? 0 : 1);
}
