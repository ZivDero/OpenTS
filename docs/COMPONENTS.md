# Component storage

Game objects can carry components: data and state stored in a
[flecs](https://github.com/SanderMertens/flecs) world beside the object rather
than as members of its class. The storage, references between objects, the
save block and the checksum are in place in `code/component.h`. Type prefabs
and the first port of an existing feature are designed here but not built yet.
[Project direction](DIRECTION.md) explains why OpenTS is moving toward
components.

## Scope

The engine keeps deciding what runs and in what order. `LogicClass::AI` in
`code/logic.cpp` and the object heaps set the order of simulation, as they do
now. Flecs systems, pipelines, worker threads and queries take no part in the
simulation. Ported vanilla logic moves its state into components and keeps
running from the object's own `AI` and event handlers, so the order in which
objects act, draw random numbers and break ties does not change.

An object's components hold state that would otherwise be new members on its
class. Once prefabs are built, a type's components will hold the values read
from the rules files, and the type's objects will inherit them.

## Why flecs

- Prefabs give the split between type data and object state directly: an
  object reads its type's component until it gets a copy of its own.
- Entity handles carry a generation, so a component can check whether the
  object it refers to still exists.
- The core is C99 and the C++ API needs C++17, so both build in the engine's
  C++20 tree, and a custom build leaves out every addon the engine does not
  use. [Building OpenTS](BUILDING.md) lists what is built.
- It is MIT-licensed.

## Using components

A component type is a default-constructible struct with two members:

- `void Serialize(SaveStreamClass & stream)` names its members for the save,
  as a class's `Serialize` does.
- `void Compute_CRC(CRCEngine & crc) const` folds its members into the sync
  checksum.

A type missing either does not compile as a component.

Register each type once in `Register_Components` in `code/component.cpp`,
with `Register_Component<T>("Name")`. Startup calls it right after the save
classes are registered, before any object exists. Saves carry the name, so
renaming a registered type breaks existing saves.

`Add_Component<T>(object)` returns the object's component, creating it with
default values when the object has none. `Get_Component<T>(object)` returns it
or `nullptr`, and `Remove_Component<T>(object)` removes it.

A component refers to another game object through an `ObjectRefClass` member;
see [Object references](#object-references).

## Entities

Every `AbstractClass` can own components. That includes types, houses,
triggers, teams and factories, as well as objects on the map. Keep simulation
state off cells for now; see [Cells](#cells).

- `AbstractClass::Entity` holds the object's flecs entity, or 0 while it has
  none. It is 64 bits wide on Win32 and x64.
- The entity is created when the object receives its first component or
  another component first refers to it. An object without an entity costs
  only that member.
- Each entity carries the object it belongs to as an owner component. The
  owner is never saved or checksummed, and is rebuilt with the entity.
- `~AbstractClass` deletes the object's entity with its components. A handle
  to it then fails its liveness check.
- `AbstractClass` cannot be copied, so no two objects share an entity.

The world is created on first use and never destroyed, so an object destroyed
during process exit can still delete its entity. `Clear_Scenario` in
`code/scenario.cpp` keeps some type objects between scenarios, such as the
isometric tile types, so the world cannot be reset per scenario either.

### Cells

A cell's components are saved and deleted like any other object's, but
`Object_CRCs` does not walk cells, so they never reach the sync check. A
component holding simulation state on a cell could drift between network
peers without a desync report. Add cells to the checksum before putting such
state on them; see [Sync checks](#sync-checks).

## Rules

These rules are fixed. A change that breaks one needs its own design decision
first.

### Simulation order

- Simulation code reaches a component through the object it is already
  processing. Do not run flecs systems or `progress()`, or iterate a query,
  in simulation code.
- Queries are allowed where order cannot reach the game state: UI, debug
  views, rendering and tests. They only read.
- Component constructor, destructor, copy and move hooks are allowed.
  Observers that change game state are not.

A query visits entities table by table, in the order flecs created the
tables, and removing an entity moves another into its place within the table.
A loaded game can create the tables in a different order from the game that
was saved, so a query in simulation code would make a loaded game play out
differently from the uninterrupted one.

### Threads

The world is used from the game thread only, with one thread and no staging.
Rendering and audio code that runs on other threads does not touch it.

### Pointers

- A component holds plain values and `ObjectRefClass` references: no
  `AbstractClass` pointers and no owning raw pointers.
- Component data moves when its entity gains or loses a component. Do not
  keep a pointer to a component across a call that adds or removes components
  on the same entity.

### Identity

Flecs numbers component types in the order they are registered, and the
engine's build turns off its automatic registration so that order is the one
in `Register_Components`. Never write those numbers to a save, a checksum or a
network packet; use the registered name or the registration index.

## Saves

`AbstractClass::Serialize` writes the components an object owns after `ID` and
`Dirty`, in registration order. [The saved game format](SAVE-FORMAT.md#components)
gives the layout.

- An object without components writes a count of 0, so every object record
  except a locomotor's is four bytes longer than before.
- A load replaces whatever components the object had, including any its
  constructor added.
- A load fails on a component name this build does not register, on a name
  that appears twice in one record, and on a body that its `Serialize` reads
  short or long, the same way a bad object record fails.

A save from a build without components is not offered: the commit that added
the component block raised the save serial, as
[Versions](SAVE-FORMAT.md#versions) describes.

## Sync checks

`AbstractClass::Compute_CRC` folds in each owned component, in registration
order, after `ID` and `Dirty`. An object that owns no components adds nothing,
so its checksum is the same as before components existed. `Object_CRCs` and
`Print_Heap_CRCs` in `code/saveload.cpp` cover components for every heap in
`OBJECT_HEAP_LIST` (`code/objheaps.hh`). That list includes houses and types
but not cells.

## Object references

`ObjectRefClass` refers to another game object through that object's entity.
`Set(object)` points it at an object, giving the object an entity if it has
none, `Get()` returns the object or `nullptr`, and `Clear()` empties it. Once
the object is destroyed, `Get()` returns `nullptr`, so a component never needs
to be told about the death. Pass the reference to `stream.Serialize` and
`Compute_CRC` like any other member.

Use a reference rather than an `AbstractClass` pointer. `Detach_This_From_All`
in `code/tracker.cpp` reaches only the trackers for the dead object's kind, so
a house or type holding a pointer to a unit is not told when the unit dies.

The entity generation in flecs is 16 bits. A reference reads its object again
once the entity's index has been deleted and reused 65,536 times, and then
returns whichever object holds that index. Do not keep a reference for a long
game to an object kind created and destroyed that often without checking what
`Get()` returns.

Make each reference a direct member of its component, not an element of a
container inside it. A load records each reference by its position in the
component, and fails the load for one read anywhere else.

The checksum folds in the referenced object's `ID`, or a marker for an empty
reference.

### References in a save

A reference travels as the save identity of the object it points at, the same
four bytes a pointer member uses, and one whose object is destroyed travels
empty. On load, the reference stays empty until `Load_Game` resolves the
save's pointers, and is set right after that pass, before `Post_Load_Game`
runs. A class's own `Post_Load` therefore sees its components' references
empty.

## Types as prefabs

This part is designed but not built.

A type's entity will be a prefab. When an object's entity is created, the
object's type gets an entity too, empty if it has no components, and the
object's entity gets an `IsA` link to it. Reads then find the type's component
until the object owns a copy.

- Rules data goes into type components where the type reads its INI section.
  Read the keys where `manage.py update` can find them, or the manual extractor
  needs an adapter for them.
- To change a value for one object, give the object its own copy first. A
  write to the type's component changes it for every object of that type,
  just as a write to a type member does now.
- For `ObjectClass` descendants, the type is the one `Class_Of` returns. Other
  kinds name their type when a component first needs one.
- A save writes only the components an entity owns. Inherited ones come back
  with the type's own record, and the load restores each object's `IsA` link
  from its type after it resolves references.

## Rollout

Each step lands separately and leaves the game playable.

1. **Storage.** Done: flecs vendored and built in all four configurations,
   the registry, the entity on `AbstractClass`, the save block and the
   checksum.
2. **Object references.** Done: the owner component, `ObjectRefClass`, and
   references resolved after a load's pointers. `tests/components` covers
   both steps with no game data.
3. **Prefabs.** Create type entities, link objects with `IsA`, and read one
   rules key into a type component. Check first what deleting a prefab does to
   the `IsA` links of objects that still have them.
4. **Pilot port.** Move one existing feature's state into a component with
   its behavior preserved. Evidence: matching heap checksums in a loopback
   network game, and a save and load that plays on identically.
5. **New mechanics.** Each pairs a type component holding its rules data with
   an object component holding its state, and gets its manual page and change
   record.

These are left for later decisions: simulation state on cells once cells are
in the sync checksum, and simulation queries whose results are sorted by a key
the save carries, such as the object's `ID`.
