// ps3_autotest.cpp -- reVC-PS3 automatic test run (ported from re3-PS3).
//
// Put a file named autotest.txt in USRDIR (by FTP), load a save in a
// safehouse (no mission running) and leave the pad alone. The test then plays
// by itself and writes "[autotest]" lines to the log, one per step with its
// frame rate, slow frames, memory, VRAM, peds and cars:
//
//   1. weather and time of day: sunny / cloudy / rain / fog / extra sunny /
//      hurricane at 0, 6, 12 and 19 h, where the player is
//   2. a flight over the map at car speed (streaming, LODs, the bridges),
//      landing at every hospital, police station and garage the script made
//      and at a few fixed places, Vice Beach first, then the mainland
//   3. save + load once on each side (GTAVCsf9.b, not one of the menu slots)
//   4. vehicles: a car, a bike, a helicopter and a boat (where the flight
//      found water), full throttle a few seconds each
//   5. side missions: taxi, paramedic, firefighter, vigilante, pizza (started
//      with R3 like a player would, then cancelled), Pay 'n' Spray with a
//      wanted level, bomb shops, Sunshine Autos / import-export garages
//   6. all of it again "loops" times (autotest.txt: "loops=3"), memory and
//      VRAM at every step: a leak is a number that keeps growing loop after
//      loop
//
// When it ends the player is put back where he was and the file is renamed to
// autotest.done.txt, so the next boot doesn't run it again. The player can't
// die and has no wanted level during the run (except on purpose).
//
// Other modes (a line in autotest.txt):
//   "mode=full"  every mission script of main.scm (story, phone, side and odd
//                jobs, as the mission switcher starts them) "secs=N" seconds
//                each (30 by default, cutscenes skipped with X), the odd jobs
//                started the player's way, a rampage, and one of each
//                collectible / pickup: hidden package, clothes (the player
//                model swap), a property for sale, plus the garages and a
//                save + load. "from=N" / "to=N": a part of the missions only.
//                If the game dies in a mission, the next boot goes on after
//                it (autotest.progress.txt; "resume=0" turns that off).
//                It changes the game's progress and money: use a save you
//                don't mind, and don't save after.
//   "mode=leak"  the same few actions over and over, heap at each step
//   "mode=rain"  the rain effects one by one where the player stands
//   "mode=gpu"   smoke / rain / ZCULL compared where the player stands, no
//                60 fps cap
// Not automated: unique stunt jumps and store robberies (they need driving
// and aiming), RC missions' vans.

#include "common.h"
#include "crossplatform.h"

#if PS3_STAGE != 1	// the test needs the renderer (stage 2)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <stdarg.h>
#include <sys/systime.h>

#include "General.h"
#include "Pad.h"
#include "Timer.h"
#include "Clock.h"
#include "Weather.h"
#include "World.h"
#include "PlayerPed.h"
#include "PlayerInfo.h"
#include "Wanted.h"
#include "Vehicle.h"
#include "Automobile.h"
#include "Bike.h"
#include "Boat.h"
#include "Pools.h"
#include "Streaming.h"
#include "ModelIndices.h"
#include "ModelInfo.h"
#include "Garages.h"
#include "Restart.h"
#include "Zones.h"
#include "Script.h"
#include "CarCtrl.h"
#include "CutsceneMgr.h"
#include "Frontend.h"
#include "Game.h"
#include "GenericGameStorage.h"
#include "PCSave.h"
#include "AudioManager.h"
#include "AnimManager.h"
#include "AnimBlendAssociation.h"
#include "WeaponInfo.h"
#include "Camera.h"
#include "Pickups.h"
#include "FileMgr.h"
#include "Darkel.h"
#include "Particle.h"
#include "WaterLevel.h"
#include "main.h"

#include "ps3_platform.h"

namespace rw { namespace ps3 { void vramStats(uint32 *used, uint32 *total, uint32 *blocks); extern bool additiveKill;
	extern bool rsxTiling, zcullOn; extern int32 blendAlphaRef; void setFlipVsync(bool on); } }
extern int32 gPS3FxOff;	// Weather.cpp
extern "C" void PS3_LogMemTag(const char *tag);
extern const int32 gaCarsToCollectInCraigsGarages[TOTAL_COLLECTCARS_GARAGES][TOTAL_COLLECTCARS_CARS];	// Garages.cpp

#define AUTOTEST_FILE      PS3_USRDIR "/autotest.txt"
#define AUTOTEST_DONE_FILE PS3_USRDIR "/autotest.done.txt"
#define AUTOTEST_PROGRESS_FILE PS3_USRDIR "/autotest.progress.txt"	// full mode: the mission being run
#define AUTOTEST_SLOT      SLOT_COUNT	// GTAVCsf9.b: not one of the 8 menu slots

#define FLY_HEIGHT  60.0f	// above the sea: over most roofs (the tallest towers go through)
#define FLY_SPEED    1.0f	// metres per frame: 60 m/s, a fast car's top speed

enum StepType {
	ST_WEATHER,	// a = weather, b = hour
	ST_FLY,		// to (x, y): flight, then landing
	ST_SAVELOAD,	// save here, load it back, check the position
	ST_SIDEMISSION,	// a = vehicle model: get in, R3, wait, R3, get out
	ST_GARAGE,	// a = garage index: drive a car in, wait
	ST_DRIVE,	// a = vehicle model, b = 1 at the water spot: full throttle x seconds
	ST_LOOPEND,	// memory snapshot, next loop
	ST_MISSION,	// a = mission script index: started (mission switcher), x seconds
	ST_MISSIONEND,	// the running mission fails and cleans up
	ST_PICKUP,	// a = PK_*: the player on it, x seconds
};

enum { PK_PACKAGE, PK_RAMPAGE, PK_CLOTHES, PK_PROPERTY };

struct Step {
	int type;
	int a, b;
	float x, y;
	char name[48];
};

#define MAX_STEPS 320
static Step steps[MAX_STEPS];
static int numSteps;

static int state;		// 0 off, 1 waiting to start, 2 running, 3 done
static int loops = 1, loop;
static int rainMode;		// autotest.txt "mode=rain": only the rain test, where the player is
static int leakMode;		// "mode=leak": the same few actions over and over, heap logged at each
static int gpuMode;		// "mode=gpu": smoke / rain / ZCULL compared where the player is, no 60 fps cap
static int fullMode;		// "mode=full": every mission script, the odd jobs, one of each collectible
static int fromMission = 0, toMission = -1;	// "from=N" / "to=N": a part of the missions only
static float missionSecs = 30.0f;		// "secs=N"
static int resumeOff;		// "resume=0": don't skip past a mission a previous run died in
static int pulseCross;		// frames: cross pressed on and off (skips cutscenes)
static int counterBefore;
static int smokeOn;		// the step puts smoke in front of the camera
static uint64 smokeLastUs;
static double lastHeapMB;
static int cur;			// step being run
static int phase;		// inside the step
static uint64 phaseStartUs;
static uint64 stepStartUs, runStartUs;
static CVector startPos;
static float startHeading;
static int wantLoad;		// ps3.cpp asks for it between frames
static int waitingForLoad;
static CVector savedPos;
static int savedLevel;
static int failures;
static CVehicle *testCar;
static int32 testCarHandle;
static CVector flyFrom, flyTo;
static float flyDist, flyDone;
static bool haveWater;		// a flight landed on water: the boat goes there
static CVector waterSpot;
static float driveMaxZ, driveMaxSpeed;
static CVector driveFrom;
static int pressR3, pressCross;
static int stepNoted;		// a one-time note of the step was logged
static uint64 lastFrameUs;

// per step frame stats
static uint32 sFrames, sSlow, sVerySlow;
static uint64 sWorstUs, sSumUs;

static uint64 nowUs(void) { return sysGetSystemTime(); }
static float phaseSecs(void) { return (nowUs() - phaseStartUs) / 1000000.0f; }
static void nextPhase(void) { phase++; phaseStartUs = nowUs(); }

static const char *levelName(int l)
{
	switch (l) {
	case LEVEL_BEACH: return "Vice Beach";
	case LEVEL_MAINLAND: return "mainland";
	default: return "-";
	}
}

static int levelAt(float x, float y)
{
	CVector p(x, y, 20.0f);
	return CTheZones::GetLevelFromPosition(&p);
}

static void addStep(int type, int a, int b, float x, float y, const char *fmt, ...)
	__attribute__((format(printf, 6, 7)));
static void addStep(int type, int a, int b, float x, float y, const char *fmt, ...)
{
	if (numSteps >= MAX_STEPS)
		return;
	Step &s = steps[numSteps++];
	s.type = type; s.a = a; s.b = b; s.x = x; s.y = y;
	va_list va;
	va_start(va, fmt);
	vsnprintf(s.name, sizeof(s.name), fmt, va);
	va_end(va);
}

// ---- the player ---------------------------------------------------------------

static void keepPlayerSafe(void)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil)
		return;
	p->m_fHealth = 100.0f;
	p->bBulletProof = p->bFireProof = p->bExplosionProof = p->bCollisionProof = p->bMeleeProof = true;
	int32 &money = CWorld::Players[CWorld::PlayerInFocus].m_nMoney;
	money = Max(money, fullMode ? 1000000 : 20000);	// full: the properties cost up to $120,000
}

static void releasePlayer(void)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil)
		return;
	p->bBulletProof = p->bFireProof = p->bExplosionProof = p->bCollisionProof = p->bMeleeProof = false;
	p->bUsesCollision = true;
	p->bAffectedByGravity = true;
}

static int wantedLevel(void)
{
	CPlayerPed *p = FindPlayerPed();
	return p && p->m_pWanted ? p->m_pWanted->GetWantedLevel() : -1;
}

static void clearWanted(void)
{
	CPlayerPed *p = FindPlayerPed();
	if (p && p->m_pWanted && p->m_pWanted->GetWantedLevel() > 0)
		p->SetWantedLevel(0);
}

static void warpOutOfCar(CVector pos)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil)
		return;
	if (p->bInVehicle && p->m_pMyVehicle) {
		CVehicle *v = p->m_pMyVehicle;
		if (v->pDriver == p) {
			v->RemoveDriver();
			v->SetStatus(STATUS_ABANDONED);
			v->bEngineOn = false;
			v->AutoPilot.m_nCruiseSpeed = 0;
		} else
			v->RemovePassenger(p);
		p->bInVehicle = false;
		p->m_pMyVehicle = nil;
		p->SetPedState(PED_IDLE);
		p->bUsesCollision = true;
		p->SetMoveSpeed(0.0f, 0.0f, 0.0f);
		p->AddWeaponModel(CWeaponInfo::GetWeaponInfo(p->GetWeapon()->m_eWeaponType)->m_nModelId);
		p->RemoveInCarAnims();
		if (p->m_pVehicleAnim)
			p->m_pVehicleAnim->blendDelta = -1000.0f;
		p->m_pVehicleAnim = nil;
		p->SetMoveState(PEDMOVE_NONE);
		CAnimManager::BlendAnimation(p->GetClump(), p->m_animGroup, ANIM_STD_IDLE, 100.0f);
		p->RestartNonPartialAnims();
		AudioManager.PlayerJustLeftCar();
	}
	pos.z += p->GetDistanceFromCentreOfMassToBaseOfModel();
	p->Teleport(pos);
}

static void deleteTestCar(void)
{
	if (testCar == nil)
		return;
	// only if it still is the one we made (a load wipes the pool)
	if (CPools::GetVehiclePool()->GetAt(testCarHandle) == testCar) {
		CPlayerPed *p = FindPlayerPed();
		if (p && p->bInVehicle && p->m_pMyVehicle == testCar)
			warpOutOfCar(testCar->GetPosition() + CVector(3.0f, 0.0f, 1.0f));
		CWorld::Remove(testCar);
		CWorld::RemoveReferencesToDeletedObject(testCar);
		delete testCar;
	}
	testCar = nil;
}

// Ground under (x, y) after loading the place; false: water or nothing there
static bool groundAt(float x, float y, float *z)
{
	CStreaming::LoadScene(CVector(x, y, 20.0f));
	bool found = false;
	float gz = CWorld::FindGroundZFor3DCoord(x, y, 1000.0f, &found);
	if (!found || gz < 1.0f || gz > 500.0f)
		return false;
	*z = gz;
	return true;
}

// a car, bike or boat of the script's kind (CREATE_CAR does the same)
static CVehicle *makeVehicle(int model, CVector pos, float heading)
{
	CStreaming::RequestModel(model, STREAMFLAGS_DEPENDENCY);
	CStreaming::LoadAllRequestedModels(false);
	if (!CStreaming::HasModelLoaded(model))
		return nil;
	CVehicle *car;
	if (CModelInfo::IsBoatModel(model))
		car = new CBoat(model, MISSION_VEHICLE);
	else if (CModelInfo::IsBikeModel(model))
		car = new CBike(model, MISSION_VEHICLE);
	else
		car = new CAutomobile(model, MISSION_VEHICLE);
	pos.z += car->GetDistanceFromCentreOfMassToBaseOfModel();
	car->SetPosition(pos);
	car->SetHeading(heading);
	CTheScripts::ClearSpaceForMissionEntity(pos, car);
	car->SetStatus(STATUS_ABANDONED);
	car->bIsLocked = false;
	car->m_nDoorLock = CARLOCK_UNLOCKED;
	if (!car->IsBoat())
		CCarCtrl::JoinCarWithRoadSystem(car);
	car->AutoPilot.m_nCarMission = MISSION_NONE;
	car->AutoPilot.m_nTempAction = TEMPACT_NONE;
	car->bEngineOn = false;
	car->m_nZoneLevel = CTheZones::GetLevelFromPosition(&pos);
	car->bHasBeenOwnedByPlayer = true;
	CWorld::Add(car);
	testCarHandle = CPools::GetVehiclePool()->GetIndex(car);
	return car;
}

static void warpIntoCar(CVehicle *car)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil || car == nil)
		return;
	p->SetObjective(OBJECTIVE_ENTER_CAR_AS_DRIVER, car);
	p->WarpPedIntoCar(car);
}

// ---- logging ------------------------------------------------------------------

static void statsReset(void)
{
	sFrames = sSlow = sVerySlow = 0;
	sWorstUs = sSumUs = 0;
}

static void statsFrame(void)
{
	uint64 n = nowUs();
	if (lastFrameUs) {
		uint64 dt = n - lastFrameUs;
		sFrames++;
		sSumUs += dt;
		if (dt > sWorstUs) sWorstUs = dt;
		if (dt > 20000) sSlow++;
		if (dt > 50000) sVerySlow++;
	}
	lastFrameUs = n;
}

static void logStep(const char *result)
{
	uint32 used = 0, total = 0, blocks = 0;
	rw::ps3::vramStats(&used, &total, &blocks);
	CVector pos = FindPlayerCoors();
	float fps = sSumUs ? sFrames * 1000000.0f / sSumUs : 0.0f;
	struct mallinfo mi = mallinfo();
	double heapMB = mi.uordblks / 1048576.0;
	int pickups = 0;
	for (int i = 0; i < NUMPICKUPS; i++)
		if (CPickups::aPickUps[i].m_eType != PICKUP_NONE)
			pickups++;
	double delta = lastHeapMB > 0.0 ? heapMB - lastHeapMB : 0.0;
	lastHeapMB = heapMB;
	PS3_Logf("[autotest] %d/%d %-40s %s | %.1f s, %.1f fps, %u frames >20 ms, %u >50 ms, worst %.0f ms | "
	         "at (%.0f, %.0f, %.0f) %s | VRAM %u MB, streaming %.1f MB, heap in use %.2f MB (%+.2f), peds %d, cars %d, objects %d, pickups %d",
	         loop + 1, loops, steps[cur].name, result,
	         (nowUs() - stepStartUs) / 1000000.0f, fps, sSlow, sVerySlow, sWorstUs / 1000.0f,
	         pos.x, pos.y, pos.z, levelName(levelAt(pos.x, pos.y)),
	         used >> 20, CStreaming::ms_memoryUsed / 1048576.0f, heapMB, delta,
	         CPools::GetPedPool()->GetNoOfUsedSpaces(), CPools::GetVehiclePool()->GetNoOfUsedSpaces(),
	         CPools::GetObjectPool()->GetNoOfUsedSpaces(), pickups);
}

static void logScripts(const char *tag)
{
	char names[200] = "";
	int n = 0;
	for (CRunningScript *s = CTheScripts::pActiveScripts; s; s = s->GetNext()) {
		if (n++ < 16) {
			char nm[9];
			memcpy(nm, s->m_abScriptName, 8);
			nm[8] = '\0';
			strncat(names, " ", sizeof(names) - strlen(names) - 1);
			strncat(names, nm, sizeof(names) - strlen(names) - 1);
		}
	}
	PS3_Logf("[autotest]     %s: on a mission %d, wanted %d, money %d, scripts %d:%s", tag,
	         CTheScripts::IsPlayerOnAMission(), wantedLevel(),
	         CWorld::Players[CWorld::PlayerInFocus].m_nMoney, n, names);
}

// ---- the plan -----------------------------------------------------------------

struct Waypoint { float x, y; char name[40]; int level; };

static int cmpWaypoint(const void *a, const void *b)
{
	const Waypoint *wa = (const Waypoint*)a, *wb = (const Waypoint*)b;
	// Vice Beach -> mainland, and south to north on each
	static const int order[3] = { 9, 0, 1 };
	int la = order[(wa->level + 3) % 3], lb = order[(wb->level + 3) % 3];
	if (la != lb) return la - lb;
	return wa->y < wb->y ? -1 : wa->y > wb->y ? 1 : 0;
}

static void addGarageSteps(bool all)
{
	bool doneType[64] = { false };
	for (uint32 i = 0; i < CGarages::NumGarages; i++) {
		CGarage &g = CGarages::aGarages[i];
		const char *what = nil;
		switch (g.m_eGarageType) {
		case GARAGE_RESPRAY: what = "Pay 'n' Spray"; break;
		case GARAGE_BOMBSHOP1: case GARAGE_BOMBSHOP2: case GARAGE_BOMBSHOP3: what = "bomb shop"; break;
		case GARAGE_COLLECTCARS_1: case GARAGE_COLLECTCARS_2: case GARAGE_COLLECTCARS_3: case GARAGE_COLLECTCARS_4:
			what = "car list garage"; break;
		case GARAGE_COLLECTSPECIFICCARS: what = "specific cars garage"; break;
		case GARAGE_CRUSHER: what = "crusher"; break;
		}
		if (what == nil || (!all && doneType[g.m_eGarageType & 63]))
			continue;
		doneType[g.m_eGarageType & 63] = true;
		addStep(ST_GARAGE, i, 1, 0, 0, "garage %u: %s (%s)", i, what,
		        levelName(levelAt(g.GetGarageCenterX(), g.GetGarageCenterY())));
	}
}

static void buildPlan(void)
{
	static const int weathers[6] = { WEATHER_SUNNY, WEATHER_CLOUDY, WEATHER_RAINY, WEATHER_FOGGY, WEATHER_EXTRA_SUNNY, WEATHER_HURRICANE };
	static const char *weatherNames[6] = { "sunny", "cloudy", "rain", "fog", "extra sunny", "hurricane" };
	static const int hours[4] = { 0, 6, 12, 19 };

	numSteps = 0;

	if (leakMode) {
		// one kind of action at a time, several times in a row: the one whose
		// "heap in use" keeps climbing is what leaks. Streaming makes the
		// number move too, so it is the trend that counts, not one step
		CVector here = startPos;
		for (int i = 0; i < 6; i++)
			addStep(ST_SAVELOAD, 0, 0, 0, 0, "leak test: save + load %d", i + 1);
		for (int i = 0; i < 3; i++) {
			addStep(ST_FLY, 0, 0, -380.0f, -560.0f, "leak test: fly to Starfish Island %d", i + 1);
			addStep(ST_FLY, 0, 0, here.x, here.y, "leak test: fly back %d", i + 1);
		}
		for (int i = 0; i < 2; i++) {
			addStep(ST_FLY, 0, 0, -800.0f, 1100.0f, "leak test: fly to Downtown %d", i + 1);
			addStep(ST_FLY, 0, 0, here.x, here.y, "leak test: fly back %d", i + 1);
		}
		for (int i = 0; i < 3; i++)
			addStep(ST_SIDEMISSION, MI_TAXI, 0, 0, 0, "leak test: taxi mission %d", i + 1);
		for (int i = 0; i < 3; i++)
			addStep(ST_DRIVE, MI_INFERNUS, 0, 6.0f, 0, "leak test: drive a car %d", i + 1);
		for (uint32 g = 0; g < CGarages::NumGarages; g++)
			if (CGarages::aGarages[g].m_eGarageType == GARAGE_RESPRAY) {
				for (int i = 0; i < 4; i++)
					addStep(ST_GARAGE, g, 0, 0, 0, "leak test: car in and out of a garage %d", i + 1);
				break;
			}
		for (int i = 0; i < 3; i++)
			addStep(ST_SAVELOAD, 0, 0, 0, 0, "leak test: save + load again %d", i + 1);
		addStep(ST_LOOPEND, 0, 0, 0, 0, "end of loop");
		PS3_Logf("[autotest] plan: leak test, %d steps, %d loops", numSteps, loops);
		return;
	}

	if (fullMode) {
		// 1. every mission script of main.scm (story, phone and side missions,
		// odd jobs...), as the debug menu's mission switcher starts them:
		// x seconds each, cutscenes skipped with X. Each start fails the one
		// before (its death/arrest cleanup, as a real fail)
		int n = CTheScripts::NumberOfMissionScripts;
		int hi = toMission >= 0 && toMission < n - 1 ? toMission : n - 1;
		for (int i = fromMission; i <= hi && numSteps < MAX_STEPS - 24; i++)
			addStep(ST_MISSION, i, 0, missionSecs, 0, "mission %d of %d", i, n - 1);
		addStep(ST_MISSIONEND, 0, 0, 0, 0, "end the last mission");
		// 2. odd jobs started the player's way (vehicle + R3)
		addStep(ST_SIDEMISSION, MI_TAXI, 0, 30.0f, 0, "odd job: taxi");
		addStep(ST_SIDEMISSION, MI_AMBULAN, 0, 30.0f, 0, "odd job: paramedic");
		addStep(ST_SIDEMISSION, MI_FIRETRUCK, 0, 30.0f, 0, "odd job: firefighter");
		addStep(ST_SIDEMISSION, MI_POLICE, 0, 30.0f, 0, "odd job: vigilante");
		addStep(ST_SIDEMISSION, MI_PIZZABOY, 0, 30.0f, 0, "odd job: pizza boy");
		addStep(ST_PICKUP, PK_RAMPAGE, 0, 30.0f, 0, "rampage");
		// 3. one of each collectible / pickup the script made
		addStep(ST_PICKUP, PK_PACKAGE, 0, 8.0f, 0, "hidden package");
		addStep(ST_PICKUP, PK_CLOTHES, 0, 15.0f, 0, "clothes (player model swap)");
		addStep(ST_PICKUP, PK_PROPERTY, 0, 15.0f, 0, "property for sale");
		// 4. garages, vehicles, a save
		addGarageSteps(false);
		addStep(ST_DRIVE, MI_MAVERICK, 0, 10.0f, 0, "helicopter take-off");
		addStep(ST_SAVELOAD, 0, 0, 0, 0, "save + load at the end");
		addStep(ST_LOOPEND, 0, 0, 0, 0, "end of loop");
		PS3_Logf("[autotest] plan: full test, missions %d..%d of %d (%.0f s each), %d steps",
		         fromMission, hi, n - 1, missionSecs, numSteps);
		return;
	}

	if (gpuMode) {
		// what the RSX costs, where the player is (outdoors): the flips go on
		// hsync for the test, so the fps is what the RSX gives (above 60, with
		// tearing) and every change shows. Smoke: black car-fire smoke, a
		// steady cloud 10 m in front of the camera
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 8.0f, 0, "gpu test: sunny (baseline)");
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 10.0f, 256, "gpu test: sunny, ZCULL off");
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 12.0f, 128, "gpu test: sunny + smoke");
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 10.0f, 128 | 256, "gpu test: sunny + smoke, ZCULL off");
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 10.0f, 128 | 512, "gpu test: sunny + smoke, faint skipped");
		addStep(ST_WEATHER, WEATHER_RAINY, -1, 12.0f, 0, "gpu test: rain");
		addStep(ST_WEATHER, WEATHER_RAINY, -1, 12.0f, 128, "gpu test: rain + smoke");
		addStep(ST_WEATHER, WEATHER_HURRICANE, -1, 12.0f, 0, "gpu test: hurricane");
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 10.0f, 0, "gpu test: sunny again");
		addStep(ST_LOOPEND, 0, 0, 0, 0, "end of loop");
		rw::ps3::setFlipVsync(false);
		PS3_Logf("[autotest] plan: gpu test, %d steps, %d loops; tiling %s", numSteps, loops,
		         rw::ps3::rsxTiling ? "on" : "OFF (notiles.txt)");
		return;
	}

	if (rainMode) {
		// the rain on its own, standing where the player is (outdoors): each
		// rain effect off in turn; the one whose absence brings the fps back
		// is the culprit
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 10.0f, 0, "rain test: sunny (dry baseline)");
		addStep(ST_WEATHER, WEATHER_RAINY, -1, 15.0f, 0, "rain test: rain");
		addStep(ST_WEATHER, WEATHER_RAINY, -1, 10.0f, 1, "rain test: rain, no rain streaks");
		addStep(ST_WEATHER, WEATHER_RAINY, -1, 10.0f, 2, "rain test: rain, no drops on the screen");
		addStep(ST_WEATHER, WEATHER_RAINY, -1, 10.0f, 4, "rain test: rain, no splashes");
		addStep(ST_WEATHER, WEATHER_HURRICANE, -1, 15.0f, 0, "rain test: hurricane");
		addStep(ST_WEATHER, WEATHER_SUNNY, -1, 10.0f, 0, "rain test: sunny again");
		addStep(ST_LOOPEND, 0, 0, 0, 0, "end of loop");
		PS3_Logf("[autotest] plan: rain test, %d steps, %d loops", numSteps, loops);
		return;
	}

	// 1. weather and time of day, where the player is
	for (int w = 0; w < 6; w++)
		for (int h = 0; h < 4; h++)
			addStep(ST_WEATHER, weathers[w], hours[h], 0, 0, "weather %s %02d:00", weatherNames[w], hours[h]);

	// 2. the map: every restart point and garage the script made, plus a few
	// fixed places (rough coordinates: a landing on water just goes on)
	static Waypoint wp[160];
	int n = 0;
	for (int i = 0; i < CRestart::NumberOfHospitalRestarts && n < 140; i++) {
		CVector &v = CRestart::HospitalRestartPoints[i];
		wp[n].x = v.x; wp[n].y = v.y; snprintf(wp[n].name, sizeof(wp[n].name), "hospital %d", i); n++;
	}
	for (int i = 0; i < CRestart::NumberOfPoliceRestarts && n < 140; i++) {
		CVector &v = CRestart::PoliceRestartPoints[i];
		wp[n].x = v.x; wp[n].y = v.y; snprintf(wp[n].name, sizeof(wp[n].name), "police station %d", i); n++;
	}
	for (uint32 i = 0; i < CGarages::NumGarages && n < 140; i++) {
		CGarage &g = CGarages::aGarages[i];
		if (!g.IsUsed())
			continue;
		wp[n].x = g.GetGarageCenterX() + 12.0f;	// in front of it, not inside
		wp[n].y = g.GetGarageCenterY();
		snprintf(wp[n].name, sizeof(wp[n].name), "garage %u (type %d)", i, g.m_eGarageType);
		n++;
	}
	static const struct { float x, y; const char *name; } fixed[] = {
		{ 230.0f, -1270.0f, "Ocean Beach, Ocean View" },
		{ 400.0f, -450.0f, "Washington Beach" },
		{ 490.0f, -80.0f, "Vice Point, Malibu" },
		{ 420.0f, 1100.0f, "Vice Point, North Point Mall" },
		{ -380.0f, -560.0f, "Starfish Island" },
		{ 30.0f, 950.0f, "Prawn Island" },
		{ -870.0f, -580.0f, "Little Havana" },
		{ -1000.0f, 50.0f, "Little Haiti" },
		{ -800.0f, 1100.0f, "Downtown" },
		{ -1400.0f, -900.0f, "Escobar airport" },
		{ -700.0f, -1300.0f, "Viceport" },
	};
	for (int i = 0; i < ARRAY_SIZE(fixed); i++) {
		wp[n].x = fixed[i].x; wp[n].y = fixed[i].y;
		snprintf(wp[n].name, sizeof(wp[n].name), "%s", fixed[i].name);
		n++;
	}
	for (int i = 0; i < n; i++)
		wp[i].level = levelAt(wp[i].x, wp[i].y);
	qsort(wp, n, sizeof(wp[0]), cmpWaypoint);

	int lastLevel = -1;
	bool savedBeach = false, savedMainland = false;
	for (int i = 0; i < n; i++) {
		addStep(ST_FLY, 0, 0, wp[i].x, wp[i].y, "fly to %s (%s)", wp[i].name, levelName(wp[i].level));
		// 3. save and load once on each side
		if (wp[i].level == LEVEL_BEACH && !savedBeach && lastLevel == LEVEL_BEACH) {
			addStep(ST_SAVELOAD, 0, 0, 0, 0, "save + load on Vice Beach");
			savedBeach = true;
		}
		if (wp[i].level == LEVEL_MAINLAND && !savedMainland && lastLevel == LEVEL_MAINLAND) {
			addStep(ST_SAVELOAD, 0, 0, 0, 0, "save + load on the mainland");
			savedMainland = true;
		}
		lastLevel = wp[i].level;
	}

	// 4. vehicles: where the flight ended (the airport area), then the boat
	addStep(ST_DRIVE, MI_MAVERICK, 0, 10.0f, 0, "helicopter take-off");
	addStep(ST_DRIVE, MI_INFERNUS, 0, 8.0f, 0, "car, full throttle");
	addStep(ST_DRIVE, MI_PCJ600, 0, 8.0f, 0, "bike, full throttle");
	addStep(ST_DRIVE, MI_SPEEDER, 1, 10.0f, 0, "boat, full throttle (where it found water)");

	// back to the start: 5. side missions and garages
	addStep(ST_FLY, 0, 0, startPos.x, startPos.y, "fly back to the start");
	addStep(ST_SIDEMISSION, MI_TAXI, 0, 0, 0, "side mission: taxi");
	addStep(ST_SIDEMISSION, MI_AMBULAN, 0, 0, 0, "side mission: paramedic");
	addStep(ST_SIDEMISSION, MI_FIRETRUCK, 0, 0, 0, "side mission: firefighter");
	addStep(ST_SIDEMISSION, MI_POLICE, 0, 0, 0, "side mission: vigilante");
	addStep(ST_SIDEMISSION, MI_PIZZABOY, 0, 0, 0, "side mission: pizza boy");
	addGarageSteps(true);
	addStep(ST_LOOPEND, 0, 0, 0, 0, "end of loop");
	PS3_Logf("[autotest] plan: %d steps per loop, %d loops, %d waypoints", numSteps, loops, n);
}

// ---- the steps ------------------------------------------------------------------

static void startStep(void)
{
	phase = 0;
	stepNoted = 0;
	phaseStartUs = stepStartUs = nowUs();
	statsReset();
	Step &s = steps[cur];
	PS3_Logf("[autotest] %d/%d step %d/%d: %s", loop + 1, loops, cur + 1, numSteps, s.name);
	PS3_CRUMB("autotest step (loop*1000 + step)", loop * 1000 + cur + 1);
}

static void finishStep(const char *result)
{
	logStep(result);
	if (strncmp(result, "FAIL", 4) == 0)
		failures++;
	cur++;
	if (cur < numSteps)
		startStep();
}

// a step that waits: true when the time is up
static bool waited(float secs) { return phaseSecs() >= secs; }

static void runWeather(Step &s)
{
	if (phase == 0) {
		CWeather::ForceWeatherNow(s.a);
		if (s.b >= 0)
			CClock::SetGameClock(s.b, 0);
		gPS3FxOff = (int)s.y & 31;
		rw::ps3::additiveKill = ((int)s.y & 64) == 0;
		smokeOn = ((int)s.y & 128) != 0;
		rw::ps3::zcullOn = ((int)s.y & 256) == 0;
		rw::ps3::blendAlphaRef = ((int)s.y & 512) ? 8 : 0;
		smokeLastUs = nowUs();
		nextPhase();
	} else if (waited(s.x > 0.0f ? s.x : 4.0f)) {
		smokeOn = 0;
		finishStep("ok");
	} else if (smokeOn) {
		// ~150 puffs a second whatever the frame rate
		uint64 n = nowUs();
		int count = (int)((n - smokeLastUs) / 6666);
		if (count > 0) {
			smokeLastUs += (uint64)count * 6666;
			if (count > 20) count = 20;
			CVector fwd = TheCamera.GetForward();
			fwd.z = 0.0f;
			fwd.Normalise();
			CVector base = TheCamera.GetPosition() + fwd * 10.0f;
			for (int i = 0; i < count; i++) {
				CVector p = base + CVector(CGeneral::GetRandomNumberInRange(-3.0f, 3.0f),
				                           CGeneral::GetRandomNumberInRange(-3.0f, 3.0f),
				                           CGeneral::GetRandomNumberInRange(-2.0f, 1.0f));
				CParticle::AddParticle(PARTICLE_CARFLAME_SMOKE, p, CVector(0.0f, 0.0f, 0.02f));
			}
		}
	}
}

static void runFly(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	switch (phase) {
	case 0:
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		flyFrom = p->GetPosition();
		flyFrom.z = Max(flyFrom.z, FLY_HEIGHT);
		flyTo = CVector(s.x, s.y, FLY_HEIGHT);
		flyDist = (flyTo - flyFrom).Magnitude2D();
		flyDone = 0.0f;
		p->bUsesCollision = false;
		p->bAffectedByGravity = false;
		nextPhase();
		break;
	case 1: {
		// the flight: streaming has to keep up by itself (no LoadScene)
		flyDone += FLY_SPEED;
		float t = flyDist > 0.0f ? Min(flyDone / flyDist, 1.0f) : 1.0f;
		CVector pos = flyFrom + (flyTo - flyFrom) * t;
		pos.z = FLY_HEIGHT;
		p->SetMoveSpeed(0.0f, 0.0f, 0.0f);
		p->Teleport(pos);
		if (t >= 1.0f)
			nextPhase();
		break;
	}
	case 2: {
		// landing
		p->bUsesCollision = true;
		p->bAffectedByGravity = true;
		float gz;
		if (groundAt(s.x, s.y, &gz)) {
			warpOutOfCar(CVector(s.x, s.y, gz + 0.5f));
			nextPhase();
		} else {
			// water: stay in the air a moment, then go on (the boat goes here)
			float wl;
			if (!haveWater && CWaterLevel::GetWaterLevel(s.x, s.y, FLY_HEIGHT, &wl, true)) {
				haveWater = true;
				waterSpot = CVector(s.x, s.y, wl);
			}
			p->bUsesCollision = false;
			p->bAffectedByGravity = false;
			phase = 4;
			phaseStartUs = nowUs();
		}
		break;
	}
	case 3:
		if (waited(3.0f))
			finishStep("ok");
		break;
	case 4:
		if (waited(1.0f))
			finishStep("ok (no ground there: water)");
		break;
	}
}

static void runSaveLoad(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	switch (phase) {
	case 0:
		if (p == nil) { finishStep("FAIL no player"); return; }
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		savedPos = p->GetPosition();
		savedLevel = CGame::currLevel;
		nextPhase();
		break;
	case 1:
		if (!waited(1.0f))
			break;
		// VC's SaveSlot returns 0 on success and on some failures: the error code says
		PcSaveHelper.SaveSlot(AUTOTEST_SLOT);
		if (PcSaveHelper.nErrorCode != SAVESTATUS_SUCCESSFUL) {
			PS3_Logf("[autotest]     save failed, error %d", PcSaveHelper.nErrorCode);
			finishStep("FAIL save");
			return;
		}
		PS3_Logf("[autotest]     saved at (%.1f, %.1f, %.1f), level %d", savedPos.x, savedPos.y, savedPos.z, savedLevel);
		nextPhase();
		break;
	case 2:
		if (!waited(1.0f))
			break;
		wantLoad = 1;	// ps3.cpp does it between two frames
		waitingForLoad = 1;
		nextPhase();
		break;
	case 3:
		if (waitingForLoad)
			break;
		// loaded: give the game a moment to fade in
		if (!waited(4.0f))
			break;
		p = FindPlayerPed();
		if (p == nil) { finishStep("FAIL no player after the load"); return; }
		{
			float d = (p->GetPosition() - savedPos).Magnitude();
			PS3_Logf("[autotest]     loaded: player at (%.1f, %.1f, %.1f), %.1f m from where it was saved, level %d",
			         p->GetPosition().x, p->GetPosition().y, p->GetPosition().z, d, (int)CGame::currLevel);
			finishStep(d < 10.0f ? "ok" : "FAIL position differs");
		}
		break;
	}
}

static void runSideMission(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		CVector pos = p->GetPosition() + CVector(4.0f, 0.0f, 0.0f);
		float gz;
		if (groundAt(pos.x, pos.y, &gz))
			pos.z = gz;
		testCar = makeVehicle(s.a, pos, 0.0f);
		if (testCar == nil) { finishStep("FAIL no vehicle"); return; }
		warpIntoCar(testCar);
		nextPhase();
		break;
	}
	case 1:
		if (waited(2.0f)) {
			logScripts("before R3");
			pressR3 = 6;	// frames
			nextPhase();
		}
		break;
	case 2:
		if (waited(s.x > 0.0f ? s.x : 20.0f)) {
			logScripts("after R3");
			pressR3 = 6;	// cancel it, as a player would
			nextPhase();
		}
		break;
	case 3:
		if (waited(5.0f)) {
			logScripts("after the second R3");
			deleteTestCar();
			finishStep("ok (see the scripts lines)");
		}
		break;
	}
}

static void runGarage(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	CGarage &g = CGarages::aGarages[s.a];
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		float cx = g.GetGarageCenterX(), cy = g.GetGarageCenterY();
		float gz;
		if (!groundAt(cx, cy, &gz))
			gz = g.m_fInfZ;
		int model = MI_ADMIRAL;
		if (s.b == 1 && g.m_eGarageType >= GARAGE_COLLECTCARS_1 &&
		    (g.m_eGarageType <= GARAGE_COLLECTCARS_3 || g.m_eGarageType == GARAGE_COLLECTCARS_4)) {
			// a car of its list it doesn't have yet
			int ct = CGarages::GetCarsCollectedIndexForGarageType(g.m_eGarageType);
			for (int i = 0; i < TOTAL_COLLECTCARS_CARS; i++)
				if (!(CGarages::CarTypesCollected[ct] & BIT(i))) {
					model = gaCarsToCollectInCraigsGarages[ct][i];
					break;
				}
			PS3_Logf("[autotest]     car list %d: model %d, collected so far 0x%x", ct, model, CGarages::CarTypesCollected[ct]);
		}
		testCar = makeVehicle(model, CVector(cx, cy, gz), 0.0f);
		if (testCar == nil) { finishStep("FAIL no car"); return; }
		warpIntoCar(testCar);
		if (g.m_eGarageType == GARAGE_RESPRAY)
			p->SetWantedLevel(2);
		PS3_Logf("[autotest]     garage type %d state %d, car inside at (%.0f, %.0f, %.0f)",
		         g.m_eGarageType, g.m_eGarageState, cx, cy, gz);
		nextPhase();
		break;
	}
	case 1:
		if (waited(12.0f)) {
			PS3_Logf("[autotest]     after 12 s: garage state %d, wanted %d, respray %d, money %d",
			         g.m_eGarageState, wantedLevel(), g.m_bResprayHappened,
			         CWorld::Players[CWorld::PlayerInFocus].m_nMoney);
			if (g.m_eGarageType >= GARAGE_COLLECTCARS_1 &&
			    (g.m_eGarageType <= GARAGE_COLLECTCARS_3 || g.m_eGarageType == GARAGE_COLLECTCARS_4))
				PS3_Logf("[autotest]     car list collected now 0x%x",
				         CGarages::CarTypesCollected[CGarages::GetCarsCollectedIndexForGarageType(g.m_eGarageType)]);
			deleteTestCar();
			clearWanted();
			finishStep("ok");
		}
		break;
	}
}

// a vehicle with full throttle (Cross: accelerate; helicopters: up)
static void runDrive(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		CVector pos;
		if (s.b == 1) {
			if (!haveWater) { finishStep("skipped: the flight found no water"); return; }
			pos = waterSpot;
			CStreaming::LoadScene(pos);
		} else {
			pos = p->GetPosition() + CVector(6.0f, 0.0f, 0.0f);
			float gz;
			if (!groundAt(pos.x, pos.y, &gz)) { finishStep("skipped: no ground here"); return; }
			pos.z = gz + 0.5f;
		}
		testCar = makeVehicle(s.a, pos, p->m_fRotationCur);
		if (testCar == nil) { finishStep("FAIL no vehicle"); return; }
		warpIntoCar(testCar);
		driveFrom = testCar->GetPosition();
		driveMaxZ = driveFrom.z;
		driveMaxSpeed = 0.0f;
		nextPhase();
		break;
	}
	case 1:
		if (CPools::GetVehiclePool()->GetAt(testCarHandle) != testCar) { testCar = nil; finishStep("FAIL the vehicle is gone"); return; }
		if (phaseSecs() > 1.0f)
			pressCross = 1;
		driveMaxZ = Max(driveMaxZ, testCar->GetPosition().z);
		driveMaxSpeed = Max(driveMaxSpeed, testCar->GetMoveSpeed().Magnitude() * 50.0f);
		if (waited(s.x > 0.0f ? s.x : 8.0f)) {
			pressCross = 0;
			PS3_Logf("[autotest]     model %d: %.0f m from the start, %.1f m up at most, top speed %.1f m/s, health %.0f",
			         s.a, (testCar->GetPosition() - driveFrom).Magnitude(), driveMaxZ - driveFrom.z,
			         driveMaxSpeed, testCar->m_fHealth);
			deleteTestCar();
			finishStep("ok");
		}
		break;
	}
}

static const char *missionName(char *buf)
{
	for (CRunningScript *sc = CTheScripts::pActiveScripts; sc; sc = sc->GetNext())
		if (sc->m_bIsMissionScript) {
			memcpy(buf, sc->m_abScriptName, 8);
			buf[8] = '\0';
			return buf;
		}
	strcpy(buf, "(none)");
	return buf;
}

static void runMission(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	char name[16];
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition() + CVector(0.0f, 0.0f, 1.0f));
		// the odd jobs start with the player in their vehicle (their main
		// thread launches them so): their name is the first command
		char mname[9] = "";
		{
			uint8 head[16];
			CFileMgr::ChangeDir("\\");
			int fh = CFileMgr::OpenFile("data\\main.scm", "rb");
			if (fh) {
				CFileMgr::Seek(fh, CTheScripts::MultiScriptArray[s.a], 0);
				if (CFileMgr::Read(fh, (char*)head, 16) == 16 && head[0] == 0xA4 && head[1] == 0x03) {
					memcpy(mname, head + 2, 8);
					mname[8] = '\0';
				}
				CFileMgr::CloseFile(fh);
			}
		}
		// INITIAL is the new game's setup: on a loaded save it makes every
		// pickup, package and property again (the 320 pickup slots filled up
		// and a later mission's CREATE_PICKUP got none)
		if (strcasecmp(mname, "INITIAL") == 0) {
			finishStep("skipped: INITIAL sets up a new game (all the pickups again)");
			return;
		}
		// names as in VC's main.scm (the ones not found just start on foot)
		static const struct { const char *name; int model; } jobs[] = {
			{ "taxi", MI_TAXI }, { "copcar", MI_POLICE }, { "ambulae", MI_AMBULAN }, { "ambulan", MI_AMBULAN },
			{ "firetru", MI_FIRETRUCK }, { "pizza", MI_PIZZABOY },
		};
		int model = -1;
		for (int i = 0; i < ARRAY_SIZE(jobs); i++)
			if (strcasecmp(mname, jobs[i].name) == 0)
				model = jobs[i].model;
		if (model >= 0) {
			CVector pos = p->GetPosition() + CVector(4.0f, 0.0f, 0.0f);
			float gz;
			if (groundAt(pos.x, pos.y, &gz))
				pos.z = gz;
			testCar = makeVehicle(model, pos, 0.0f);
			if (testCar)
				warpIntoCar(testCar);
		}
		PS3_Logf("[autotest]     mission %d is \"%s\"%s", s.a, mname, model >= 0 ? ", started in its vehicle" : "");
		{
			// if the game dies in this mission, the next boot goes on after it
			FILE *pf = fopen(AUTOTEST_PROGRESS_FILE, "wb");
			if (pf) {
				fprintf(pf, "%d %s\n", s.a, mname);
				fclose(pf);
			}
		}
		CTheScripts::SwitchToMission(s.a);
		testCar = nil;	// the mission's now (its cleanup may delete it)
		nextPhase();
		break;
	}
	case 1:
		if (CCutsceneMgr::IsRunning() && phaseSecs() > 2.0f && pulseCross == 0)
			pulseCross = 20;
		if (!stepNoted && phaseSecs() > 3.0f && !CTheScripts::IsPlayerOnAMission() && (stepNoted = 1))
			PS3_Logf("[autotest]     not on a mission any more after 3 s (needs a vehicle or a place?)");
		if (waited(s.x)) {
			if (CCutsceneMgr::IsRunning())
				nextPhase();	// let it end first
			else
				phase = 3;
		}
		break;
	case 2:
		if (CCutsceneMgr::IsRunning() && pulseCross == 0)
			pulseCross = 20;
		if (!CCutsceneMgr::IsRunning() || waited(20.0f))
			phase = 3;
		break;
	case 3:
		PS3_Logf("[autotest]     script %s, on a mission %d, cutscene %d, wanted %d, player at (%.0f, %.0f, %.0f) %s",
		         missionName(name), CTheScripts::IsPlayerOnAMission(), CCutsceneMgr::IsRunning(), wantedLevel(),
		         p->GetPosition().x, p->GetPosition().y, p->GetPosition().z, p->bInVehicle ? "in a vehicle" : "on foot");
		finishStep(CCutsceneMgr::IsRunning() ? "ok (a cutscene still running)" : "ok");
		break;
	}
}

static void runMissionEnd(Step &s)
{
	(void)s;
	switch (phase) {
	case 0:
		if (CCutsceneMgr::IsRunning()) {
			if (pulseCross == 0) pulseCross = 20;
			if (!waited(20.0f)) break;
		}
		CTheScripts::EndMissionScripts();
		remove(AUTOTEST_PROGRESS_FILE);
		nextPhase();
		break;
	case 1:
		if (waited(5.0f)) {
			logScripts("after the end");
			finishStep(CTheScripts::IsPlayerOnAMission() ? "FAIL still on a mission" : "ok");
		}
		break;
	}
}

static bool pickupMatches(CPickup &pk, int kind)
{
	switch (kind) {
	case PK_PACKAGE: return pk.m_eType == PICKUP_COLLECTABLE1;
	case PK_RAMPAGE: return pk.m_eModelIndex == MI_PICKUP_KILLFRENZY;
	case PK_CLOTHES: return pk.m_eModelIndex == MI_PICKUP_CLOTHES;
	case PK_PROPERTY: return pk.m_eType == PICKUP_PROPERTY_FORSALE;
	}
	return false;
}

static void runPickup(Step &s)
{
	static const char *kinds[] = { "hidden package", "rampage", "clothes", "property for sale" };
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	switch (phase) {
	case 0: {
		deleteTestCar();
		int found = -1;
		for (int i = 0; i < NUMPICKUPS && found < 0; i++) {
			CPickup &pk = CPickups::aPickUps[i];
			if (pk.m_eType == PICKUP_NONE || pk.m_bRemoved)
				continue;
			if (pickupMatches(pk, s.a))
				found = i;
		}
		if (found < 0) {
			static char msg[64];
			snprintf(msg, sizeof(msg), "skipped: no %s pickup", kinds[s.a]);
			finishStep(msg);
			return;
		}
		CVector pos = CPickups::aPickUps[found].m_vecPos;
		counterBefore = s.a == PK_PACKAGE ? CWorld::Players[CWorld::PlayerInFocus].m_nCollectedPackages
		                                  : CWorld::Players[CWorld::PlayerInFocus].m_nMoney;
		PS3_Logf("[autotest]     %s pickup %d at (%.0f, %.0f, %.0f), player model %d",
		         kinds[s.a], found, pos.x, pos.y, pos.z, p->GetModelIndex());
		CStreaming::LoadScene(pos);
		warpOutOfCar(pos);
		nextPhase();
		break;
	}
	case 1:
		if (waited(s.x)) {
			p = FindPlayerPed();
			if (p == nil) { finishStep("FAIL no player"); return; }
			switch (s.a) {
			case PK_PACKAGE: {
				int now = CWorld::Players[CWorld::PlayerInFocus].m_nCollectedPackages;
				PS3_Logf("[autotest]     hidden packages %d -> %d", counterBefore, now);
				finishStep(now > counterBefore ? "ok" : "FAIL not collected");
				break;
			}
			case PK_RAMPAGE: {
				bool on = CDarkel::FrenzyOnGoing();
				PS3_Logf("[autotest]     rampage going on %d, status %d", on, CDarkel::ReadStatus());
				if (on)
					CDarkel::ResetOnPlayerDeath();	// over, as if the player had died
				finishStep(on ? "ok" : "FAIL no rampage started");
				break;
			}
			default:
				PS3_Logf("[autotest]     player model %d, money %d -> %d, on a mission %d",
				         p->GetModelIndex(), counterBefore, CWorld::Players[CWorld::PlayerInFocus].m_nMoney,
				         CTheScripts::IsPlayerOnAMission());
				logScripts("after the pickup");
				finishStep("ok (see the lines above)");
				break;
			}
		}
		break;
	}
}

static void runLoopEnd(void)
{
	PS3_Logf("[autotest] ---- loop %d of %d done in %.1f min, %d failures so far ----",
	         loop + 1, loops, (nowUs() - runStartUs) / 60000000.0f, failures);
	PS3_LogMemTag("[autotest]    ");
	cur++;
}

// ---- start / end ----------------------------------------------------------------

static void finishRun(void)
{
	deleteTestCar();
	gPS3FxOff = 0;
	rw::ps3::additiveKill = true;
	rw::ps3::zcullOn = true;
	rw::ps3::blendAlphaRef = 0;
	rw::ps3::setFlipVsync(true);
	smokeOn = 0;
	pressCross = 0;
	CWeather::ReleaseWeather();
	releasePlayer();
	clearWanted();
	CPlayerPed *p = FindPlayerPed();
	if (p) {
		float gz;
		if (groundAt(startPos.x, startPos.y, &gz))
			startPos.z = gz + 0.5f;
		warpOutOfCar(startPos);
		p->m_fRotationCur = p->m_fRotationDest = startHeading;
	}
	PS3_Logf("[autotest] ==== DONE: %d loops in %.1f min, %d failures ====",
	         loops, (nowUs() - runStartUs) / 60000000.0f, failures);
	PS3_LogMemTag("[autotest]    ");
	rename(AUTOTEST_FILE, AUTOTEST_DONE_FILE);
	remove(AUTOTEST_PROGRESS_FILE);
	state = 3;
}

static bool readConfig(void)
{
	FILE *f = fopen(AUTOTEST_FILE, "rb");
	if (f == nil)
		return false;
	char line[128];
	loops = 1;
	while (fgets(line, sizeof(line), f)) {
		int v;
		if (sscanf(line, "loops=%d", &v) == 1 && v >= 1 && v <= 50)
			loops = v;
		if (strncmp(line, "mode=rain", 9) == 0)
			rainMode = 1;
		if (strncmp(line, "mode=leak", 9) == 0)
			leakMode = 1;
		if (strncmp(line, "mode=gpu", 8) == 0)
			gpuMode = 1;
		if (strncmp(line, "mode=full", 9) == 0)
			fullMode = 1;
		if (sscanf(line, "from=%d", &v) == 1 && v >= 0)
			fromMission = v;
		if (sscanf(line, "to=%d", &v) == 1 && v >= 0)
			toMission = v;
		if (sscanf(line, "secs=%d", &v) == 1 && v >= 5 && v <= 600)
			missionSecs = (float)v;
		if (strncmp(line, "resume=0", 8) == 0)
			resumeOff = 1;
	}
	fclose(f);
	if (fullMode && !resumeOff) {
		FILE *pf = fopen(AUTOTEST_PROGRESS_FILE, "rb");
		if (pf) {
			int died = -1;
			char nm[16] = "";
			if (fscanf(pf, "%d %15s", &died, nm) >= 1 && died >= fromMission) {
				PS3_Logf("[autotest] the previous run died in mission %d (\"%s\"): going on from mission %d", died, nm, died + 1);
				fromMission = died + 1;
			}
			fclose(pf);
		}
	}
	return true;
}

static bool canStart(void)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil || FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::IsRunning())
		return false;
	if (CTimer::GetTimeInMilliseconds() < 8000)
		return false;
	return true;
}

// ---- hooks ------------------------------------------------------------------------

static void PS3_AutotestFrameStep(void);

// From CPad::UpdatePads (start of CGame::Process): runs the plan and puts the
// buttons it wants into the pad, before the scripts read it.
void
PS3_AutotestFrame(void)
{
	// a save calls CPad::FixPadsAfterSave -> UpdatePads -> here again: the
	// save step ran inside its own save (nested saves, "error 1")
	static int inside;
	if (inside)
		return;
	inside = 1;
	PS3_AutotestFrameStep();
	inside = 0;
}

static void
PS3_AutotestFrameStep(void)
{
	if (state == 0) {
		state = readConfig() ? 1 : 3;
		if (state == 1)
			PS3_Logf("[autotest] %s found: the test starts once a game is running", AUTOTEST_FILE);
	}
	if (state != 1 && state != 2)
		return;
	if (gGameState != GS_PLAYING_GAME || FrontEndMenuManager.m_bGameNotLoaded)
		return;

	if (state == 1) {
		if (!canStart())
			return;
		if (CTheScripts::IsPlayerOnAMission()) {
			static int warned;
			if (!warned++)
				PS3_Log("[autotest] waiting: a mission is running (load a save in a safehouse)");
			return;
		}
		CPlayerPed *p = FindPlayerPed();
		startPos = p->GetPosition();
		startHeading = p->m_fRotationCur;
		runStartUs = nowUs();
		loop = 0;
		failures = 0;
		haveWater = false;
		buildPlan();
		PS3_LogMemTag("[autotest] start:");
		state = 2;
		cur = 0;
		startStep();
	}

	if (waitingForLoad || FrontEndMenuManager.m_bMenuActive)
		return;

	statsFrame();
	keepPlayerSafe();

	if (cur >= numSteps) {
		loop++;
		if (loop >= loops) {
			finishRun();
			return;
		}
		buildPlan();	// garages/restarts may have changed
		cur = 0;
		startStep();
	}

	Step &s = steps[cur];
	if (s.type != ST_GARAGE && s.type != ST_MISSION)
		clearWanted();
	switch (s.type) {
	case ST_WEATHER: runWeather(s); break;
	case ST_FLY: runFly(s); break;
	case ST_SAVELOAD: runSaveLoad(s); break;
	case ST_SIDEMISSION: runSideMission(s); break;
	case ST_GARAGE: runGarage(s); break;
	case ST_DRIVE: runDrive(s); break;
	case ST_LOOPEND: runLoopEnd(); if (cur < numSteps) startStep(); break;
	case ST_MISSION: runMission(s); break;
	case ST_MISSIONEND: runMissionEnd(s); break;
	case ST_PICKUP: runPickup(s); break;
	}

	// the buttons, over what the real pad says
	CPad *pad = CPad::GetPad(0);
	if (pressR3 > 0) {
		// pressed for a few frames, then released (scripts look for a press)
		pad->NewState.RightShock = pressR3 > 3 ? 255 : 0;
		pressR3--;
	}
	if (pressCross)
		pad->NewState.Cross = 255;
	if (pulseCross > 0) {
		// on for 3 frames, off for the rest: a fresh press each time
		pad->NewState.Cross = pulseCross > 17 ? 255 : 0;
		pulseCross--;
	}
}

// From ps3.cpp, between frames: a load the test asked for
int
PS3_AutotestWantsLoad(void)
{
	if (!wantLoad)
		return 0;
	wantLoad = 0;
	testCar = nil;	// the load throws all the cars away
	if (!CheckSlotDataValid(AUTOTEST_SLOT)) {
		PS3_Logf("[autotest]     the save doesn't check out (error %d)", PcSaveHelper.nErrorCode);
		waitingForLoad = 0;
		return 0;
	}
	FrontEndMenuManager.m_bWantToRestart = true;
	FrontEndMenuManager.m_bWantToLoad = true;
	b_FoundRecentSavedGameWantToLoad = true;
	return 1;
}

// From ps3.cpp, after CGame::InitialiseWhenRestarting
void
PS3_AutotestLoaded(void)
{
	if (waitingForLoad) {
		waitingForLoad = 0;
		phaseStartUs = nowUs();
		lastFrameUs = 0;
	}
}

#endif // PS3_STAGE != 1
