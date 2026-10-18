/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "component.h"

#include "crc.h"
#include "savestream.h"

#include <cassert>


namespace {

std::vector<ComponentTypeClass> & Types(void)
{
	static std::vector<ComponentTypeClass> types;
	return(types);
}


ComponentTypeClass const * Find_Type(std::string const & name)
{
	for (ComponentTypeClass const & type : Types()) {
		if (type.Name == name) {
			return(&type);
		}
	}
	return(nullptr);
}

}	// namespace


/// <summary>
/// Fetches the world that holds every object's components.
/// The world is created on first use and is never destroyed, so an object destroyed
/// during process exit can still delete its entity.
/// </summary>
flecs::world & Component_World(void)
{
	static flecs::world * world = new flecs::world;
	return(*world);
}


/// <summary>
/// Fetches the registered component types in registration order, which is the order a
/// save writes them and the sync checksum folds them in.
/// </summary>
std::vector<ComponentTypeClass> const & Component_Types(void)
{
	return(Types());
}


/// <summary>
/// Records a component type for saving and checksums. Register_Component calls this; a
/// second type under a name already registered asserts.
/// </summary>
void Add_Component_Type(ComponentTypeClass const & type)
{
	assert(Find_Type(type.Name) == nullptr);
	Types().push_back(type);
}


/// <summary>
/// Registers every component type the engine defines. Startup calls this once, after the
/// save classes are registered and before any object exists.
/// </summary>
void Register_Components(void)
{
}


/// <summary>
/// Fetches the entity behind an object's slot, creating it when the slot holds none.
/// </summary>
std::uint64_t Component_Entity(std::uint64_t & slot)
{
	if (slot == 0) {
		slot = Component_World().entity().id();
	}
	return(slot);
}


/// <summary>
/// Deletes the entity behind an object's slot together with its components, and empties
/// the slot. A handle another component holds to the entity then fails its liveness check.
/// </summary>
void Destroy_Components(std::uint64_t & slot)
{
	if (slot == 0) {
		return;
	}

	flecs::world & world = Component_World();
	if (world.is_alive(slot)) {
		flecs::entity(world, slot).destruct();
	}
	slot = 0;
}


/// <summary>
/// Writes or reads the components an object owns, in registration order. A load replaces
/// whatever components the object had before, and fails the stream on a component type
/// this build does not register, on one named twice, or on a body that its Serialize
/// reads short or long.
/// </summary>
void Serialize_Components(std::uint64_t & slot, SaveStreamClass & stream)
{
	std::vector<ComponentTypeClass> const & types = Types();
	flecs::world & world = Component_World();

	if (stream.Is_Saving()) {
		flecs::entity const entity(world, slot);

		int count = 0;
		if (slot != 0) {
			for (ComponentTypeClass const & type : types) {
				if (entity.owns(type.ID)) {
					count++;
				}
			}
		}
		stream.Serialize(count);

		for (ComponentTypeClass const & type : types) {
			if (count == 0) {
				break;
			}
			if (!entity.owns(type.ID)) {
				continue;
			}
			count--;

			unsigned short namelength = (unsigned short)type.Name.size();
			stream.Serialize(namelength);
			stream.Serialize_Bytes((void *)type.Name.data(), namelength);

			unsigned int const lengthoffset = stream.Offset();
			int bodylength = 0;
			stream.Serialize(bodylength);

			unsigned int const bodystart = stream.Offset();
			type.Serialize(entity, stream);
			bodylength = (int)(stream.Offset() - bodystart);
			stream.Overwrite_Bytes(lengthoffset, &bodylength, sizeof(bodylength));
		}
		return;
	}

	Destroy_Components(slot);

	int count = 0;
	stream.Serialize(count);

	// Each component carries at least its name length and its body length.
	if (stream.Was_Error() || !stream.Fits(count, sizeof(unsigned short) + sizeof(int))) {
		return;
	}

	for (int index = 0; index < count; index++) {
		unsigned short namelength = 0;
		stream.Serialize(namelength);
		if (stream.Was_Error() || !stream.Fits(namelength, 1)) {
			return;
		}

		std::string name(namelength, '\0');
		stream.Serialize_Bytes(name.data(), namelength);

		int bodylength = 0;
		stream.Serialize(bodylength);
		if (stream.Was_Error() || !stream.Fits(bodylength, 1)) {
			return;
		}

		ComponentTypeClass const * type = Find_Type(name);
		if (type == nullptr || (slot != 0 && flecs::entity(world, slot).owns(type->ID))) {
			stream.Fail();
			return;
		}

		unsigned int const bodyend = stream.Offset() + (unsigned int)bodylength;
		{
			SaveStreamClass::BoundScope bound(stream, bodyend);
			type->Serialize(flecs::entity(world, Component_Entity(slot)), stream);
		}
		if (stream.Was_Error()) {
			return;
		}
		if (stream.Offset() != bodyend) {
			stream.Fail();
			return;
		}
	}
}


/// <summary>
/// Folds the components an object owns into a sync checksum, in registration order. An
/// object that owns none leaves the checksum as it was.
/// </summary>
void Compute_Component_CRC(std::uint64_t slot, CRCEngine & crc)
{
	if (slot == 0) {
		return;
	}

	flecs::entity const entity(Component_World(), slot);
	std::vector<ComponentTypeClass> const & types = Types();

	for (int index = 0; index < (int)types.size(); index++) {
		if (entity.owns(types[index].ID)) {
			crc(index);
			types[index].Compute_CRC(entity, crc);
		}
	}
}
