/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises component storage, object references, the save block and the checksum on bare
// game objects, without the rest of the engine.

#include "_rtti.h"
#include "component.h"
#include "crc.h"
#include "saveload.h"
#include "savestream.h"
#include "scenario.h"
#include "swizzle.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


// Stand-ins for what abstract.cpp reaches in the rest of the game. None of the objects here
// is a techno.
unsigned int ExpectedGameVersion = 1;
ScenarioClass * Scen = nullptr;
Coord const COORD_NONE(0, 0, 0);


template<>
TechnoClass const * Dynamic_Cast<TechnoClass const *>(AbstractClass const *)
{
	return(nullptr);
}


int ScenarioClass::Get_Unique_ID(void)
{
	return(0);
}


/*
 * A stand-in for the swizzle manager, so the harness needs nothing of the game around it.
 * It numbers objects as the real one does and patches every request it can answer.
 */
namespace {

std::unordered_map<void const *, SwizzleIDType> SwizzleIDs;
SwizzleIDType NextSwizzleID = 1;
int Unanswered = 0;

}	// namespace


SwizzleManagerClass Swizzler;


SwizzleManagerClass::SwizzleManagerClass(void)
{
}


void SwizzleManagerClass::Begin_Save(void)
{
	SwizzleIDs.clear();
	NextSwizzleID = 1;
}


SwizzleIDType SwizzleManagerClass::ID_Of(void const * pointer)
{
	if (pointer == nullptr) {
		return(0);
	}
	auto found = SwizzleIDs.find(pointer);
	if (found != SwizzleIDs.end()) {
		return(found->second);
	}
	SwizzleIDs.emplace(pointer, NextSwizzleID);
	return(NextSwizzleID++);
}


void SwizzleManagerClass::Swizzle(void ** pointer, SwizzleIDType id, char const *, SwizzleIDType, char const *, char const *, unsigned int)
{
	*pointer = nullptr;
	if (id != 0) {
		RequestTable.emplace_back(id, pointer);
	}
}


void SwizzleManagerClass::Here_I_Am(SwizzleIDType id, void * pointer)
{
	PointerTable.emplace_back(id, pointer);
}


bool SwizzleManagerClass::Resolve(void)
{
	int const before = Unanswered;

	for (SwizzleRequestClass const & request : RequestTable) {
		auto entry = std::find_if(PointerTable.begin(), PointerTable.end(),
			[&](SwizzlePointerClass const & pointer) { return(pointer.ID == request.ID); });
		if (entry != PointerTable.end()) {
			*(void **)request.Pointer = entry->Pointer;
		} else {
			Unanswered++;
		}
	}
	Discard();
	return(Unanswered == before);
}


void SwizzleManagerClass::Discard(void)
{
	RequestTable.clear();
	PointerTable.clear();
}


namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-72s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition) {
		Failures++;
	}
}


class TestObjectClass : public AbstractClass
{
	public:
		TestObjectClass(void) = default;
		explicit TestObjectClass(AbstractClass * type) : Type(type) {}

		virtual ClassID Class_ID(void) const override {return(ClassID{});}
		virtual RTTIType Fetch_RTTI(void) const override {return(RTTI_NONE);}
		virtual AbstractClass * Component_Base(void) const override {return(Type);}

		AbstractClass * Type = nullptr;
};


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


// Registered first, so a load reads it before the holder's other components, and adding
// those moves it to another flecs table after its references were read.
struct LinkComponent
{
	int Before = 0;
	ObjectRefClass Target;
	ObjectRefClass Second;

	void Serialize(SaveStreamClass & stream)
	{
		stream.Serialize(Before);
		stream.Serialize(Target);
		stream.Serialize(Second);
	}

	void Compute_CRC(CRCEngine & crc) const
	{
		crc(Before);
		Target.Compute_CRC(crc);
		Second.Compute_CRC(crc);
	}
};


// Data a type holds for its objects.
struct SpeedComponent
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


// Saves the objects as a sequence of records, the way Put_All writes a heap.
std::vector<unsigned char> Save(std::vector<TestObjectClass *> const & objects)
{
	Swizzler.Begin_Save();
	std::vector<unsigned char> buffer;
	SaveStreamClass stream(buffer, SaveStreamClass::MODE_SAVE);
	for (TestObjectClass * object : objects) {
		object->Save(stream, false);
	}
	return(buffer);
}


// Loads the records into the objects given, then patches pointers and references as
// Load_Game does, and reports whether everything was read whole and exactly.
bool Load(std::vector<unsigned char> & buffer, std::vector<TestObjectClass *> const & objects)
{
	Swizzler.Discard();
	Discard_Component_Refs();
	Unanswered = 0;

	SaveStreamClass stream(buffer, SaveStreamClass::MODE_LOAD);
	for (TestObjectClass * object : objects) {
		if (!object->Load(stream)) {
			Swizzler.Discard();
			Discard_Component_Refs();
			return(false);
		}
	}
	Swizzler.Resolve();
	Resolve_Component_Refs();
	return(!stream.Was_Error() && stream.Offset() == stream.Size() && Unanswered == 0);
}


// Writes a record body by hand: the swizzle identity, ID, Dirty, and then the caller's bytes.
std::vector<unsigned char> Record_Start(void)
{
	std::vector<unsigned char> buffer;
	SwizzleIDType id = 1;
	int objectid = -1;
	bool dirty = false;
	buffer.insert(buffer.end(), (unsigned char *)&id, (unsigned char *)&id + sizeof(id));
	buffer.insert(buffer.end(), (unsigned char *)&objectid, (unsigned char *)&objectid + sizeof(objectid));
	buffer.insert(buffer.end(), (unsigned char *)&dirty, (unsigned char *)&dirty + sizeof(dirty));
	return(buffer);
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


bool Load_One(std::vector<unsigned char> & buffer)
{
	TestObjectClass object;
	return(Load(buffer, {&object}));
}


int Checksum(AbstractClass const & object)
{
	CRCEngine crc;
	object.Compute_CRC(crc);
	return(crc());
}

}	// namespace


int main(void)
{
	Register_Component<LinkComponent>("Link");
	Register_Component<HealthComponent>("Health");
	Register_Component<TagComponent>("Tag");
	Register_Component<LopsidedComponent>("Lopsided");
	Register_Type_Component<SpeedComponent>("Speed");

	// The size of a record for an object without components: identity, ID, Dirty, count.
	std::size_t const bare = sizeof(SwizzleIDType) + sizeof(int) + sizeof(bool) + sizeof(int);

	/*
	 * An object without components has no entity and writes a count of zero.
	 */
	{
		TestObjectClass object;
		Check(Get_Component<HealthComponent>(object) == nullptr, "an object starts without components");
		std::vector<unsigned char> buffer = Save({&object});
		Check(buffer.size() == bare, "an object without components writes only its count");

		TestObjectClass loaded;
		Check(Load(buffer, {&loaded}), "an empty block loads");
		Check(loaded.Entity == 0, "an empty block creates no entity");
	}

	/*
	 * Components come back with their values, and the record is read exactly.
	 */
	{
		TestObjectClass object;
		Add_Component<TagComponent>(object).Value = 77;
		HealthComponent & health = Add_Component<HealthComponent>(object);
		health.Strength = 300;
		health.Regen = 4;
		Check(object.Entity != 0, "adding a component creates the entity");

		std::vector<unsigned char> buffer = Save({&object});

		TestObjectClass loaded;
		Check(Load(buffer, {&loaded}), "a saved block loads whole");
		HealthComponent const * back = Get_Component<HealthComponent>(loaded);
		TagComponent const * tag = Get_Component<TagComponent>(loaded);
		Check(back != nullptr && back->Strength == 300 && back->Regen == 4, "the health component comes back");
		Check(tag != nullptr && tag->Value == 77, "the tag component comes back");
		Check(Get_Component<LopsidedComponent>(loaded) == nullptr, "no component appears that was not saved");

		// Health was registered first, so its name follows the count although it was added second.
		Check(buffer.size() > bare + 6 && std::memcmp(&buffer[bare + 2], "Health", 6) == 0, "components are written in registration order");
	}

	/*
	 * A load replaces the components the object had before.
	 */
	{
		TestObjectClass source;
		Add_Component<TagComponent>(source).Value = 5;
		std::vector<unsigned char> buffer = Save({&source});

		TestObjectClass target;
		Add_Component<HealthComponent>(target).Strength = 9;
		Check(Load(buffer, {&target}), "a block loads over existing components");
		Check(Get_Component<HealthComponent>(target) == nullptr, "a component the block lacks is gone");
		Check(Get_Component<TagComponent>(target) != nullptr, "the block's component is present");
	}

	/*
	 * A removed component is not written, and destroying the object deletes its entity.
	 */
	{
		TestObjectClass object;
		Add_Component<TagComponent>(object);
		Remove_Component<TagComponent>(object);
		Check(Get_Component<TagComponent>(object) == nullptr, "a removed component is gone");
		Check(Save({&object}).size() == bare, "an object whose components were removed writes a count of zero");
	}
	{
		std::uint64_t handle = 0;
		{
			TestObjectClass object;
			Add_Component<TagComponent>(object);
			handle = object.Entity;
		}
		Check(!Component_World().is_alive(handle), "destroying an object deletes its entity");

		TestObjectClass reused;
		Add_Component<TagComponent>(reused);
		Check(!Component_World().is_alive(handle), "a recycled index does not revive the old handle");
	}

	/*
	 * The loader refuses what this build could not have written.
	 */
	{
		std::vector<unsigned char> buffer = Record_Start();
		Put_Int(buffer, 1);
		Put_Name(buffer, "Unknown");
		Put_Int(buffer, 4);
		Put_Int(buffer, 0);
		Check(!Load_One(buffer), "an unregistered component fails the load");
	}
	{
		std::vector<unsigned char> buffer = Record_Start();
		Put_Int(buffer, 2);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 4);
		Put_Int(buffer, 1);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 4);
		Put_Int(buffer, 2);
		Check(!Load_One(buffer), "a component named twice fails the load");
	}
	{
		TestObjectClass source;
		Add_Component<LopsidedComponent>(source).Value = 3;
		std::vector<unsigned char> buffer = Save({&source});
		Check(!Load_One(buffer), "a body read short fails the load");
	}
	{
		std::vector<unsigned char> buffer = Record_Start();
		Put_Int(buffer, 1);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 2);
		Put_Int(buffer, 1);
		Check(!Load_One(buffer), "a body read long fails the load");
	}
	{
		std::vector<unsigned char> buffer = Record_Start();
		Put_Int(buffer, 1000000);
		TestObjectClass object;
		Check(!Load(buffer, {&object}), "a count the stream cannot hold fails the load");
		Check(object.Entity == 0, "a refused count creates no entity");
	}
	{
		std::vector<unsigned char> buffer = Record_Start();
		Put_Int(buffer, 1);
		Put_Name(buffer, "Tag");
		Put_Int(buffer, 100);
		Put_Int(buffer, 1);
		Check(!Load_One(buffer), "a body length past the stream fails the load");
	}

	/*
	 * A reference finds its object while it lives and reads as empty once it is destroyed.
	 */
	{
		TestObjectClass holder;
		auto target = std::make_unique<TestObjectClass>();
		Check(target->Entity == 0, "a target starts without an entity");

		LinkComponent & link = Add_Component<LinkComponent>(holder);
		link.Target.Set(target.get());
		Check(target->Entity != 0, "referring to an object gives it an entity");
		Check(Get_Component<LinkComponent>(holder)->Target.Get() == target.get(), "a reference finds its object");
		Check(Get_Component<LinkComponent>(holder)->Second.Get() == nullptr, "an unset reference is empty");

		std::vector<unsigned char> buffer = Save({target.get()});
		Check(buffer.size() == bare, "the entity's owner is not saved");

		target.reset();
		Check(Get_Component<LinkComponent>(holder)->Target.Get() == nullptr, "a reference to a destroyed object is empty");

		TestObjectClass other;
		Get_Own_Component<LinkComponent>(holder)->Target.Set(&other);
		Get_Own_Component<LinkComponent>(holder)->Target.Clear();
		Check(Get_Component<LinkComponent>(holder)->Target.Get() == nullptr, "a cleared reference is empty");
	}

	/*
	 * References survive a save, including one to the holder itself, and keep their place
	 * when loading a later component moves the one holding them.
	 */
	{
		TestObjectClass mover;
		LinkComponent const * before = &Add_Component<LinkComponent>(mover);
		Add_Component<TagComponent>(mover);
		Check(Get_Component<LinkComponent>(mover) != before, "adding a component moves the ones already there");
	}
	{
		TestObjectClass holder;
		TestObjectClass target;
		Add_Component<TagComponent>(holder).Value = 6;
		LinkComponent & link = Add_Component<LinkComponent>(holder);
		link.Before = 41;
		link.Target.Set(&target);
		link.Second.Set(&holder);

		std::vector<unsigned char> buffer = Save({&holder, &target});

		TestObjectClass newholder;
		TestObjectClass newtarget;
		Check(Load(buffer, {&newholder, &newtarget}), "objects with references load whole");
		LinkComponent const * back = Get_Component<LinkComponent>(newholder);
		Check(back != nullptr && back->Before == 41, "the component holding references comes back");
		Check(back != nullptr && back->Target.Get() == &newtarget, "a reference points at the loaded target");
		Check(back != nullptr && back->Second.Get() == &newholder, "a reference to the holder points at the loaded holder");
		Check(Get_Component<TagComponent>(newholder) != nullptr, "the holder's other component comes back");
	}

	/*
	 * A reference to an object destroyed before the save comes back empty, and a reference
	 * read anywhere but directly in a component fails the load.
	 */
	{
		TestObjectClass holder;
		{
			TestObjectClass doomed;
			Add_Component<LinkComponent>(holder).Target.Set(&doomed);
		}
		std::vector<unsigned char> buffer = Save({&holder});

		TestObjectClass loaded;
		Check(Load(buffer, {&loaded}), "a reference to a destroyed object saves and loads");
		LinkComponent const * back = Get_Component<LinkComponent>(loaded);
		Check(back != nullptr && back->Target.Get() == nullptr, "it comes back empty");
	}
	{
		std::vector<unsigned char> buffer;
		Put_Int(buffer, 0);
		SaveStreamClass stream(buffer, SaveStreamClass::MODE_LOAD);
		ObjectRefClass loose;
		loose.Serialize(stream);
		Check(stream.Was_Error(), "a reference read outside a component fails the load");
		Discard_Component_Refs();
	}

	/*
	 * The checksum ignores an object without components and the order components were added,
	 * and follows the object a reference points at.
	 */
	{
		TestObjectClass empty;
		CRCEngine plain;
		plain(empty.ID);
		Check(Checksum(empty) == plain(), "an object without components leaves the checksum alone");

		TestObjectClass first;
		Add_Component<TagComponent>(first).Value = 1;
		Add_Component<HealthComponent>(first).Strength = 2;

		TestObjectClass second;
		Add_Component<HealthComponent>(second).Strength = 2;
		Add_Component<TagComponent>(second).Value = 1;

		Check(Checksum(first) == Checksum(second), "the checksum follows registration order, not addition order");
		Check(Checksum(first) != Checksum(empty), "components change the checksum");

		Get_Own_Component<TagComponent>(second)->Value = 8;
		Check(Checksum(first) != Checksum(second), "a changed value changes the checksum");

		TestObjectClass a;
		TestObjectClass b;
		a.ID = 10;
		b.ID = 20;
		TestObjectClass left;
		TestObjectClass right;
		Add_Component<LinkComponent>(left).Target.Set(&a);
		Add_Component<LinkComponent>(right).Target.Set(&b);
		Check(Checksum(left) != Checksum(right), "references to different objects change the checksum");
		Get_Own_Component<LinkComponent>(right)->Target.Set(&a);
		Check(Checksum(left) == Checksum(right), "references to the same object checksum alike");
	}

	/*
	 * An object reads its type's type component without an entity of its own, and sees the
	 * type's later changes.
	 */
	{
		TestObjectClass type;
		Add_Component<SpeedComponent>(type).Value = 6;

		TestObjectClass object(&type);
		SpeedComponent const * speed = Get_Component<SpeedComponent>(object);
		Check(speed != nullptr && speed->Value == 6, "an object without an entity reads its type's component");
		Check(object.Entity == 0, "reading a type's component creates no entity");
		Check(Get_Own_Component<SpeedComponent>(object) == nullptr, "an object does not own what it reads from its type");

		Add_Component<TagComponent>(object).Value = 2;
		speed = Get_Component<SpeedComponent>(object);
		Check(speed != nullptr && speed->Value == 6, "an object with an entity reads its type's component");
		Check(Get_Own_Component<SpeedComponent>(object) == nullptr, "gaining an entity copies nothing from the type");

		Get_Own_Component<SpeedComponent>(type)->Value = 8;
		speed = Get_Component<SpeedComponent>(object);
		Check(speed != nullptr && speed->Value == 8, "an object sees its type's later changes");
	}

	/*
	 * An object's entity links to its type, and the type gets an entity for that even with
	 * no components.
	 */
	{
		TestObjectClass type;
		TestObjectClass object(&type);
		Add_Component<TagComponent>(object);
		Check(type.Entity != 0, "linking an object gives its type an entity");
		Check(flecs::entity(Component_World(), type.Entity).has(flecs::Prefab), "a type's entity is a prefab");
		Check(flecs::entity(Component_World(), object.Entity).target(flecs::IsA).id() == type.Entity, "an object's entity links to its type");

		Add_Component<SpeedComponent>(type).Value = 3;
		SpeedComponent const * speed = Get_Component<SpeedComponent>(object);
		Check(speed != nullptr && speed->Value == 3, "a component the type gains after linking reads through");
	}

	/*
	 * Components registered as object state stay with the object that holds them.
	 */
	{
		TestObjectClass type;
		Add_Component<TagComponent>(type).Value = 5;

		TestObjectClass bare(&type);
		Check(Get_Component<TagComponent>(bare) == nullptr, "an object without an entity does not read its type's state");

		TestObjectClass linked(&type);
		Add_Component<HealthComponent>(linked);
		Check(Get_Component<TagComponent>(linked) == nullptr, "an object with an entity does not read its type's state");
		Check(Add_Component<TagComponent>(linked).Value == 0, "state an object gains starts from defaults, not from its type");
	}

	/*
	 * Adding a type component to an object gives it a copy, which only that object sees.
	 * Removing the copy returns it to the type's.
	 */
	{
		TestObjectClass type;
		Add_Component<SpeedComponent>(type).Value = 6;
		TestObjectClass object(&type);
		TestObjectClass other(&type);
		Add_Component<TagComponent>(other);

		SpeedComponent & own = Add_Component<SpeedComponent>(object);
		Check(own.Value == 6, "an object's copy starts with its type's value");
		own.Value = 9;

		Check(Get_Component<SpeedComponent>(object)->Value == 9, "an object reads its copy");
		Check(Get_Component<SpeedComponent>(type)->Value == 6, "an object's copy leaves the type alone");
		Check(Get_Component<SpeedComponent>(other)->Value == 6, "an object's copy leaves other objects alone");

		Get_Own_Component<SpeedComponent>(type)->Value = 7;
		Check(Get_Component<SpeedComponent>(object)->Value == 9, "a copy does not follow the type");

		Remove_Component<SpeedComponent>(object);
		Check(Get_Component<SpeedComponent>(object)->Value == 7, "removing a copy returns the object to its type's value");

		Remove_Component<SpeedComponent>(other);
		Check(Get_Component<SpeedComponent>(other)->Value == 7, "removing what an object only reads changes nothing");
	}

	/*
	 * A type component the type lacks starts from defaults.
	 */
	{
		TestObjectClass type;
		TestObjectClass object(&type);
		Check(Get_Component<SpeedComponent>(object) == nullptr, "a type without the component gives its objects none");
		Check(Add_Component<SpeedComponent>(object).Value == 0, "with nothing to copy, a type component starts from defaults");
	}

	/*
	 * Changing an object's type moves its reads to the new type and keeps what it owns.
	 */
	{
		TestObjectClass small;
		TestObjectClass large;
		Add_Component<SpeedComponent>(small).Value = 1;
		Add_Component<SpeedComponent>(large).Value = 2;

		TestObjectClass bare(&small);
		bare.Type = &large;
		Relink_Component_Type(bare);
		Check(bare.Entity == 0, "relinking an object without an entity creates none");
		Check(Get_Component<SpeedComponent>(bare)->Value == 2, "an object without an entity reads its new type");

		TestObjectClass linked(&small);
		Add_Component<TagComponent>(linked).Value = 4;
		linked.Type = &large;
		Relink_Component_Type(linked);
		Check(Get_Component<SpeedComponent>(linked)->Value == 2, "a relinked object reads its new type");
		Check(flecs::entity(Component_World(), linked.Entity).target(flecs::IsA).id() == large.Entity, "a relinked object has one link, to its new type");
		Check(Get_Component<TagComponent>(linked)->Value == 4, "relinking keeps the object's state");

		TestObjectClass copied(&small);
		Add_Component<SpeedComponent>(copied).Value = 12;
		copied.Type = &large;
		Relink_Component_Type(copied);
		Check(Get_Component<SpeedComponent>(copied)->Value == 12, "relinking keeps the object's copy");
	}

	/*
	 * Deleting a type takes its data away from objects still linked to it, and leaves
	 * those objects and their own components alive.
	 */
	{
		auto type = std::make_unique<TestObjectClass>();
		Add_Component<SpeedComponent>(*type).Value = 6;

		TestObjectClass reader(type.get());
		Add_Component<TagComponent>(reader).Value = 3;
		TestObjectClass copied(type.get());
		Add_Component<SpeedComponent>(copied).Value = 11;

		type.reset();
		reader.Type = nullptr;
		copied.Type = nullptr;

		Check(Component_World().is_alive(reader.Entity), "deleting a type leaves its objects' entities alive");
		Check(!flecs::entity(Component_World(), reader.Entity).target(flecs::IsA), "deleting a type removes its objects' links");
		Check(Get_Component<SpeedComponent>(reader) == nullptr, "an object loses what it read from a deleted type");
		Check(Get_Component<TagComponent>(reader)->Value == 3, "an object keeps its state when its type is deleted");
		Check(Get_Component<SpeedComponent>(copied)->Value == 11, "an object keeps its copy when its type is deleted");
	}

	/*
	 * A save holds what each entity owns: a type's data with the type, an object's copy with
	 * the object. A load restores the links once the type pointers are patched.
	 */
	{
		TestObjectClass type;
		Add_Component<SpeedComponent>(type).Value = 6;

		TestObjectClass reader(&type);
		TestObjectClass stateful(&type);
		Add_Component<TagComponent>(stateful).Value = 2;
		TestObjectClass copied(&type);
		Add_Component<SpeedComponent>(copied).Value = 9;

		Check(Save({&reader}).size() == bare, "an object without an entity saves nothing of its type's");
		std::size_t const tagonly = bare + sizeof(unsigned short) + 3 + sizeof(int) + sizeof(int);
		Check(Save({&stateful}).size() == tagonly, "an object saves only the components it owns");

		std::vector<unsigned char> buffer = Save({&type, &reader, &stateful, &copied});

		TestObjectClass newtype;
		TestObjectClass newreader(&newtype);
		TestObjectClass newstateful(&newtype);
		TestObjectClass newcopied(&newtype);
		Check(Load(buffer, {&newtype, &newreader, &newstateful, &newcopied}), "types and objects load whole");

		Check(newreader.Entity == 0, "an object that saved nothing loads without an entity");
		Check(Get_Component<SpeedComponent>(newreader)->Value == 6, "it reads the loaded type");
		Check(flecs::entity(Component_World(), newstateful.Entity).target(flecs::IsA).id() == newtype.Entity, "a load links an object's entity to its type");
		Check(Get_Component<SpeedComponent>(newstateful)->Value == 6, "a linked object reads the loaded type");
		Check(Get_Own_Component<SpeedComponent>(newstateful) == nullptr, "a load copies nothing from the type");
		Check(Get_Component<SpeedComponent>(newcopied)->Value == 9, "an object's copy comes back");

		Get_Own_Component<SpeedComponent>(newtype)->Value = 1;
		Check(Get_Component<SpeedComponent>(newstateful)->Value == 1, "a loaded object follows its type");
		Check(Get_Component<SpeedComponent>(newcopied)->Value == 9, "a loaded copy does not follow its type");
	}

	/*
	 * A load that fails leaves no link behind to be made later.
	 */
	{
		TestObjectClass source;
		Add_Component<TagComponent>(source).Value = 1;
		std::vector<unsigned char> buffer = Save({&source});

		TestObjectClass type;
		Add_Component<SpeedComponent>(type).Value = 6;
		TestObjectClass abandoned(&type);
		std::vector<unsigned char> copy = buffer;
		copy.resize(copy.size() - 1);
		Check(!Load(copy, {&abandoned}), "a truncated record fails the load");
		Resolve_Component_Refs();
		Check(abandoned.Entity == 0 || !flecs::entity(Component_World(), abandoned.Entity).target(flecs::IsA), "a failed load makes no link");
	}

	/*
	 * What an object reads from its type stays out of the object's checksum, and its own
	 * copy goes in.
	 */
	{
		TestObjectClass type;
		Add_Component<SpeedComponent>(type).Value = 6;

		TestObjectClass plain;
		Add_Component<TagComponent>(plain).Value = 2;
		TestObjectClass linked(&type);
		Add_Component<TagComponent>(linked).Value = 2;
		Check(Checksum(plain) == Checksum(linked), "an object's checksum leaves out what it reads from its type");

		int const before = Checksum(type);
		Get_Own_Component<SpeedComponent>(type)->Value = 7;
		Check(Checksum(type) != before, "a type's checksum covers its component");

		int const unchanged = Checksum(linked);
		Add_Component<SpeedComponent>(linked);
		Check(Checksum(linked) != unchanged, "an object's copy changes its checksum");
	}

	std::printf("\n%d failure(s)\n", Failures);
	return(Failures == 0 ? 0 : 1);
}
