/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Components stored on game objects in a flecs world. docs/COMPONENTS.md holds the rules
// every component follows; in short, simulation code reaches a component only through the
// object it belongs to, and never through a flecs query, system or observer.

#pragma once

#include "abstract.h"

#include <flecs.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

class CRCEngine;
class SaveStreamClass;


// A component is plain data that writes its members to a save and folds them into the
// sync checksum.
template<typename T>
concept ComponentType = std::is_default_constructible_v<T>
	&& requires(T & component, T const & view, SaveStreamClass & stream, CRCEngine & crc) {
		component.Serialize(stream);
		view.Compute_CRC(crc);
	};


struct ComponentTypeClass
{
	// Saves carry this name, so renaming a registered component breaks them.
	std::string Name;

	// Numbered by flecs in registration order; never written to a save or a checksum.
	flecs::entity_t ID;

	std::size_t Size;

	void (*Serialize)(flecs::entity entity, SaveStreamClass & stream);
	void (*Compute_CRC)(flecs::entity entity, CRCEngine & crc);
};


// Every entity carries the object it belongs to. It is never saved, and an object does not
// read it from its type.
struct ComponentOwnerClass
{
	AbstractClass * Object = nullptr;
};


// Refers to another game object through that object's entity, and reads as empty once the
// object is destroyed. It must be a direct member of a component, since a load finds it again
// by its position in the component.
class ObjectRefClass
{
	public:
		ObjectRefClass(void) = default;

		void Set(AbstractClass * object);
		void Clear(void) {Entity = 0;}
		AbstractClass * Get(void) const;

		void Serialize(SaveStreamClass & stream);
		void Compute_CRC(CRCEngine & crc) const;

	private:
		std::uint64_t Entity = 0;
};


flecs::world & Component_World(void);
std::vector<ComponentTypeClass> const & Component_Types(void);
void Add_Component_Type(ComponentTypeClass const & type);
void Register_Components(void);
bool Component_Reads_From_Type(flecs::entity_t component);

// Call this after changing the type Component_Base returns for an object.
void Relink_Component_Type(AbstractClass & object);

// AbstractClass calls these from its destructor, Serialize and Compute_CRC.
std::uint64_t Component_Entity(AbstractClass & object);
void Destroy_Components(AbstractClass & object);
void Serialize_Components(AbstractClass & object, SaveStreamClass & stream);
void Compute_Component_CRC(AbstractClass const & object, CRCEngine & crc);

// Load_Game calls these around the swizzle pass that patches loaded pointers.
void Resolve_Component_Refs(void);
void Discard_Component_Refs(void);


template<ComponentType T>
void Register_Component_With(char const * name, flecs::entity_t oninstantiate)
{
	flecs::entity_t const id = Component_World().component<T>(name).add(flecs::OnInstantiate, oninstantiate).id();

	Add_Component_Type({
		name,
		id,
		sizeof(T),
		[](flecs::entity entity, SaveStreamClass & stream) { entity.ensure<T>().Serialize(stream); },
		[](flecs::entity entity, CRCEngine & crc) { entity.get<T>().Compute_CRC(crc); },
	});
}


// Registers state each object holds for itself; an object never reads it from its type.
template<ComponentType T>
void Register_Component(char const * name)
{
	Register_Component_With<T>(name, flecs::DontInherit);
}


// Registers data a type holds for its objects, which read it until they get their own copy.
template<ComponentType T>
void Register_Type_Component(char const * name)
{
	Register_Component_With<T>(name, flecs::Inherit);
}


// Reads the object's own component, or its type's for a type component it has no copy of.
template<ComponentType T>
T const * Get_Component(AbstractClass const & object)
{
	flecs::world & world = Component_World();
	if (object.Entity != 0) {
		return(flecs::entity(world, object.Entity).try_get<T>());
	}

	AbstractClass const * base = object.Component_Base();
	if (base == nullptr || base->Entity == 0 || !Component_Reads_From_Type(world.id<T>())) {
		return(nullptr);
	}
	return(flecs::entity(world, base->Entity).try_get<T>());
}


// Fetches the component the object itself owns for writing, or nullptr while it has none.
template<ComponentType T>
T * Get_Own_Component(AbstractClass & object)
{
	if (object.Entity == 0) {
		return(nullptr);
	}
	return(flecs::entity(Component_World(), object.Entity).try_get_mut<T>());
}


// Returns the component the object owns. One it lacks starts as a copy of its type's type
// component, or with default values when there is none to copy.
template<ComponentType T>
T & Add_Component(AbstractClass & object)
{
	return(flecs::entity(Component_World(), Component_Entity(object)).ensure<T>());
}


template<ComponentType T>
void Remove_Component(AbstractClass & object)
{
	if (object.Entity != 0) {
		flecs::entity(Component_World(), object.Entity).remove<T>();
	}
}
