/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "syncrechook.h"

#include "_rtti.h"
#include "abstract.h"
#include "anim.h"
#include "animtype.h"
#include "coord.h"
#include "event.h"
#include "face.h"
#include "globals.h"
#include "mission.h"
#include "object.h"
#include "random.h"
#include "scenario.h"
#include "session.h"

#include "win.h"


static uintptr_t ModuleBase = 0;
static uint32_t ModuleSize = 0;


static uint32_t Sync_Caller_RVA(unsigned caller)
{
	uintptr_t const address = caller;
	if (ModuleBase != 0 && address >= ModuleBase && address < ModuleBase + ModuleSize) {
		return((uint32_t)(address - ModuleBase));
	}
	// Outside the running image; keep the low bits and flag it so the printer says so.
	return(0x80000000u | ((uint32_t)address & 0x7fffffffu));
}


void Sync_Record_Random_Impl(Random2Class const & gen, int value, int minval, int maxval, bool ranged, unsigned caller)
{
	SyncRandomEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.Index1 = gen.Index_1();
	entry.Index2 = gen.Index_2();
	entry.Value = value;
	entry.Min = minval;
	entry.Max = maxval;
	entry.Kind = (Scen != nullptr && &gen == &Scen->RandomNumber) ? SYNC_RANDOM_CRITICAL : SYNC_RANDOM_NONCRITICAL;
	entry.Shape = ranged ? SYNC_DRAW_RANGED : (SyncRecorder.In_Ranged_Draw() ? SYNC_DRAW_INNER : SYNC_DRAW_PLAIN);
	SyncRecorder.Add_Random(entry);
}


void Sync_Record_Facing_Impl(DirType const & facing, unsigned caller)
{
	SyncFacingEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.Facing = (int16_t)(int)facing.As_Dir256();
	entry.Kind = SYNC_FACING_SET;
	SyncRecorder.Add_Facing(entry);
}


void Sync_Record_Target_Impl(AbstractClass const & subject, AbstractClass const * target, unsigned caller)
{
	SyncTargetEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.SubjectRTTI = (int8_t)subject.Fetch_RTTI();
	entry.SubjectID = subject.Fetch_ID();
	entry.TargetRTTI = (int8_t)(target != nullptr ? target->Fetch_RTTI() : RTTI_NONE);
	entry.TargetID = (target != nullptr ? target->Fetch_ID() : 0);
	SyncRecorder.Add_Target(entry);
}


void Sync_Record_Mission_Impl(ObjectClass const & subject, int before, int after, int kind, unsigned caller)
{
	SyncMissionEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.SubjectRTTI = (int8_t)subject.Fetch_RTTI();
	entry.SubjectID = subject.Fetch_ID();
	entry.Before = (int8_t)before;
	entry.After = (int8_t)after;
	entry.Kind = (uint8_t)kind;
	SyncRecorder.Add_Mission(entry);
}


void Sync_Record_Anim_Impl(AnimClass const & anim, Coord const & coord, unsigned caller)
{
	SyncAnimEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.AnimID = anim.Fetch_ID();
	entry.TypeHeapID = (int16_t)(anim.Class != nullptr ? anim.Class->Fetch_Heap_ID() : -1);
	Coord coord_copy = coord;
	entry.Coord = coord_copy.As_Int();
	SyncRecorder.Add_Anim(entry);
}


void Sync_Record_Event_Impl(EventClass const & event, int source)
{
	switch (event.Type) {
		case EventClass::FRAMEINFO:
		case EventClass::FRAMESYNC:
		case EventClass::RESPONSE_TIME:
		case EventClass::PROCESS_TIME:
		case EventClass::TIMING:
			return;
		default:
			break;
	}

	SyncEventEntryType entry {};
	entry.Frame = event.Frame;
	entry.SeenAt = Frame;
	entry.House = event.ID;
	entry.Type = event.Type;
	entry.Source = (uint8_t)source;

	unsigned length = 0;
	if (event.Type < EventClass::LAST_EVENT) {
		length = EventClass::EventLength[event.Type];
	}
	if (length > sizeof(entry.Bytes)) {
		length = sizeof(entry.Bytes);
	}
	entry.Length = (uint8_t)length;
	memcpy(entry.Bytes, &event.Data, length);

	if (source == SYNC_EVENT_QUEUED) {
		SyncRecorder.Add_Queued_Event(entry);
	} else {
		SyncRecorder.Add_Executed_Event(entry);
	}
}


void Sync_Recorder_Arm(void)
{
	SyncRecorder.Reset();

	bool const network = (Session.Type == GAME_IPX || Session.Type == GAME_INTERNET);
	SyncRecorder.Set_Recording(network || Session.Record || Session.Play);

	ModuleBase = (uintptr_t)GetModuleHandle(nullptr);
	ModuleSize = 0;
	if (ModuleBase != 0) {
		IMAGE_DOS_HEADER const * dos = (IMAGE_DOS_HEADER const *)ModuleBase;
		if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
			IMAGE_NT_HEADERS const * nt = (IMAGE_NT_HEADERS const *)(ModuleBase + dos->e_lfanew);
			if (nt->Signature == IMAGE_NT_SIGNATURE) {
				ModuleSize = nt->OptionalHeader.SizeOfImage;
			}
		}
	}
}


void Sync_Recorder_Disarm(void)
{
	SyncRecorder.Set_Recording(false);
	SyncRecorder.Reset();
}


static char const * Anim_Name(int heap_id)
{
	if (heap_id >= 0 && heap_id < AnimTypes.Count()) {
		return(AnimTypes[heap_id]->Name());
	}
	return("?");
}


static char const * Mission_Name(int mission)
{
	return(MissionClass::Mission_Name((MissionType)mission));
}


static char const * Rtti_Name(int rtti)
{
	return(Name_From_RTTI((RTTIType)rtti));
}


static char const * Event_Name(int type)
{
	if (type >= 0 && type < EventClass::LAST_EVENT) {
		return(EventClass::EventNames[type]);
	}
	return("?");
}


SyncNamesType const & Sync_Engine_Names(void)
{
	static SyncNamesType const names = { Rtti_Name, Mission_Name, Event_Name, Anim_Name };
	return(names);
}
