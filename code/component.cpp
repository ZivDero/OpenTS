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
#include <deque>


namespace {

// A reference read from a save. Its target is a swizzle slot, so it lives in a deque, which
// keeps each element in place as more are added. The reference itself is found again through
// its component, whose storage may have moved by the time the slot is patched.
struct StagedRefClass
{
	AbstractClass * Target;
	flecs::entity_t Holder;
	flecs::entity_t Component;
	std::size_t Offset;
};


// The component a load is reading, so that a reference inside it can record where it sits.
struct ReadingClass
{
	flecs::entity_t Holder;
	flecs::entity_t Component;
	unsigned char const * Base;
	std::size_t Size;
};


std::vector<ComponentTypeClass> & Types(void)
{
	static std::vector<ComponentTypeClass> types;
	return(types);
}


std::deque<StagedRefClass> & Staged_Refs(void)
{
	static std::deque<StagedRefClass> staged;
	return(staged);
}


ReadingClass const *& Reading(void)
{
	static ReadingClass const * reading = nullptr;
	return(reading);
}


class ReadingScope
{
	public:
		explicit ReadingScope(ReadingClass const & reading) {Reading() = &reading;}
		~ReadingScope(void) {Reading() = nullptr;}

		ReadingScope(ReadingScope const &) = delete;
		ReadingScope & operator = (ReadingScope const &) = delete;
};


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
	static flecs::world * world = [] {
		flecs::world * created = new flecs::world;
		created->component<ComponentOwnerClass>("Owner").add(flecs::OnInstantiate, flecs::DontInherit);
		return(created);
	}();
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
/// Fetches the object's entity, creating it with the object as its owner when the object
/// has none.
/// </summary>
std::uint64_t Component_Entity(AbstractClass & object)
{
	if (object.Entity == 0) {
		object.Entity = Component_World().entity().set<ComponentOwnerClass>({&object}).id();
	}
	return(object.Entity);
}


/// <summary>
/// Deletes the object's entity together with its components. A reference another
/// component holds to the object then reads as empty.
/// </summary>
void Destroy_Components(AbstractClass & object)
{
	if (object.Entity == 0) {
		return;
	}

	flecs::world & world = Component_World();
	if (world.is_alive(object.Entity)) {
		flecs::entity(world, object.Entity).destruct();
	}
	object.Entity = 0;
}


/// <summary>
/// Writes or reads the components an object owns, in registration order. A load replaces
/// whatever components the object had before, and fails the stream on a component type
/// this build does not register, on one named twice, or on a body that its Serialize
/// reads short or long. References read here stay empty until Resolve_Component_Refs.
/// </summary>
void Serialize_Components(AbstractClass & object, SaveStreamClass & stream)
{
	std::vector<ComponentTypeClass> const & types = Types();
	flecs::world & world = Component_World();

	if (stream.Is_Saving()) {
		flecs::entity const entity(world, object.Entity);

		int count = 0;
		if (object.Entity != 0) {
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

	Destroy_Components(object);

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
		if (type == nullptr || (object.Entity != 0 && flecs::entity(world, object.Entity).owns(type->ID))) {
			stream.Fail();
			return;
		}

		flecs::entity_t const holder = Component_Entity(object);
		ReadingClass const reading = {
			holder,
			type->ID,
			(unsigned char const *)ecs_ensure_id(world, holder, type->ID, type->Size),
			type->Size,
		};

		unsigned int const bodyend = stream.Offset() + (unsigned int)bodylength;
		{
			SaveStreamClass::BoundScope bound(stream, bodyend);
			ReadingScope const scope(reading);
			type->Serialize(flecs::entity(world, holder), stream);
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
void Compute_Component_CRC(AbstractClass const & object, CRCEngine & crc)
{
	if (object.Entity == 0) {
		return;
	}

	flecs::entity const entity(Component_World(), object.Entity);
	std::vector<ComponentTypeClass> const & types = Types();

	for (int index = 0; index < (int)types.size(); index++) {
		if (entity.owns(types[index].ID)) {
			crc(index);
			types[index].Compute_CRC(entity, crc);
		}
	}
}


/// <summary>
/// Points every reference read since the load began at the object its slot now names. Call
/// this once the swizzle pass has patched the slots, and before anything reads a reference.
/// A reference whose component is gone by then is skipped.
/// </summary>
void Resolve_Component_Refs(void)
{
	flecs::world & world = Component_World();

	for (StagedRefClass const & staged : Staged_Refs()) {
		if (!world.is_alive(staged.Holder)) {
			continue;
		}
		unsigned char * component = (unsigned char *)ecs_get_mut_id(world, staged.Holder, staged.Component);
		if (component != nullptr) {
			((ObjectRefClass *)(component + staged.Offset))->Set(staged.Target);
		}
	}
	Staged_Refs().clear();
}


/// <summary>
/// Drops the references read since the load began without resolving them, for a load that
/// starts over or fails.
/// </summary>
void Discard_Component_Refs(void)
{
	Staged_Refs().clear();
}


/// <summary>
/// Points the reference at an object, giving the object an entity if it has none. A null
/// object empties the reference.
/// </summary>
void ObjectRefClass::Set(AbstractClass * object)
{
	Entity = (object != nullptr) ? Component_Entity(*object) : 0;
}


/// <summary>
/// Fetches the object the reference points at, or nullptr when it is empty or that object
/// has been destroyed.
/// </summary>
AbstractClass * ObjectRefClass::Get(void) const
{
	if (Entity == 0) {
		return(nullptr);
	}

	flecs::world & world = Component_World();
	if (!world.is_alive(Entity)) {
		return(nullptr);
	}

	ComponentOwnerClass const * owner = flecs::entity(world, Entity).try_get<ComponentOwnerClass>();
	return((owner != nullptr) ? owner->Object : nullptr);
}


/// <summary>
/// Writes the reference as the save identity of its object, or reads one back. A reference
/// to a destroyed object is written empty. A loaded reference stays empty until
/// Resolve_Component_Refs, and one read anywhere but directly in a component fails the
/// stream.
/// </summary>
void ObjectRefClass::Serialize(SaveStreamClass & stream)
{
	if (stream.Is_Saving()) {
		AbstractClass * target = Get();
		stream.Serialize(target);
		return;
	}

	Entity = 0;

	ReadingClass const * reading = Reading();
	unsigned char const * const self = (unsigned char const *)this;
	if (reading == nullptr || self < reading->Base || self + sizeof(*this) > reading->Base + reading->Size) {
		stream.Fail();
		return;
	}

	StagedRefClass & staged = Staged_Refs().emplace_back(StagedRefClass{
		nullptr,
		reading->Holder,
		reading->Component,
		(std::size_t)(self - reading->Base),
	});
	stream.Serialize(staged.Target);
}


/// <summary>
/// Folds the identity of the referenced object into a sync checksum, or a marker for an
/// empty reference.
/// </summary>
void ObjectRefClass::Compute_CRC(CRCEngine & crc) const
{
	AbstractClass const * target = Get();
	crc(target != nullptr);
	if (target != nullptr) {
		crc(target->ID);
	}
}
