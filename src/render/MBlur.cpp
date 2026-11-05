#ifndef LIBRW
#define WITHD3D
#endif
#include "common.h"
#ifndef LIBRW
#include <d3d8caps.h>
#endif

#include "General.h"
#include "RwHelper.h"
#include "Camera.h"
#include "Timecycle.h"
#include "Particle.h"
#include "Timer.h"
#include "Hud.h"
#include "Frontend.h"
#include "MBlur.h"
#include "postfx.h"
#ifdef __PS3__
// patch 88: what happens to the drops on the screen, for the [drops] log line
// (Particle.cpp): [0] added, [1] list full, [2] off the screen, [3] on
// another drop, [4] radar, [5] HUD, [6] vehicle or zone name; [7] drawn
int32 gPS3DropFx[8];
#endif

// Originally taken from RW example 'mblur'

RwRaster *CMBlur::pFrontBuffer;
bool CMBlur::ms_bJustInitialised;
bool CMBlur::ms_bScaledBlur;
bool CMBlur::BlurOn;
float CMBlur::Drunkness;

int32 CMBlur::pBufVertCount;

static RwIm2DVertex Vertex[4];
static RwIm2DVertex Vertex2[4];
static RwImVertexIndex Index[6] = { 0, 1, 2, 0, 2, 3 };

#ifndef LIBRW
extern "C" D3DCAPS8 _RwD3D8DeviceCaps;
#endif
RwBool
CMBlur::MotionBlurOpen(RwCamera *cam)
{
#ifdef EXTENDED_COLOURFILTER
	CPostFX::Open(cam);
	return TRUE;
#else
#ifdef GTA_PS2
	RwRect rect = {0, 0, 0, 0};
	
	if (pFrontBuffer)
		return TRUE;
	
	BlurOn = true;
	
	rect.w = RwRasterGetWidth(RwCameraGetRaster(cam));
	rect.h = RwRasterGetHeight(RwCameraGetRaster(cam));
	
	pFrontBuffer = RwRasterCreate(0, 0, 0, rwRASTERDONTALLOCATE|rwRASTERTYPECAMERATEXTURE);
	if (!pFrontBuffer)
	{
		printf("Error creating raster\n");
		return FALSE;
	}
	
	RwRaster *raster = RwRasterSubRaster(pFrontBuffer, RwCameraGetRaster(cam), &rect);
	if (!raster)
	{
		RwRasterDestroy(pFrontBuffer);
		pFrontBuffer = NULL;
		printf("Error subrastering\n");
		return FALSE;
	}
	
	CreateImmediateModeData(cam, &rect);
#else
	RwRect rect = { 0, 0, 0, 0 };

	if(pFrontBuffer)
		MotionBlurClose();
	
#ifndef LIBRW
	extern void _GetVideoMemInfo(LPDWORD total, LPDWORD avaible);
	DWORD total, avaible;
	
	_GetVideoMemInfo(&total, &avaible);
	debug("Available video memory %d\n", avaible);
#endif
		
	if(BlurOn)
	{
		uint32 width  = Pow(2.0f, int32(log2(RwRasterGetWidth (RwCameraGetRaster(cam))))+1);
		uint32 height = Pow(2.0f, int32(log2(RwRasterGetHeight(RwCameraGetRaster(cam))))+1);
		uint32 depth  = RwRasterGetDepth(RwCameraGetRaster(cam));
		
#ifndef LIBRW
		extern DWORD _dwMemTotalVideo;
		if ( _RwD3D8DeviceCaps.MaxTextureWidth >= width && _RwD3D8DeviceCaps.MaxTextureHeight >= height )
		{
			total = _dwMemTotalVideo - 3 *
				( RwRasterGetDepth(RwCameraGetRaster(cam))
				* RwRasterGetHeight(RwCameraGetRaster(cam))
				* RwRasterGetWidth(RwCameraGetRaster(cam)) / 8 );
			BlurOn = total >= height*width*(depth/8) + (12*1024*1024) /*12 MB*/;
		}
		else
			BlurOn = false;
#endif
		
		if ( BlurOn )
		{
			ms_bScaledBlur = false;
			rect.w = width;
			rect.h = height;
			
			pFrontBuffer = RwRasterCreate(rect.w, rect.h, depth, rwRASTERTYPECAMERATEXTURE);
			if ( !pFrontBuffer )
			{
				debug("MBlurOpen can't create raster.");
				BlurOn = false;
				rect.w = RwRasterGetWidth(RwCameraGetRaster(cam));
				rect.h = RwRasterGetHeight(RwCameraGetRaster(cam));
			}
			else
				ms_bJustInitialised = true;
		}
		else
		{
			rect.w = RwRasterGetWidth(RwCameraGetRaster(cam));
			rect.h = RwRasterGetHeight(RwCameraGetRaster(cam));
		}
		
#ifndef LIBRW
		_GetVideoMemInfo(&total, &avaible);
		debug("Available video memory %d\n", avaible);
#endif
		CreateImmediateModeData(cam, &rect);
	}
	else
	{
		rect.w = RwRasterGetWidth(RwCameraGetRaster(cam));
		rect.h = RwRasterGetHeight(RwCameraGetRaster(cam));
		CreateImmediateModeData(cam, &rect);
	}
	
	return TRUE;
#endif
#endif
}

RwBool
CMBlur::MotionBlurClose(void)
{
#ifdef EXTENDED_COLOURFILTER
	CPostFX::Close();
#else
	if(pFrontBuffer){
		RwRasterDestroy(pFrontBuffer);
		pFrontBuffer = nil;
		
		return TRUE;
	}
#endif
	return FALSE;
}

void
CMBlur::CreateImmediateModeData(RwCamera *cam, RwRect *rect)
{
	float zero, xmax, ymax;

	if(RwRasterGetDepth(RwCameraGetRaster(cam)) == 16){
		zero = HALFPX;
		xmax = rect->w + HALFPX;
		ymax = rect->h + HALFPX;
	}else{
		zero = -HALFPX;
		xmax = rect->w - HALFPX;
		ymax = rect->h - HALFPX;
	}

	RwIm2DVertexSetScreenX(&Vertex[0], zero);
	RwIm2DVertexSetScreenY(&Vertex[0], zero);
	RwIm2DVertexSetScreenZ(&Vertex[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[0], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[1], zero);
	RwIm2DVertexSetScreenY(&Vertex[1], ymax);
	RwIm2DVertexSetScreenZ(&Vertex[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[1], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[1], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[1], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[2], xmax);
	RwIm2DVertexSetScreenY(&Vertex[2], ymax);
	RwIm2DVertexSetScreenZ(&Vertex[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[2], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[3], xmax);
	RwIm2DVertexSetScreenY(&Vertex[3], zero);
	RwIm2DVertexSetScreenZ(&Vertex[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[3], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[3], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[3], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[3], 255, 255, 255, 255);


	RwIm2DVertexSetScreenX(&Vertex2[0], zero + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[0], zero + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[0], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[1], 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[1], ymax + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[1], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[1], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[1], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[2], xmax + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[2], ymax + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[2], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[3], xmax + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[3], zero + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[3], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[3], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[3], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[3], 255, 255, 255, 255);
}

void
CMBlur::CreateImmediateModeData(RwCamera *cam, RwRect *rect, RwIm2DVertex *verts, RwRGBA color, float u1Off, float v1Off, float u2Off, float v2Off, float z, int fullTexture)
{
	float x1 = rect->x;
	float y1 = rect->y;
	float x2 = rect->w;
	float y2 = rect->h;

	float u1, v1, u2, v2;
	if(fullTexture){
		u1 = 0.0f;
		v1 = 0.0f;
		u2 = 1.0f;
		v2 = 1.0f;
	}else{
		if(RwRasterGetDepth(RwCameraGetRaster(cam)) == 16){
			x1 += HALFPX;
			y1 += HALFPX;
			x2 += HALFPX;
			y2 += HALFPX;
		}else{
			x1 -= HALFPX;
			y1 -= HALFPX;
			x2 -= HALFPX;
			y2 -= HALFPX;
		}

		int32 width  = Pow(2.0f, int32(log2(RwRasterGetWidth (RwCameraGetRaster(cam))))+1);
		int32 height = Pow(2.0f, int32(log2(RwRasterGetHeight(RwCameraGetRaster(cam))))+1);
		u1 = x1/width + u1Off;
		v1 = y1/height + v1Off;
		u2 = x2/width + u2Off;
		v2 = y2/height + v2Off;
		u1 = clamp(u1, 0.0f, 1.0f);
		v1 = clamp(v1, 0.0f, 1.0f);
		u2 = clamp(u2, 0.0f, 1.0f);
		v2 = clamp(v2, 0.0f, 1.0f);
	}

#ifdef __PS3__
	// patch 89: the screen effects (drops, heat haze) were cut by the RSX
	// when the camera looked straight or up. z is a screen Z made between
	// RwIm2DGetNearScreenZ and GetFarScreenZ, -1 and 1 on the PS3 (0 and 1 in
	// D3D8), and 1/z went in as RecipCameraZ: the im2d shader takes the
	// camera z (1/recipz) as the vertex's w. A drop 1 m (blood) or 1.5-2 m
	// (water) from the camera has a negative z with the near clip at 0.9 m
	// (straight or up; looking down at the ground behind brings the near
	// clip closer and z went positive): a negative w, the quad clipped, not
	// drawn. The camera z is the near clip's (as CameraZ already says)
	float recipz = 1.0f/RwCameraGetNearClipPlane(cam);
	// and a drop nearer than the near clip (a script's near clip over 1 m)
	// would be out of the depth range: kept just inside it
	float zn = RwIm2DGetNearScreenZ(), zf = RwIm2DGetFarScreenZ();
	z = clamp(z, zn + (zf - zn)*0.0001f, zf - (zf - zn)*0.0001f);
#else
	float recipz = 1.0f/z;
#endif
	// TODO: CameraZ is wrong, what should we do?
	RwIm2DVertexSetScreenX(&verts[0], x1);
	RwIm2DVertexSetScreenY(&verts[0], y1);
	RwIm2DVertexSetScreenZ(&verts[0], z);
	RwIm2DVertexSetCameraZ(&verts[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&verts[0], recipz);
	RwIm2DVertexSetU(&verts[0], u1, recipz);
	RwIm2DVertexSetV(&verts[0], v1, recipz);
	RwIm2DVertexSetIntRGBA(&verts[0], color.red, color.green, color.blue, color.alpha);

	RwIm2DVertexSetScreenX(&verts[1], x1);
	RwIm2DVertexSetScreenY(&verts[1], y2);
	RwIm2DVertexSetScreenZ(&verts[1], z);
	RwIm2DVertexSetCameraZ(&verts[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&verts[1], recipz);
	RwIm2DVertexSetU(&verts[1], u1, recipz);
	RwIm2DVertexSetV(&verts[1], v2, recipz);
	RwIm2DVertexSetIntRGBA(&verts[1], color.red, color.green, color.blue, color.alpha);

	RwIm2DVertexSetScreenX(&verts[2], x2);
	RwIm2DVertexSetScreenY(&verts[2], y2);
	RwIm2DVertexSetScreenZ(&verts[2], z);
	RwIm2DVertexSetCameraZ(&verts[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&verts[2], recipz);
	RwIm2DVertexSetU(&verts[2], u2, recipz);
	RwIm2DVertexSetV(&verts[2], v2, recipz);
	RwIm2DVertexSetIntRGBA(&verts[2], color.red, color.green, color.blue, color.alpha);

	RwIm2DVertexSetScreenX(&verts[3], x2);
	RwIm2DVertexSetScreenY(&verts[3], y1);
	RwIm2DVertexSetScreenZ(&verts[3], z);
	RwIm2DVertexSetCameraZ(&verts[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&verts[3], recipz);
	RwIm2DVertexSetU(&verts[3], u2, recipz);
	RwIm2DVertexSetV(&verts[3], v1, recipz);
	RwIm2DVertexSetIntRGBA(&verts[3], color.red, color.green, color.blue, color.alpha);
}

void
CMBlur::MotionBlurRender(RwCamera *cam, uint32 red, uint32 green, uint32 blue, uint32 blur, int32 type, uint32 bluralpha)
{
#ifdef EXTENDED_COLOURFILTER
	CPostFX::Render(cam, red, green, blue, blur, type, bluralpha);
#else
	RwRGBA color = { (RwUInt8)red, (RwUInt8)green, (RwUInt8)blue, (RwUInt8)blur };
#ifdef GTA_PS2
	if( pFrontBuffer )
		OverlayRender(cam, pFrontBuffer, color, type, bluralpha);
#else
	bool ps3Overlaid = false;
	if(ms_bJustInitialised)
		ms_bJustInitialised = false;
	else{
		OverlayRender(cam, pFrontBuffer, color, type, bluralpha);
		ps3Overlaid = true;
	}
	if(BlurOn){
		RwRasterPushContext(pFrontBuffer);
		RwRasterRenderFast(RwCameraGetRaster(cam), 0, 0);
		RwRasterPopContext();
	}
#ifdef __PS3__
	// from reVC-PS3-Optimised (Gamer100Dev): the drops and blood on the lens sample the
	// scene through pFrontBuffer; drawn after it holds this frame (in
	// OverlayRender it was the frame before: a patch of stale colour on what
	// moves, cutscene characters). The trail above still uses the old one
	if(ps3Overlaid && type != MOTION_BLUR_SNIPER)
		OverlayRenderFx(cam, pFrontBuffer);
#endif
#endif
#endif
}

static uint8 DrunkBlurRed = 128;
static uint8 DrunkBlurGreen = 128;
static uint8 DrunkBlurBlue = 128;
static int32 DrunkBlurIncrement = 1;

#ifdef __PS3__
#include "ps3_platform.h"	// PS3_Logf
// The sniper and rocket launcher views (and the trails) draw pFrontBuffer, the
// last frame copied 1:1 into its top left, over the screen. The quad was made
// once, at MotionBlurOpen, power of two sized with UVs 0..1: on the PS3 that
// showed the picture shrunk in the middle of a black frame. Every draw now: the
// quad over the camera, UVs over the part of pFrontBuffer the copy fills.
// [mblur] logs the sizes (three times) to find out why they didn't match
static void
PS3_MBlurSetQuad(RwIm2DVertex *v, RwCamera *cam, float x0, float y0, float x1, float y1, float u, float vv)
{
	float rz = 1.0f/RwCameraGetNearClipPlane(cam);
	float xs[4] = { x0, x0, x1, x1 }, ys[4] = { y0, y1, y1, y0 };
	float us[4] = { 0.0f, 0.0f, u, u }, vs[4] = { 0.0f, vv, vv, 0.0f };
	for(int i = 0; i < 4; i++){
		RwIm2DVertexSetScreenX(&v[i], xs[i]);
		RwIm2DVertexSetScreenY(&v[i], ys[i]);
		RwIm2DVertexSetScreenZ(&v[i], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&v[i], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&v[i], rz);
		RwIm2DVertexSetU(&v[i], us[i], rz);
		RwIm2DVertexSetV(&v[i], vs[i], rz);
	}
}

static void
PS3_MBlurFit(RwCamera *cam, RwRaster *raster, int32 type)
{
	RwRaster *cr = RwCameraGetRaster(cam);
	float w = RwRasterGetWidth(cr), h = RwRasterGetHeight(cr);
	float u = 1.0f, v = 1.0f;
	if(raster && CMBlur::BlurOn && RwRasterGetWidth(raster) > 0 && RwRasterGetHeight(raster) > 0){
		u = Min(w / RwRasterGetWidth(raster), 1.0f);
		v = Min(h / RwRasterGetHeight(raster), 1.0f);
	}
	float zero = -HALFPX;
	PS3_MBlurSetQuad(Vertex, cam, zero, zero, w - HALFPX, h - HALFPX, u, v);
	PS3_MBlurSetQuad(Vertex2, cam, zero + 2.0f, zero + 2.0f, w + 2.0f - HALFPX, h + 2.0f - HALFPX, u, v);
	static int logged;
	if(type == MOTION_BLUR_SNIPER && logged < 3){
		logged++;
		PS3_Logf("[mblur] sniper view: camera raster %dx%d at %d,%d of %dx%d, front buffer %dx%d (blur %d), uv %.3f %.3f, near %.3f",
		         cr->width, cr->height, cr->offsetX, cr->offsetY, cr->parent ? cr->parent->width : 0, cr->parent ? cr->parent->height : 0,
		         raster ? raster->width : 0, raster ? raster->height : 0, CMBlur::BlurOn, u, v, RwCameraGetNearClipPlane(cam));
	}
}
#endif

void
CMBlur::OverlayRender(RwCamera *cam, RwRaster *raster, RwRGBA color, int32 type, int32 bluralpha)
{
	int r, g, b, a;

	r = color.red;
	g = color.green;
	b = color.blue;
	a = color.alpha;

	DefinedState();
#ifdef __PS3__
	PS3_MBlurFit(cam, raster, type);
#endif

	switch(type)
	{
	case MOTION_BLUR_SECURITY_CAM:
		r = 0;
		g = 255;
		b = 0;
		a = 128;
		break;
	case MOTION_BLUR_INTRO:
		r = 100;
		g = 220;
		b = 230;
		a = 158;
		break;
	case MOTION_BLUR_INTRO2:
		r = 80;
		g = 255;
		b = 230;
		a = 138;
		break;
	case MOTION_BLUR_INTRO3:
		r = 255;
		g = 60;
		b = 60;
		a = 200;
		break;
	case MOTION_BLUR_INTRO4:
		r = 255;
		g = 180;
		b = 180;
		a = 128;
		break;
	}

	if(!BlurOn){
		// gta clamps these to 255 (probably a macro or inlined function)
		int ovR = r * 0.6f;
		int ovG = g * 0.6f;
		int ovB = b * 0.6f;
		int ovA = type == MOTION_BLUR_SNIPER ? a : a*0.6f;
		RwIm2DVertexSetIntRGBA(&Vertex[0], ovR, ovG, ovB, ovA);
		RwIm2DVertexSetIntRGBA(&Vertex[1], ovR, ovG, ovB, ovA);
		RwIm2DVertexSetIntRGBA(&Vertex[2], ovR, ovG, ovB, ovA);
		RwIm2DVertexSetIntRGBA(&Vertex[3], ovR, ovG, ovB, ovA);
	}else{
		RwIm2DVertexSetIntRGBA(&Vertex2[0], r, g, b, a);
		RwIm2DVertexSetIntRGBA(&Vertex[0], r, g, b, a);
		RwIm2DVertexSetIntRGBA(&Vertex2[1], r, g, b, a);
		RwIm2DVertexSetIntRGBA(&Vertex[1], r, g, b, a);
		RwIm2DVertexSetIntRGBA(&Vertex2[2], r, g, b, a);
		RwIm2DVertexSetIntRGBA(&Vertex[2], r, g, b, a);
		RwIm2DVertexSetIntRGBA(&Vertex2[3], r, g, b, a);
		RwIm2DVertexSetIntRGBA(&Vertex[3], r, g, b, a);
	}

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

	if(BlurOn){
		if(type == MOTION_BLUR_SNIPER){
			RwIm2DVertexSetIntRGBA(&Vertex2[0], r, g, b, 80);
			RwIm2DVertexSetIntRGBA(&Vertex2[1], r, g, b, 80);
			RwIm2DVertexSetIntRGBA(&Vertex2[2], r, g, b, 80);
			RwIm2DVertexSetIntRGBA(&Vertex2[3], r, g, b, 80);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			pBufVertCount = 0;
		}else{
			RwIm2DVertexSetIntRGBA(&Vertex2[0], r*2, g*2, b*2, 30);
			RwIm2DVertexSetIntRGBA(&Vertex2[1], r*2, g*2, b*2, 30);
			RwIm2DVertexSetIntRGBA(&Vertex2[2], r*2, g*2, b*2, 30);
			RwIm2DVertexSetIntRGBA(&Vertex2[3], r*2, g*2, b*2, 30);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex2, 4, Index, 6);

			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

			RwIm2DVertexSetIntRGBA(&Vertex2[0], r, g, b, a);
			RwIm2DVertexSetIntRGBA(&Vertex[0], r, g, b, a);
			RwIm2DVertexSetIntRGBA(&Vertex2[1], r, g, b, a);
			RwIm2DVertexSetIntRGBA(&Vertex[1], r, g, b, a);
			RwIm2DVertexSetIntRGBA(&Vertex2[2], r, g, b, a);
			RwIm2DVertexSetIntRGBA(&Vertex[2], r, g, b, a);
			RwIm2DVertexSetIntRGBA(&Vertex2[3], r, g, b, a);
			RwIm2DVertexSetIntRGBA(&Vertex[3], r, g, b, a);
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex2, 4, Index, 6);
		}
	}

	int DrunkBlurAlpha = 175.0f * Drunkness;
	if(DrunkBlurAlpha != 0){
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		if(BlurOn){
			RwIm2DVertexSetIntRGBA(&Vertex[0], 255, 255, 255, DrunkBlurAlpha);
			RwIm2DVertexSetIntRGBA(&Vertex[1], 255, 255, 255, DrunkBlurAlpha);
			RwIm2DVertexSetIntRGBA(&Vertex[2], 255, 255, 255, DrunkBlurAlpha);
			RwIm2DVertexSetIntRGBA(&Vertex[3], 255, 255, 255, DrunkBlurAlpha);
		}else{
			RwIm2DVertexSetIntRGBA(&Vertex[0], DrunkBlurRed, DrunkBlurGreen, DrunkBlurBlue, DrunkBlurAlpha);
			RwIm2DVertexSetIntRGBA(&Vertex[1], DrunkBlurRed, DrunkBlurGreen, DrunkBlurBlue, DrunkBlurAlpha);
			RwIm2DVertexSetIntRGBA(&Vertex[2], DrunkBlurRed, DrunkBlurGreen, DrunkBlurBlue, DrunkBlurAlpha);
			RwIm2DVertexSetIntRGBA(&Vertex[3], DrunkBlurRed, DrunkBlurGreen, DrunkBlurBlue, DrunkBlurAlpha);
			if(DrunkBlurIncrement){
				if(DrunkBlurRed < 255) DrunkBlurRed++;
				if(DrunkBlurGreen < 255) DrunkBlurGreen++;
				if(DrunkBlurBlue < 255) DrunkBlurBlue++;
				if(DrunkBlurRed == 255)
					DrunkBlurIncrement = 0;
			}else{
				if(DrunkBlurRed > 128) DrunkBlurRed--;
				if(DrunkBlurGreen > 128) DrunkBlurGreen--;
				if(DrunkBlurBlue > 128) DrunkBlurBlue--;
				if(DrunkBlurRed == 128)
					DrunkBlurIncrement = 1;
			}
		}
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	}

#ifndef __PS3__	// PS3: in MotionBlurRender, after pFrontBuffer is this frame
	if(type != MOTION_BLUR_SNIPER)
		OverlayRenderFx(cam, pFrontBuffer);
#endif

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

void
CMBlur::SetDrunkBlur(float drunkness)
{
	Drunkness = clamp(drunkness, 0.0f, 1.0f);
}

void
CMBlur::ClearDrunkBlur()
{
	Drunkness = 0.0f;
	CTimer::SetTimeScale(1.0f);
}

#define NUM_RENDER_FX 64

static RwRect fxRect[NUM_RENDER_FX];
static FxType fxType[NUM_RENDER_FX];
static float fxZ[NUM_RENDER_FX];

bool
CMBlur::PosInside(RwRect *rect, float x1, float y1, float x2, float y2)
{
	if((rect->x < x1 - 10.0f || rect->x > x2 + 10.0f || rect->y < y1 - 10.0f || rect->y > y2 + 10.0f) &&
	   (rect->w < x1 - 10.0f || rect->w > x2 + 10.0f || rect->h < y1 - 10.0f || rect->h > y2 + 10.0f) &&
	   (rect->x < x1 - 10.0f || rect->x > x2 + 10.0f || rect->h < y1 - 10.0f || rect->h > y2 + 10.0f) &&
	   (rect->w < x1 - 10.0f || rect->w > x2 + 10.0f || rect->y < y1 - 10.0f || rect->y > y2 + 10.0f))
		return false;
	return true;
}

bool
CMBlur::AddRenderFx(RwCamera *cam, RwRect *rect, float z, FxType type)
{
#ifdef __PS3__
	bool ps3Drop = type == FXTYPE_WATER1 || type == FXTYPE_WATER2 || type == FXTYPE_BLOOD1 || type == FXTYPE_BLOOD2;
#define PS3_DROPFX(n) do { if(ps3Drop) gPS3DropFx[n]++; } while(0)
#else
#define PS3_DROPFX(n)
#endif
	if(pBufVertCount >= NUM_RENDER_FX){
		PS3_DROPFX(1);
		return false;
	}

	rect->x = Max(rect->x, 0);
	rect->y = Max(rect->y, 0);
	rect->w = Min(rect->w, SCREEN_WIDTH);
	rect->h = Min(rect->h, SCREEN_HEIGHT);
	if(rect->x >= rect->w || rect->y >= rect->h){
		PS3_DROPFX(2);
		return false;
	}

	switch(type){
	case FXTYPE_WATER1:
	case FXTYPE_WATER2:
	case FXTYPE_BLOOD1:
	case FXTYPE_BLOOD2:
	case FXTYPE_HEATHAZE:	// code seems to be duplicated for this case
		for(int i = 0; i < pBufVertCount; i++)
			if(fxType[i] == type && PosInside(rect, fxRect[i].x-10.0f, fxRect[i].y-10.0f, fxRect[i].w+10.0f, fxRect[i].h+10.0f)){
				PS3_DROPFX(3);
				return false;
			}
		// TODO: fix aspect ratio scaling
		// radar
		if(PosInside(rect, 40.0f, SCREEN_SCALE_FROM_BOTTOM(116.0f), 40.0f + SCREEN_SCALE_X(94.0f), SCREEN_SCALE_FROM_BOTTOM(116.0f - 76.0f))){
			PS3_DROPFX(4);
			return false;
		}
		// HUD
		if(PosInside(rect, 400.0f, 0.0f, SCREEN_WIDTH, 90.0f)){
			PS3_DROPFX(5);
			return false;
		}
		// vehicle name
		if(CHud::m_VehicleState != 0 && PosInside(rect, SCREEN_WIDTH/2, 350.0f, SCREEN_WIDTH, SCREEN_HEIGHT)){
			PS3_DROPFX(6);
			return false;
		}
		// zone name
		if(CHud::m_ZoneState != 0 && PosInside(rect, SCREEN_WIDTH/2, 350.0f, SCREEN_WIDTH, SCREEN_HEIGHT)){
			PS3_DROPFX(6);
			return false;
		}
		break;
	}
	PS3_DROPFX(0);
#undef PS3_DROPFX

	fxRect[pBufVertCount] = *rect;
	fxZ[pBufVertCount] = z;
	fxType[pBufVertCount] = type;
	pBufVertCount++;

	return true;
}

void
CMBlur::OverlayRenderFx(RwCamera *cam, RwRaster *frontBuf)
{
	bool drawWaterDrops = false;
	RwIm2DVertex verts[4];
	int red = (0.75f*CTimeCycle::GetDirectionalRed() + CTimeCycle::GetAmbientRed())*0.55f * 255;
	int green = (0.75f*CTimeCycle::GetDirectionalGreen() + CTimeCycle::GetAmbientGreen())*0.55f * 255;
	int blue = (0.75f*CTimeCycle::GetDirectionalBlue() + CTimeCycle::GetAmbientBlue())*0.55f * 255;
	red = clamp(red, 0, 255);
	green = clamp(green, 0, 255);
	blue = clamp(blue, 0, 255);

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

#ifdef LIBRW
	rw::SetRenderState(rw::STENCILENABLE, TRUE);
#else
	RwD3D8SetRenderState(D3DRS_STENCILENABLE, TRUE);
#endif

	for(int i = 0; i < pBufVertCount; i++)
		switch(fxType[i]){
		case FXTYPE_WATER1:
		case FXTYPE_WATER2:
		case FXTYPE_BLOOD1:
		case FXTYPE_BLOOD2: {
			drawWaterDrops = true;
#ifdef __PS3__
			// patch 88: the drops are on the lens: drawn over everything, no
			// depth test (it was on, with the drop's depth at 1-2 m)
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
			gPS3DropFx[7]++;
#endif
			int32 width  = Pow(2.0f, int32(log2(RwRasterGetWidth (RwCameraGetRaster(cam))))+1);
			int32 height = Pow(2.0f, int32(log2(RwRasterGetHeight(RwCameraGetRaster(cam))))+1);

			float u1Off = (fxRect[i].w - fxRect[i].x)/width;
			float u2Off = u1Off - (fxRect[i].w - fxRect[i].x + 0.5f)*0.66f/width;
			float halfHeight = (fxRect[i].h - fxRect[i].y + 0.5f)*0.25f/height;

			if(RwRasterGetDepth(RwCameraGetRaster(cam)) == 16){
				if(fxType[i] == FXTYPE_BLOOD1 || fxType[i] == FXTYPE_BLOOD2)
					CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(255, 0, 0, 128), 0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
				else
					CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(32, 32, 32, 225), 0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
			}else{
				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(32, 32, 32, 225), 0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
			}

			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpDotRaster);
#ifdef LIBRW
			rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILALWAYS);
			rw::SetRenderState(rw::STENCILFUNCTIONREF, 1);
			rw::SetRenderState(rw::STENCILFUNCTIONMASK, 0xFFFFFFFF);
			rw::SetRenderState(rw::STENCILFUNCTIONWRITEMASK, 0xFFFFFFFF);
			rw::SetRenderState(rw::STENCILZFAIL, rw::STENCILKEEP);
			rw::SetRenderState(rw::STENCILFAIL, rw::STENCILKEEP);
			rw::SetRenderState(rw::STENCILPASS, rw::STENCILREPLACE);
#else
			RwD3D8SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
			RwD3D8SetRenderState(D3DRS_STENCILREF, 1);
			RwD3D8SetRenderState(D3DRS_STENCILMASK, 0xFFFFFFFF);
			RwD3D8SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
			RwD3D8SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
			RwD3D8SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
			RwD3D8SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
#endif
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);

			if(RwRasterGetDepth(RwCameraGetRaster(cam)) != 16){
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, frontBuf);
#ifdef LIBRW
				rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILEQUAL);
				rw::SetRenderState(rw::STENCILPASS, rw::STENCILKEEP);
#else
				RwD3D8SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
				RwD3D8SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
#endif
				if(BlurOn){
					if(fxType[i] == FXTYPE_BLOOD1 || fxType[i] == FXTYPE_BLOOD2)
						CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(255, 0, 0, 255), u1Off, 0.0f+halfHeight, u2Off, 0.0f-halfHeight, fxZ[i], false);
					else
						CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(225, 225, 225, 160), u1Off, 0.0f+halfHeight, u2Off, 0.0f-halfHeight, fxZ[i], false);
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDDESTALPHA);
					RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVDESTALPHA);
				}else{
					if(fxType[i] == FXTYPE_BLOOD1 || fxType[i] == FXTYPE_BLOOD2)
						CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(255, 0, 0, 128), u1Off, 0.0f+halfHeight, u2Off, 0.0f-halfHeight, fxZ[i], false);
					else
						CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(128, 128, 128, 32), u1Off, 0.0f+halfHeight, u2Off, 0.0f-halfHeight, fxZ[i], false);
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
					RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
				}
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);
			}
			break;
		}
		case FXTYPE_SPLASH1:
		case FXTYPE_SPLASH2:
		case FXTYPE_SPLASH3:
			drawWaterDrops = true;
			break;

		case FXTYPE_HEATHAZE:
#ifdef __PS3__
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);	// the haze is in the world
#endif
			if(TheCamera.GetScreenFadeStatus() == FADE_0 && frontBuf){
				int alpha = FrontEndMenuManager.m_PrefsBrightness > 255 ?
					FrontEndMenuManager.m_PrefsBrightness - 90 :
					FrontEndMenuManager.m_PrefsBrightness - 130;
				alpha = clamp(alpha, 16, 200)/2;

				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(0, 0, 0, alpha), 0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpHeatHazeRaster);
#ifdef LIBRW
				rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILALWAYS);
				rw::SetRenderState(rw::STENCILFUNCTIONREF, 1);
				rw::SetRenderState(rw::STENCILFUNCTIONMASK, 0xFFFFFFFF);
				rw::SetRenderState(rw::STENCILFUNCTIONWRITEMASK, 0xFFFFFFFF);
				rw::SetRenderState(rw::STENCILZFAIL, rw::STENCILKEEP);
				rw::SetRenderState(rw::STENCILFAIL, rw::STENCILKEEP);
				rw::SetRenderState(rw::STENCILPASS, rw::STENCILREPLACE);
#else
				RwD3D8SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
				RwD3D8SetRenderState(D3DRS_STENCILREF, 1);
				RwD3D8SetRenderState(D3DRS_STENCILMASK, 0xFFFFFFFF);
				RwD3D8SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
				RwD3D8SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
				RwD3D8SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
				RwD3D8SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
#endif
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);

				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(255, 255, 255, alpha),
					CGeneral::GetRandomNumberInRange(-0.002f, 0.002f),
					CGeneral::GetRandomNumberInRange(-0.002f, 0.002f),
					CGeneral::GetRandomNumberInRange(-0.002f, 0.002f),
					CGeneral::GetRandomNumberInRange(-0.002f, 0.002f),
					fxZ[i], false);
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, frontBuf);
#ifdef LIBRW
				rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILEQUAL);
				rw::SetRenderState(rw::STENCILPASS, rw::STENCILKEEP);
#else
				RwD3D8SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
				RwD3D8SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
#endif
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);
			}
			break;
		}
#ifdef LIBRW
	rw::SetRenderState(rw::STENCILENABLE, FALSE);
#else
	RwD3D8SetRenderState(D3DRS_STENCILENABLE, FALSE);
#endif

	if(drawWaterDrops){
#ifdef __PS3__
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);	// patch 88
#endif
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

		// Draw drops
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpRainDripRaster[0]);
		for(int i = 0; i < pBufVertCount; i++)
			if(fxType[i] == FXTYPE_WATER1 || fxType[i] == FXTYPE_BLOOD1){
				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(red, green, blue, fxType[i] == FXTYPE_BLOOD1 ? 255 : 192),
					0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);
			}
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpRainDripRaster[1]);
		for(int i = 0; i < pBufVertCount; i++)
			if(fxType[i] == FXTYPE_WATER2 || fxType[i] == FXTYPE_BLOOD2){
				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(red, green, blue, fxType[i] == FXTYPE_BLOOD2 ? 255 : 192),
					0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);
			}

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpCarSplashRaster[0]);
		for(int i = 0; i < pBufVertCount; i++)
			if(fxType[i] == FXTYPE_SPLASH1 || fxType[i] == FXTYPE_SPLASH2 || fxType[i] == FXTYPE_SPLASH3){
				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(200, 200, 200, 255),
					0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);
			}

		// Darken the water drops
		int alpha = 192*0.5f;
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpRainDripDarkRaster[0]);
		for(int i = 0; i < pBufVertCount; i++)
			if(fxType[i] == FXTYPE_WATER1){
				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(red, green, blue, alpha),
					0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);
			}
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpRainDripDarkRaster[1]);
		for(int i = 0; i < pBufVertCount; i++)
			if(fxType[i] == FXTYPE_WATER2){
				CreateImmediateModeData(cam, &fxRect[i], verts, CRGBA(red, green, blue, alpha),
					0.0f, 0.0f, 0.0f, 0.0f, fxZ[i], true);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, Index, 6);
			}
	}

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	pBufVertCount = 0;
}
