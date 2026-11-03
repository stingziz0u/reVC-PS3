#include "common.h"

#include "General.h"
#include "Timer.h"
#ifdef __PS3__
#include "ps3_platform.h"
#include <sys/systime.h>
#include <malloc.h>
#include "src/ps3/rwps3.h"			// ringAlloc, im2DDrawQuadsAt
#include "../../ps3/spu/spuparticles.h"	// the SPU job
// from res-PS3 (patch 50): the particles' sprites built on an SPU
// (PS3_ParticlesSpu, at the end of the file). nospuparticles.txt in USRDIR
// keeps them on the PPU. gPS3ParticleUs: the game thread's time in
// CParticle::Render, gPS3SpuPartWaitUs: the part of it waiting for the SPU
bool gPS3SpuParticles = true;
uint64 gPS3ParticleUs, gPS3SpuPartWaitUs;
static PS3SpuTask sPartTask;
// patch 87: the drops on the screen (PS3_ScreenDropStep, before CParticle::Update)
#define PS3_DROP_RESTS 6	// rests (of 1250 ms more) a water drop gets at most: 9.5 s
#define PS3_BLOOD_RESTS 3	// and a blood drop: 5.75 s
#define PS3_DROP_DRY_MS 400	// a drop dries up (gets smaller) in its last 400 ms
#define PS3_BLOOD_DROPS 32	// blood drops on the screen at most (they live longer now)
static float sPS3DropClock;	// 30 fps frames gone by
static int32 sPS3BloodDrops;
#endif
#include "TxdStore.h"
#include "Sprite.h"
#include "Camera.h"
#include "Clock.h"
#include "Collision.h"
#include "World.h"
#include "Shadows.h"
#include "Replay.h"
#include "Stats.h"
#include "Weather.h"
#include "MBlur.h"
#include "main.h"
#include "AudioScriptObject.h"
#include "ParticleObject.h"
#include "Particle.h"
#include "soundlist.h"


#define MAX_PARTICLES_ON_SCREEN   (750)


//(5)
#define MAX_SMOKE_FILES           ARRAY_SIZE(SmokeFiles)

//(5) 
#define MAX_RUBBER_FILES          ARRAY_SIZE(RubberFiles)
//(5)
#define MAX_RAINSPLASH_FILES      ARRAY_SIZE(RainSplashFiles)
//(3)
#define MAX_WATERSPRAY_FILES      ARRAY_SIZE(WatersprayFiles)
//(6)
#define MAX_EXPLOSIONMEDIUM_FILES ARRAY_SIZE(ExplosionMediumFiles)
//(4)
#define MAX_GUNFLASH_FILES        ARRAY_SIZE(GunFlashFiles)
//(2)
#define MAX_RAINSPLASHUP_FILES    ARRAY_SIZE(RainSplashupFiles)
//(4)
#define MAX_BIRDFRONT_FILES       ARRAY_SIZE(BirdfrontFiles)
//(8)
#define MAX_BOAT_FILES            ARRAY_SIZE(BoatFiles)
//(4)
#define MAX_CARDEBRIS_FILES       ARRAY_SIZE(CardebrisFiles)
//(4)
#define MAX_CARSPLASH_FILES       ARRAY_SIZE(CarsplashFiles)

#define MAX_RAINDRIP_FILES       (2)

	
const char SmokeFiles[][6+1] =
{
	"smoke1",
	"smoke2",
	"smoke3",
	"smoke4",
	"smoke5"
};


const char RubberFiles[][7+1] =
{
	"rubber1",
	"rubber2",
	"rubber3",
	"rubber4",
	"rubber5"
};

const char RainSplashFiles[][7+1] =
{
	"splash1",
	"splash2",
	"splash3",
	"splash4",
	"splash5"
};

const char WatersprayFiles[][11+1] =
{
	"waterspray1",
	"waterspray2",
	"waterspray3"
};

const char ExplosionMediumFiles[][7+1] =
{
	"explo01",
	"explo02",
	"explo03",
	"explo04",
	"explo05",
	"explo06"
};

const char GunFlashFiles[][9+1] =
{
	"gunflash1",
	"gunflash2",
	"gunflash3",
	"gunflash4"
};

const char RainSplashupFiles[][10+1] =
{
	"splash_up1",
	"splash_up2"
};

const char BirdfrontFiles[][8+1] =
{
	"birdf_01",
	"birdf_02",
	"birdf_03",
	"birdf_04"
};

const char BoatFiles[][8+1] =
{
	"boats_01",
	"boats_02",
	"boats_03",
	"boats_04",
	"boats_05",
	"boats_06",
	"boats_07",
	"boats_08"
};

const char CardebrisFiles[][12+1] =
{
	"cardebris_01",
	"cardebris_02",
	"cardebris_03",
	"cardebris_04"
};
				
const char CarsplashFiles[][12+1] =
{
	"carsplash_01",
	"carsplash_02",
	"carsplash_03",
	"carsplash_04"
};

CParticle gParticleArray[MAX_PARTICLES_ON_SCREEN];

RwTexture *gpSmokeTex[MAX_SMOKE_FILES];
RwTexture *gpSmoke2Tex;
RwTexture *gpRubberTex[MAX_RUBBER_FILES];
RwTexture *gpRainSplashTex[MAX_RAINSPLASH_FILES];
RwTexture *gpWatersprayTex[MAX_WATERSPRAY_FILES];
RwTexture *gpExplosionMediumTex[MAX_EXPLOSIONMEDIUM_FILES];
RwTexture *gpGunFlashTex[MAX_GUNFLASH_FILES];
RwTexture *gpRainSplashupTex[MAX_RAINSPLASHUP_FILES];
RwTexture *gpBirdfrontTex[MAX_BIRDFRONT_FILES];
RwTexture *gpBoatTex[MAX_BOAT_FILES];
RwTexture *gpCarDebrisTex[MAX_CARDEBRIS_FILES];
RwTexture *gpCarSplashTex[MAX_CARSPLASH_FILES];

RwTexture *gpBoatWakeTex;
RwTexture *gpFlame1Tex;
RwTexture *gpFlame5Tex;
RwTexture *gpRainDropSmallTex;
RwTexture *gpBloodTex;
RwTexture *gpLeafTex;
RwTexture *gpCloudTex1;
RwTexture *gpCloudTex4;
RwTexture *gpBloodSmallTex;
RwTexture *gpGungeTex;
RwTexture *gpCollisionSmokeTex;
RwTexture *gpBulletHitTex;
RwTexture *gpGunShellTex;
RwTexture *gpPointlightTex;

RwRaster  *gpSmokeRaster[MAX_SMOKE_FILES];
RwRaster  *gpSmoke2Raster;
RwRaster  *gpRubberRaster[MAX_RUBBER_FILES];
RwRaster  *gpRainSplashRaster[MAX_RAINSPLASH_FILES];
RwRaster  *gpWatersprayRaster[MAX_WATERSPRAY_FILES];
RwRaster  *gpExplosionMediumRaster[MAX_EXPLOSIONMEDIUM_FILES];
RwRaster  *gpGunFlashRaster[MAX_GUNFLASH_FILES];
RwRaster  *gpRainSplashupRaster[MAX_RAINSPLASHUP_FILES];
RwRaster  *gpBirdfrontRaster[MAX_BIRDFRONT_FILES];
RwRaster  *gpBoatRaster[MAX_BOAT_FILES];
RwRaster  *gpCarDebrisRaster[MAX_CARDEBRIS_FILES];
RwRaster  *gpCarSplashRaster[MAX_CARSPLASH_FILES];

RwRaster  *gpBoatWakeRaster;
RwRaster  *gpFlame1Raster;
RwRaster  *gpFlame5Raster;
RwRaster  *gpRainDropSmallRaster;
RwRaster  *gpBloodRaster;
RwRaster  *gpLeafRaster;
RwRaster  *gpCloudRaster1;
RwRaster  *gpCloudRaster4;
RwRaster  *gpBloodSmallRaster;
RwRaster  *gpGungeRaster;
RwRaster  *gpCollisionSmokeRaster;
RwRaster  *gpBulletHitRaster;
RwRaster  *gpGunShellRaster;
RwRaster  *gpPointlightRaster;

RwTexture *gpRainDropTex;
RwRaster  *gpRainDropRaster;

RwTexture *gpLetterTex;
RwRaster *gpLetterRaster;

RwTexture *gpSparkTex;
RwTexture *gpNewspaperTex;
RwTexture *gpGunSmokeTex;
RwTexture *gpDotTex;
RwTexture *gpHeatHazeTex;
RwTexture *gpBeastieTex;
RwTexture *gpRainDripTex[MAX_RAINDRIP_FILES];
RwTexture *gpRainDripDarkTex[MAX_RAINDRIP_FILES];

RwRaster *gpSparkRaster;
RwRaster *gpNewspaperRaster;
RwRaster *gpGunSmokeRaster;
RwRaster *gpDotRaster;
RwRaster *gpHeatHazeRaster;
RwRaster *gpBeastieRaster;
RwRaster *gpRainDripRaster[MAX_RAINDRIP_FILES];
RwRaster *gpRainDripDarkRaster[MAX_RAINDRIP_FILES];

float      CParticle::ms_afRandTable[CParticle::RAND_TABLE_SIZE];
CParticle *CParticle::m_pUnusedListHead;
float      CParticle::m_SinTable[CParticle::SIN_COS_TABLE_SIZE];
float      CParticle::m_CosTable[CParticle::SIN_COS_TABLE_SIZE]; 

int32 Randomizer;
int32 nParticleCreationInterval = 1;
float PARTICLE_WIND_TEST_SCALE  = 0.002f;
float fParticleScaleLimit       = 0.5f;

bool clearWaterDrop;
int32 numWaterDropOnScreen;

#ifdef DEBUGMENU
SETTWEAKPATH("Particle");
TWEAKINT32(nParticleCreationInterval, 0, 5, 1);
TWEAKFLOAT(fParticleScaleLimit, 0.0f, 1.0f, 0.1f);
TWEAKFUNC(CParticle::ReloadConfig);
#endif



void CParticle::ReloadConfig()
{
	debug("Initialising CParticleMgr...");
	
	mod_ParticleSystemManager.Initialise();
	
	debug("Initialising CParticle...");
	
	m_pUnusedListHead = gParticleArray;
	
	for ( int32 i = 0; i < MAX_PARTICLES_ON_SCREEN; i++ )
	{
		if ( i == MAX_PARTICLES_ON_SCREEN - 1 )
			gParticleArray[i].m_pNext = nil;
		else
			gParticleArray[i].m_pNext = &gParticleArray[i + 1];
		
		gParticleArray[i].m_vecPosition = CVector(0.0f, 0.0f, 0.0f);

		gParticleArray[i].m_vecVelocity = CVector(0.0f, 0.0f, 0.0f);

		gParticleArray[i].m_nTimeWhenWillBeDestroyed = 0;

		gParticleArray[i].m_nTimeWhenColorWillBeChanged = 0;

		gParticleArray[i].m_fSize = 0.2f;

		gParticleArray[i].m_fExpansionRate = 0.0f;

		gParticleArray[i].m_nColorIntensity = 255;

		gParticleArray[i].m_nFadeToBlackTimer = 0;

		gParticleArray[i].m_nAlpha = 255;

		gParticleArray[i].m_nFadeAlphaTimer = 0;

		gParticleArray[i].m_nCurrentZRotation = 0;

		gParticleArray[i].m_nZRotationTimer = 0;

		gParticleArray[i].m_fCurrentZRadius = 0.0f;

		gParticleArray[i].m_nZRadiusTimer = 0;

		gParticleArray[i].m_nCurrentFrame = 0;

		gParticleArray[i].m_nAnimationSpeedTimer = 0;

		gParticleArray[i].m_nRotation = 0;

		gParticleArray[i].m_nRotationStep = 0;
	}
}

void CParticle::Initialise()
{
	ReloadConfig();

	CParticleObject::Initialise();

	float randVal = -1.0f;
	for ( int32 i = 0; i < RAND_TABLE_SIZE; i++ )
	{
		ms_afRandTable[i] = randVal;
		randVal += 0.1f;
	}
	
	for ( int32 i = 0; i < SIN_COS_TABLE_SIZE; i++ )
	{		
		float angle = DEGTORAD(float(i) * float(360.0f / SIN_COS_TABLE_SIZE));

		m_SinTable[i] = ::Sin(angle);
		m_CosTable[i] = ::Cos(angle);
	}
	
	int32 slot = CTxdStore::FindTxdSlot("particle");

	CTxdStore::PushCurrentTxd();
	CTxdStore::SetCurrentTxd(slot);
	
	for ( int32 i = 0; i < MAX_SMOKE_FILES; i++ )
	{
		gpSmokeTex[i] = RwTextureRead(SmokeFiles[i], nil);
		gpSmokeRaster[i] = RwTextureGetRaster(gpSmokeTex[i]);
	}
	
	gpSmoke2Tex = RwTextureRead("smokeII_3", nil);
	gpSmoke2Raster = RwTextureGetRaster(gpSmoke2Tex);
	
	for ( int32 i = 0; i < MAX_RUBBER_FILES; i++ )
	{
		gpRubberTex[i] = RwTextureRead(RubberFiles[i], nil);
		gpRubberRaster[i] = RwTextureGetRaster(gpRubberTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_RAINSPLASH_FILES; i++ )
	{
		gpRainSplashTex[i] = RwTextureRead(RainSplashFiles[i], nil);
		gpRainSplashRaster[i] = RwTextureGetRaster(gpRainSplashTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_WATERSPRAY_FILES; i++ )
	{
		gpWatersprayTex[i] = RwTextureRead(WatersprayFiles[i], nil);
		gpWatersprayRaster[i] = RwTextureGetRaster(gpWatersprayTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_EXPLOSIONMEDIUM_FILES; i++ )
	{
		gpExplosionMediumTex[i] = RwTextureRead(ExplosionMediumFiles[i], nil);
		gpExplosionMediumRaster[i] = RwTextureGetRaster(gpExplosionMediumTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_GUNFLASH_FILES; i++ )
	{
		gpGunFlashTex[i] = RwTextureRead(GunFlashFiles[i], nil);
		gpGunFlashRaster[i] = RwTextureGetRaster(gpGunFlashTex[i]);
	}
	
	gpRainDropTex = RwTextureRead("raindrop4", nil);
	gpRainDropRaster = RwTextureGetRaster(gpRainDropTex);

	
	for ( int32 i = 0; i < MAX_RAINSPLASHUP_FILES; i++ )
	{
		gpRainSplashupTex[i] = RwTextureRead(RainSplashupFiles[i], nil);
		gpRainSplashupRaster[i] = RwTextureGetRaster(gpRainSplashupTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_BIRDFRONT_FILES; i++ )
	{
		gpBirdfrontTex[i] = RwTextureRead(BirdfrontFiles[i], nil);
		gpBirdfrontRaster[i] = RwTextureGetRaster(gpBirdfrontTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_BOAT_FILES; i++ )
	{
		gpBoatTex[i] = RwTextureRead(BoatFiles[i], nil);
		gpBoatRaster[i] = RwTextureGetRaster(gpBoatTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_CARDEBRIS_FILES; i++ )
	{
		gpCarDebrisTex[i] = RwTextureRead(CardebrisFiles[i], nil);
		gpCarDebrisRaster[i] = RwTextureGetRaster(gpCarDebrisTex[i]);
	}
	
	for ( int32 i = 0; i < MAX_CARSPLASH_FILES; i++ )
	{
		gpCarSplashTex[i] = RwTextureRead(CarsplashFiles[i], nil);
		gpCarSplashRaster[i] = RwTextureGetRaster(gpCarSplashTex[i]);
	}

	gpBoatWakeTex = RwTextureRead("boatwake2", nil);
	gpBoatWakeRaster = RwTextureGetRaster(gpBoatWakeTex);
	
	gpFlame1Tex = RwTextureRead("flame1", nil);
	gpFlame1Raster = RwTextureGetRaster(gpFlame1Tex);

	gpFlame5Tex = RwTextureRead("flame5", nil);
	
//#ifdef FIX_BUGS
#if 0
	gpFlame5Raster = RwTextureGetRaster(gpFlame5Tex);
#else
	// this seems to have become more of a design choice
	gpFlame5Raster = RwTextureGetRaster(gpFlame1Tex);	// copy-paste bug ?
#endif

	gpRainDropSmallTex = RwTextureRead("rainsmall", nil);
	gpRainDropSmallRaster = RwTextureGetRaster(gpRainDropSmallTex);

	gpBloodTex = RwTextureRead("blood", nil);
	gpBloodRaster = RwTextureGetRaster(gpBloodTex);

	gpLeafTex = RwTextureRead("gameleaf01_64", nil);
	gpLeafRaster = RwTextureGetRaster(gpLeafTex);
	
	gpLetterTex = RwTextureRead("letter", nil);
	gpLetterRaster = RwTextureGetRaster(gpLetterTex);

	gpCloudTex1 = RwTextureRead("cloud3", nil);
	gpCloudRaster1 = RwTextureGetRaster(gpCloudTex1);

	gpCloudTex4 = RwTextureRead("cloudmasked", nil);
	gpCloudRaster4 = RwTextureGetRaster(gpCloudTex4);

	gpBloodSmallTex = RwTextureRead("bloodsplat2", nil);
	gpBloodSmallRaster = RwTextureGetRaster(gpBloodSmallTex);

	gpGungeTex = RwTextureRead("gunge", nil);
	gpGungeRaster = RwTextureGetRaster(gpGungeTex);

	gpCollisionSmokeTex = RwTextureRead("collisionsmoke", nil);
	gpCollisionSmokeRaster = RwTextureGetRaster(gpCollisionSmokeTex);

	gpBulletHitTex = RwTextureRead("bullethitsmoke", nil);
	gpBulletHitRaster = RwTextureGetRaster(gpBulletHitTex);

	gpGunShellTex = RwTextureRead("gunshell", nil);
	gpGunShellRaster = RwTextureGetRaster(gpGunShellTex);

	gpPointlightTex = RwTextureRead("pointlight", nil);
	gpPointlightRaster = RwTextureGetRaster(gpPointlightTex);
	
	gpSparkTex = RwTextureRead("spark", nil);
	gpSparkRaster = RwTextureGetRaster(gpSparkTex);
	
	gpNewspaperTex = RwTextureRead("newspaper02_64", nil);
	gpNewspaperRaster = RwTextureGetRaster(gpNewspaperTex);
	
	gpGunSmokeTex = RwTextureRead("gunsmoke3", nil);
	gpGunSmokeRaster = RwTextureGetRaster(gpGunSmokeTex);
	
	gpDotTex = RwTextureRead("dot", nil);
	gpDotRaster = RwTextureGetRaster(gpDotTex);
	
	gpHeatHazeTex = RwTextureRead("heathaze", nil);
	gpHeatHazeRaster = RwTextureGetRaster(gpHeatHazeTex);
	
	gpBeastieTex = RwTextureRead("beastie", nil);
	gpBeastieRaster = RwTextureGetRaster(gpBeastieTex);
	
	gpRainDripTex[0] = RwTextureRead("raindrip64", nil);
	gpRainDripRaster[0] = RwTextureGetRaster(gpRainDripTex[0]);
	
	gpRainDripTex[1] = RwTextureRead("raindripb64", nil);
	gpRainDripRaster[1] = RwTextureGetRaster(gpRainDripTex[1]);
	
	gpRainDripDarkTex[0] = RwTextureRead("raindrip64_d", nil);
	gpRainDripDarkRaster[0] = RwTextureGetRaster(gpRainDripDarkTex[0]);
	
	gpRainDripDarkTex[1] = RwTextureRead("raindripb64_d", nil);
	gpRainDripDarkRaster[1] = RwTextureGetRaster(gpRainDripDarkTex[1]);
	
	CTxdStore::PopCurrentTxd();
	
	for ( int32 i = 0; i < MAX_PARTICLES; i++ )
	{
		tParticleSystemData *entry = &mod_ParticleSystemManager.m_aParticles[i];
		
		switch ( i )
		{
			case PARTICLE_SPARK:
			case PARTICLE_SPARK_SMALL:
			case PARTICLE_RAINDROP_SMALL:
			case PARTICLE_HELI_ATTACK:
				entry->m_ppRaster = &gpRainDropSmallRaster;
				break;
			
			case PARTICLE_WATER_SPARK:
				entry->m_ppRaster = &gpSparkRaster;
				break;
			
			case PARTICLE_WHEEL_DIRT:
			case PARTICLE_SAND:
			case PARTICLE_STEAM2:
			case PARTICLE_STEAM_NY:
			case PARTICLE_STEAM_NY_SLOWMOTION:
			case PARTICLE_GROUND_STEAM:
			case PARTICLE_ENGINE_STEAM:
			case PARTICLE_PEDFOOT_DUST:
			case PARTICLE_CAR_DUST:
			case PARTICLE_EXHAUST_FUMES:
				entry->m_ppRaster = &gpSmoke2Raster;
				break;
			
			case PARTICLE_WHEEL_WATER:
			case PARTICLE_WATER:
			case PARTICLE_SMOKE:
			case PARTICLE_SMOKE_SLOWMOTION:
			case PARTICLE_DRY_ICE:
			case PARTICLE_GARAGEPAINT_SPRAY:
			case PARTICLE_STEAM:
			case PARTICLE_WATER_CANNON:
			case PARTICLE_EXTINGUISH_STEAM:
			case PARTICLE_HELI_DUST:
			case PARTICLE_PAINT_SMOKE:
			case PARTICLE_BULLETHIT_SMOKE:
				entry->m_ppRaster = gpSmokeRaster;
				break;
			
			case PARTICLE_BLOOD:
				entry->m_ppRaster = &gpBloodRaster;
				break;
			
			case PARTICLE_BLOOD_SMALL:
			case PARTICLE_BLOOD_SPURT:
				entry->m_ppRaster = &gpBloodSmallRaster;
				break;
			
			case PARTICLE_DEBRIS:
			case PARTICLE_TREE_LEAVES:
				entry->m_ppRaster = &gpLeafRaster;
				break;
			
			case PARTICLE_DEBRIS2:
				entry->m_ppRaster = &gpGungeRaster;
				break;
			
			case PARTICLE_FLYERS:
				entry->m_ppRaster = &gpNewspaperRaster;
				break;
			
			case PARTICLE_FLAME:
			case PARTICLE_CARFLAME:
				entry->m_ppRaster = &gpFlame1Raster;
				break;
			
			case PARTICLE_FIREBALL:
				entry->m_ppRaster = &gpFlame5Raster;
				break;
			
			case PARTICLE_GUNFLASH:
			case PARTICLE_GUNFLASH_NOANIM:
				entry->m_ppRaster = gpGunFlashRaster;
				break;
			
			
			case PARTICLE_GUNSMOKE:
			case PARTICLE_WATERDROP:
			case PARTICLE_BLOODDROP:
			case PARTICLE_HEATHAZE:
			case PARTICLE_HEATHAZE_IN_DIST:
				entry->m_ppRaster = nil;
				break;
			
			case PARTICLE_GUNSMOKE2:
			case PARTICLE_BOAT_THRUSTJET:
			case PARTICLE_RUBBER_SMOKE:
				entry->m_ppRaster = gpRubberRaster;
				break;
			
			case PARTICLE_CIGARETTE_SMOKE:
				entry->m_ppRaster = &gpGunSmokeRaster;
				break;
			
			case PARTICLE_TEARGAS:
				entry->m_ppRaster = &gpHeatHazeRaster;
				break;
			
			case PARTICLE_SHARD:
			case PARTICLE_RAINDROP:
			case PARTICLE_RAINDROP_2D:
				entry->m_ppRaster = &gpRainDropRaster;
				break;
			
			case PARTICLE_SPLASH:
			case PARTICLE_PED_SPLASH:
			case PARTICLE_CAR_SPLASH:
			case PARTICLE_WATER_HYDRANT:
				entry->m_ppRaster = gpCarSplashRaster;
				break;
			
			case PARTICLE_RAIN_SPLASH:
			case PARTICLE_RAIN_SPLASH_BIGGROW:
				entry->m_ppRaster = gpRainSplashRaster;
				break;
			
			case PARTICLE_RAIN_SPLASHUP:
				entry->m_ppRaster = gpRainSplashupRaster;
				break;
			
			case PARTICLE_WATERSPRAY:
				entry->m_ppRaster = gpWatersprayRaster;
				break;
			
			case PARTICLE_EXPLOSION_MEDIUM:
			case PARTICLE_EXPLOSION_LARGE:
			case PARTICLE_EXPLOSION_MFAST:
			case PARTICLE_EXPLOSION_LFAST:
				entry->m_ppRaster = gpExplosionMediumRaster;
				break;
			
			case PARTICLE_BOAT_SPLASH:
				entry->m_ppRaster = &gpBoatWakeRaster;
				break;
			
			case PARTICLE_ENGINE_SMOKE:
			case PARTICLE_ENGINE_SMOKE2:
			case PARTICLE_CARFLAME_SMOKE:
			case PARTICLE_FIREBALL_SMOKE:
			case PARTICLE_ROCKET_SMOKE:
			case PARTICLE_TEST:
				entry->m_ppRaster = &gpCloudRaster4;
				break;
			
			case PARTICLE_CARCOLLISION_DUST:
			case PARTICLE_BURNINGRUBBER_SMOKE:
				entry->m_ppRaster = &gpCollisionSmokeRaster;
				break;
			
			case PARTICLE_CAR_DEBRIS:
			case PARTICLE_HELI_DEBRIS:
			case PARTICLE_BIRD_DEBRIS:
				entry->m_ppRaster = gpCarDebrisRaster;
				break;
			
			case PARTICLE_GUNSHELL_FIRST:
			case PARTICLE_GUNSHELL:
			case PARTICLE_GUNSHELL_BUMP1:
			case PARTICLE_GUNSHELL_BUMP2:
				entry->m_ppRaster = &gpGunShellRaster;
				break;
			
			
			case PARTICLE_BIRD_FRONT:
				entry->m_ppRaster = gpBirdfrontRaster;
				break;
			
			case PARTICLE_SHIP_SIDE:
				entry->m_ppRaster = gpBoatRaster;
				break;
			
			case PARTICLE_BEASTIE:
				entry->m_ppRaster = &gpBeastieRaster;
				break;
		}
	}

	debug("CParticle ready");
}

void CParticle::Shutdown()
{
#ifdef __PS3__
	PS3_SpuTaskStop(&sPartTask);	// from res-PS3 (patch 50)
#endif
	debug("Shutting down CParticle...");

	for ( int32 i = 0; i < MAX_SMOKE_FILES; i++ )
	{
		RwTextureDestroy(gpSmokeTex[i]);
		gpSmokeTex[i] = nil;
	}

	RwTextureDestroy(gpSmoke2Tex);
	gpSmoke2Tex = nil;
	 
	for ( int32 i = 0; i < MAX_RUBBER_FILES; i++ )
	{
		RwTextureDestroy(gpRubberTex[i]);
		gpRubberTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_RAINSPLASH_FILES; i++ )
	{
		RwTextureDestroy(gpRainSplashTex[i]);
		gpRainSplashTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_WATERSPRAY_FILES; i++ )
	{
		RwTextureDestroy(gpWatersprayTex[i]);
		gpWatersprayTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_EXPLOSIONMEDIUM_FILES; i++ )
	{
		RwTextureDestroy(gpExplosionMediumTex[i]);
		gpExplosionMediumTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_GUNFLASH_FILES; i++ )
	{
		RwTextureDestroy(gpGunFlashTex[i]);
		gpGunFlashTex[i] = nil;
	}
	
	RwTextureDestroy(gpRainDropTex);
	gpRainDropTex = nil;
	
	for ( int32 i = 0; i < MAX_RAINSPLASHUP_FILES; i++ )
	{
		RwTextureDestroy(gpRainSplashupTex[i]);
		gpRainSplashupTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_BIRDFRONT_FILES; i++ )
	{
		RwTextureDestroy(gpBirdfrontTex[i]);
		gpBirdfrontTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_BOAT_FILES; i++ )
	{
		RwTextureDestroy(gpBoatTex[i]);
		gpBoatTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_CARDEBRIS_FILES; i++ )
	{
		RwTextureDestroy(gpCarDebrisTex[i]);
		gpCarDebrisTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_CARSPLASH_FILES; i++ )
	{
		RwTextureDestroy(gpCarSplashTex[i]);
		gpCarSplashTex[i] = nil;
	}
	
	for ( int32 i = 0; i < MAX_RAINDRIP_FILES; i++ )
	{
		RwTextureDestroy(gpRainDripTex[i]);
		gpRainDripTex[i] = nil;

		RwTextureDestroy(gpRainDripDarkTex[i]);
		gpRainDripDarkTex[i] = nil;
	}
	
	RwTextureDestroy(gpBoatWakeTex);
	gpBoatWakeTex = nil;

	RwTextureDestroy(gpFlame1Tex);
	gpFlame1Tex = nil;

	RwTextureDestroy(gpFlame5Tex);
	gpFlame5Tex = nil;
	
	RwTextureDestroy(gpRainDropSmallTex);
	gpRainDropSmallTex = nil;
	
	RwTextureDestroy(gpBloodTex);
	gpBloodTex = nil;
	
	RwTextureDestroy(gpLeafTex);
	gpLeafTex = nil;

	RwTextureDestroy(gpLetterTex);
	gpLetterTex = nil;
	
	RwTextureDestroy(gpCloudTex1);
	gpCloudTex1 = nil;
	
	RwTextureDestroy(gpCloudTex4);
	gpCloudTex4 = nil;
	
	RwTextureDestroy(gpBloodSmallTex);
	gpBloodSmallTex = nil;
	
	RwTextureDestroy(gpGungeTex);
	gpGungeTex = nil;
	
	RwTextureDestroy(gpCollisionSmokeTex);
	gpCollisionSmokeTex = nil;
	
	RwTextureDestroy(gpBulletHitTex);
	gpBulletHitTex = nil;
	
	RwTextureDestroy(gpGunShellTex);
	gpGunShellTex = nil;
	
	RwTextureDestroy(gpPointlightTex);
	gpPointlightTex = nil;

	RwTextureDestroy(gpSparkTex);
	gpSparkTex = nil;
	
	RwTextureDestroy(gpNewspaperTex);
	gpNewspaperTex = nil;

	RwTextureDestroy(gpGunSmokeTex);
	gpGunSmokeTex = nil;
	
	RwTextureDestroy(gpDotTex);
	gpDotTex = nil;
	RwTextureDestroy(gpHeatHazeTex);
	gpHeatHazeTex = nil;
	
	RwTextureDestroy(gpBeastieTex);
	gpBeastieTex = nil;

	int32 slot;

	slot = CTxdStore::FindTxdSlot("particle");
	CTxdStore::RemoveTxdSlot(slot);

	debug("CParticle shut down");
}


void CParticle::AddParticlesAlongLine(tParticleType type, CVector const &vecStart, CVector const &vecEnd, CVector const &vecDir, float fPower, CEntity *pEntity, float fSize, int32 nRotationSpeed, int32 nRotation, int32 nCurFrame, int32 nLifeSpan)
{
	CVector vecDist = vecEnd - vecStart;
	float fDist = vecDist.Magnitude();
	float fSteps = Max(fDist / fPower, 1.0f);
	int32 nSteps = (int32)fSteps;

	CVector vecStep = vecDist * (1.0f / (float)nSteps); 

	for ( int32 i = 0; i < nSteps; i++ )
	{
		CVector vecPos = float(i) * vecStep + vecStart;
		AddParticle(type, vecPos, vecDir, pEntity, fSize, nRotationSpeed, nRotation, nCurFrame, nLifeSpan);
	}
}

void CParticle::AddParticlesAlongLine(tParticleType type, CVector const &vecStart, CVector const &vecEnd, CVector const &vecDir, float fPower, CEntity *pEntity, float fSize, RwRGBA const &color, int32 nRotationSpeed, int32 nRotation, int32 nCurFrame, int32 nLifeSpan)
{
	CVector vecDist = vecEnd - vecStart;
	float fDist = vecDist.Magnitude();
	float fSteps = Max(fDist / fPower, 1.0f);
	int32 nSteps = (int32)fSteps;
	
	CVector vecStep = vecDist * (1.0f / (float)nSteps); 

	for ( int32 i = 0; i < nSteps; i++ )
	{		
		CVector vecPos = float(i) * vecStep + vecStart;
		
		AddParticle(type, vecPos, vecDir, pEntity, fSize, color, nRotationSpeed, nRotation, nCurFrame, nLifeSpan);
	}
}

#ifdef __PS3__
// both AddParticle()s leave a crumb with their caller's address (the crash
// log then says who added the last particles), and share the code below

CParticle *CParticle::AddParticle(tParticleType type, CVector const &vecPos, CVector const &vecDir, CEntity *pEntity, float fSize, int32 nRotationSpeed, int32 nRotation, int32 nCurFrame, int32 nLifeSpan)
{
	PS3_CRUMB_CALLER("CParticle::AddParticle type (@ caller)", type);
	CRGBA color(0, 0, 0, 0);
	return AddParticleImpl(type, vecPos, vecDir, pEntity, fSize, color, nRotationSpeed, nRotation, nCurFrame, nLifeSpan);
}

CParticle *CParticle::AddParticle(tParticleType type, CVector const &vecPos, CVector const &vecDir, CEntity *pEntity, float fSize, RwRGBA const &color, int32 nRotationSpeed, int32 nRotation, int32 nCurFrame, int32 nLifeSpan)
{
	PS3_CRUMB_CALLER("CParticle::AddParticle type (@ caller)", type);
	return AddParticleImpl(type, vecPos, vecDir, pEntity, fSize, color, nRotationSpeed, nRotation, nCurFrame, nLifeSpan);
}

CParticle *CParticle::AddParticleImpl(tParticleType type, CVector const &vecPos, CVector const &vecDir, CEntity *pEntity, float fSize, RwRGBA const &color, int32 nRotationSpeed, int32 nRotation, int32 nCurFrame, int32 nLifeSpan)
#else
CParticle *CParticle::AddParticle(tParticleType type, CVector const &vecPos, CVector const &vecDir, CEntity *pEntity, float fSize, int32 nRotationSpeed, int32 nRotation, int32 nCurFrame, int32 nLifeSpan)
{
	CRGBA color(0, 0, 0, 0);
	return AddParticle(type, vecPos, vecDir, pEntity, fSize, color, nRotationSpeed, nRotation, nCurFrame, nLifeSpan);
}

CParticle *CParticle::AddParticle(tParticleType type, CVector const &vecPos, CVector const &vecDir, CEntity *pEntity, float fSize, RwRGBA const &color, int32 nRotationSpeed, int32 nRotation, int32 nCurFrame, int32 nLifeSpan)
#endif
{
	if ( CTimer::GetIsPaused() )
		return nil;

#ifdef __PS3__
	if ( type == PARTICLE_BLOODDROP )	// patch 87
	{
		if ( sPS3BloodDrops >= PS3_BLOOD_DROPS )
			return nil;
		sPS3BloodDrops++;
	}
#endif

	if ( ( type == PARTICLE_ENGINE_SMOKE
		|| type == PARTICLE_ENGINE_SMOKE2
		|| type == PARTICLE_ENGINE_STEAM
		|| type == PARTICLE_CARFLAME_SMOKE
		|| type == PARTICLE_RUBBER_SMOKE
		|| type == PARTICLE_BURNINGRUBBER_SMOKE
		|| type == PARTICLE_EXHAUST_FUMES
		|| type == PARTICLE_CARCOLLISION_DUST )
#ifdef __PS3__
		// these are added every frame and kept 1 frame in 2 (made for 30 fps);
		// at 60 fps keep 1 in 4: the same number per second
		&& (CTimer::GetTimeStep() < 0.75f ? nParticleCreationInterval * 2 + 1 : nParticleCreationInterval) & CTimer::GetFrameCounter() )
#else
		&& nParticleCreationInterval & CTimer::GetFrameCounter() )
#endif
	{
		return nil;
	}

	if ( !CReplay::IsPlayingBack() )
		CReplay::RecordParticle(type, vecPos, vecDir, fSize, color);
	
	CParticle *pParticle = m_pUnusedListHead;
	
	if ( pParticle == nil )
		return nil;
	
	tParticleSystemData *psystem = &mod_ParticleSystemManager.m_aParticles[type];
	
	if ( psystem->m_fCreateRange != 0.0f && psystem->m_fCreateRange < ( TheCamera.GetPosition() - vecPos ).MagnitudeSqr() )
		return nil;
	
	
	pParticle->m_fSize = psystem->m_fDefaultInitialRadius;
	pParticle->m_fExpansionRate = psystem->m_fExpansionRate;
	
	if ( nLifeSpan != 0 )
		pParticle->m_nTimeWhenWillBeDestroyed = CTimer::GetTimeInMilliseconds() + nLifeSpan;
	else
		pParticle->m_nTimeWhenWillBeDestroyed = CTimer::GetTimeInMilliseconds() + psystem->m_nLifeSpan;

	pParticle->m_nColorIntensity = psystem->m_nFadeToBlackInitialIntensity;
	
	pParticle->m_nFadeToBlackTimer = psystem->m_nFadeToBlackAmount;
	
	if ( psystem->m_nFadeToBlackTime )
		pParticle->m_nFadeToBlackTimer /= psystem->m_nFadeToBlackTime;
	
	pParticle->m_nAlpha = psystem->m_nFadeAlphaInitialIntensity;
	
	pParticle->m_nFadeAlphaTimer = psystem->m_nFadeAlphaAmount;
	
	if ( psystem->m_nFadeAlphaTime )
		pParticle->m_nFadeAlphaTimer /= psystem->m_nFadeAlphaTime;
	
	pParticle->m_nCurrentZRotation = psystem->m_nZRotationInitialAngle;
	pParticle->m_fCurrentZRadius = psystem->m_fInitialZRadius;
	
	if ( nCurFrame != 0 )
		pParticle->m_nCurrentFrame = nCurFrame;
	else
		pParticle->m_nCurrentFrame = psystem->m_nStartAnimationFrame;
	
	
	pParticle->m_nZRotationTimer = 0;
	pParticle->m_nZRadiusTimer = 0;
	pParticle->m_nAnimationSpeedTimer = 0;
	pParticle->m_fZGround = 0.0f;
	
	if ( type != PARTICLE_HEATHAZE )
		pParticle->m_vecPosition = vecPos;
	else
	{
		CVector screen;
		float w, h;

		if ( !CSprite::CalcScreenCoors(vecPos, &screen, &w, &h, true) )
			return nil;
		
		pParticle->m_vecPosition = screen;
		psystem->m_vecTextureStretch.x = w;
		psystem->m_vecTextureStretch.y = h;
	}
	
	pParticle->m_vecVelocity = vecDir;
	
	pParticle->m_vecParticleMovementOffset = CVector(0.0f, 0.0f, 0.0f);
	pParticle->m_nTimeWhenColorWillBeChanged = 0;
	
	if ( color.alpha != 0 )
		RwRGBAAssign(&pParticle->m_Color, &color);
	else
	{
		RwRGBAAssign(&pParticle->m_Color, psystem->m_RenderColouring);

		if ( psystem->m_ColorFadeTime != 0 )
			pParticle->m_nTimeWhenColorWillBeChanged = CTimer::GetTimeInMilliseconds() + psystem->m_ColorFadeTime;

		if ( psystem->m_InitialColorVariation != 0 )
		{
			int32 ColorVariation = CGeneral::GetRandomNumberInRange(-psystem->m_InitialColorVariation, psystem->m_InitialColorVariation);
			//float ColorVariation = CGeneral::GetRandomNumberInRange((float)-psystem->m_InitialColorVariation, (float)psystem->m_InitialColorVariation);
  
			pParticle->m_Color.red   = clamp(pParticle->m_Color.red +
				PERCENT(pParticle->m_Color.red, ColorVariation),
				0, 255);
			
			pParticle->m_Color.green = clamp(pParticle->m_Color.green +
				PERCENT(pParticle->m_Color.green, ColorVariation),
				0, 255);
			
			pParticle->m_Color.blue  = clamp(pParticle->m_Color.blue +
				PERCENT(pParticle->m_Color.blue, ColorVariation),
				0, 255);
		}
	}

	pParticle->m_nRotation = nRotation;
		
	if ( nRotationSpeed != 0 )
		pParticle->m_nRotationStep = nRotationSpeed;
	else
		pParticle->m_nRotationStep = psystem->m_nRotationSpeed;
	
	if ( CGeneral::GetRandomNumber() & 1 )
		pParticle->m_nRotationStep = -pParticle->m_nRotationStep;
	
	if ( psystem->m_fPositionRandomError != 0.0f )
	{
		pParticle->m_vecPosition.x += psystem->m_fPositionRandomError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
		pParticle->m_vecPosition.y += psystem->m_fPositionRandomError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
		
		if ( psystem->Flags & RAND_VERT_V )
			pParticle->m_vecPosition.z += psystem->m_fPositionRandomError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
	}
	
	if ( psystem->m_fVelocityRandomError != 0.0f )
	{
		pParticle->m_vecVelocity.x += psystem->m_fVelocityRandomError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
		pParticle->m_vecVelocity.y += psystem->m_fVelocityRandomError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
    
		if ( psystem->Flags & RAND_VERT_V )
			pParticle->m_vecVelocity.z += psystem->m_fVelocityRandomError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
	}
	
	if ( psystem->m_fExpansionRateError != 0.0f && !(psystem->Flags & SCREEN_TRAIL) )
		pParticle->m_fExpansionRate += psystem->m_fExpansionRateError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE] + psystem->m_fExpansionRateError;
	
	if ( psystem->m_nRotationRateError != 0 )
		pParticle->m_nRotationStep += CGeneral::GetRandomNumberInRange(-psystem->m_nRotationRateError, psystem->m_nRotationRateError);

	if ( psystem->m_nLifeSpanErrorShape != 0 )
	{
		float randVal = ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
		if ( randVal > 0.0f )
			pParticle->m_nTimeWhenWillBeDestroyed += int32(float(psystem->m_nLifeSpan) * randVal * float(psystem->m_nLifeSpanErrorShape));
		else
			pParticle->m_nTimeWhenWillBeDestroyed += int32(float(psystem->m_nLifeSpan) * randVal / float(psystem->m_nLifeSpanErrorShape));
	}
	
	if ( psystem->Flags & ZCHECK_FIRST )
	{
		static bool bValidGroundFound = false;
		static CVector LastTestCoors;
		static float LastTestGroundZ;
		
		if ( bValidGroundFound 
			&& vecPos.x == LastTestCoors.x 
			&& vecPos.y == LastTestCoors.y 
			&& vecPos.z == LastTestCoors.z )
		{
			pParticle->m_fZGround = LastTestGroundZ;
		}
		else
		{
			bValidGroundFound = false;
			
			CColPoint point;
			CEntity *entity;
			
			if ( !CWorld::ProcessVerticalLine(
						pParticle->m_vecPosition + CVector(0.0f, 0.0f, 0.5f),
						-100.0f, point, entity, true, true, false, false, true, false, nil) )
			{
				return nil;
			}
			
			if ( point.point.z >= pParticle->m_vecPosition.z )
				return nil;
			
			pParticle->m_fZGround = point.point.z;
			bValidGroundFound = true;
			LastTestCoors = vecPos;
			LastTestGroundZ = point.point.z;
		}
	}
	
	if ( psystem->Flags & ZCHECK_BUMP )
	{
		static float Z_Ground = 0.0f;
		
		if ( psystem->Flags & ZCHECK_BUMP_FIRST )
		{
			bool bZFound = false;

			Z_Ground = CWorld::FindGroundZFor3DCoord(vecPos.x, vecPos.y, vecPos.z, (bool *)&bZFound);

			if ( bZFound == false )
				return nil;

			pParticle->m_fZGround = Z_Ground;
		}
		
		pParticle->m_fZGround = Z_Ground;
	}
	
	switch ( type )
	{
		case PARTICLE_DEBRIS:
			pParticle->m_vecVelocity.z *= CGeneral::GetRandomNumberInRange(0.5f, 3.0f);
			break;
		
		case PARTICLE_EXPLOSION_MEDIUM:
			pParticle->m_nColorIntensity -= 30 * (CGeneral::GetRandomNumber() & 1); // mb "+= -30 * rand" here ?
			pParticle->m_nAnimationSpeedTimer = CGeneral::GetRandomNumber() & 7;
			pParticle->m_fSize = CGeneral::GetRandomNumberInRange(0.3f, 0.8f);
			pParticle->m_vecPosition.z -= CGeneral::GetRandomNumberInRange(-0.1f, 0.1f);
			break;
		
		case PARTICLE_EXPLOSION_LARGE:
			pParticle->m_nColorIntensity -= 30 * (CGeneral::GetRandomNumber() & 1); // mb "+= -30 * rand" here ?
			pParticle->m_nAnimationSpeedTimer = CGeneral::GetRandomNumber() & 7;
			pParticle->m_fSize = CGeneral::GetRandomNumberInRange(0.8f, 1.4f);
			pParticle->m_vecPosition.z -= CGeneral::GetRandomNumberInRange(-0.3f, 0.3f);
			break;
		
		case PARTICLE_WATER_HYDRANT:
			pParticle->m_vecPosition.z += 20.0f * psystem->m_fPositionRandomError * ms_afRandTable[CGeneral::GetRandomNumber() % RAND_TABLE_SIZE];
			break;
		default: break;
	}
	
	if ( fSize != 0.0f )
		pParticle->m_fSize = fSize;
	
	m_pUnusedListHead = pParticle->m_pNext;

	pParticle->m_pNext = psystem->m_pParticles;

	psystem->m_pParticles = pParticle;
	
	return pParticle;
}

#ifdef __PS3__
// patch 87: the drops on the screen (buttzfer's issue: "the water droplets
// that appear on screen from a hydrant or rain do not persist as long as they
// should. They show up briefly and cut off too soon"; the chainsaw's blood
// drops are the same). PARTICLE_WATERDROP (rain, car splash, hydrant water)
// and PARTICLE_BLOODDROP (chainsaw) live 2 s (particle.cfg). The game gives a
// water drop a rest every 6th frame (it stops for one frame and gets 1250 ms
// more), so that one drop on the screen runs down it until it leaves, but the
// counter is one for all the drops: with 6, 12... of them on the screen, it's
// always the same one that rests and the rest of them are cut off at 2 s. The
// blood drops never get one. And the frames are the PS2's 30 fps ones: at 60
// fps the moves made per frame (the camera pushing the drops) are twice as
// fast. Here every drop counts its own rests, on a 30 fps clock (5 ticks
// moving, 1 at rest, as the PS2's frames), up to PS3_DROP_RESTS of them for
// the water (2 s + 6 x 1.25 s = 9.5 s at most) and PS3_BLOOD_RESTS for the
// blood (5.75 s), and at the end it dries up (it gets smaller in its last
// PS3_DROP_DRY_MS) instead of going away all at once.
// (PS3_DROP_RESTS, PS3_DROP_DRY_MS, PS3_BLOOD_DROPS: at the top of the file)

// the per-frame part of a drop's move, on a 30 fps clock. The drop's own
// counter is m_nZRotationTimer (no Z rotation for the drops): bits 0-2 the
// ticks since its last rest, bit 3 resting, bits 4-7 the rests given
static void
PS3_ScreenDropStep(CParticle *particle, CVector &vecMoveStep, bool tick, bool water)
{
	uint16 st = particle->m_nZRotationTimer;
	if ( tick )
	{
		if ( st & 8 )
			st &= ~8;
		else if ( (st & 7) == 4 )
		{
			st = (st & ~7) | 8;
			if ( (st >> 4) < (water ? PS3_DROP_RESTS : PS3_BLOOD_RESTS) )
			{
				st += 16;
				particle->m_nTimeWhenWillBeDestroyed += 1250;
			}
		}
		else
			st++;
	}
	particle->m_nZRotationTimer = st;

	if ( st & 8 )
	{
		vecMoveStep = CVector(0.0f, 0.0f, 0.0f);
		return;
	}

	// the camera pushing the water drops, as the game does it per 30 fps frame
	float k30 = CTimer::GetTimeStep() * (30.0f / 50.0f);	// time step 1 = 1/50 s
	if ( water && k30 > 0.0f && TheCamera.m_CameraAverageSpeed > 0.35f )
	{
		float step30 = vecMoveStep.Magnitude() / k30;
		if ( step30 > 0.5f )
		{
			if ( step30 > 0.4f && step30 < 0.8f )
			{
				vecMoveStep.x += TheCamera.m_CameraAverageSpeed * 1.5f * k30;
				vecMoveStep.y += TheCamera.m_CameraAverageSpeed * 1.5f * k30;
			}
			else
			{
				vecMoveStep.x += CGeneral::GetRandomNumberInRange(0.01f, 0.05f) * k30;
				vecMoveStep.y += CGeneral::GetRandomNumberInRange(0.01f, 0.05f) * k30;
			}
		}
	}
}

// 1 while the drop lives, down to 0 in its last PS3_DROP_DRY_MS
static float
PS3_ScreenDropDry(CParticle *particle)
{
	int32 left = int32(particle->m_nTimeWhenWillBeDestroyed - CTimer::GetTimeInMilliseconds());
	return left >= PS3_DROP_DRY_MS ? 1.0f : left <= 0 ? 0.0f : float(left) / PS3_DROP_DRY_MS;
}
#endif

void CParticle::Update()
{
	if ( CTimer::GetIsPaused() )
		return;

#ifdef __PS3__
	// patch 87: the drops' 30 fps clock (slower than 30 fps: a tick every frame, as before)
	sPS3DropClock += CTimer::GetTimeStep() * (30.0f / 50.0f);
	bool bPS3DropTick = sPS3DropClock >= 1.0f;
	if ( bPS3DropTick )
		sPS3DropClock = Min(sPS3DropClock - 1.0f, 0.99f);
	sPS3BloodDrops = 0;
	for ( CParticle *p = mod_ParticleSystemManager.m_aParticles[PARTICLE_BLOODDROP].m_pParticles; p; p = p->m_pNext )
		sPS3BloodDrops++;
#endif

	CRGBA color(0, 0, 0, 0);
	
	float fFricDeccel50 = pow(0.50f, CTimer::GetTimeStep());
	float fFricDeccel80 = pow(0.80f, CTimer::GetTimeStep());
	float fFricDeccel90 = pow(0.90f, CTimer::GetTimeStep());
	float fFricDeccel95 = pow(0.95f, CTimer::GetTimeStep());
	float fFricDeccel96 = pow(0.96f, CTimer::GetTimeStep());
	float fFricDeccel99 = pow(0.99f, CTimer::GetTimeStep());
	
	CParticleObject::UpdateAll();
	
	// ejaculation at 23:00, 23:15, 23:30, 23:45 
	if ( CClock::ms_nGameClockHours == 23 &&
		(	   CClock::ms_nGameClockMinutes == 0 
			|| CClock::ms_nGameClockMinutes == 15 
			|| CClock::ms_nGameClockMinutes == 30
			|| CClock::ms_nGameClockMinutes == 45 ) )
	{
		AddParticle(PARTICLE_CAR_SPLASH,
					CVector(557.03f, -4.0f, 151.46f),
					CVector(0.0f, 0.0f, 2.5f),
					NULL,
					2.0f,
					CRGBA(255, 255, 255, 255),
					0,
					0,
					1,
					1000);
	}

	for ( int32 i = 0; i < MAX_PARTICLES; i++ )
	{
		tParticleSystemData *psystem = &mod_ParticleSystemManager.m_aParticles[i];
		CParticle *particle = psystem->m_pParticles;
		CParticle *prevParticle = nil;
		bool bRemoveParticle;
		
		if ( particle == nil )
			continue;
				
		for ( ; particle != nil; _Next(particle, prevParticle, psystem, bRemoveParticle) )
		{
			CVector vecWind(0.0f, 0.0f, 0.0f);
			
			bRemoveParticle = false;

			CVector vecMoveStep = particle->m_vecVelocity * CTimer::GetTimeStep();
			CVector vecPos = particle->m_vecPosition;
			
			if ( numWaterDropOnScreen == 0 )
				clearWaterDrop = false;
			
			if ( psystem->m_Type == PARTICLE_WATERDROP )
			{
				if ( CGame::IsInInterior() || clearWaterDrop == true )
				{
					bRemoveParticle = true;
					continue;
				}
				
#ifdef __PS3__
				PS3_ScreenDropStep(particle, vecMoveStep, bPS3DropTick, true);	// patch 87
#else
				static uint8 nWaterDropCount;

				if ( nWaterDropCount == 5 )
				{
					vecMoveStep = CVector(0.0f, 0.0f, 0.0f);
					particle->m_nTimeWhenWillBeDestroyed += 1250;
					nWaterDropCount = 0;
				}
				else
				{
					if ( TheCamera.m_CameraAverageSpeed > 0.35f )
					{
						if ( vecMoveStep.Magnitude() > 0.5f )
						{
							if ( vecMoveStep.Magnitude() > 0.4f && vecMoveStep.Magnitude() < 0.8f )
							{
								vecMoveStep.x += TheCamera.m_CameraAverageSpeed * 1.5f;
								vecMoveStep.y += TheCamera.m_CameraAverageSpeed * 1.5f;
							}
							else if ( vecMoveStep.Magnitude() != 0.0f )
							{
								vecMoveStep.x += CGeneral::GetRandomNumberInRange(0.01f, 0.05f);
								vecMoveStep.y += CGeneral::GetRandomNumberInRange(0.01f, 0.05f);
							}
						}
					}
					
					nWaterDropCount++;
				}
#endif
				
				if ( vecPos.z <= 1.5f )
					vecMoveStep.z = 0.0f;
			}
#ifdef __PS3__
			else if ( psystem->m_Type == PARTICLE_BLOODDROP )
				PS3_ScreenDropStep(particle, vecMoveStep, bPS3DropTick, false);	// patch 87
#endif
			
			if ( psystem->m_Type == PARTICLE_HEATHAZE || psystem->m_Type == PARTICLE_HEATHAZE_IN_DIST )
			{
#ifdef FIX_BUGS
				int32 nSinCosIndex = (int32(DEGTORAD((float)particle->m_nRotation) * float(SIN_COS_TABLE_SIZE) / TWOPI) + SIN_COS_TABLE_SIZE) % SIN_COS_TABLE_SIZE;
#else
				int32 nSinCosIndex = int32(DEGTORAD((float)particle->m_nRotation) * float(SIN_COS_TABLE_SIZE) / TWOPI) % SIN_COS_TABLE_SIZE;
#endif
				vecMoveStep.x = Sin(nSinCosIndex);
				vecMoveStep.y = Sin(nSinCosIndex);
				
				if ( psystem->m_Type == PARTICLE_HEATHAZE_IN_DIST )
					particle->m_nRotation = int16((float)particle->m_nRotation + 0.75f);
				else
					particle->m_nRotation = int16((float)particle->m_nRotation + 1.0f);
			}
			
			if ( psystem->m_Type == PARTICLE_BEASTIE )
			{
#ifdef FIX_BUGS
				int32 nSinCosIndex = (int32(DEGTORAD((float)particle->m_nRotation) * float(SIN_COS_TABLE_SIZE) / TWOPI) + SIN_COS_TABLE_SIZE) % SIN_COS_TABLE_SIZE;
#else
				int32 nSinCosIndex = int32(DEGTORAD((float)particle->m_nRotation) * float(SIN_COS_TABLE_SIZE) / TWOPI) % SIN_COS_TABLE_SIZE;
#endif				
				particle->m_vecVelocity.x = 0.50f * Cos(nSinCosIndex);
				particle->m_vecVelocity.y = Cos(nSinCosIndex);
				particle->m_vecVelocity.z = 0.25f * Sin(nSinCosIndex);
				
				if ( particle->m_vecVelocity.Magnitude() > 2.0f
						|| vecPos.z > 40.0f
						|| (TheCamera.GetPosition() - vecPos).Magnitude() < 60.0f
					)
				{
					bRemoveParticle = true;
					continue;
				}
			}
			
			vecPos += vecMoveStep;
			
			if ( psystem->m_Type == PARTICLE_FIREBALL )
			{
				  AddParticle(PARTICLE_HEATHAZE, particle->m_vecPosition, CVector(0.0f, 0.0f, 0.0f),
					nil, particle->m_fSize * 5.0f);
			}
			
			if ( psystem->m_Type == PARTICLE_GUNSMOKE2 )
			{
				if ( CTimer::GetFrameCounter() & 10 )
				{
#ifdef FIX_BUGS
					if ( FindPlayerPed() && FindPlayerPed()->GetWeapon()->m_eWeaponType == WEAPONTYPE_MINIGUN )
#else
					if ( FindPlayerPed()->GetWeapon()->m_eWeaponType == WEAPONTYPE_MINIGUN )
#endif
					{
						AddParticle(PARTICLE_HEATHAZE, particle->m_vecPosition, CVector(0.0f, 0.0f, 0.0f));
					}
				}
			}
			
			if ( CWeather::Wind > 0.0f )
			{
				if ( vecMoveStep.Magnitude() != 0.0f )
				{
					vecWind.x = CGeneral::GetRandomNumberInRange(0.75f, 1.25f) * -CWeather::Wind;
					vecWind.y = CGeneral::GetRandomNumberInRange(0.75f, 1.25f) * -CWeather::Wind;
					vecWind *= PARTICLE_WIND_TEST_SCALE * psystem->m_fWindFactor * CTimer::GetTimeStep();
					particle->m_vecVelocity += vecWind;
				}
			}
			
			if ( psystem->m_Type == PARTICLE_RAINDROP
				|| psystem->m_Type == PARTICLE_RAINDROP_SMALL
				|| psystem->m_Type == PARTICLE_RAIN_SPLASH
				|| psystem->m_Type == PARTICLE_RAIN_SPLASH_BIGGROW
				|| psystem->m_Type == PARTICLE_CAR_SPLASH
				|| psystem->m_Type == PARTICLE_BOAT_SPLASH
				|| psystem->m_Type == PARTICLE_RAINDROP_2D )
			{
				int32 nMaxDrops = int32(6.0f * TheCamera.m_CameraAverageSpeed + 1.0f);
				float fDistToCam = 0.0f;
				
				if ( psystem->m_Type == PARTICLE_BOAT_SPLASH || psystem->m_Type == PARTICLE_CAR_SPLASH )
				{
					if ( vecPos.z + particle->m_fSize < 5.0f )
					{
						bRemoveParticle = true;
						continue;
					}
					
					switch ( TheCamera.GetLookDirection() )
					{
						case LOOKING_LEFT:
						case LOOKING_RIGHT:
						case LOOKING_FORWARD:
							nMaxDrops /= 2;
							break;
						
						default:
							nMaxDrops = 0;
							break;
					}
					
					fDistToCam = (TheCamera.GetPosition() - vecPos).Magnitude();
				}

				if ( numWaterDropOnScreen < nMaxDrops && numWaterDropOnScreen < 63
					&& fDistToCam < 10.0f
					&& clearWaterDrop == false
					&& !CGame::IsInInterior() )
				{
					CVector vecWaterdropTarget
					(
						CGeneral::GetRandomNumberInRange(-0.25f, 0.25f),
						CGeneral::GetRandomNumberInRange(0.1f, 0.75f),
						-0.01f
					);
					
					CVector vecWaterdropPos;
					
					if ( TheCamera.m_CameraAverageSpeed < 0.35f )
						vecWaterdropPos.x = (float)CGeneral::GetRandomNumberInRange(50, int32(SCREEN_WIDTH) - 50);
					else
						vecWaterdropPos.x = (float)CGeneral::GetRandomNumberInRange(200, int32(SCREEN_WIDTH) - 200);
					
					if ( psystem->m_Type == PARTICLE_BOAT_SPLASH || psystem->m_Type == PARTICLE_CAR_SPLASH )
						vecWaterdropPos.y = (float)CGeneral::GetRandomNumberInRange(SCREEN_HEIGHT / 2, SCREEN_HEIGHT);
					else
					{
						if ( TheCamera.m_CameraAverageSpeed < 0.35f )
							vecWaterdropPos.y  = (float)CGeneral::GetRandomNumberInRange(0, int32(SCREEN_HEIGHT));
						else
							vecWaterdropPos.y  = (float)CGeneral::GetRandomNumberInRange(150, int32(SCREEN_HEIGHT) - 200);
					}
					
					vecWaterdropPos.z = 2.0f;

					if ( AddParticle(PARTICLE_WATERDROP,
										vecWaterdropPos,
										vecWaterdropTarget,
										nil,
										CGeneral::GetRandomNumberInRange(0.1f, 0.15f),
										0,
										0,
										CGeneral::GetRandomNumber() & 1,
										0) != nil )
					{
						numWaterDropOnScreen++;
					}
				}
			}
			
			if (  CTimer::GetTimeInMilliseconds() > particle->m_nTimeWhenWillBeDestroyed || particle->m_nAlpha == 0 )
			{
				bRemoveParticle = true;
				continue;
			}

			if ( particle->m_nTimeWhenColorWillBeChanged != 0 )
			{
				if ( particle->m_nTimeWhenColorWillBeChanged > CTimer::GetTimeInMilliseconds() )
				{
					float colorMul = 1.0f - float(particle->m_nTimeWhenColorWillBeChanged - CTimer::GetTimeInMilliseconds()) / float(psystem->m_ColorFadeTime);
				
					particle->m_Color.red = clamp(
						psystem->m_RenderColouring.red + int32(float(psystem->m_FadeDestinationColor.red - psystem->m_RenderColouring.red) * colorMul),
						0, 255);
					
					particle->m_Color.green = clamp(
						psystem->m_RenderColouring.green + int32(float(psystem->m_FadeDestinationColor.green - psystem->m_RenderColouring.green) * colorMul),
						0, 255);
						
					particle->m_Color.blue = clamp(
						psystem->m_RenderColouring.blue + int32(float(psystem->m_FadeDestinationColor.blue - psystem->m_RenderColouring.blue) * colorMul),
						0, 255);
				}
				else
					RwRGBAAssign(&particle->m_Color, psystem->m_FadeDestinationColor);
			}
			
			if ( psystem->Flags & CLIPOUT2D )
			{
				if ( particle->m_vecPosition.x < -10.0f || particle->m_vecPosition.x > SCREEN_WIDTH + 10.0f
					|| particle->m_vecPosition.y < -10.0f || particle->m_vecPosition.y > SCREEN_HEIGHT + 10.0f )
				{
					bRemoveParticle = true;
					continue;
				}
			}
			
			if ( !(psystem->Flags & SCREEN_TRAIL) )
			{
				float size;

				if ( particle->m_fExpansionRate > 0.0f )
				{
					float speed = Max(vecWind.Magnitude(), vecMoveStep.Magnitude());
					
					if ( psystem->m_Type == PARTICLE_EXHAUST_FUMES || psystem->m_Type == PARTICLE_ENGINE_STEAM )
						speed *= 2.0f;
					
					if ( ( psystem->m_Type == PARTICLE_BOAT_SPLASH || psystem->m_Type == PARTICLE_CAR_SPLASH )
							&& particle->m_fSize > 1.2f )
					{
						size = particle->m_fSize - (1.0f + speed) * particle->m_fExpansionRate;
						particle->m_vecVelocity.z -= 0.15f;
					}
					else
						size = particle->m_fSize + (1.0f + speed) * particle->m_fExpansionRate;
				}
				else
					size = particle->m_fSize + particle->m_fExpansionRate;
				
				if ( psystem->m_Type == PARTICLE_WATERDROP )
					size = (size - Abs(vecMoveStep.x) * 0.000150000007f) + (Abs(vecMoveStep.z) * 0.0500000007f); //TODO:
				
				if ( size < 0.0f )
				{
					bRemoveParticle = true;
					continue;
				}
				
				particle->m_fSize = size;
			}
			
			switch ( psystem->m_nFrictionDecceleration )
			{
				case 50:
					particle->m_vecVelocity *= fFricDeccel50;
					break;
		
				case 80:
					particle->m_vecVelocity *= fFricDeccel80;
					break;
		
				case 90:
					particle->m_vecVelocity *= fFricDeccel90;
					break;
		
				case 95:
					particle->m_vecVelocity *= fFricDeccel95;
					break;
		
				case 96:
					particle->m_vecVelocity *= fFricDeccel96;
					break;
		
				case 99:
					particle->m_vecVelocity *= fFricDeccel99;
					break;				
			}
			
			if ( psystem->m_fGravitationalAcceleration > 0.0f )
			{
				if ( -50.0f * psystem->m_fGravitationalAcceleration < particle->m_vecVelocity.z )
					particle->m_vecVelocity.z -= psystem->m_fGravitationalAcceleration * CTimer::GetTimeStep();

				if ( psystem->Flags & ZCHECK_FIRST )
				{
					if ( particle->m_vecPosition.z < particle->m_fZGround )
					{
						switch ( psystem->m_Type )
						{
							case PARTICLE_RAINDROP:
							case PARTICLE_RAINDROP_SMALL:
								{
									bRemoveParticle = true;
									
									if ( CGeneral::GetRandomNumber() & 1 )
									{
										AddParticle(PARTICLE_RAIN_SPLASH,
													CVector
													(
														particle->m_vecPosition.x,
														particle->m_vecPosition.y,
														0.05f + particle->m_fZGround
													),
													CVector(0.0f, 0.0f, 0.0f), nil, 0.0f, 0, 0, 0, 0);
									}
									else
									{
										AddParticle(PARTICLE_RAIN_SPLASHUP,
													CVector
													(
														particle->m_vecPosition.x,
														particle->m_vecPosition.y,
														0.05f + particle->m_fZGround
													),
													CVector(0.0f, 0.0f, 0.0f), nil, 0.0f, 0, 0, 0, 0);
									}
									
									continue;
								}
								break;

							case PARTICLE_WHEEL_WATER:
								{
									bRemoveParticle = true;
									
									int32 randVal = CGeneral::GetRandomNumber();
									
									if ( randVal & 1 )
									{
										if ( (randVal % 5) == 0 )
										{
											AddParticle(PARTICLE_RAIN_SPLASH,
														CVector
														(
															particle->m_vecPosition.x,
															particle->m_vecPosition.y,
															0.05f + particle->m_fZGround
														),
														CVector(0.0f, 0.0f, 0.0f), nil, 0.0f, 0, 0, 0, 0);
										}
										else
										{
											AddParticle(PARTICLE_RAIN_SPLASHUP,
														CVector
														(
															particle->m_vecPosition.x,
															particle->m_vecPosition.y,
															0.05f + particle->m_fZGround
														),
														CVector(0.0f, 0.0f, 0.0f), nil, 0.0f, 0, 0, 0, 0);
										}
										
									}
									continue;
								}
								break;

							case PARTICLE_BLOOD:
							case PARTICLE_BLOOD_SMALL:
								{
									bRemoveParticle = true;
									
									CVector vecPosn = particle->m_vecPosition;
									vecPosn.z += 1.0f;
									
									Randomizer++;
									int32 randVal = int32(Randomizer & 7);
									
									if ( randVal == 5 )
									{
										CShadows::AddPermanentShadow(SHADOWTYPE_DARK, gpBloodPoolTex, &vecPosn,
												0.1f, 0.0f, 0.0f, -0.1f,
												255,
												255, 0, 0,
												4.0f, (CGeneral::GetRandomNumber() & 4095) + 2000, 1.0f);
									}
									else if ( randVal == 2 )
									{
										CShadows::AddPermanentShadow(SHADOWTYPE_DARK, gpBloodPoolTex, &vecPosn,
												0.2f, 0.0f, 0.0f, -0.2f,
												255,
												255, 0, 0,
												4.0f, (CGeneral::GetRandomNumber() & 4095) + 8000, 1.0f);
									}
									continue;
								}
								break;
							default: break;
						}
					}
				}
				else if ( psystem->Flags & ZCHECK_STEP )
				{
					CColPoint point;
					CEntity *entity;

					if ( CWorld::ProcessVerticalLine(particle->m_vecPosition, vecPos.z, point, entity, 
														true, true, false, false, true, false, nil) )
					{
						if ( vecPos.z <= point.point.z )
						{
							vecPos.z = point.point.z;
							if ( psystem->m_Type == PARTICLE_DEBRIS2 )
							{
								particle->m_vecVelocity.x *= 0.8f;
								particle->m_vecVelocity.y *= 0.8f;
								particle->m_vecVelocity.z *= -0.4f;
								if ( particle->m_vecVelocity.z < 0.005f )
									particle->m_vecVelocity.z = 0.0f;
							}
						}
					}
				}
				else if ( psystem->Flags & ZCHECK_BUMP )
				{
					if ( particle->m_vecPosition.z < particle->m_fZGround )
					{
						switch ( psystem->m_Type )
						{
							case PARTICLE_GUNSHELL_FIRST:
							case PARTICLE_GUNSHELL:
								{
									bRemoveParticle = true;

									AddParticle(PARTICLE_GUNSHELL_BUMP1,
												CVector
												(
													particle->m_vecPosition.x,
													particle->m_vecPosition.y,
													0.05f + particle->m_fZGround
												),
												CVector
												(
													CGeneral::GetRandomNumberInRange(-0.02f, 0.02f),
													CGeneral::GetRandomNumberInRange(-0.02f, 0.02f),
													CGeneral::GetRandomNumberInRange(0.05f, 0.1f)
												),
												nil,
												particle->m_fSize, color, particle->m_nRotationStep, 0, 0, 0);
									
									PlayOneShotScriptObject(SCRIPT_SOUND_GUNSHELL_DROP, particle->m_vecPosition);
								}
								break;
							
							case PARTICLE_GUNSHELL_BUMP1:
								{
									bRemoveParticle = true;
									
									AddParticle(PARTICLE_GUNSHELL_BUMP2,
												CVector
												(
													particle->m_vecPosition.x,
													particle->m_vecPosition.y,
													0.05f + particle->m_fZGround
												),
												CVector(0.0f, 0.0f, CGeneral::GetRandomNumberInRange(0.03f, 0.06f)),
												nil,
												particle->m_fSize, color, 0, 0, 0, 0);
									
									PlayOneShotScriptObject(SCRIPT_SOUND_GUNSHELL_DROP_SOFT, particle->m_vecPosition);
								}
								break;
								
							case PARTICLE_GUNSHELL_BUMP2:
								{
									bRemoveParticle = true;
									continue;
								}
								break;
							default: break;
						}
					}
				}
			}
			else
			{
				if ( psystem->m_fGravitationalAcceleration < 0.0f )
				{
					if ( -5.0f * psystem->m_fGravitationalAcceleration > particle->m_vecVelocity.z )
						particle->m_vecVelocity.z -= psystem->m_fGravitationalAcceleration * CTimer::GetTimeStep();
				}
				else
				{
					if ( psystem->Flags & ZCHECK_STEP )
					{
						CColPoint point;
						CEntity *entity;
			
						if ( CWorld::ProcessVerticalLine(particle->m_vecPosition, vecPos.z, point, entity,
														true, false, false, false, true, false, nil) )
						{
							if ( vecPos.z <= point.point.z )
							{
								vecPos.z = point.point.z;
								if ( psystem->m_Type == PARTICLE_HELI_ATTACK )
								{
									bRemoveParticle = true;
									AddParticle(PARTICLE_STEAM, vecPos, CVector(0.0f, 0.0f, 0.05f), nil, 0.2f, 0, 0, 0, 0);
									continue;
								}
							}
						}
					}
				}
			}

			if ( particle->m_nFadeToBlackTimer != 0 )
			{
				particle->m_nColorIntensity = clamp(particle->m_nColorIntensity - particle->m_nFadeToBlackTimer,
														0, 255);
			}

			if ( particle->m_nFadeAlphaTimer != 0 )
			{
				particle->m_nAlpha = clamp(particle->m_nAlpha - particle->m_nFadeAlphaTimer,
														0, 255);
				if ( particle->m_nAlpha == 0 )
				{
					bRemoveParticle = true;
					continue;
				}
			}
			
			if ( psystem->m_nZRotationAngleChangeAmount != 0 )
			{
				if ( particle->m_nZRotationTimer >= psystem->m_nZRotationChangeTime )
				{
					particle->m_nZRotationTimer = 0;
					particle->m_nCurrentZRotation += psystem->m_nZRotationAngleChangeAmount;
				}
				else
					++particle->m_nZRotationTimer;
			}
			
			if ( psystem->m_fZRadiusChangeAmount != 0.0f )
			{
				if ( particle->m_nZRadiusTimer >= psystem->m_nZRadiusChangeTime )
				{
					particle->m_nZRadiusTimer = 0;
					particle->m_fCurrentZRadius += psystem->m_fZRadiusChangeAmount;
				}
				else
					++particle->m_nZRadiusTimer;
			}

			if ( psystem->m_nAnimationSpeed != 0 )
			{
				if ( particle->m_nAnimationSpeedTimer > psystem->m_nAnimationSpeed )
				{
					particle->m_nAnimationSpeedTimer = 0;
					
					if ( ++particle->m_nCurrentFrame > psystem->m_nFinalAnimationFrame )
					{
						if ( psystem->Flags & CYCLE_ANIM )
							particle->m_nCurrentFrame = psystem->m_nStartAnimationFrame;
						else
							--particle->m_nCurrentFrame;
					}	
				}
				else
					++particle->m_nAnimationSpeedTimer;
			}
			
			if ( particle->m_nRotationStep != 0 )
#ifdef FIX_BUGS
				particle->m_nRotation = CGeneral::LimitAngle(particle->m_nRotation + particle->m_nRotationStep);
#else
				particle->m_nRotation += particle->m_nRotationStep;
#endif
			
			if ( particle->m_fCurrentZRadius != 0.0f )
			{
				int32 nSinCosIndex = particle->m_nCurrentZRotation % SIN_COS_TABLE_SIZE;
				
				float fX = (Cos(nSinCosIndex) - Sin(nSinCosIndex)) * particle->m_fCurrentZRadius;
				
				float fY = (Sin(nSinCosIndex) + Cos(nSinCosIndex)) * particle->m_fCurrentZRadius;

				vecPos -= particle->m_vecParticleMovementOffset;

				vecPos += CVector(fX, fY, 0.0f);
				
				particle->m_vecParticleMovementOffset = CVector(fX, fY, 0.0f);
			}
			
			particle->m_vecPosition = vecPos;
		}
	}
}

#ifdef __PS3__
// How many rasters the frame table of a particle type has. particle.cfg can
// animate a type past the end of its table (e.g. 5 smoke frames for a
// 4-raster table): on PC the read hit the next global, which the original
// layout made another raster table. GCC lays globals out its own way, so
// the read can get any pointer (or a texture, not a raster) and the RSX code
// dies on it: Treacherous Swine, patch 21 log, in CParticle::Render. The
// frame is kept inside the table instead ([particle] line, once per type).
static int
PS3_ParticleFrameCount(RwRaster **frames)
{
	static const struct { RwRaster **table; int count; } tables[] = {
		{ gpSmokeRaster, MAX_SMOKE_FILES }, { gpRubberRaster, MAX_RUBBER_FILES },
		{ gpRainSplashRaster, MAX_RAINSPLASH_FILES }, { gpWatersprayRaster, MAX_WATERSPRAY_FILES },
		{ gpExplosionMediumRaster, MAX_EXPLOSIONMEDIUM_FILES }, { gpGunFlashRaster, MAX_GUNFLASH_FILES },
		{ gpRainSplashupRaster, MAX_RAINSPLASHUP_FILES }, { gpBirdfrontRaster, MAX_BIRDFRONT_FILES },
		{ gpBoatRaster, MAX_BOAT_FILES }, { gpCarDebrisRaster, MAX_CARDEBRIS_FILES },
		{ gpCarSplashRaster, MAX_CARSPLASH_FILES }, { gpRainDripRaster, MAX_RAINDRIP_FILES },
		{ gpRainDripDarkRaster, MAX_RAINDRIP_FILES },
	};
	for (int i = 0; i < ARRAY_SIZE(tables); i++)
		if (tables[i].table == frames)
			return tables[i].count;
	return 1;	// &gpFlame1Raster and the other single rasters
}

static void
PS3_ParticleFrameClamp(CParticle *particle, int type, RwRaster **frames)
{
	static uint8 logged[MAX_PARTICLES];
	int count = PS3_ParticleFrameCount(frames);
	if (particle->m_nCurrentFrame < count)
		return;
	if (!logged[type]) {
		logged[type] = 1;
		PS3_Logf("[particle] type %d at frame %d of a %d-raster table: kept at the last one",
		         type, particle->m_nCurrentFrame, count);
	}
	particle->m_nCurrentFrame = count - 1;
}

// patch 43: what the particles cost the RSX. Their quads are alpha blended
// over the picture, and a puff of smoke next to the camera covers it all:
// with the tank firing (patch 41-42 stress logs) the RSX took up to 12 ms
// a frame. Every quad's area on screen (clipped) is counted per type for
// the [fx] log line. gPS3FxLimit 1: quads bigger than 0.7 of the screen's
// height fade out up to 1.4, as the PC game already does with car smoke
// (fParticleScaleLimit), so they don't pop. On since patch 44 (patch 43's
// stress log: nothing looked different, the tank's fill 3.8 -> 0.05 screens).
int32 gPS3FxLimit = 1;
float gPS3FxFillAB;		// pixels drawn, read and reset by the autotest
static double sFxFill[MAX_PARTICLES], sFxFaded;
static uint32 sFxCount[MAX_PARTICLES];

// false: not drawn; *alpha may come back lower (the fade)
static bool
PS3_FxQuad(int type, float hw, float hh, float x, float y, uint8 *alpha)
{
	if (hw < 0.0f) hw = -hw;
	if (hh < 0.0f) hh = -hh;
	float x0 = Max(x - hw, 0.0f), x1 = Min(x + hw, (float)SCREEN_WIDTH);
	float y0 = Max(y - hh, 0.0f), y1 = Min(y + hh, (float)SCREEN_HEIGHT);
	float area = x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) : 0.0f;
	if (gPS3FxLimit && type != PARTICLE_WATER_HYDRANT) {
		float r = Max(hw, hh) / SCREEN_HEIGHT;	// half the quad over the screen
		if (r > 0.35f) {
			float k = (0.7f - r) / 0.35f;
			int a = k > 0.0f ? (int)(*alpha * k) : 0;
			if (a < 4) {
				sFxFaded += area;
				return false;
			}
			*alpha = (uint8)a;
		}
	}
	sFxFill[type] += area;
	sFxCount[type]++;
	gPS3FxFillAB += area;
	return true;
}

// "[fx] particles per frame: ..." with the [prof] lines
void
PS3_FxReport(unsigned int frames)
{
	if (frames == 0)
		return;
	double screen = (double)SCREEN_WIDTH * SCREEN_HEIGHT, total = 0.0;
	uint32 quads = 0;
	int order[MAX_PARTICLES];
	int n = 0;
	for (int i = 0; i < MAX_PARTICLES; i++) {
		total += sFxFill[i];
		quads += sFxCount[i];
		if (sFxFill[i] > 0.0)
			order[n++] = i;
	}
	if (quads == 0 && sFxFaded == 0.0)
		return;
	for (int a = 0; a < n; a++)	// biggest fill first (few types)
		for (int b = a + 1; b < n; b++)
			if (sFxFill[order[b]] > sFxFill[order[a]]) { int t = order[a]; order[a] = order[b]; order[b] = t; }
	char line[600];
	int len = snprintf(line, sizeof(line), "[fx]   particles per frame: %.0f quads, %.2f screens of fill%s, %.2f faded out |",
	                   (double)quads / frames, total / screen / frames, gPS3FxLimit ? "" : " (limit off)",
	                   sFxFaded / screen / frames);
	for (int k = 0; k < n && k < 6 && len < (int)sizeof(line) - 60; k++) {
		int i = order[k];
		len += snprintf(line + len, sizeof(line) - len, " %d %s %.2f (x%.0f)", i,
		                mod_ParticleSystemManager.m_aParticles[i].m_aName,
		                sFxFill[i] / screen / frames, (double)sFxCount[i] / frames);
	}
	PS3_Log(line);
	memset(sFxFill, 0, sizeof(sFxFill));
	memset(sFxCount, 0, sizeof(sFxCount));
	sFxFaded = 0.0;
}
#endif

#ifdef __PS3__
extern int32 gPS3FxOff;
#endif
// One particle system drawn by the PPU, as CParticle::Render always did (from
// res-PS3 patch 50: out of it, for the systems the SPU doesn't take and for
// when it's off)
static void
RenderParticleSystem(int32 i, uint32 &flags, RwRaster *&prevFrame)
{
	tParticleSystemData *psystem = &mod_ParticleSystemManager.m_aParticles[i];
	bool particleBanned = false;
	CParticle *particle = psystem->m_pParticles;
	
	RwRaster **frames = psystem->m_ppRaster;
	tParticleType type = psystem->m_Type;

	if ( type == PARTICLE_ENGINE_SMOKE
		|| type == PARTICLE_ENGINE_SMOKE2
		|| type == PARTICLE_ENGINE_STEAM
		|| type == PARTICLE_CARFLAME_SMOKE
		|| type == PARTICLE_RUBBER_SMOKE
		|| type == PARTICLE_BURNINGRUBBER_SMOKE
		|| type == PARTICLE_EXHAUST_FUMES
		|| type == PARTICLE_CARCOLLISION_DUST )
	{
		particleBanned = true;
	}

	if ( particle )
	{
		if ( (flags & DRAW_OPAQUE) != (psystem->Flags & DRAW_OPAQUE)
			|| (flags & DRAW_DARK) != (psystem->Flags & DRAW_DARK) )
		{
			CSprite::FlushSpriteBuffer();
			
			if ( psystem->Flags & DRAW_OPAQUE )
			{
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDINVSRCALPHA);
			}
			else
			{
				if ( psystem->Flags & DRAW_DARK )
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
				else
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDONE);

				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDONE);
			}

			flags = psystem->Flags;
		}
		
		if ( frames != nil )
		{
			RwRaster *curFrame = *frames;
			if ( curFrame != prevFrame )
			{
				CSprite::FlushSpriteBuffer();
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void *)curFrame);
				prevFrame = curFrame;
			}
		}
	}
	
	while ( particle != nil )
	{
		bool canDraw = true;

		if ( particle->m_nAlpha == 0 )
			canDraw = false;
		
		if ( canDraw && psystem->m_nFinalAnimationFrame != 0 && frames != nil )
		{
#ifdef __PS3__
			PS3_ParticleFrameClamp(particle, i, frames);
#endif
			RwRaster *curFrame = frames[particle->m_nCurrentFrame];
			if ( prevFrame != curFrame )
			{
				CSprite::FlushSpriteBuffer();
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void *)curFrame);
				prevFrame = curFrame;
			}
		}
		
		if ( canDraw && psystem->Flags & DRAWTOP2D )
		{
			float screenZ = (particle->m_vecPosition.z - CDraw::GetNearClipZ())
				* (CSprite::GetFarScreenZ() - CSprite::GetNearScreenZ())
				* CDraw::GetFarClipZ()
				/ ( (CDraw::GetFarClipZ() - CDraw::GetNearClipZ()) * particle->m_vecPosition.z )
				+ CSprite::GetNearScreenZ();
			
			float stretchTexW;
			float stretchTexH;
				
			if ( i == PARTICLE_RAINDROP || i == PARTICLE_RAINDROP_SMALL || i == PARTICLE_RAINDROP_2D )
			{
				stretchTexW = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x * (float)particle->m_nCurrentFrame + 63.0f;
				stretchTexH = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y * (float)particle->m_nCurrentFrame + 63.0f;
			}
			else
			{
				stretchTexW = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x + 63.0f;
				stretchTexH = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y + 63.0f;
			}

			
			if ( i == PARTICLE_WATERDROP )
			{
				int32 timeLeft = (particle->m_nTimeWhenWillBeDestroyed - CTimer::GetTimeInMilliseconds()) / particle->m_nTimeWhenWillBeDestroyed;
				
				stretchTexH += (1.0f - (float)timeLeft ) * psystem->m_vecTextureStretch.y;
				
#ifdef __PS3__
				float ps3Dry = PS3_ScreenDropDry(particle);	// patch 87
				stretchTexW *= ps3Dry;
				stretchTexH *= ps3Dry;
#endif
				RwRect rect;
				
				rect.x = int32(particle->m_vecPosition.x - SCREEN_STRETCH_X(particle->m_fSize * stretchTexW));
				rect.y = int32(particle->m_vecPosition.y - SCREEN_STRETCH_Y(particle->m_fSize * stretchTexH));
				rect.w = int32(particle->m_vecPosition.x + SCREEN_STRETCH_X(particle->m_fSize * stretchTexW));
				rect.h = int32(particle->m_vecPosition.y + SCREEN_STRETCH_Y(particle->m_fSize * stretchTexH));
				
				FxType fxtype;

				if ( particle->m_nCurrentFrame != 0 )
					fxtype = FXTYPE_WATER2;
				else
					fxtype = FXTYPE_WATER1;
	
				CMBlur::AddRenderFx(Scene.camera, &rect, screenZ, fxtype);
				
				canDraw = false;
			}
			
			if ( i == PARTICLE_BLOODDROP )
			{
				int32 timeLeft = (particle->m_nTimeWhenWillBeDestroyed - CTimer::GetTimeInMilliseconds()) / particle->m_nTimeWhenWillBeDestroyed;
				
				stretchTexH += (1.0f + (float)timeLeft) * psystem->m_vecTextureStretch.y;
				stretchTexW += (1.0f - (float)timeLeft) * psystem->m_vecTextureStretch.x;

#ifdef __PS3__
				float ps3Dry = PS3_ScreenDropDry(particle);	// patch 87
				stretchTexW *= ps3Dry;
				stretchTexH *= ps3Dry;
#endif
				RwRect rect;

				rect.x = int32(particle->m_vecPosition.x - SCREEN_STRETCH_X(particle->m_fSize * stretchTexW));
				rect.y = int32(particle->m_vecPosition.y - SCREEN_STRETCH_Y(particle->m_fSize * stretchTexH));
				rect.w = int32(particle->m_vecPosition.x + SCREEN_STRETCH_X(particle->m_fSize * stretchTexW));
				rect.h = int32(particle->m_vecPosition.y + SCREEN_STRETCH_Y(particle->m_fSize * stretchTexH));
				
				FxType fxtype;
				
				if ( particle->m_nCurrentFrame )
					fxtype = FXTYPE_BLOOD2;
				else
					fxtype = FXTYPE_BLOOD1;
				
				CMBlur::AddRenderFx(Scene.camera, &rect, screenZ, fxtype);
				
				canDraw = false;
			}
			
			if ( i == PARTICLE_HEATHAZE_IN_DIST )
			{
				RwRect rect;
				
				rect.x = int32(particle->m_vecPosition.x - SCREEN_STRETCH_X(particle->m_fSize * stretchTexW));
				rect.y = int32(particle->m_vecPosition.y - SCREEN_STRETCH_Y(particle->m_fSize * stretchTexH * 0.15f));
				rect.w = int32(particle->m_vecPosition.x + SCREEN_STRETCH_X(particle->m_fSize * stretchTexW));
				rect.h = int32(particle->m_vecPosition.y + SCREEN_STRETCH_Y(particle->m_fSize * stretchTexH * 0.15f));
				
				CMBlur::AddRenderFx(Scene.camera, &rect, screenZ, FXTYPE_HEATHAZE);
				
				canDraw = false;
			}
			
			if ( i == PARTICLE_HEATHAZE )
			{
				RwRect rect;
				
				switch ( TheCamera.GetLookDirection() )
				{
					case LOOKING_LEFT:
						rect.x = int32(particle->m_vecPosition.x - SCREEN_STRETCH_X(particle->m_fSize * psystem->m_vecTextureStretch.x * 2.0f));
						rect.y = int32(particle->m_vecPosition.y - SCREEN_STRETCH_Y(particle->m_fSize * psystem->m_vecTextureStretch.y));
						rect.w = int32(particle->m_vecPosition.x - SCREEN_STRETCH_X(particle->m_fSize * psystem->m_vecTextureStretch.x));
						rect.h = int32(particle->m_vecPosition.y + SCREEN_STRETCH_Y(particle->m_fSize * psystem->m_vecTextureStretch.y));
				
						break;

					case LOOKING_RIGHT:
						rect.x = int32(particle->m_vecPosition.x + SCREEN_STRETCH_X(particle->m_fSize * psystem->m_vecTextureStretch.x));
						rect.y = int32(particle->m_vecPosition.y - SCREEN_STRETCH_Y(particle->m_fSize * psystem->m_vecTextureStretch.y));
						rect.w = int32(particle->m_vecPosition.x + SCREEN_STRETCH_X(particle->m_fSize * psystem->m_vecTextureStretch.x * 4.0f));
						rect.h = int32(particle->m_vecPosition.y + SCREEN_STRETCH_Y(particle->m_fSize * psystem->m_vecTextureStretch.y));
				
						break;

					default:
						rect.x = int32(particle->m_vecPosition.x - SCREEN_STRETCH_X(particle->m_fSize * psystem->m_vecTextureStretch.x));
						rect.y = int32(particle->m_vecPosition.y - SCREEN_STRETCH_Y(particle->m_fSize * psystem->m_vecTextureStretch.y));
						rect.w = int32(particle->m_vecPosition.x + SCREEN_STRETCH_X(particle->m_fSize * psystem->m_vecTextureStretch.x));
						rect.h = int32(particle->m_vecPosition.y + SCREEN_STRETCH_Y(particle->m_fSize * psystem->m_vecTextureStretch.y));
				
						break;
				}
													 
				CMBlur::AddRenderFx(Scene.camera, &rect, screenZ, FXTYPE_HEATHAZE);
				
				canDraw = false;
			}
			
			if ( canDraw )
			{
				if ( particle->m_nRotation != 0 )
				{
					CSprite::RenderBufferedOneXLUSprite2D_Rotate_Dimension(
							particle->m_vecPosition.x,
							particle->m_vecPosition.y,
							particle->m_fSize * stretchTexW,
							particle->m_fSize * stretchTexH,
							particle->m_Color,
							particle->m_nColorIntensity,
							DEGTORAD((float)particle->m_nRotation),
							particle->m_nAlpha);
				}
				else
				{
					CSprite::RenderBufferedOneXLUSprite2D(
							particle->m_vecPosition.x,
							particle->m_vecPosition.y,
							particle->m_fSize * stretchTexW,
							particle->m_fSize * stretchTexH,
							particle->m_Color,
							particle->m_nColorIntensity,
							particle->m_nAlpha);
				}
			}
			
			canDraw = false;
		}
		
		if ( canDraw )
		{
			CVector coors;
			float w;
			float h;

			if ( CSprite::CalcScreenCoors(particle->m_vecPosition, &coors, &w, &h, true) )
			{
				
				if ( i == PARTICLE_ENGINE_STEAM
					|| i == PARTICLE_ENGINE_SMOKE
					|| i == PARTICLE_ENGINE_SMOKE2
					|| i == PARTICLE_CARFLAME_SMOKE
					|| i == PARTICLE_CARCOLLISION_DUST
					|| i == PARTICLE_EXHAUST_FUMES
					|| i == PARTICLE_RUBBER_SMOKE
					|| i == PARTICLE_BURNINGRUBBER_SMOKE )
				{
					switch ( TheCamera.GetLookDirection() )
					{
						case LOOKING_LEFT:
						case LOOKING_RIGHT:
							w += CGeneral::GetRandomNumberInRange(1.0f, 7.5f) * psystem->m_vecTextureStretch.x;
							h += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y;
							break;

						default:
							w += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x;
							h += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y;
							break;
					}
				}
				else if ( i == PARTICLE_WATER_HYDRANT )
				{
					int32 timeLeft = (particle->m_nTimeWhenWillBeDestroyed - CTimer::GetTimeInMilliseconds()) / particle->m_nTimeWhenWillBeDestroyed;
				
					w += (1.0f - (float)timeLeft) * psystem->m_vecTextureStretch.x;
					h += (1.0f - (float)timeLeft) * psystem->m_vecTextureStretch.y;
				}
				else if ( i == PARTICLE_FLYERS )
				{
					w += psystem->m_vecTextureStretch.x;
					h += psystem->m_vecTextureStretch.y;
					
					w = Max(w, 12.0f);
					h = Max(h, 12.0f);
				}
				else
				{
					w += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x;
					h += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y;
				}
				
#ifdef __PS3__
				uint8 ps3Alpha = particle->m_nAlpha;
				if ( (i == PARTICLE_WATER_HYDRANT
						|| (!particleBanned || SCREEN_WIDTH * fParticleScaleLimit >= w)
						&& SCREEN_HEIGHT * fParticleScaleLimit >= h)
					&& PS3_FxQuad(i, particle->m_fSize * w, particle->m_fSize * h, coors.x, coors.y, &particle->m_nAlpha) )
#else
				if ( i == PARTICLE_WATER_HYDRANT
						|| (!particleBanned || SCREEN_WIDTH * fParticleScaleLimit >= w)
						&& SCREEN_HEIGHT * fParticleScaleLimit >= h )
#endif
				{
					if ( i == PARTICLE_WATER_HYDRANT )
					{
						RwRect rect;
						
						if ( w > 0.0f )
						{
							rect.x = int32(coors.x - SCREEN_STRETCH_X(particle->m_fSize * w));
							rect.w = int32(coors.x + SCREEN_STRETCH_X(particle->m_fSize * w));
						}
						else
						{
							rect.w = int32(coors.x - SCREEN_STRETCH_X(particle->m_fSize * w));
							rect.x = int32(coors.x + SCREEN_STRETCH_X(particle->m_fSize * w));
						}
						
						if ( h > 0.0f )
						{
							rect.y = int32(coors.y - SCREEN_STRETCH_Y(particle->m_fSize * h));
							rect.h = int32(coors.y + SCREEN_STRETCH_Y(particle->m_fSize * h));
						}
						else
						{
							rect.h = int32(coors.y - SCREEN_STRETCH_Y(particle->m_fSize * h));
							rect.y = int32(coors.y + SCREEN_STRETCH_Y(particle->m_fSize * h));
						}
						
						float screenZ = (coors.z - CDraw::GetNearClipZ())
							* (CSprite::GetFarScreenZ() - CSprite::GetNearScreenZ()) * CDraw::GetFarClipZ()
							/ ( (CDraw::GetFarClipZ() - CDraw::GetNearClipZ()) * coors.z ) + CSprite::GetNearScreenZ();

						CMBlur::AddRenderFx(Scene.camera, &rect, screenZ, FXTYPE_SPLASH1);
					}
					else
					{
						if ( particle->m_nRotation != 0 && i != PARTICLE_BEASTIE )
						{					
							CSprite::RenderBufferedOneXLUSprite_Rotate_Dimension(coors.x, coors.y, coors.z,
									particle->m_fSize * w, particle->m_fSize * h,
									particle->m_Color.red,
									particle->m_Color.green,
									particle->m_Color.blue,
									particle->m_nColorIntensity,
									1.0f / coors.z,
									DEGTORAD((float)particle->m_nRotation),
									particle->m_nAlpha);
						}
						else if ( psystem->Flags & SCREEN_TRAIL )
						{
							float fRotation;
							float fTrailLength;
							
							if ( particle->m_fZGround == 0.0f )
							{
								fTrailLength = 0.0f;
								fRotation = 0.0f;
							}
							else
							{
								CVector2D vecDist
								(
									coors.x - particle->m_fZGround,
									coors.y - particle->m_fExpansionRate
								);

								float fDist = vecDist.Magnitude();

								fTrailLength = fDist;
								
								float fRot = Asin(vecDist.x / fDist);

								fRotation = fRot;

								if ( vecDist.y < 0.0f )
									fRotation = -1.0f * fRot + DEGTORAD(180.0f);
								
								float fSpeed = particle->m_vecVelocity.Magnitude();
								
								float fNewTrailLength = fSpeed * CTimer::GetTimeStep() * w * 2.0f;
								
								if ( fDist > fNewTrailLength )
									fTrailLength = fNewTrailLength;
							}
							
							CSprite::RenderBufferedOneXLUSprite_Rotate_Dimension(coors.x, coors.y, coors.z,
									particle->m_fSize * w,
									particle->m_fSize * h + fTrailLength * psystem->m_fTrailLengthMultiplier,
									particle->m_Color.red,
									particle->m_Color.green,
									particle->m_Color.blue,
									particle->m_nColorIntensity,
									1.0f / coors.z,
									fRotation,
									particle->m_nAlpha);
			
							particle->m_fZGround = coors.x;				// WTF ?
							particle->m_fExpansionRate =  coors.y;		// WTF ?
						}
						else if ( psystem->Flags & SPEED_TRAIL )
						{
							CVector vecPrevPos = particle->m_vecPosition - particle->m_vecVelocity;
							float fRotation;
							float fTrailLength;
							CVector vecScreenPosition;
							
							if ( CSprite::CalcScreenCoors(vecPrevPos, &vecScreenPosition, &fTrailLength, &fRotation, true) )
							{
								CVector2D vecDist
								(
									coors.x - vecScreenPosition.x,
									coors.y - vecScreenPosition.y
								);
								
								float fDist = vecDist.Magnitude();
								
								fTrailLength = fDist;
								
								float fRot = Asin(vecDist.x / fDist);
								
								fRotation = fRot;
								
								if ( vecDist.y < 0.0f )
									fRotation = -1.0f * fRot + DEGTORAD(180.0f);
							}
							else
							{
								fRotation = 0.0f;
								fTrailLength = 0.0f;
							}
							
							CSprite::RenderBufferedOneXLUSprite_Rotate_Dimension(coors.x, coors.y, coors.z,
									particle->m_fSize * w,
									particle->m_fSize * h + fTrailLength * psystem->m_fTrailLengthMultiplier,
									particle->m_Color.red,
									particle->m_Color.green,
									particle->m_Color.blue,
									particle->m_nColorIntensity,
									1.0f / coors.z,
									fRotation,
									particle->m_nAlpha);
						}
						else if ( psystem->Flags & VERT_TRAIL )
						{
							float fTrailLength = fabsf(particle->m_vecVelocity.z * 10.0f);

							CSprite::RenderBufferedOneXLUSprite(coors.x, coors.y, coors.z,
									particle->m_fSize * w,
									(particle->m_fSize + fTrailLength * psystem->m_fTrailLengthMultiplier) * h,
									particle->m_Color.red,
									particle->m_Color.green,
									particle->m_Color.blue,
									particle->m_nColorIntensity,
									1.0f / coors.z,
									particle->m_nAlpha);
						}
						else if ( i == PARTICLE_RAINDROP_SMALL )
						{
							CSprite::RenderBufferedOneXLUSprite(coors.x, coors.y, coors.z,
									particle->m_fSize * w * 0.05f,
									particle->m_fSize * h,
									particle->m_Color.red,
									particle->m_Color.green,
									particle->m_Color.blue,
									particle->m_nColorIntensity,
									1.0f / coors.z,
									particle->m_nAlpha);
						}
						/*else if ( i == PARTICLE_BOAT_WAKE )*/
						else
						{							
							CSprite::RenderBufferedOneXLUSprite(coors.x, coors.y, coors.z,
									particle->m_fSize * w,
									particle->m_fSize * h,
									particle->m_Color.red,
									particle->m_Color.green,
									particle->m_Color.blue,
									particle->m_nColorIntensity,
									1.0f / coors.z,
									particle->m_nAlpha);
						}
					}
#ifdef __PS3__
					particle->m_nAlpha = ps3Alpha;	// the fade is only for this frame
#endif
				}
			}
		}
		
		particle = particle->m_pNext;
	}

	CSprite::FlushSpriteBuffer();

}

#ifdef __PS3__
static bool PS3_ParticlesSpu(uint32 &flags, RwRaster *&prevFrame);
#endif

void CParticle::Render()
{
#ifdef __PS3__
	if (gPS3FxOff & 8)
		return;
#endif
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void *)rwTEXTUREADDRESSWRAP);
	RwRenderStateSet(rwRENDERSTATETEXTUREPERSPECTIVE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void *)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void *)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDINVSRCALPHA);
	
	CSprite::InitSpriteBuffer2D();
	
	uint32 flags = DRAW_OPAQUE;
	
	RwRaster *prevFrame = nil;
	
#ifdef __PS3__
	uint64 ps3T0 = sysGetSystemTime();
	if (!(gPS3SpuParticles && PS3_ParticlesSpu(flags, prevFrame)))
#endif
	for ( int32 i = 0; i < MAX_PARTICLES; i++ )
		RenderParticleSystem(i, flags, prevFrame);
#ifdef __PS3__
	gPS3ParticleUs += sysGetSystemTime() - ps3T0;
#endif
	
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void *)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDINVSRCALPHA);
}


#ifdef __PS3__
// ============================================================================
// from res-PS3 (patch 50): the sprites on an SPU (ps3/spu/spuparticles.c)
// ============================================================================
// CParticle::Render in three steps:
//  1. the PPU walks the systems as RenderParticleSystem does, with the same
//     render state and texture frame changes, but records them as commands
//     and writes one SpuPartSprite per particle (position, sizes, colour, the
//     kind of sprite, the size limits); the sprites between two state changes
//     are a batch. Systems drawn on the screen (DRAWTOP2D), that keep screen positions in the
// particles (SCREEN_TRAIL) and the hydrant's water (a CMBlur effect) are a
// command "the PPU draws this system" instead.
//  2. the SPU projects, limits (the PC's fParticleScaleLimit, the PS3 fill
//     limit) and builds the visible sprites as quads straight into this
//     frame's vertex ring (spuparticles_ram.txt: into memory here, then
//     copied, in case the SPU can't write the RSX's memory).
//  3. the commands are played back: states, the batches' quads in one draw
//     each, and the PPU's systems.
// The SPU sprite has the PPU sprite's maths; what differs: sprites clipped to
// nothing aren't sent, a speed trail with no length is not rotated by a NaN,
// and the [fx] fill count is the PPU's systems' only. If the SPU doesn't
// answer, the PPU draws from then on.

extern "C" const uint8 spuparticles_elf[];	// the SPU program (Makefile, .incbin)
void PS3_SpriteScreenScale(float *kw, float *kh);	// Sprite.cpp

enum { PCMD_BLEND, PCMD_RASTER, PCMD_BATCH, PCMD_PPU };
struct PartCmd
{
	uint8 type;
	int16 arg;		// BATCH: batch, PPU: particle type
	uint32 flags;		// BLEND
	RwRaster *raster;	// RASTER
};
#define MAX_PART_CMDS (SPUPART_MAX_BATCHES * 3 + MAX_PARTICLES + 8)

static SpuPartJob sPartJob __attribute__((aligned(128)));
static SpuPartSprite sPartSprites[SPUPART_MAX_SPRITES] __attribute__((aligned(128)));
static SpuPartBatch sPartBatches[SPUPART_MAX_BATCHES] __attribute__((aligned(128)));
static PartCmd sPartCmds[MAX_PART_CMDS];
static uint8 *sPartStaging;	// spuparticles_ram.txt
static int sPartState;		// 0 not started, 1 on, 2 off
static uint32 sPartSeq;

static_assert(sizeof(SpuPartSprite) == 64, "SpuPartSprite layout");
static_assert(sizeof(SpuPartBatch) == 16, "SpuPartBatch layout");
static_assert(sizeof(SpuPartJob) == 128, "SpuPartJob layout");
static_assert(SPUPART_QUAD_BYTES == 4 * sizeof(RwIm2DVertex), "quad size");
static_assert(MAX_PARTICLES_ON_SCREEN <= SPUPART_MAX_SPRITES, "sprite array");

static void
PS3_ParticlesSpuStart(void)
{
	sPartState = 2;
	FILE *f = fopen(PS3_USRDIR "/nospuparticles.txt", "rb");
	if (f) {
		fclose(f);
		gPS3SpuParticles = false;
		PS3_Log("[spu] nospuparticles.txt: the particles stay on the PPU");
		return;
	}
	f = fopen(PS3_USRDIR "/spuparticles_ram.txt", "rb");
	if (f) {
		fclose(f);
		sPartStaging = (uint8 *)memalign(128, SPUPART_MAX_SPRITES * SPUPART_QUAD_BYTES);
	}
	if (!PS3_SpuTaskStart(&sPartTask, spuparticles_elf, "revcfx", SPUPART_SPUP)) {
		PS3_Log("[spu] the particles stay on the PPU");
		return;
	}
	PS3_Logf("[spu] particles: sprites built by the SPU %s", sPartStaging ? "into memory, copied to the ring (spuparticles_ram.txt)" : "straight into the vertex ring");
	sPartState = 1;
}

static bool
PS3_ParticlesSpu(uint32 &flags, RwRaster *&prevFrame)
{
	if (sPartState == 0)
		PS3_ParticlesSpuStart();
	if (sPartState != 1)
		return false;

	// 1. gather
	int ns = 0, nb = 0, nc = 0;
	int batchFirst = -1;
	bool known = true, overflow = false;
	uint32 gFlags = flags;
	RwRaster *gPrev = prevFrame;
	auto addCmd = [&](uint8 type, int arg, uint32 f, RwRaster *r) {
		if (nc >= MAX_PART_CMDS) { overflow = true; return; }
		PartCmd &c = sPartCmds[nc++];
		c.type = type; c.arg = arg; c.flags = f; c.raster = r;
	};
	auto closeBatch = [&]() {
		if (batchFirst < 0)
			return;
		if (nb >= SPUPART_MAX_BATCHES) { overflow = true; batchFirst = -1; return; }
		sPartBatches[nb].first = batchFirst;
		sPartBatches[nb].count = ns - batchFirst;
		sPartBatches[nb].outFirst = sPartBatches[nb].outCount = 0;
		addCmd(PCMD_BATCH, nb, 0, nil);
		nb++;
		batchFirst = -1;
	};
	bool lookSide = TheCamera.GetLookDirection() == LOOKING_LEFT || TheCamera.GetLookDirection() == LOOKING_RIGHT;
	(void)lookSide;

	for (int32 i = 0; i < MAX_PARTICLES && !overflow; i++) {
		tParticleSystemData *psystem = &mod_ParticleSystemManager.m_aParticles[i];
		CParticle *particle = psystem->m_pParticles;
		if (particle == nil)
			continue;
		RwRaster **frames = psystem->m_ppRaster;
		if ((psystem->Flags & (DRAWTOP2D | SCREEN_TRAIL)) || i == PARTICLE_WATER_HYDRANT) {
			closeBatch();
			addCmd(PCMD_PPU, i, 0, nil);
			known = false;	// its state changes aren't followed here
			continue;
		}
		if (!known || (gFlags & DRAW_OPAQUE) != (psystem->Flags & DRAW_OPAQUE) ||
		    (gFlags & DRAW_DARK) != (psystem->Flags & DRAW_DARK)) {
			closeBatch();
			addCmd(PCMD_BLEND, 0, psystem->Flags, nil);
			gFlags = psystem->Flags;
		}
		if (frames != nil && (!known || *frames != gPrev)) {
			closeBatch();
			addCmd(PCMD_RASTER, 0, 0, *frames);
			gPrev = *frames;
		}
		known = true;
		bool smoke = i == PARTICLE_ENGINE_STEAM || i == PARTICLE_ENGINE_SMOKE || i == PARTICLE_ENGINE_SMOKE2 ||
		             i == PARTICLE_CARFLAME_SMOKE || i == PARTICLE_CARCOLLISION_DUST || i == PARTICLE_EXHAUST_FUMES ||
		             i == PARTICLE_RUBBER_SMOKE || i == PARTICLE_BURNINGRUBBER_SMOKE;
		tParticleType type = psystem->m_Type;
		bool banned = type == PARTICLE_ENGINE_SMOKE || type == PARTICLE_ENGINE_SMOKE2 || type == PARTICLE_ENGINE_STEAM ||
		              type == PARTICLE_CARFLAME_SMOKE || type == PARTICLE_RUBBER_SMOKE || type == PARTICLE_BURNINGRUBBER_SMOKE ||
		              type == PARTICLE_EXHAUST_FUMES || type == PARTICLE_CARCOLLISION_DUST;
		uint16 limits = SPUPART_LIMIT_H | (banned ? SPUPART_LIMIT_W : 0) | (gPS3FxLimit ? SPUPART_FADE : 0);
		const CVector2D &stretch = psystem->m_vecTextureStretch;
		for (; particle != nil; particle = particle->m_pNext) {
			if (particle->m_nAlpha == 0)
				continue;
			if (psystem->m_nFinalAnimationFrame != 0 && frames != nil) {
				PS3_ParticleFrameClamp(particle, i, frames);
				RwRaster *curFrame = frames[particle->m_nCurrentFrame];
				if (curFrame != gPrev) {
					closeBatch();
					addCmd(PCMD_RASTER, 0, 0, curFrame);
					gPrev = curFrame;
				}
			}
			if (ns >= SPUPART_MAX_SPRITES) {
				overflow = true;
				break;
			}
			SpuPartSprite &sp = sPartSprites[ns];
			sp.x = particle->m_vecPosition.x;
			sp.y = particle->m_vecPosition.y;
			sp.z = particle->m_vecPosition.z;
			sp.flags = limits;
			if (smoke) {
				if (lookSide) {
					sp.addW = CGeneral::GetRandomNumberInRange(1.0f, 7.5f) * stretch.x;
					sp.addH = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * stretch.y;
				} else {
					sp.addW = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * stretch.x;
					sp.addH = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * stretch.y;
				}
			} else if (i == PARTICLE_FLYERS) {
				sp.addW = stretch.x;
				sp.addH = stretch.y;
				sp.flags |= SPUPART_MIN12;
			} else {
				sp.addW = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * stretch.x;
				sp.addH = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * stretch.y;
			}
			float size = particle->m_fSize;
			sp.size = size;
			sp.wMul = sp.hMul = size;
			sp.rot = 0.0f;
			sp.px = sp.py = sp.pz = 0.0f;
			sp.trailMul = psystem->m_fTrailLengthMultiplier;
			if (particle->m_nRotation != 0 && i != PARTICLE_BEASTIE) {
				sp.kind = SPUPART_ROTATE;
				sp.rot = DEGTORAD((float)particle->m_nRotation);
			} else if (psystem->Flags & SPEED_TRAIL) {
				sp.kind = SPUPART_SPEEDTRAIL;
				CVector prev = particle->m_vecPosition - particle->m_vecVelocity;
				sp.px = prev.x;
				sp.py = prev.y;
				sp.pz = prev.z;
			} else if (psystem->Flags & VERT_TRAIL) {
				sp.kind = SPUPART_PLAIN;
				sp.hMul = size + Abs(particle->m_vecVelocity.z * 10.0f) * psystem->m_fTrailLengthMultiplier;
			} else if (i == PARTICLE_RAINDROP_SMALL) {
				sp.kind = SPUPART_PLAIN;
				sp.wMul = size * 0.05f;
			} else
				sp.kind = SPUPART_PLAIN;
			int16 intens = particle->m_nColorIntensity;
			sp.r = (uint8)(particle->m_Color.red * intens >> 8);
			sp.g = (uint8)(particle->m_Color.green * intens >> 8);
			sp.b = (uint8)(particle->m_Color.blue * intens >> 8);
			sp.a = particle->m_nAlpha;
			if (batchFirst < 0)
				batchFirst = ns;
			ns++;
		}
	}
	closeBatch();
	if (overflow) {
		static int logged;
		if (logged++ < 4)
			PS3_Logf("[spu] particles: %d sprites, %d batches, %d commands: over the arrays, the PPU draws this frame", ns, nb, nc);
		return false;
	}

	// 2. the SPU builds the quads
	uint32 ringOff = 0;
	if (ns > 0) {
		uint8 *dst = sPartStaging;
		if (dst == nil) {
			dst = rw::ps3::ringAlloc(ns * SPUPART_QUAD_BYTES, &ringOff);
			if (dst == nil)
				return false;	// the ring is full: the PPU's draws will say so
		}
		SpuPartJob &j = sPartJob;
		j.spritesEa = (uint64)(uintptr_t)sPartSprites;
		j.batchesEa = (uint64)(uintptr_t)sPartBatches;
		j.outEa = (uint64)(uintptr_t)dst;
		j.numSprites = ns;
		j.numBatches = nb;
		const CMatrix &m = TheCamera.m_viewMatrix;
		j.view[0] = m.rx; j.view[1] = m.ry; j.view[2] = m.rz;
		j.view[3] = m.fx; j.view[4] = m.fy; j.view[5] = m.fz;
		j.view[6] = m.ux; j.view[7] = m.uy; j.view[8] = m.uz;
		j.view[9] = m.px; j.view[10] = m.py; j.view[11] = m.pz;
		j.nearClip = CDraw::GetNearClipZ();
		j.farClip = CDraw::GetFarClipZ();
		j.screenW = SCREEN_WIDTH;
		j.screenH = SCREEN_HEIGHT;
		PS3_SpriteScreenScale(&j.kw, &j.kh);
		j.near2D = CSprite::GetNearScreenZ();
		j.far2D = CSprite::GetFarScreenZ();
		j.scaleLimit = fParticleScaleLimit;
		j.seq = ++sPartSeq & 0xFFFFFF;
		j.numQuads = 0;
		if (!PS3_SpuTaskKick(&sPartTask, (uint32)(uintptr_t)&sPartJob) ||
		    !PS3_SpuTaskWait(&sPartTask, j.seq, 50 * 1000, &gPS3SpuPartWaitUs)) {
			sPartState = 2;
			gPS3SpuParticles = false;
			return false;
		}
		if (sPartStaging && j.numQuads > 0) {
			uint8 *ring = rw::ps3::ringAlloc(j.numQuads * SPUPART_QUAD_BYTES, &ringOff);
			if (ring == nil)
				return false;
			memcpy(ring, sPartStaging, j.numQuads * SPUPART_QUAD_BYTES);
		}
	}

	// 3. play the commands back
	for (int c = 0; c < nc; c++) {
		const PartCmd &cmd = sPartCmds[c];
		switch (cmd.type) {
		case PCMD_BLEND:
			if (cmd.flags & DRAW_OPAQUE) {
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDINVSRCALPHA);
			} else {
				if (cmd.flags & DRAW_DARK)
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
				else
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDONE);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDONE);
			}
			flags = cmd.flags;
			break;
		case PCMD_RASTER:
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void *)cmd.raster);
			prevFrame = cmd.raster;
			break;
		case PCMD_BATCH: {
			const SpuPartBatch &b = sPartBatches[cmd.arg];
			if (b.outCount > 0)
				rw::ps3::im2DDrawQuadsAt(ringOff + b.outFirst * SPUPART_QUAD_BYTES, b.outCount);
			break;
		}
		case PCMD_PPU:
			RenderParticleSystem(cmd.arg, flags, prevFrame);
			break;
		}
	}
	return true;
}
#endif

void CParticle::RemovePSystem(tParticleType type)
{
	tParticleSystemData *psystemdata = &mod_ParticleSystemManager.m_aParticles[type];
	
	for ( CParticle *particle = psystemdata->m_pParticles; particle; particle = psystemdata->m_pParticles )
		RemoveParticle(particle, nil, psystemdata);
}

void CParticle::RemoveParticle(CParticle *pParticle, CParticle *pPrevParticle, tParticleSystemData *pPSystemData)
{
	if ( pPSystemData->m_Type == PARTICLE_WATERDROP )
		--numWaterDropOnScreen;
	
	if ( pPrevParticle )
		pPrevParticle->m_pNext = pParticle->m_pNext;
	else
		pPSystemData->m_pParticles = pParticle->m_pNext;

	pParticle->m_pNext = m_pUnusedListHead;
	m_pUnusedListHead = pParticle;
}

void CParticle::AddJetExplosion(CVector const &vecPos, float fPower, float fSize)
{
	CRGBA color(240, 240, 240, 255);

	if ( fPower < 1.0f )
		fPower = 1.0f;
	
	CVector vecRandOffset
	(
		CGeneral::GetRandomNumberInRange(-0.4f, 0.4f),
		CGeneral::GetRandomNumberInRange(-0.4f, 0.4f),
		CGeneral::GetRandomNumberInRange(0.1f, 0.3f)
	);
	
	vecRandOffset *= 2.0f;

	CVector vecStepPos = vecPos;

	for ( int32 i = 0; i < int32(fPower * 4.0f); i++ )
	{
		AddParticle(PARTICLE_EXPLOSION_MFAST,
					vecStepPos,
					CVector
					(
						CGeneral::GetRandomNumberInRange(-0.02f, 0.02f),
						CGeneral::GetRandomNumberInRange(-0.02f, 0.02f),
						CGeneral::GetRandomNumberInRange(-0.02f, 0.0f)
					),
					nil,
					fSize, color, 0, 0, 0, 0);

		AddParticle(PARTICLE_EXPLOSION_MFAST,
					vecStepPos,
					CVector
					(
						CGeneral::GetRandomNumberInRange(-0.04f, 0.04f),
						CGeneral::GetRandomNumberInRange(-0.04f, 0.04f),
						CGeneral::GetRandomNumberInRange(0.0f, 0.07f)
					),
					nil,
					fSize, color, 0, 0, 0, 0);

		AddParticle(PARTICLE_EXPLOSION_MFAST,
					vecStepPos,
					CVector
					(
						CGeneral::GetRandomNumberInRange(-0.04f, 0.04f),
						CGeneral::GetRandomNumberInRange(-0.04f, 0.04f),
						CGeneral::GetRandomNumberInRange(0.0f, 0.07f)
					),
					nil,
					fSize, color, 0, 0, 0, 0);
		
		vecStepPos += vecRandOffset;
	}
}

void CParticle::AddYardieDoorSmoke(CVector const &vecPos, CMatrix const &matMatrix)
{
	CRGBA color(0, 0, 0, 0);
	
	CMatrix invMat(Invert(matMatrix));
	
	CVector vecBasePos = matMatrix * (invMat * vecPos + CVector(0.0f, -1.0f, 0.5f));
	
	for ( int32 i = 0; i < 5; i++ )
	{
		CVector pos = vecBasePos;

		pos.x += CGeneral::GetRandomNumberInRange(-0.5f, 0.5f);
		pos.y += CGeneral::GetRandomNumberInRange(-0.5f, 0.5f);
		
		AddParticle(PARTICLE_CARCOLLISION_DUST,
					pos,
					CVector(0.0f, 0.0f, 0.0f),
					nil,
					0.3f, color, 0, 0, 0, 0);
	}
}

void CParticle::CalWindDir(CVector *vecDirIn, CVector *vecDirOut)
{
	vecDirOut->x = (Cos(128) * vecDirIn->x) + (Sin(128) * vecDirIn->y);

	vecDirOut->x = (Cos(128) * vecDirIn->x) + (Sin(128) * vecDirIn->y) * CWeather::Wind;
	vecDirOut->y = (Sin(128) * vecDirIn->x) - (Cos(128) * vecDirIn->y) * CWeather::Wind;
}

void CParticle::HandleShipsAtHorizonStuff()
{
	tParticleSystemData *psystemdata = &mod_ParticleSystemManager.m_aParticles[PARTICLE_SHIP_SIDE];

	for ( CParticle *particle = psystemdata->m_pParticles; particle; particle = particle->m_pNext )
	{
		if ( CTimer::GetTimeInMilliseconds() > particle->m_nTimeWhenWillBeDestroyed - 32000 
				&& CTimer::GetTimeInMilliseconds() < particle->m_nTimeWhenWillBeDestroyed - 22000 )
		{		
			particle->m_nAlpha = Min(particle->m_nAlpha + 1, 96);
		}
		if ( CTimer::GetTimeInMilliseconds() > particle->m_nTimeWhenWillBeDestroyed - 10000 )
			particle->m_nFadeAlphaTimer = 1;
	}
}

void CParticle::HandleShootableBirdsStuff(CEntity *entity, CVector const&camPos)
{
	float fHeadingRad = entity->GetForward().Heading();
	float fHeading = RADTODEG(fHeadingRad);
	float fBirdAngle = ::Cos(DEGTORAD(1.5f));
	
	tParticleSystemData *psystem = &mod_ParticleSystemManager.m_aParticles[PARTICLE_BIRD_FRONT];
	CParticle *particle = psystem->m_pParticles;
	CParticle *prevParticle = nil;
	bool bRemoveParticle;
			
	for ( ; particle != nil; _Next(particle, prevParticle, psystem, bRemoveParticle) )
	{
		bRemoveParticle = false;
		
		CVector2D vecPos(particle->m_vecPosition.x, particle->m_vecPosition.y);
		CVector2D vecCamPos(camPos.x, camPos.y);

		CVector2D vecDist = vecPos - vecCamPos;
		vecDist.Normalise();
		
		float fHead = DEGTORAD(fHeading);

		CVector2D vecDir(-::Sin(fHead), ::Cos(fHead));
		vecDir.Normalise();
		
		float fDot = DotProduct2D(vecDir, vecDist);
		
		if ( fDot > 0.0f && fDot > fBirdAngle )
		{
			if ( (camPos - particle->m_vecPosition).MagnitudeSqr() < 40000.0f )
			{
				CStats::SeagullsKilled++;
				
				bRemoveParticle = true;

				for ( int32 i = 0; i < 8; i++ )
				{	
					CParticle *pBirdDerbis = AddParticle(PARTICLE_BIRD_DEBRIS,
												particle->m_vecPosition,
												CVector
												(
													CGeneral::GetRandomNumberInRange(-3.0f, 3.0f),
													CGeneral::GetRandomNumberInRange(-3.0f, 3.0f),
													CGeneral::GetRandomNumberInRange(-3.0f, 3.0f)
												),
												nil,
												0.3f,
												particle->m_Color,
												CGeneral::GetRandomNumberInRange(20, 40),
												0,
												CGeneral::GetRandomNumber() & 3,
												200);
					if ( pBirdDerbis )
						pBirdDerbis->m_nAlpha = particle->m_nAlpha;
				}
			}
		}
	}
	
}

void
CEntity::AddSteamsFromGround(CVector *unused)
{
	int i, n;
	C2dEffect *effect;
	CVector pos;

	n = CModelInfo::GetModelInfo(GetModelIndex())->GetNum2dEffects();
	for(i = 0; i < n; i++){
		effect = CModelInfo::GetModelInfo(GetModelIndex())->Get2dEffect(i);
		if(effect->type != EFFECT_PARTICLE)
			continue;

		pos = GetMatrix() * effect->pos;
		switch(effect->particle.particleType){
		case 0:
			CParticleObject::AddObject(POBJECT_PAVEMENT_STEAM, pos, effect->particle.dir, effect->particle.scale, false);
			break;
		case 1:
			CParticleObject::AddObject(POBJECT_WALL_STEAM, pos, effect->particle.dir, effect->particle.scale, false);
			break;
		case 2:
			CParticleObject::AddObject(POBJECT_DRY_ICE, pos, effect->particle.scale, false);
			break;
		case 3:
			CParticleObject::AddObject(POBJECT_SMALL_FIRE, pos, effect->particle.dir, effect->particle.scale, false);
			break;
		case 4:
			CParticleObject::AddObject(POBJECT_DARK_SMOKE, pos, effect->particle.dir, effect->particle.scale, false);
			break;
		case 5:
			CParticleObject::AddObject(POBJECT_WATER_FOUNTAIN_VERT, pos, effect->particle.dir, effect->particle.scale, false);
			break;
		case 6:
			CParticleObject::AddObject(POBJECT_WATER_FOUNTAIN_HORIZ, pos, effect->particle.dir, effect->particle.scale, false);
			break;
		}
	}
}
