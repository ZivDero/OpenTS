/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "flecsstorage.h"
#include "private_api.h"

#if FLECS_VERSION_MAJOR != 4 || FLECS_VERSION_MINOR != 1 || FLECS_VERSION_PATCH != 6
#error Check the flecs table store and sparse storage layout before updating this harness.
#endif

int32_t World_Table_Count(const ecs_world_t * world)
{
	return flecs_sparse_count(&world->store.tables);
}


// Dense slot zero is reserved for the root, which is stored outside the sparse set.
ecs_table_t * Table_In_World_Order(ecs_world_t * world, int32_t index)
{
	return index == 0 ? &world->store.root :
		flecs_sparse_get_dense_t(&world->store.tables, ecs_table_t, index);
}


const ecs_entity_t * Dont_Fragment_Storage(const ecs_world_t * world, ecs_id_t id, int32_t * count)
{
	const ecs_component_record_t * record = flecs_components_get(world, id);
	const ecs_sparse_t * sparse = record ? record->sparse : NULL;
	*count = sparse ? flecs_sparse_count(sparse) : 0;
	return sparse ? flecs_sparse_ids(sparse) : NULL;
}
