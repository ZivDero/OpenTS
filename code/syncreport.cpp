/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#include "always.h"

#include "syncreport.h"

#include "syncrechook.h"

#include "_logic.h"
#include "_map.h"
#include "_mono.h"
#include "_rtti.h"
#include "_script.h"
#include "_vanim.h"
#include "_warhead.h"
#include "_weapon.h"
#include "aircraft.h"
#include "airctype.h"
#include "anim.h"
#include "animtype.h"
#include "blight.h"
#include "building.h"
#include "builtype.h"
#include "bullet.h"
#include "bullettype.h"
#include "cell.h"
#include "crc.h"
#include "dbgprint.h"
#include "drive.h"
#include "empulse.h"
#include "event.h"
#include "factory.h"
#include "getcpu.h"
#include "globals.h"
#include "goptions.h"
#include "house.h"
#include "houstype.h"
#include "iloco.h"
#include "incdec.h"
#include "infantry.h"
#include "infatype.h"
#include "light.h"
#include "mono.h"
#include "opents_build.h"
#include "overlay.h"
#include "overtype.h"
#include "particle.h"
#include "partsys.h"
#include "psystype.h"
#include "ptype.h"
#include "queue.h"
#include "scenario.h"
#include "saveload.h"
#include "scheme.h"
#include "script.h"
#include "session.h"
#include "side.h"
#include "smudge.h"
#include "smudtype.h"
#include "spawner.h"
#include "taction.h"
#include "tag.h"
#include "tagtype.h"
#include "target.h"
#include "taskforc.h"
#include "team.h"
#include "teamtype.h"
#include "terrain.h"
#include "tevent.h"
#include "tiberium.h"
#include "trigger.h"
#include "trigtype.h"
#include "tube.h"
#include "unit.h"
#include "unittype.h"
#include "vanim.h"
#include "vanimtype.h"
#include "version.h"
#include "warhead.h"
#include "waypoint.h"
#include "weapon.h"

#include <cstdio>
#include <float.h>


namespace {
	int LastReportFrame = -1;
	int ReportsThisSession = 0;
	constexpr int SYNC_REPORT_MAX_AGE_DAYS = 30;
	constexpr int SYNC_REPORT_SESSION_CAP = 3;
}


void Sync_Report_Reset(void)
{
	LastReportFrame = -1;
	ReportsThisSession = 0;
}


bool Report_Out_Of_Sync(EventClass const & ev, unsigned const * crc_ring, unsigned ring_size)
{
	if (Frame == LastReportFrame || ReportsThisSession >= SYNC_REPORT_SESSION_CAP) {
		return(false);
	}
	LastReportFrame = Frame;
	ReportsThisSession++;
	Write_Sync_Report(&ev, crc_ring, ring_size);
	return(true);
}


void Write_Sync_Report(EventClass const * ev, unsigned const * crc_ring, unsigned ring_size)
{
	static char _NO_NAME [] = "None";

	int i,j;
	InfantryClass *infp;
	UnitClass *unitp;
	BuildingClass *bldgp;
	AircraftClass *airp;
	ObjectClass *objp;
	FILE *fp;
	HouseClass *housep;
	HousesType house;
	unsigned int GameCRC = 0;

	char filename[512];
	char const * debug_dir = Debug_Directory();
	if (debug_dir != NULL && debug_dir[0] != '\0') {
		SYSTEMTIME now;
		GetLocalTime(&now);
		Delete_Files_Older_Than(debug_dir, "SYNC_*.LOG", SYNC_REPORT_MAX_AGE_DAYS);
		sprintf(filename, "%s\SYNC_H%d_%02u-%02u-%04u_%02u-%02u-%02u_F%d.LOG",
			debug_dir, PlayerPtr->HeapID,
			now.wDay, now.wMonth, now.wYear, now.wHour, now.wMinute, now.wSecond, Frame);
	} else {
		sprintf(filename, "SYNC%01d.TXT", PlayerPtr->HeapID);
	}

	Mono_Clear_Screen();
	Mono_Set_Cursor (0,0);

	fp = fopen(filename,"wt");
	if (fp==NULL) {
		DWORD const error = GetLastError();
		DebugString("Failed to open the out-of-sync report %s. Error %d - %s\n", filename, error, Last_Error_Text(error));
		return;
	}
	DebugString("Writing out-of-sync report to %s\n", filename);

	fprintf(fp, "TS Sync\n");

	fprintf(fp, "\nVersion %s\n", Version_Name());
	fprintf(fp, "Internal Version %s\n", VerNum.Version_Name());

	fprintf(fp, "Release Build: %s - %s\r\n", OPENTS_BUILD_DESCRIPTION, OPENTS_COMMIT_DATE);

	fprintf(fp, "Local house: H%d %s\n", PlayerPtr->HeapID, PlayerPtr->IniName.c_str());
	int identity = Spawner_Session_Identity();
	if (identity != 0) {
		fprintf(fp, "Session identity: %08x\n", identity);
	} else {
		fprintf(fp, "Session identity: (lobby) scenario=%s seed=%08x players=%d\n",
			Scen->ScenarioName, Seed, Session.MaxPlayers);
	}
	fprintf(fp, "Seed: %08x\n", Seed);
	fprintf(fp, "Session type: %d\n", Session.Type);
	fprintf(fp, "FPU control word: %x\n", _controlfp(0, 0));

	int cpu_type = PROC_PENTIUM_PRO;
	bool mmx = false;
	char vendor[32];
	vendor[0] = '\0';
	Get_CPU_Type(cpu_type, mmx, vendor, sizeof(vendor) - 1);
	fprintf(fp, "CPU vendor: %s\r\n", vendor);

	fprintf(fp, "Frames: %d\n", Frame);

	fprintf(fp, "Average FPS: %d\n", SecondsPassed != 0 ? TotalFrames / SecondsPassed : 0);
	fprintf(fp, "Max MaxAhead: %d\n", Session.MaxMaxAhead);
	fprintf(fp, "Latency setting: %d\n", Session.LatencyFudge);
	fprintf(fp, "Game speed setting: %d\n", Options.GameSpeed);
	fprintf(fp, "FrameSendRate: %d\n", Session.FrameSendRate);

	for (i = 0; i < MAX_PLAYERS; i++) {
		MPStatsType *stat = &Session.ConnectionStats[i];
		if (*stat->Name != '\0') {
			fprintf(fp, "\nName: %s\n", stat->Name);
			fprintf(fp, "Max avg round trip: %d\n", stat->MaxAvgRoundTrip);
			fprintf(fp, "Max round trip: %d\n", stat->MaxRoundTrip);
			fprintf(fp, "Resends: %d\n", stat->Resends);
			fprintf(fp, "Frame sync stalls: %d\n", stat->FrameSyncStalls);
			fprintf(fp, "Command cound stalls: %d\n", stat->CommandCountStalls);
			fprintf(fp, "Lost: %d\n", stat->Lost);
			fprintf(fp, "Percent lost: %d\n", stat->PercentLost);
		}
	}

	fprintf(fp, "\n----- Frame checksum ring (newest first) -----\n");
	{
		int const mask = (int)ring_size - 1;
		int const filled = (Frame + 1 < (int)ring_size) ? (Frame + 1) : (int)ring_size;
		for (i = 0; i < filled; i++) {
			int const frame = Frame - i;
			fprintf(fp, "Frame %d slot %3d %08x\n", frame, frame & mask, crc_ring[frame & mask]);
		}
	}

	//
	// Houses
	//
	for (house = HOUSE_FIRST; house < Houses.Count(); house++) {
		GameCRC = 0;
		housep = Houses[house];
		fprintf(fp,"%s: IsHuman:%d  Color:%s  ID:%d  HouseType:%s\n",
			(const char *)housep->IniName,
			housep->IsHuman,
			ColorSchemes[housep->Class->Scheme]->Name,
			housep->HeapID,
			HouseTypes[housep->Class->HeapID]->Name());
		//Add_CRC (&GameCRC, (int)housep->Credits + (int)housep->Power +
		//	(int)housep->Drain);
		Mono_Printf("House %s:%x\n",housep->Class->Name(),GameCRC);
	}

	//
	// Infantry
	//
	for (house = HOUSE_FIRST; house < Houses.Count(); house++) {
		housep = Houses[house];
		GameCRC = 0;
		fprintf(fp,"-------------------- %s Infantry -------------------\n",
			housep->Class->Name());
		for (i = 0; i < Infantry.Count(); i++) {
			infp = (InfantryClass *)Infantry[i];
			if (infp->House==housep) {
				Add_CRC (&GameCRC, (int)infp->PositionCoord.As_Int() + (int)infp->PrimaryFacing.Current().As_Dir256());

				int tarcom_index = 0;
				const char *tarcom_name = _NO_NAME;
				AbstractClass *tarcom = infp->TarCom;
				if (tarcom != NULL) {
					if (tarcom->Is_Techno()) {
						tarcom_index = tarcom->Fetch_Heap_ID();
					} else if (tarcom->RTTI == RTTI_CELL) {
						CellClass *cptr = (CellClass *)tarcom;
						tarcom_index = cptr->CellID.X | cptr->CellID.Y << 16;
					}
					tarcom_name = Name_From_RTTI(tarcom->RTTI);
				}

				int navcom_index = 0;
				const char *navcom_name = _NO_NAME;
				AbstractClass *navcom = infp->NavCom;
				if (navcom != NULL) {
					if (navcom->Is_Techno()) {
						navcom_index = navcom->Fetch_Heap_ID();
					} else if (navcom->RTTI == RTTI_CELL) {
						CellClass *cptr = (CellClass *)navcom;
						navcom_index = cptr->CellID.X | cptr->CellID.Y << 16;
					}
					navcom_name = Name_From_RTTI(navcom->RTTI);
				}

				fprintf(fp,"COORD:%x   Facing:%d   Mission:%d   Type:%d(%s)   Tgt:%s(%d) Speed:%d NavCom:%s(%d)\n",
					infp->PositionCoord.As_Int(),(int)infp->PrimaryFacing.Current().As_Dir256(),infp->Get_Mission(),
					infp->Class->HeapID, infp->Class->Name(), tarcom_name, tarcom_index, int(infp->Speed * (MPH_LIGHT_SPEED + 1.0)), navcom_name, navcom_index);
			}
		}
		Mono_Printf("%s Infantry:%x\n",housep->Class->Name(),GameCRC);
	}

	//
	// Units
	//
	for (house = HOUSE_FIRST; house < Houses.Count(); house++) {
		housep = Houses[house];
		GameCRC = 0;
		fprintf(fp,"-------------------- %s Units -------------------\n",
			housep->Class->Name());
		for (i = 0; i < Units.Count(); i++) {
			unitp = (UnitClass *)Units[i];
			if (unitp->House==housep) {
				Add_CRC (&GameCRC, (int)unitp->PositionCoord.As_Int() + (int)unitp->PrimaryFacing.Current().As_Dir256() +
					(int)unitp->SecondaryFacing.Current().As_Dir256());

				int tarcom_index = 0;
				const char *tarcom_name = _NO_NAME;
				AbstractClass *tarcom = unitp->TarCom;
				if (tarcom != NULL) {
					if (tarcom->Is_Techno()) {
						tarcom_index = tarcom->Fetch_Heap_ID();
					} else if (tarcom->RTTI == RTTI_CELL) {
						CellClass *cptr = (CellClass *)tarcom;
						tarcom_index = cptr->CellID.X | cptr->CellID.Y << 16;
					}
					tarcom_name = Name_From_RTTI(tarcom->RTTI);
				}

				int navcom_index = 0;
				const char *navcom_name = _NO_NAME;
				AbstractClass *navcom = unitp->NavCom;
				if (navcom != NULL) {
					if (navcom->Is_Techno()) {
						navcom_index = navcom->Fetch_Heap_ID();
					} else if (navcom->RTTI == RTTI_CELL) {
						CellClass *cptr = (CellClass *)navcom;
						navcom_index = cptr->CellID.X | cptr->CellID.Y << 16;
					}
					navcom_name = Name_From_RTTI(navcom->RTTI);
				}
				fprintf(fp,"COORD:%x   Facing:%d   Facing2:%d   Mission:%d   Type:%d(%s)   Tgt:%s(%d)  NavCom:%s(%d)   TrkNum:%d TrkInd:%d SpdAc:%d\n",
					unitp->PositionCoord.As_Int(),(int)unitp->PrimaryFacing.Current().As_Dir256(),(int)unitp->SecondaryFacing.Current().As_Dir256(),unitp->Get_Mission(),
					unitp->Class->HeapID, unitp->Class->Name(), tarcom_name, tarcom_index, navcom_name, navcom_index,
					unitp->Locomotion->Get_Track_Number(), unitp->Locomotion->Get_Track_Index(), unitp->Locomotion->Get_Speed_Accum());
			}
		}
		Mono_Printf("%s Units:%x\n",housep->Class->Name(),GameCRC);
	}

	//
	// Buildings
	//
	for (house = HOUSE_FIRST; house < Houses.Count(); house++) {
		housep = Houses[house];
		GameCRC = 0;
		fprintf(fp,"-------------------- %s Buildings -------------------\n",
			housep->Class->Name());
		for (i = 0; i < Buildings.Count(); i++) {
			bldgp = (BuildingClass *)Buildings[i];
			if (bldgp->House==housep) {
				Add_CRC (&GameCRC, (int)bldgp->PositionCoord.As_Int() + (int)bldgp->PrimaryFacing.Current().As_Dir256());
				fprintf(fp,"COORD:%x   Facing:%d   Mission:%d   Type:%d(%s)   Tgt:%x\n",
					bldgp->PositionCoord.As_Int(),(int)bldgp->PrimaryFacing.Current().As_Dir256(),bldgp->Get_Mission(),
					bldgp->Class->HeapID, bldgp->Class->Name(), TargetClass(bldgp).Encode());
			}
		}
		Mono_Printf("%s Buildings:%x\n",housep->Class->Name(),GameCRC);
	}


	/// Aircraft
	for (house = HOUSE_FIRST; house < Houses.Count(); house++) {
		housep = Houses[house];
		GameCRC = 0;
		fprintf(fp,"-------------------- %s Aircraft -------------------\n",
			housep->Class->Name());
		for (i = 0; i < Aircraft.Count(); i++) {
			airp = (AircraftClass *)Aircraft[i];
			if (airp->House==housep) {
				Add_CRC (&GameCRC, (int)airp->PositionCoord.As_Int() + (int)airp->PrimaryFacing.Current().As_Dir256());
				fprintf(fp,"COORD:%x   Facing:%d   Mission:%d   Type:%d(%s)   Tgt:%x\n",
					airp->PositionCoord.As_Int(),(int)airp->PrimaryFacing.Current().As_Dir256(),airp->Get_Mission(),
					airp->Class->HeapID, airp->Class->Name(), TargetClass(airp).Encode());
			}
		}
		Mono_Printf("%s Buildings:%x\n",housep->Class->Name(),GameCRC);
	}

	#if 0
	//
	// Animations
	//
	AnimClass *animp;
		fprintf(fp,"-------------------- Animations -------------------\n");
	for (i = 0; i < Anims.Count(); i++) {
		animp = (AnimClass *)Anims[i];
		fprintf(fp,"Target:%x OwnerHouse:%d Loops:%d\n",
			animp->xObject,
			animp->OwnerHouse,
			animp->Loops);
	}
	#endif

	//------------------------------------------------------------------------
	// Map Layers
	//------------------------------------------------------------------------
	GameCRC = 0;
	for (i = 0; i < LAYER_COUNT; i++) {
		fprintf(fp,">>>> MAP LAYER %d <<<<\n",i);
		for (j = 0; j < Map.Layer[i].Count(); j++) {
			objp = Map.Layer[i][j];
			if (objp->RTTI != RTTI_ANIM || objp->Fetch_ID() != -2) {
				Add_CRC (&GameCRC, (int)objp->PositionCoord.As_Int() + (int)objp->RTTI);
				fprintf(fp,"Object %d: %x ",j,objp->PositionCoord.As_Int());
				if (objp->RTTI != RTTI_WAVE && objp->RTTI != RTTI_LIGHT) {
					fprintf(fp,"%s\t(Type:%d(%s)) ",
						Name_From_RTTI(objp->RTTI),
						((ObjectClass *)objp)->Class_Of()->Fetch_Heap_ID(),
						(const char *)((ObjectClass *)objp)->Class_Of()->IniName);
				} else {
					fprintf(fp,"%s\t(Type:%d) ",
						Name_From_RTTI(objp->RTTI),
						0);
				}
				housep = objp->Owner_HouseClass();
				if (housep!=NULL) {
					fprintf(fp,"Owner: %s\n",housep->Class->Name());
				}
				else {
					fprintf(fp,"Owner: NONE\n");
				}
			}
		}
	}
	Mono_Printf("Map Layers:%x  \n",GameCRC);

	//------------------------------------------------------------------------
	// Logic Layers
	//------------------------------------------------------------------------
	GameCRC = 0;
	fprintf(fp,">>>> LOGIC LAYER <<<<\n");
	for (i = 0; i < Logic.Count(); i++) {
		objp = Logic[i];
		if (objp->RTTI != RTTI_ANIM || objp->Fetch_ID() != -2) {
			Add_CRC (&GameCRC, (int)objp->PositionCoord.As_Int() + (int)objp->RTTI);
			fprintf(fp,"Object %d: %x ",i,objp->PositionCoord.As_Int());

			if (objp->RTTI == RTTI_AIRCRAFT)
				fprintf(fp,"Aircraft  (Type:%d(%s)) ",
					((AircraftClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((AircraftClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_ANIM)
				fprintf(fp,"Anim      (Type:%d(%s)) ",
					((AnimClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((AnimClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_BUILDING)
				fprintf(fp,"Building  (Type:%d(%s)) ",
					((BuildingClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((BuildingClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_BULLET)
				fprintf(fp,"Bullet    (Type:%d(%s)) ",
					((BulletClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((BulletClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_INFANTRY)
				fprintf(fp,"Infantry  (Type:%d(%s)) ",
					((InfantryClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((InfantryClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_OVERLAY)
				fprintf(fp,"Overlay   (Type:%d(%s)) ",
					((OverlayClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((OverlayClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_SMUDGE)
				fprintf(fp,"Smudge    (Type:%d(%s)) ",
					((SmudgeClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((SmudgeClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_TERRAIN)
				fprintf(fp,"Terrain   (Type:%d(%s)) ",
					((TerrainClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((TerrainClass *)objp)->Class_Of()->IniName);
			else if (objp->RTTI == RTTI_UNIT)
				fprintf(fp,"Unit      (Type:%d(%s)) ",
					((UnitClass *)objp)->Class_Of()->Fetch_Heap_ID(),
					(const char *)((UnitClass *)objp)->Class_Of()->IniName);

			housep = objp->Owner_HouseClass();
			if (housep!=NULL) {
				fprintf(fp,"Owner: %s\n",housep->Class->Name());
			}
			else {
				fprintf(fp,"Owner: NONE\n");
			}
		}
	}
	Mono_Printf("Logic:%x  \n",GameCRC);

	// Report the generator's table cursors rather than drawing a number, which would advance the
	// shared generator and diverge the machines further while the report is being written.
	fprintf(fp,"\nScenario generator: Index1=%d Index2=%d\n",
		Scen->RandomNumber.Index_1(), Scen->RandomNumber.Index_2());
	fprintf(fp,"My Frame:%d\n",Frame);

	if (ev) {
		int const delay = ev->Data.FrameInfo.Delay;
		int const checked = ev->Frame - delay;
		fprintf(fp,"Offending event:\n");
		fprintf(fp,"Type:         %d\n",ev->Type);
		fprintf(fp,"Frame:        %d\n",ev->Frame);
		fprintf(fp,"House ID:     %d\n",ev->ID);
		fprintf(fp,"Their CRC:    %x\n",ev->Data.FrameInfo.CRC);
		fprintf(fp,"CommandCount: %d\n",ev->Data.FrameInfo.CommandCount);
		fprintf(fp,"Delay:        %d\n",delay);
		fprintf(fp,"Checked frame:%d\n",checked);
		if (delay < (int)ring_size) {
			fprintf(fp,"Our CRC:      %x\n", crc_ring[checked & ((int)ring_size - 1)]);
		}
	} else {
		fprintf(fp,"Report written by the PrintCRC playback trap.\n");
	}

	SyncRecorder.Print_All(fp, Sync_Engine_Names());

	Print_Heap_CRCs(fp);

	fclose(fp);

}
