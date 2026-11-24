/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "flecs.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t World_Table_Count(const ecs_world_t * world);
ecs_table_t * Table_In_World_Order(ecs_world_t * world, int32_t index);
const ecs_entity_t * Dont_Fragment_Storage(const ecs_world_t * world, ecs_id_t id, int32_t * count);
bool Restore_Recyclable_Ids(ecs_world_t * world, ecs_entity_t max_id, int32_t alive_count,
	const ecs_entity_t * ids, int32_t count);

#ifdef __cplusplus
}
#endif
