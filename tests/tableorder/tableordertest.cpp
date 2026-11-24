/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "flecsstorage.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum Component { KEY, X, Y, Z, INHERITED, SPARSE, MARKER, EARLY, DORMANT, FRESH };
constexpr std::array NAMES{"StableKey", "X", "Y", "Z", "Inherited", "Scattered",
	"Marker", "Early", "Dormant", "Fresh"};
constexpr int FIRST_RANDOM_KEY = 100;
constexpr std::array BASE_KEYS{1, 2};
constexpr std::array BASE_NAMES{"IsA(BaseOne)", "IsA(BaseTwo)"};
constexpr std::array EARLY_KEYS{10, 11, 12, 13};
constexpr std::array REFILL_KEYS{14, 15, 16};
constexpr std::array REPLAY_REFILL_KEYS{17, 18, 19};

struct Random {
	uint32_t State;

	uint32_t Next()
	{
		State = State * 1664525u + 1013904223u;
		return State >> 8;
	}
};

struct Row {
	// Flecs-owned rows have no StableKey and get their live payloads from world and query setup.
	int Key = 0;
	ecs_entity_t Id = 0;
	std::array<int, INHERITED + 1> Values{};
	bool operator==(Row const &) const = default;
};

using Signature = std::vector<std::string>;

struct Table {
	Signature Type;
	std::vector<Row> Rows;
	bool operator==(Table const &) const = default;
};

struct SparseRow {
	int Key;
	int Value;
	bool operator==(SparseRow const &) const = default;
};

struct Snapshot {
	std::vector<Table> Tables;
	std::vector<SparseRow> Sparse;
};

struct Batch {
	Signature Type;
	int Rows;
	int TableRows;
	bool operator==(Batch const &) const = default;
};

struct Visit {
	std::vector<int> Keys;
	std::vector<Batch> Batches;
};

using Query = std::unique_ptr<ecs_query_t, decltype(&ecs_query_fini)>;

struct NamedQuery {
	std::string_view Name;
	Query Handle;
};

class World {
	std::unique_ptr<ecs_world_t, decltype(&ecs_fini)> Handle{ecs_mini(), ecs_fini};
	std::array<ecs_id_t, NAMES.size()> Ids{};

public:
	World();
	ecs_world_t * Get() const { return Handle.get(); }
	ecs_id_t Id(Component component) const { return Ids[component]; }
	int Key(ecs_entity_t entity) const;
	Signature Type(ecs_table_t * table) const;
	ecs_id_t Resolve(std::string const & name) const;
	ecs_table_t * Find(std::vector<ecs_id_t> ids) const;
	void Set(ecs_entity_t entity, Component component, int value) const;
	void Remember(int key, ecs_entity_t entity);
	ecs_entity_t Spawn(int key);
	void Erase(int key);

	std::map<int, ecs_entity_t> Entities;
	std::vector<NamedQuery> Queries;
};


World::World()
{
	for (std::size_t i = 0; i < NAMES.size(); ++i) {
		ecs_entity_desc_t entity{};
		entity.name = NAMES[i];
		entity.use_low_id = true;
		Ids[i] = ecs_entity_init(Get(), &entity);
		REQUIRE(Ids[i] != 0);
		if (i <= SPARSE) {
			ecs_component_desc_t data{};
			data.entity = Ids[i];
			data.type.size = sizeof(int);
			data.type.alignment = alignof(int);
			REQUIRE(ecs_component_init(Get(), &data) == Ids[i]);
		}
	}

	ecs_add_pair(Get(), Id(KEY), EcsOnInstantiate, EcsDontInherit);
	ecs_add_pair(Get(), Id(INHERITED), EcsOnInstantiate, EcsInherit);
	ecs_add_id(Get(), Id(SPARSE), EcsDontFragment);

	auto query = [&](std::string_view name, ecs_query_cache_kind_t cache,
		std::initializer_list<ecs_term_t> terms, ecs_flags32_t flags = 0) {
		ecs_query_desc_t desc{};
		desc.cache_kind = cache;
		desc.flags = flags;
		std::copy(terms.begin(), terms.end(), desc.terms);
		Query handle{ecs_query_init(Get(), &desc), ecs_query_fini};
		INFO(name);
		REQUIRE(handle != nullptr);
		Queries.push_back({name, std::move(handle)});
	};

	// None bypasses the query cache, All requires every term to be cacheable,
	// and Auto lets flecs choose which terms to cache.
	query("X (None)", EcsQueryCacheNone, {{Id(X)}});
	query("X (All)", EcsQueryCacheAll, {{Id(X)}});
	query("X including empty (All)", EcsQueryCacheAll, {{Id(X)}}, EcsQueryMatchEmptyTables);
	query("X, Y (All)", EcsQueryCacheAll, {{Id(X)}, {Id(Y)}});
	query("X, ?Y (Auto)", EcsQueryCacheAuto, {{Id(X)}, {.id = Id(Y), .oper = EcsOptional}});
	query("X, !Marker (None)", EcsQueryCacheNone, {{Id(X)}, {.id = Id(MARKER), .oper = EcsNot}});
	query("Inherited self|up IsA (Auto)", EcsQueryCacheAuto,
		{{.id = Id(INHERITED), .src = {.id = EcsSelf | EcsUp}, .trav = EcsIsA}});
	query("Scattered DontFragment (None)", EcsQueryCacheNone, {{Id(SPARSE)}});
	query("Any including empty (None)", EcsQueryCacheNone, {{.id = EcsAny, .inout = EcsInOutNone}},
		EcsQueryMatchEmptyTables | EcsQueryMatchPrefab | EcsQueryMatchDisabled);
}


int World::Key(ecs_entity_t entity) const
{
	auto const * value = static_cast<int const *>(ecs_get_id(Get(), entity, Id(KEY)));
	return value ? *value : 0;
}


Signature World::Type(ecs_table_t * table) const
{
	Signature names;
	ecs_type_t const * type = ecs_table_get_type(table);
	for (int32_t i = 0; i < type->count; ++i) {
		ecs_id_t const id = type->array[i];
		if (ECS_HAS_RELATION(id, EcsIsA)) {
			auto const base = std::ranges::find(BASE_KEYS, Key(ecs_pair_second(Get(), id)));
			if (base != BASE_KEYS.end()) {
				names.emplace_back(BASE_NAMES[base - BASE_KEYS.begin()]);
				continue;
			}
		}
		if (id == EcsPrefab) {
			names.emplace_back("Prefab");
			continue;
		}
		auto const component = std::ranges::find(Ids, id);
		if (component != Ids.end()) {
			names.emplace_back(NAMES[component - Ids.begin()]);
			continue;
		}
		std::unique_ptr<char, decltype(ecs_os_api.free_)> name{ecs_id_str(Get(), id), ecs_os_api.free_};
		REQUIRE(name != nullptr);
		names.emplace_back(name.get());
	}
	return names;
}


ecs_id_t World::Resolve(std::string const & name) const
{
	for (std::size_t i = 0; i < BASE_NAMES.size(); ++i) {
		if (name == BASE_NAMES[i]) {
			auto const target = Entities.find(BASE_KEYS[i]);
			REQUIRE(target != Entities.end());
			return ecs_pair(EcsIsA, target->second);
		}
	}
	if (name == "Prefab") {
		return EcsPrefab;
	}
	if (name == "Query") {
		return EcsQuery;
	}
	if (name == "(Poly,Query)") {
		return ecs_pair(ecs_id(EcsPoly), EcsQuery);
	}
	auto const component = std::ranges::find(NAMES, std::string_view{name});
	INFO(name);
	REQUIRE(component != NAMES.end());
	return Ids[component - NAMES.begin()];
}


ecs_table_t * World::Find(std::vector<ecs_id_t> ids) const
{
	std::ranges::sort(ids);
	ecs_table_t * table = ecs_table_find(Get(), ids.data(), static_cast<int32_t>(ids.size()));
	REQUIRE(table != nullptr);
	return table;
}


void World::Set(ecs_entity_t entity, Component component, int value) const
{
	ecs_set_id(Get(), entity, Id(component), sizeof(value), &value);
}


void World::Remember(int key, ecs_entity_t entity)
{
	REQUIRE(entity != 0);
	REQUIRE(Entities.emplace(key, entity).second);
}


ecs_entity_t World::Spawn(int key)
{
	ecs_entity_t const entity = ecs_new_w_id(Get(), Id(KEY));
	Remember(key, entity);
	Set(entity, KEY, key);
	return entity;
}


void World::Erase(int key)
{
	ecs_delete(Get(), Entities.at(key));
	Entities.erase(key);
}


void Check_Line(std::string_view name, bool passed)
{
	std::cout << (passed ? "PASS " : "FAIL ") << name << std::endl;
	INFO(name);
	CHECK(passed);
}


std::ostream & operator<<(std::ostream & out, Signature const & type)
{
	out << '[';
	for (std::size_t i = 0; i < type.size(); ++i) {
		out << (i ? ", " : "") << type[i];
	}
	return out << ']';
}


std::ostream & operator<<(std::ostream & out, Batch const & batch)
{
	return out << batch.Type << ' ' << batch.Rows << '/' << batch.TableRows;
}


std::ostream & operator<<(std::ostream & out, SparseRow const & row)
{
	return out << row.Key << '=' << row.Value;
}


std::ostream & operator<<(std::ostream & out, Row const & row)
{
	out << "key " << row.Key << " ID " << row.Id << " values";
	for (int const value : row.Values) {
		out << ' ' << value;
	}
	return out;
}


template<typename T>
void Print_First_Difference(std::string_view sequence, std::vector<T> const & left,
	std::vector<T> const & right, char right_world = 'B')
{
	auto const [a, b] = std::ranges::mismatch(left, right);
	if (a == left.end() && b == right.end()) {
		return;
	}
	std::cout << "  " << sequence << " position " << (a - left.begin() + 1) << " A=";
	if (a == left.end()) {
		std::cout << "<end>";
	} else {
		std::cout << *a;
	}
	std::cout << ' ' << right_world << '=';
	if (b == right.end()) {
		std::cout << "<end>";
	} else {
		std::cout << *b;
	}
	std::cout << '\n';
}


Visit Read_Query(World const & world, NamedQuery const & query)
{
	Visit result;
	ecs_iter_t it = ecs_query_iter(world.Get(), query.Handle.get());
	while (ecs_query_next(&it)) {
		REQUIRE(it.table != nullptr);
		result.Batches.push_back({world.Type(it.table), it.count, ecs_table_count(it.table)});
		for (int32_t i = 0; i < it.count; ++i) {
			if (int const key = world.Key(it.entities[i])) {
				result.Keys.push_back(key);
			}
		}
	}
	return result;
}


Snapshot Capture(World const & world)
{
	Snapshot result;
	for (int32_t i = 0; i < World_Table_Count(world.Get()); ++i) {
		ecs_table_t * table = Table_In_World_Order(world.Get(), i);
		Table saved{world.Type(table), {}};
		for (int32_t j = 0; j < ecs_table_count(table); ++j) {
			ecs_entity_t const entity = ecs_table_entities(table)[j];
			Row row{world.Key(entity), entity, {}};
			row.Values[KEY] = row.Key;
			for (auto const id : {X, Y, Z, INHERITED}) {
				auto const * value = static_cast<int const *>(
					ecs_table_get_id(world.Get(), table, world.Id(id), j));
				if (value) {
					row.Values[id] = *value;
				}
			}
			saved.Rows.push_back(row);
		}
		result.Tables.push_back(std::move(saved));
	}

	int32_t count = 0;
	ecs_entity_t const * ids = Dont_Fragment_Storage(world.Get(), world.Id(SPARSE), &count);
	for (int32_t i = 0; i < count; ++i) {
		int const key = world.Key(ids[i]);
		auto const * value = static_cast<int const *>(ecs_get_id(world.Get(), ids[i], world.Id(SPARSE)));
		REQUIRE(key != 0);
		REQUIRE(value != nullptr);
		result.Sparse.push_back({key, *value});
	}
	return result;
}


void Append_Rows(World & world, ecs_table_t * table, std::vector<Row> const & rows)
{
	if (rows.empty()) {
		return;
	}

	std::vector<ecs_entity_t> ids;
	std::array<std::vector<int>, INHERITED + 1> columns;
	for (Row const & row : rows) {
		REQUIRE(row.Key != 0);
		REQUIRE_FALSE(ecs_is_alive(world.Get(), row.Id));
		ids.push_back(row.Id);
		for (std::size_t i = 0; i < columns.size(); ++i) {
			columns[i].push_back(row.Values[i]);
		}
	}

	ecs_bulk_desc_t desc{};
	desc.entities = ids.data();
	desc.count = static_cast<int32_t>(ids.size());
	desc.table = table;
	ecs_type_t const * type = ecs_table_get_type(table);
	std::array<void *, FLECS_ID_DESC_MAX> data{};
	REQUIRE(type->count < FLECS_ID_DESC_MAX);
	for (int32_t i = 0; i < type->count; ++i) {
		desc.ids[i] = type->array[i];
		for (std::size_t j = 0; j < columns.size(); ++j) {
			if (desc.ids[i] == world.Id(static_cast<Component>(j))) {
				data[i] = columns[j].data();
			}
		}
	}
	desc.data = data.data();
	// The explicit-table branch uses ids to count data columns in 4.1.6.
	REQUIRE(ecs_bulk_init(world.Get(), &desc) != nullptr);
	REQUIRE(ecs_table_count(table) == desc.count);
	for (int32_t i = 0; i < desc.count; ++i) {
		REQUIRE(ecs_table_entities(table)[i] == ids[i]);
		REQUIRE(ecs_get_table(world.Get(), ids[i]) == table);
		world.Remember(rows[i].Key, ids[i]);
	}
}


bool Is_Prefab_Table(Table const & table)
{
	return std::ranges::find(table.Type, "Prefab") != table.Type.end();
}


bool Is_Query_Table(Table const & table)
{
	return std::ranges::find(table.Type, "(Poly,Query)") != table.Type.end();
}


void Restore(World & world, Snapshot const & snapshot)
{
	Snapshot const bootstrap = Capture(world);
	REQUIRE(bootstrap.Tables.size() <= snapshot.Tables.size());
	Check_Line("B bootstrap table type prefix", std::equal(bootstrap.Tables.begin(), bootstrap.Tables.end(),
		snapshot.Tables.begin(), [](auto const & a, auto const & b) { return a.Type == b.Type; }));

	int64_t const before = ecs_get_world_info(world.Get())->table_create_total;
	std::vector<ecs_table_t *> tables;
	for (std::size_t i = bootstrap.Tables.size(); i < snapshot.Tables.size(); ++i) {
		Table const & saved = snapshot.Tables[i];
		std::vector<ecs_id_t> ids;
		for (std::string const & name : saved.Type) {
			ids.push_back(world.Resolve(name));
		}
		ecs_table_t * table = world.Find(std::move(ids));
		tables.push_back(table);
		// Pair registration requires live targets before their IsA tables can be created.
		if (Is_Prefab_Table(saved)) {
			Append_Rows(world, table, saved.Rows);
		}
	}

	int64_t const after_tables = ecs_get_world_info(world.Get())->table_create_total;
	for (std::size_t i = 0; i < tables.size(); ++i) {
		Table const & saved = snapshot.Tables[bootstrap.Tables.size() + i];
		// Flecs moves its live query entities when their caches first match a table.
		if (!Is_Prefab_Table(saved) && !Is_Query_Table(saved)) {
			Append_Rows(world, tables[i], saved.Rows);
		}
	}

	bool sparse_stayed_in_place = true;
	for (SparseRow const & row : snapshot.Sparse) {
		ecs_entity_t const entity = world.Entities.at(row.Key);
		ecs_table_t * const table = ecs_get_table(world.Get(), entity);
		ecs_entity_t const * entities = ecs_table_entities(table);
		auto const * slot = std::find(entities, entities + ecs_table_count(table), entity);
		REQUIRE(slot != entities + ecs_table_count(table));
		auto const index = slot - entities;
		world.Set(entity, SPARSE, row.Value);
		sparse_stayed_in_place &= ecs_get_table(world.Get(), entity) == table &&
			ecs_table_entities(table)[index] == entity;
	}
	Check_Line("B direct placement created no intermediate tables or sparse moves",
		after_tables - before == static_cast<int64_t>(tables.size()) &&
		ecs_get_world_info(world.Get())->table_create_total == after_tables &&
		sparse_stayed_in_place);
}


void Restore_Naive(World & world, Snapshot const & snapshot)
{
	std::map<int, std::pair<Table const *, Row const *>> rows;
	std::map<int, int> scattered;
	for (Table const & table : snapshot.Tables) {
		for (Row const & row : table.Rows) {
			if (row.Key) {
				rows.emplace(row.Key, std::pair{&table, &row});
			}
		}
	}
	for (SparseRow const & row : snapshot.Sparse) {
		scattered.emplace(row.Key, row.Value);
	}

	for (auto const & [key, saved] : rows) {
		ecs_entity_t const entity = world.Spawn(key);
		for (std::string const & name : saved.first->Type) {
			ecs_id_t const id = world.Resolve(name);
			if (id == world.Id(KEY)) {
				continue;
			}
			if (ecs_get_type_info(world.Get(), id)) {
				auto const pos = std::ranges::find(NAMES, std::string_view{name});
				REQUIRE(pos != NAMES.end());
				world.Set(entity, static_cast<Component>(pos - NAMES.begin()),
					saved.second->Values[pos - NAMES.begin()]);
			} else {
				ecs_add_id(world.Get(), entity, id);
			}
		}
		if (auto const it = scattered.find(key); it != scattered.end()) {
			world.Set(entity, SPARSE, it->second);
		}
	}
}



enum class Action { CREATE, DELETE, SET, REMOVE, ADD_TAG };

struct Operation {
	Action What;
	int Key;
	Component Which = KEY;
	uint32_t Value = 0;
};


void Apply(World & world, Operation const & op)
{
	if (op.What == Action::CREATE) {
		ecs_entity_t const entity = world.Spawn(op.Key);
		for (auto const component : {X, Y, Z}) {
			if (op.Value & (1u << component)) {
				world.Set(entity, component, op.Key * 10 + component);
			}
		}
		return;
	}
	if (op.What == Action::DELETE) {
		world.Erase(op.Key);
		return;
	}

	ecs_entity_t const entity = world.Entities.at(op.Key);
	switch (op.What) {
	case Action::SET:
		world.Set(entity, op.Which, static_cast<int>(op.Value));
		break;
	case Action::REMOVE:
		ecs_remove_id(world.Get(), entity, world.Id(op.Which));
		break;
	case Action::ADD_TAG:
		ecs_add_id(world.Get(), entity, world.Id(op.Which));
		break;
	default:
		FAIL("unexpected operation");
	}
}


Operation Next_Operation(World const & world, Random & rng, int & next_key)
{
	auto const action = rng.Next() % 8;
	auto const first = world.Entities.lower_bound(FIRST_RANDOM_KEY);
	if (action == 0 || first == world.Entities.end()) {
		return {Action::CREATE, next_key++, KEY, rng.Next()};
	}

	auto const live = static_cast<uint32_t>(std::distance(first, world.Entities.end()));
	auto const offset = rng.Next() % live;
	int const key = std::next(first, offset)->first;
	auto const component = static_cast<Component>(X + rng.Next() % 3);
	switch (action) {
	case 1: return {Action::DELETE, key};
	case 2: return {Action::SET, key, component, rng.Next()};
	case 3: return {Action::REMOVE, key, component};
	case 4: return {Action::SET, key, SPARSE, rng.Next()};
	case 5: return {Action::REMOVE, key, SPARSE};
	case 6: return {Action::ADD_TAG, key, MARKER};
	default: return {Action::REMOVE, key, MARKER};
	}
}


template<std::size_t Size>
void Fill_Early(World & world, std::array<int, Size> const & keys)
{
	for (auto const key : keys) {
		Apply(world, {Action::CREATE, key, KEY, 1u << X});
		Apply(world, {Action::ADD_TAG, key, EARLY});
	}
}


std::vector<int> Row_Keys(World const & world, ecs_table_t * table)
{
	std::vector<int> keys;
	for (int32_t i = 0; i < ecs_table_count(table); ++i) {
		keys.push_back(world.Key(ecs_table_entities(table)[i]));
	}
	return keys;
}


bool Has_Batch(Visit const & visit, Signature const & type, int rows)
{
	return std::ranges::find(visit.Batches, Batch{type, rows, rows}) != visit.Batches.end();
}


NamedQuery const & Get_Query(World const & world, std::string_view name)
{
	auto const it = std::ranges::find(world.Queries, name, &NamedQuery::Name);
	REQUIRE(it != world.Queries.end());
	return *it;
}


void Build_History(World & world, int & next_key)
{
	ecs_table_t * prefab = world.Find({world.Id(KEY), world.Id(INHERITED), EcsPrefab});
	for (int const key : BASE_KEYS) {
		ecs_entity_t const entity = ecs_new_w_table(world.Get(), prefab);
		world.Remember(key, entity);
		world.Set(entity, KEY, key);
		world.Set(entity, INHERITED, 9000 + key);
	}

	Fill_Early(world, EARLY_KEYS);
	for (unsigned i = 0; i < 24; ++i) {
		Apply(world, {Action::CREATE, next_key++, KEY, i << X});
	}

	ecs_table_t * early = ecs_get_table(world.Get(), world.Entities.at(EARLY_KEYS.front()));
	ecs_entity_t const deleted = world.Entities.at(EARLY_KEYS[1]);
	world.Erase(EARLY_KEYS[1]);
	Check_Line("A middle deletion swapped the last row into its slot",
		Row_Keys(world, early) == std::vector<int>{10, 13, 12});
	ecs_entity_t const recycled = world.Spawn(next_key++);
	Check_Line("A reused a deleted entity index with another generation",
		static_cast<uint32_t>(deleted) == static_cast<uint32_t>(recycled) && deleted != recycled);

	auto const & cached = Get_Query(world, "X (All)");
	auto const & with_empty = Get_Query(world, "X including empty (All)");
	Visit const before = Read_Query(world, cached);
	for (int const key : {10, 12, 13}) {
		world.Erase(key);
	}
	Signature const early_type = world.Type(early);
	Visit const empty = Read_Query(world, cached);
	Visit const including_empty = Read_Query(world, with_empty);
	Check_Line("A cached X skips the drained table and MatchEmptyTables visits it",
		ecs_table_count(early) == 0 && !Has_Batch(empty, early_type, 0) &&
		Has_Batch(including_empty, early_type, 0));
	Fill_Early(world, REFILL_KEYS);
	Visit const refilled = Read_Query(world, cached);
	Check_Line("A cached X returns the refilled table to its original position",
		Has_Batch(before, early_type, 3) && before.Batches == refilled.Batches &&
		Row_Keys(world, early) == std::vector<int>(REFILL_KEYS.begin(), REFILL_KEYS.end()));

	ecs_table_t * dormant = world.Find({world.Id(KEY), world.Id(Z), world.Id(DORMANT)});
	std::array<int, 4> observed{};
	for (int i = 0; i < 4; ++i) {
		int const base_key = BASE_KEYS[i / 2];
		ecs_entity_t const entity = world.Spawn(20 + i);
		ecs_add_pair(world.Get(), entity, EcsIsA, world.Entities.at(base_key));
		if (i % 2) {
			world.Set(entity, INHERITED, 9100 + i);
		}
		auto const * value = static_cast<int const *>(
			ecs_get_id(world.Get(), entity, world.Id(INHERITED)));
		REQUIRE(value != nullptr);
		observed[i] = *value;
	}
	Check_Line("A has inherited prefab values and own overrides",
		observed == std::array{9001, 9101, 9002, 9103});

	Random rng{0xa82f19d3u};
	for (int i = 0; i < 420; ++i) {
		Apply(world, Next_Operation(world, rng, next_key));
	}
	world.Spawn(next_key);
	world.Erase(next_key++);
	auto const any = Read_Query(world, Get_Query(world, "Any including empty (None)"));
	Check_Line("A Any visits the table created empty",
		Has_Batch(any, world.Type(dormant), 0));
	std::cout << "A world store has " << World_Table_Count(world.Get())
		<< " tables including root; Any yielded " << any.Batches.size() << " batches\n";
	for (int32_t i = 0; i < World_Table_Count(world.Get()); ++i) {
		ecs_table_t * table = Table_In_World_Order(world.Get(), i);
		auto const type = world.Type(table);
		if (!Has_Batch(any, type, ecs_table_count(table))) {
			std::cout << "  Any omitted " << type << '\n';
		}
	}
}


void Further_Operations(World & a, World & b, int & next_key)
{
	for (int const key : REFILL_KEYS) {
		a.Erase(key);
		b.Erase(key);
	}

	int const fresh_key = next_key++;
	for (World * world : {&a, &b}) {
		int64_t const previous = ecs_get_world_info(world->Get())->table_create_total;
		ecs_table_t * early = world->Find({world->Id(KEY), world->Id(X), world->Id(EARLY)});
		ecs_table_t * table = world->Find({world->Id(KEY), world->Id(X), world->Id(FRESH)});
		Check_Line(world == &a ? "A replay starts a new table" : "B replay starts a new table",
			ecs_get_world_info(world->Get())->table_create_total == previous + 1);
		auto const entity = ecs_new_w_table(world->Get(), table);
		world->Set(entity, KEY, fresh_key);
		world->Set(entity, X, 4400);
		world->Remember(fresh_key, entity);
		bool const was_empty = ecs_table_count(early) == 0;
		Fill_Early(*world, REPLAY_REFILL_KEYS);
		Check_Line(world == &a ? "A replay refills the emptied table" : "B replay refills the emptied table",
			was_empty && Row_Keys(*world, early) == std::vector<int>(REPLAY_REFILL_KEYS.begin(), REPLAY_REFILL_KEYS.end()));
	}
	Random rng{0x573bbfa1u};
	for (int i = 0; i < 360; ++i) {
		Operation const op = Next_Operation(a, rng, next_key);
		Apply(a, op);
		Apply(b, op);
	}
	auto const [left, right] = std::ranges::mismatch(a.Entities, b.Entities);
	if (left != a.Entities.end() && right != b.Entities.end() && left->first == right->first) {
		std::cout << "Replay first different live handle at key " << left->first
			<< " A=" << left->second << " B=" << right->second << '\n';
	} else if (left == a.Entities.end() && right == b.Entities.end()) {
		std::cout << "Replay live handles matched\n";
	}
}


std::vector<Batch> Table_List(Snapshot const & snapshot)
{
	std::vector<Batch> result;
	for (auto const & table : snapshot.Tables) {
		int const count = static_cast<int>(table.Rows.size());
		result.push_back({table.Type, count, count});
	}
	return result;
}


auto Keyed_Values(Snapshot const & snapshot)
{
	std::map<int, std::pair<Signature, decltype(Row::Values)>> result;
	for (auto const & table : snapshot.Tables) {
		for (auto const & row : table.Rows) {
			if (row.Key) {
				REQUIRE(result.emplace(row.Key, std::pair{table.Type, row.Values}).second);
			}
		}
	}
	return result;
}


void Compare_Storage(std::string const & phase, Snapshot a, Snapshot b, bool compare_ids)
{
	auto const a_tables = Table_List(a);
	auto const b_tables = Table_List(b);
	Check_Line(phase + " full table order and row counts", a_tables == b_tables);
	Print_First_Difference("world table", a_tables, b_tables);

	if (!compare_ids) {
		for (Snapshot * snapshot : {&a, &b}) {
			for (Table & table : snapshot->Tables) {
				for (Row & row : table.Rows) {
					if (row.Key) {
						row.Id = 0;
					}
				}
			}
		}
	}
	Check_Line(phase + (compare_ids ? " row order, owned values and live IDs" : " row order and owned values"),
		a.Tables == b.Tables);
	for (std::size_t i = 0; i < std::min(a.Tables.size(), b.Tables.size()); ++i) {
		if (a.Tables[i].Type == b.Tables[i].Type && a.Tables[i].Rows != b.Tables[i].Rows) {
			std::cout << "  world table " << i + 1 << ' ' << a.Tables[i].Type << '\n';
			Print_First_Difference("row", a.Tables[i].Rows, b.Tables[i].Rows);
			break;
		}
	}
	Check_Line(phase + " DontFragment dense storage order and values", a.Sparse == b.Sparse);
	Print_First_Difference("sparse storage", a.Sparse, b.Sparse);
}


std::vector<Visit> Read_Queries(World const & world)
{
	std::vector<Visit> result;
	for (NamedQuery const & query : world.Queries) {
		result.push_back(Read_Query(world, query));
	}
	return result;
}


void Compare_Queries(std::string const & phase, World const & world, std::vector<Visit> const & expected,
	World const & candidate, bool control = false)
{
	bool any_difference = false;
	for (std::size_t i = 0; i < expected.size(); ++i) {
		Visit const & a = expected[i];
		Visit const b = Read_Query(candidate, candidate.Queries[i]);
		bool const same = a.Keys == b.Keys && a.Batches == b.Batches;
		std::string const name = phase + " " + std::string{world.Queries[i].Name};
		if (control) {
			std::cout << (same ? "SAME " : "DIFFERENT ") << name << '\n';
			any_difference |= !same;
		} else {
			Check_Line(name, same);
		}
		Print_First_Difference("key", a.Keys, b.Keys, control ? 'C' : 'B');
		Print_First_Difference("batch", a.Batches, b.Batches, control ? 'C' : 'B');
	}
	if (control) {
		Check_Line("C differs from A in at least one query", any_difference);
	}
}

} // namespace


TEST_CASE("snapshot and replay preserve flecs table and query order")
{
	World a;
	int64_t const a_deleted_tables = ecs_get_world_info(a.Get())->table_delete_total;
	int next_key = FIRST_RANDOM_KEY;
	Build_History(a, next_key);

	Snapshot const snapshot = Capture(a);
	auto const keyed_tables = std::ranges::count_if(snapshot.Tables, [](Table const & table) {
		return std::ranges::find(table.Type, NAMES[KEY]) != table.Type.end();
	});
	Check_Line("A created at least six keyed tables", keyed_tables >= 6);
	auto const queries = Read_Queries(a);
	for (std::size_t i = 0; i < queries.size(); ++i) {
		Check_Line("A has matches for " + std::string{a.Queries[i].Name}, !queries[i].Keys.empty());
	}
	for (std::string_view name : {"X, ?Y (Auto)", "Inherited self|up IsA (Auto)"}) {
		auto const kind = Get_Query(a, name).Handle->cache_kind;
		std::cout << "Effective cache for " << name << " = "
			<< (kind == EcsQueryCacheAll ? "All" : kind == EcsQueryCacheAuto ? "Auto" : "None") << '\n';
	}
	auto inherited = Read_Query(a, Get_Query(a, "Inherited self|up IsA (Auto)")).Keys;
	std::ranges::sort(inherited);
	Check_Line("A inheritance query visits both inherited and overriding instances",
		inherited == std::vector<int>{20, 21, 22, 23});
	std::vector<int> reverse_sparse;
	for (auto it = snapshot.Sparse.rbegin(); it != snapshot.Sparse.rend(); ++it) {
		reverse_sparse.push_back(it->Key);
	}
	Check_Line("A DontFragment query visits the reverse of dense sparse storage",
		Read_Query(a, Get_Query(a, "Scattered DontFragment (None)")).Keys == reverse_sparse);

	World b;
	int64_t const b_deleted_tables = ecs_get_world_info(b.Get())->table_delete_total;
	Restore(b, snapshot);
	Compare_Storage("After rebuild", snapshot, Capture(b), true);
	Compare_Queries("After rebuild", a, queries, b);

	World c;
	Restore_Naive(c, snapshot);
	Snapshot const control = Capture(c);
	auto a_sparse = snapshot.Sparse;
	auto c_sparse = control.Sparse;
	std::ranges::sort(a_sparse, {}, &SparseRow::Key);
	std::ranges::sort(c_sparse, {}, &SparseRow::Key);
	Check_Line("C has the same tables by key, owned values and sparse membership",
		Keyed_Values(snapshot) == Keyed_Values(control) && a_sparse == c_sparse);
	auto const a_tables = Table_List(snapshot);
	auto const c_tables = Table_List(control);
	std::cout << (a_tables == c_tables ? "SAME " : "DIFFERENT ") << "C full table order and row counts\n";
	Print_First_Difference("world table", a_tables, c_tables, 'C');
	Compare_Queries("C control", a, queries, c, true);

	Further_Operations(a, b, next_key);
	Compare_Storage("After further operations", Capture(a), Capture(b), false);
	Compare_Queries("After further operations", a, Read_Queries(a), b);
	Check_Line("A and B retained all tables after registration",
		ecs_get_world_info(a.Get())->table_delete_total == a_deleted_tables &&
		ecs_get_world_info(b.Get())->table_delete_total == b_deleted_tables);
}

