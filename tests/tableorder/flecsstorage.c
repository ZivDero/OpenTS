/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "flecsstorage.h"
#include "private_api.h"

#if FLECS_VERSION_MAJOR != 4 || FLECS_VERSION_MINOR != 1 || FLECS_VERSION_PATCH != 6
#error Check the flecs table store, sparse storage and entity index before updating this harness.
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


bool Restore_Recyclable_Ids(ecs_world_t * world, ecs_entity_t max_id, int32_t alive_count,
	const ecs_entity_t * ids, int32_t count)
{
	ecs_entity_index_t * index = ecs_eis(world);
	if (index->active_range || flecs_entity_index_count(index) != alive_count ||
		flecs_entity_index_not_alive_count(index) != 0 || index->max_id > max_id) {
		return false;
	}
	for (int32_t i = 0; i < count; ++i) {
		uint32_t const id = (uint32_t)ids[i];
		if (!id || id > max_id || flecs_entity_index_exists(index, id)) {
			return false;
		}
	}

	// Flecs 4.1.6 exposes the recyclable suffix for reading but provides no public restore operation.
	int32_t const start = index->alive_count;
	ecs_vec_set_count_t(index->allocator, &index->dense, ecs_entity_t, start + count);
	ecs_entity_t * dense = ecs_vec_first_t(&index->dense, ecs_entity_t);
	for (int32_t i = 0; i < count; ++i) {
		uint32_t const id = (uint32_t)ids[i];
		ecs_entity_index_page_t * page = flecs_entity_index_ensure_page(index, id);
		page->records[id & FLECS_ENTITY_PAGE_MASK].dense = start + i;
		dense[start + i] = ids[i];
	}
	index->max_id = max_id;
	return true;
}
