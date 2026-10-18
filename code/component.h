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

	void (*Serialize)(flecs::entity entity, SaveStreamClass & stream);
	void (*Compute_CRC)(flecs::entity entity, CRCEngine & crc);
};


flecs::world & Component_World(void);
std::vector<ComponentTypeClass> const & Component_Types(void);
void Add_Component_Type(ComponentTypeClass const & type);
void Register_Components(void);

// The functions below take the entity slot an object keeps, AbstractClass::Entity, which
// holds 0 until the object receives its first component.
std::uint64_t Component_Entity(std::uint64_t & slot);
void Destroy_Components(std::uint64_t & slot);
void Serialize_Components(std::uint64_t & slot, SaveStreamClass & stream);
void Compute_Component_CRC(std::uint64_t slot, CRCEngine & crc);


template<ComponentType T>
void Register_Component(char const * name)
{
	flecs::entity_t const id = Component_World().component<T>(name).id();

	Add_Component_Type({
		name,
		id,
		[](flecs::entity entity, SaveStreamClass & stream) { entity.ensure<T>().Serialize(stream); },
		[](flecs::entity entity, CRCEngine & crc) { entity.get<T>().Compute_CRC(crc); },
	});
}


template<ComponentType T>
T * Get_Component(std::uint64_t slot)
{
	if (slot == 0) {
		return(nullptr);
	}
	return(flecs::entity(Component_World(), slot).try_get_mut<T>());
}


// Adds a default-constructed component, or returns the one the object already has.
template<ComponentType T>
T & Add_Component(std::uint64_t & slot)
{
	return(flecs::entity(Component_World(), Component_Entity(slot)).ensure<T>());
}


template<ComponentType T>
void Remove_Component(std::uint64_t slot)
{
	if (slot != 0) {
		flecs::entity(Component_World(), slot).remove<T>();
	}
}


template<ComponentType T>
T * Get_Component(AbstractClass & object)
{
	return(Get_Component<T>(object.Entity));
}


template<ComponentType T>
T const * Get_Component(AbstractClass const & object)
{
	return(Get_Component<T>(object.Entity));
}


template<ComponentType T>
T & Add_Component(AbstractClass & object)
{
	return(Add_Component<T>(object.Entity));
}


template<ComponentType T>
void Remove_Component(AbstractClass & object)
{
	Remove_Component<T>(object.Entity);
}
