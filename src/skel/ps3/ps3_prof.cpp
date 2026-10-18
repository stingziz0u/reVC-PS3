// ps3_prof.cpp -- where the CPU time of a frame goes: main.cpp's timebar
// timers (CGame::Process, CnstrRenderList, RenderScene, Render2dStuff...),
// averaged per frame and logged every 10 seconds next to librw's [rsx] line.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/systime.h>

#include "ps3_platform.h"

// microseconds from the timebase (sysGetSystemTime is a system call)
static u64
profNow(void)
{
	static u64 freq;
	if (freq == 0)
		freq = sysGetTimebaseFrequency();
	u64 tb;
	__asm__ volatile("mftb %0" : "=r"(tb));
	return tb * 1000000 / freq;
}

// the timers are also crash breadcrumbs (1 = started, 2 = done): main.cpp's
// frame phases after CGame::Process (audio, render list, scene, 2D, present)
// had none. The names are string literals, as PS3_Crumb needs.
extern "C" void PS3_Crumb(const char *tag, int val);

#if PS3_STAGE != 1
// the same timers on the RSX (librw ps3video.cpp)
namespace rw { namespace ps3 {
void gpuMark(int32_t id);
void gpuStatsTake(uint64_t *ns, uint64_t *frameNs, uint32_t *samples);
} }
#define GPU_MARK(id) rw::ps3::gpuMark(id)
#define GPU_TIMERS 30
#else
#define GPU_MARK(id)
#endif

#define MAX_TIMERS	24
#define LOG_EVERY_US	10000000ull

struct ProfTimer
{
	const char *name;
	u64 start;
	u64 total;
};

void PS3_FxReport(unsigned int frames);	// Particle.cpp
void PS3_ColReport(unsigned int frames);	// World.cpp
void PS3_PoolsReport(void);	// World.cpp

static ProfTimer timers[MAX_TIMERS];
static int numTimers;
static u64 frameStart, frameTotal, lastLog;
static u32 frames;

static ProfTimer*
FindTimer(const char *name)
{
	for (int i = 0; i < numTimers; i++)
		if (timers[i].name == name || strcmp(timers[i].name, name) == 0)
			return &timers[i];
	if (numTimers >= MAX_TIMERS)
		return NULL;
	timers[numTimers].name = name;
	timers[numTimers].start = 0;
	timers[numTimers].total = 0;
	return &timers[numTimers++];
}

void
PS3_ProfStart(const char *name)
{
	PS3_Crumb(name, 1);
	ProfTimer *t = FindTimer(name);
	if (t) {
		t->start = profNow();
		GPU_MARK(2 * (int)(t - timers));
	}
}

void
PS3_ProfEnd(const char *name)
{
	PS3_Crumb(name, 2);
	ProfTimer *t = FindTimer(name);
	if (t)
		GPU_MARK(2 * (int)(t - timers) + 1);
	if (t && t->start) {
		t->total += profNow() - t->start;
		t->start = 0;
	}
}

// ---- steps inside CGame::Process / CWorld::Process (common.h) ----------------
// One timebase read per step or entity; buckets found by the name's pointer
// (string literals), logged sorted, every 10 s.
#define PROF_GROUPS	3
#define MAX_STEPS	80

struct ProfStepBucket
{
	const char *name;
	u64 ticks;
	u32 count;
};

// the weather of the last frame (Game.cpp): rain costs particles and streaks
float PS3_profRain;
int PS3_profWeather;

static ProfStepBucket stepBuckets[PROF_GROUPS][MAX_STEPS];
static int numStepBuckets[PROF_GROUPS];
static const char *stepCur[PROF_GROUPS];
static u64 stepStart[PROF_GROUPS];

static inline u64
profTicks(void)
{
	u64 tb;
	__asm__ volatile("mftb %0" : "=r"(tb));
	return tb;
}

extern "C" void
PS3_ProfAdd(int group, const char *name, unsigned long long ticks)
{
	if (group < 0 || group >= PROF_GROUPS || name == NULL)
		return;
	ProfStepBucket *b = stepBuckets[group];
	int n = numStepBuckets[group];
	for (int i = 0; i < n; i++)
		if (b[i].name == name) {
			b[i].ticks += ticks;
			b[i].count++;
			return;
		}
	if (n >= MAX_STEPS)
		return;
	b[n].name = name;
	b[n].ticks = ticks;
	b[n].count = 1;
	numStepBuckets[group] = n + 1;
}

extern "C" void
PS3_ProfStep(int group, const char *name)
{
	if (group < 0 || group >= PROF_GROUPS)
		return;
	u64 now = profTicks();
	if (stepCur[group])
		PS3_ProfAdd(group, stepCur[group], now - stepStart[group]);
	stepCur[group] = name;
	stepStart[group] = now;
}

static int
CmpBucket(const void *a, const void *b)
{
	u64 x = ((const ProfStepBucket*)a)->ticks, y = ((const ProfStepBucket*)b)->ticks;
	return x < y ? 1 : x > y ? -1 : 0;
}

// "[prof] game: ..." / "[prof] world: ...": the buckets of at least 0.05 ms
// per frame, biggest first; with the times per frame after those that run
// more than once a frame (entities)
static void
LogStepGroup(int group, const char *title, u32 nframes)
{
	static u64 freq;
	if (freq == 0)
		freq = sysGetTimebaseFrequency();
	ProfStepBucket sorted[MAX_STEPS];
	int n = numStepBuckets[group];
	memcpy(sorted, stepBuckets[group], n * sizeof(ProfStepBucket));
	qsort(sorted, n, sizeof(ProfStepBucket), CmpBucket);
	char line[1024];
	u64 total = 0;
	for (int i = 0; i < n; i++)
		total += sorted[i].ticks;
	int len = snprintf(line, sizeof(line), "[prof] %s ms per frame:", title);
	for (int i = 0; i < n && len < (int)sizeof(line) - 80; i++) {
		double ms = sorted[i].ticks * 1000.0 / freq / nframes;
		if (ms < 0.05)
			break;
		double per = (double)sorted[i].count / nframes;
		if (per > 1.5)
			len += snprintf(line + len, sizeof(line) - len, " | %s %.2f (x%.0f)", sorted[i].name, ms, per);
		else
			len += snprintf(line + len, sizeof(line) - len, " | %s %.2f", sorted[i].name, ms);
	}
	PS3_Log(line);
	for (int i = 0; i < n; i++) {
		stepBuckets[group][i].ticks = 0;
		stepBuckets[group][i].count = 0;
	}
}

// start of a frame (main.cpp's Idle)
void
PS3_ProfFrame(void)
{
	u64 now = profNow();
	PS3_crumbFrame++;	// the frame number in the [crash] lines
	if (frameStart)
		frameTotal += now - frameStart;
	frameStart = now;
	if (lastLog == 0) {
		lastLog = now;
		return;
	}
	frames++;
	if (now - lastLog < LOG_EVERY_US || frames == 0)
		return;

	char line[512];
	int n = snprintf(line, sizeof(line), "[prof] ms per frame: total %.1f |", frameTotal / 1000.0 / frames);
	for (int i = 0; i < numTimers && n < (int)sizeof(line) - 40; i++) {
		n += snprintf(line + n, sizeof(line) - n, " %s %.2f", timers[i].name, timers[i].total / 1000.0 / frames);
		timers[i].total = 0;
	}
	PS3_Log(line);
	{
		static const char *weathers[] = { "sunny", "cloudy", "rainy", "foggy", "extra sunny", "hurricane", "extra colours", "sunny and foggy" };
		char title[96];
		snprintf(title, sizeof(title), "game (CGame::Process; weather %s, rain %.2f)",
		         PS3_profWeather >= 0 && PS3_profWeather < 8 ? weathers[PS3_profWeather] : "?", PS3_profRain);
		LogStepGroup(0, title, frames);
	}
	LogStepGroup(1, "world (CWorld::Process; the checks are inside control peds)", frames);
	LogStepGroup(2, "scene (RenderScene, RenderEffects)", frames);
	PS3_FxReport(frames);	// Particle.cpp: what the particles drew
	PS3_ColReport(frames);	// World.cpp: ProcessCollision's passes and models
	PS3_LogMemTag("[mem] ");	// heap, free user memory, textures, streaming
#if PS3_STAGE != 1
	PS3_PoolsReport();
#endif
#if PS3_STAGE != 1
	{
		uint64_t ns[GPU_TIMERS], frameNs;
		uint32_t samples;
		rw::ps3::gpuStatsTake(ns, &frameNs, &samples);
		if (samples) {
			n = snprintf(line, sizeof(line), "[gpu]  RSX ms per frame: total %.1f |", frameNs / 1e6 / samples);
			for (int i = 0; i < numTimers && i < GPU_TIMERS && n < (int)sizeof(line) - 40; i++)
				if (ns[i] / samples >= 10000)	// 0.01 ms
					n += snprintf(line + n, sizeof(line) - n, " %s %.2f", timers[i].name, ns[i] / 1e6 / samples);
			PS3_Log(line);
		}
	}
#endif
	frameTotal = 0;
	frames = 0;
	lastLog = now;
}
