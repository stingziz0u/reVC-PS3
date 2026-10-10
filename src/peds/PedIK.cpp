#include "common.h"

#include "Bones.h"
#include "Camera.h"
#include "PedIK.h"
#include "Ped.h"
#include "General.h"
#include "RwHelper.h"
#ifdef __PS3__
#include "Pools.h"
#include "RpAnimBlend.h"
#include "AnimBlendClumpData.h"
#include "ps3_platform.h"
#endif

LimbMovementInfo CPedIK::ms_torsoInfo = { DEGTORAD(50.0f), DEGTORAD(-50.0f), DEGTORAD(8.0f), DEGTORAD(45.0f), DEGTORAD(-45.0f), DEGTORAD(5.0f) };
LimbMovementInfo CPedIK::ms_headInfo = { DEGTORAD(90.0f), DEGTORAD(-90.0f), DEGTORAD(15.0f), DEGTORAD(45.0f), DEGTORAD(-45.0f), DEGTORAD(8.0f) };
LimbMovementInfo CPedIK::ms_headRestoreInfo = { DEGTORAD(90.0f), DEGTORAD(-90.0f), DEGTORAD(10.0f), DEGTORAD(45.0f), DEGTORAD(-45.0f), DEGTORAD(5.0f) };
LimbMovementInfo CPedIK::ms_upperArmInfo = { DEGTORAD(5.0f), DEGTORAD(-120.0f), DEGTORAD(20.0f), DEGTORAD(70.0f), DEGTORAD(-70.0f), DEGTORAD(20.0f) };
LimbMovementInfo CPedIK::ms_lowerArmInfo = { DEGTORAD(60.0f), DEGTORAD(0.0f), DEGTORAD(15.0f), DEGTORAD(90.0f), DEGTORAD(-90.0f), DEGTORAD(10.0f) };

const RwV3d XaxisIK = { 1.0f, 0.0f, 0.0f};
const RwV3d YaxisIK = { 0.0f, 1.0f, 0.0f};
const RwV3d ZaxisIK = { 0.0f, 0.0f, 1.0f};

CPedIK::CPedIK(CPed *ped)
{
	m_ped = ped;
	m_flags = 0;
	m_headOrient.yaw = 0.0f;
	m_headOrient.pitch = 0.0f;
	m_torsoOrient.yaw = 0.0f;
	m_torsoOrient.pitch = 0.0f;
	m_upperArmOrient.yaw = 0.0f;
	m_upperArmOrient.pitch = 0.0f;
	m_lowerArmOrient.yaw = 0.0f;
	m_lowerArmOrient.pitch = 0.0f;
}

#ifdef __PS3__
void PS3_DescribeEntityPtr(const void *p, char *buf, int size);	// below

// a bone's matrix by bone tag (PointGunInDirectionUsingArm): checked as below
inline RwMatrix*
GetBoneMatrix(CPed *ped, int32 bone)
{
	static RwMatrix fallback;
	int idx;
	bool isFree;
	if (CPools::GetPedPool()->PS3_Owns(ped, &idx, &isFree) && !isFree && ped->m_rwObject &&
	    RwObjectGetType(ped->m_rwObject) == rpCLUMP) {
		RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(ped->GetClump());
		if (hier && hier->matrices) {
			int i = RpHAnimIDGetIndex(hier, bone);
			if (i >= 0 && i < hier->numNodes)
				return &RpHAnimHierarchyGetMatrixArray(hier)[i];
		}
	}
	PS3_CRUMB_CALLER("ped: bad bone matrix, bone", bone);
	RwMatrixSetIdentity(&fallback);
	*RwMatrixGetPos(&fallback) = TheCamera.GetPosition();
	return &fallback;
}

// The Riot crash (patch 13 log, with the trace): a rioter looking at the
// player died in LookAtPosition -> GetComponentMatrix(m_ped, PED_MID), reading
// its own node data before GetBoneMatrix. Every bone matrix of a ped is found
// through here now: each step (the ped, its clump, its frame table, the HAnim
// hierarchy, the bone index) is checked before it is followed; if one is
// wrong, it is logged ([ped] node ...) and the caller gets a matrix at the
// ped's position instead.
// The cause was CPlayerPed::RemovePedFromMeleeList clearing the ped's IK
// (fixed in patch 17); this stays as a safety net.
RwMatrix*
PS3_PedNodeMatrix(CPed *ped, int32 node, unsigned caller)
{
	static int logged;
	static RwMatrix fallback;
	const char *why = nil;
	int idx = -1, bone = -1, numNodes = -1;
	bool isFree = true, inPool;
	AnimBlendFrameData *f = nil, *first = nil;
	int numFrames = 0;
	RpHAnimHierarchy *hier = nil;

	inPool = CPools::GetPedPool()->PS3_Owns(ped, &idx, &isFree);
	if (!inPool)
		why = "not a ped";
	else if (isFree)
		why = "deleted ped";
	else if (node < 0 || node >= PED_NODE_MAX)
		why = "bad node";
	else if (ped->m_rwObject == nil || RwObjectGetType(ped->m_rwObject) != rpCLUMP)
		why = "no clump";
	else {
		CAnimBlendClumpData *data = *RPANIMBLENDCLUMPDATA(ped->GetClump());
		f = ped->m_pFrames[node];
		if (data == nil || data->frames == nil)
			why = "no animation data";
		else {
			first = data->frames;
			numFrames = data->numFrames;
			if (f < first || f >= first + numFrames)
				why = "frame not in its clump's frame table";
			else if ((hier = GetAnimHierarchyFromSkinClump(ped->GetClump())) == nil)
				why = "no HAnim hierarchy";
			else if (hier->matrices == nil || hier->numNodes <= 0)
				why = "hierarchy without matrices";
			else {
				numNodes = hier->numNodes;
				bone = f->nodeID;
				idx = RpHAnimIDGetIndex(hier, bone);
				if (idx < 0 || idx >= numNodes)
					why = "bone not in the hierarchy";
				else
					return &RpHAnimHierarchyGetMatrixArray(hier)[idx];
			}
		}
	}

	PS3_CRUMB_CALLER("ped: bad node matrix, node", node);
	if (logged < 24) {
		char desc[48];
		logged++;
		PS3_DescribeEntityPtr(ped, desc, sizeof(desc));
		if (inPool && !isFree)
			PS3_Logf("[ped] node %d: %s. ped %p = %s, model %d, state %d, rwObject %p, frame %p (table %p, %d frames), "
			         "hierarchy %p (%d nodes), bone %d -> index %d; called from @%08x",
			         node, why, ped, desc, ped->GetModelIndex(), ped->m_nPedState, (void*)ped->m_rwObject, f, first,
			         numFrames, hier, numNodes, bone, idx, caller);
		else
			PS3_Logf("[ped] node %d: %s. ped %p = %s; called from @%08x", node, why, ped, desc, caller);
	}
	RwMatrixSetIdentity(&fallback);
	if (inPool && !isFree)
		*RwMatrixGetPos(&fallback) = ped->GetPosition();
	else
		*RwMatrixGetPos(&fallback) = TheCamera.GetPosition();
	return &fallback;
}

// Once per frame (ProcessControl): the ped's IK must point to the ped, and
// every node of its frame table must be inside its clump's frame data. The
// IK code writes through these (RotateTorso, the head/arm quaternions), so a
// bad table is rebuilt from the clump; if it can't be, the ped stops looking
// and aiming with IK. Logged as [ped] skeleton ...
static bool
PS3_PedFrameTableOk(CPed *ped, int *badNode)
{
	CAnimBlendClumpData *data = *RPANIMBLENDCLUMPDATA(ped->GetClump());
	if (data == nil || data->frames == nil || GetAnimHierarchyFromSkinClump(ped->GetClump()) == nil) {
		*badNode = -1;
		return false;
	}
	for (int i = PED_MID; i < PED_NODE_MAX; i++) {
		AnimBlendFrameData *f = ped->m_pFrames[i];
		if (f < data->frames || f >= data->frames + data->numFrames) {
			*badNode = i;
			return false;
		}
	}
	return true;
}

bool
PS3_CheckPedSkeleton(CPed *ped)
{
	static int logged;
	int bad;
	if (ped->m_pedIK.m_ped != ped) {
		if (logged < 24) {
			char desc[48];
			logged++;
			PS3_DescribeEntityPtr(ped->m_pedIK.m_ped, desc, sizeof(desc));
			PS3_Logf("[ped] skeleton: IK of ped %p (model %d) pointed to %p (%s): fixed",
			         ped, ped->GetModelIndex(), ped->m_pedIK.m_ped, desc);
		}
		PS3_CRUMB("ped: IK owner fixed, model", ped->GetModelIndex());
		ped->m_pedIK.m_ped = ped;
	}
	if (ped->m_rwObject == nil || RwObjectGetType(ped->m_rwObject) != rpCLUMP || !IsClumpSkinned(ped->GetClump()))
		return false;
	if (PS3_PedFrameTableOk(ped, &bad))
		return true;
	AnimBlendFrameData *was = bad >= 0 ? ped->m_pFrames[bad] : nil;
	CAnimBlendClumpData *data = *RPANIMBLENDCLUMPDATA(ped->GetClump());
	bool fixed = false;
	if (bad >= 0) {
		RpAnimBlendClumpFillFrameArray(ped->GetClump(), ped->m_pFrames);
		fixed = PS3_PedFrameTableOk(ped, &bad);
	}
	if (logged < 24) {
		logged++;
		PS3_Logf("[ped] skeleton: ped %p model %d state %d: node %d frame %p not in its clump (table %p, %d frames): %s",
		         ped, ped->GetModelIndex(), ped->m_nPedState, bad, was, data ? data->frames : nil,
		         data ? data->numFrames : 0, fixed ? "table rebuilt" : "can't rebuild, IK off");
	}
	PS3_CRUMB("ped: frame table bad, node", bad);
	if (!fixed) {
		ped->bDontAcceptIKLookAts = true;
		ped->bIsLooking = false;
		ped->bIsRestoringLook = false;
		ped->bIsAimingGun = false;
		ped->bIsRestoringGun = false;
	}
	return fixed;
}

inline RwMatrix*
GetComponentMatrix(CPed *ped, int32 node)
{
	return PS3_PedNodeMatrix(ped, node, (unsigned)(uintptr_t)__builtin_return_address(0));
}
#else
inline RwMatrix*
GetBoneMatrix(CPed *ped, int32 bone)
{
	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(ped->GetClump());
	int idx = RpHAnimIDGetIndex(hier, bone);
	RwMatrix *mats = RpHAnimHierarchyGetMatrixArray(hier);
	return &mats[idx];
}
inline RwMatrix*
GetComponentMatrix(CPed *ped, int32 node)
{
	return GetBoneMatrix(ped, ped->m_pFrames[node]->nodeID);
}
#endif

void
CPedIK::RotateTorso(AnimBlendFrameData *node, LimbOrientation *limb, bool changeRoll)
{
	RtQuat *q = &node->hanimFrame->q;
	RtQuatRotate(q, &XaxisIK, RADTODEG(limb->yaw), rwCOMBINEREPLACE);
	RtQuatRotate(q, &ZaxisIK, RADTODEG(limb->pitch), rwCOMBINEPRECONCAT);
	m_ped->bDontAcceptIKLookAts = true;
}

#ifdef __PS3__
// The Riot crash (patches 3 and 4): a worker with a KILL_CHAR_ON_FOOT
// objective looked at its target (MoveHeadToLook -> GetComponentPosition)
// and died reading the target's ped/frame data. Other peds' positions are read
// through here (look targets, aim targets, revived peds, the camera), so check
// the ped first, touching nothing that isn't known to be mapped, and log what
// was wrong with it.

// where p points: "ped slot 12 (live)", "vehicle slot 3 (free)", ...
void
PS3_DescribeEntityPtr(const void *p, char *buf, int size)
{
	int idx;
	bool isFree;
	if (CPools::GetPedPool()->PS3_Owns(p, &idx, &isFree))
		snprintf(buf, size, "ped slot %d (%s)", idx, isFree ? "free" : "live");
	else if (CPools::GetVehiclePool()->PS3_Owns(p, &idx, &isFree))
		snprintf(buf, size, "vehicle slot %d (%s)", idx, isFree ? "free" : "live");
	else if (CPools::GetObjectPool()->PS3_Owns(p, &idx, &isFree))
		snprintf(buf, size, "object slot %d (%s)", idx, isFree ? "free" : "live");
	else if (CPools::GetDummyPool()->PS3_Owns(p, &idx, &isFree))
		snprintf(buf, size, "dummy slot %d (%s)", idx, isFree ? "free" : "live");
	else if (CPools::GetBuildingPool()->PS3_Owns(p, &idx, &isFree))
		snprintf(buf, size, "building slot %d (%s)", idx, isFree ? "free" : "live");
	else if (CPools::GetTreadablePool()->PS3_Owns(p, &idx, &isFree))
		snprintf(buf, size, "treadable slot %d (%s)", idx, isFree ? "free" : "live");
	else
		snprintf(buf, size, "not in any entity pool");
}

// p is the start of a live entity in one of the entity pools
bool
PS3_EntityLive(const void *p)
{
	int idx;
	bool isFree;
	if (CPools::GetPedPool()->PS3_Owns(p, &idx, &isFree) ||
	    CPools::GetVehiclePool()->PS3_Owns(p, &idx, &isFree) ||
	    CPools::GetObjectPool()->PS3_Owns(p, &idx, &isFree) ||
	    CPools::GetDummyPool()->PS3_Owns(p, &idx, &isFree) ||
	    CPools::GetBuildingPool()->PS3_Owns(p, &idx, &isFree) ||
	    CPools::GetTreadablePool()->PS3_Owns(p, &idx, &isFree))
		return !isFree;
	return false;
}

static bool
PS3_PedFramesOk(const CPedIK *ik, CPed *ped, uint32 node, unsigned caller)
{
	static int logged;
	const char *why = nil;
	int idx = -1;
	bool isFree = true, inPool;
	// the ped that owns this CPedIK (m_ped should be that same ped)
	CPed *owner = (CPed*)((uintptr)ik - ((uintptr)&((CPed*)0x10000)->m_pedIK - 0x10000));

	inPool = CPools::GetPedPool()->PS3_Owns(ped, &idx, &isFree);
	if (node >= PED_NODE_MAX)
		why = "bad node";
	else if (!inPool)
		why = "m_ped isn't a ped";
	else if (isFree)
		why = "deleted ped";
	else if (ped->m_rwObject == nil || RwObjectGetType(ped->m_rwObject) != rpCLUMP)
		why = "no clump";
	else {
		CAnimBlendClumpData *data = *RPANIMBLENDCLUMPDATA(ped->GetClump());
		AnimBlendFrameData *f = ped->m_pFrames[node];
		if (data == nil || data->frames == nil)
			why = "no animation data";
		else if (f < data->frames || f >= data->frames + data->numFrames)
			why = "frame table not from its clump";
	}
	if (why && logged < 16) {
		char ownerDesc[48], pedDesc[48];
		logged++;
		PS3_DescribeEntityPtr(owner, ownerDesc, sizeof(ownerDesc));
		PS3_DescribeEntityPtr(ped, pedDesc, sizeof(pedDesc));
		if (inPool)
			PS3_Logf("[ped] component %u: %s. m_ped %p = %s, model %d, state %d, rwObject %p; IK owner %p = %s; called from @%08x",
			         node, why, ped, pedDesc, ped->GetModelIndex(), ped->m_nPedState, ped->m_rwObject, owner, ownerDesc, caller);
		else
			PS3_Logf("[ped] component %u: %s. m_ped %p = %s; IK owner %p = %s; called from @%08x",
			         node, why, ped, pedDesc, owner, ownerDesc, caller);
	}
	return why == nil;
}
#endif

void
CPedIK::GetComponentPosition(RwV3d &pos, uint32 node)
{
#ifdef __PS3__
	if (!PS3_PedFramesOk(this, m_ped, node, (unsigned)(uintptr_t)__builtin_return_address(0))) {
		// the owner's position: m_ped itself may not be readable
		CPed *owner = (CPed*)((uintptr)this - ((uintptr)&((CPed*)0x10000)->m_pedIK - 0x10000));
		int idx;
		bool isFree;
		if (CPools::GetPedPool()->PS3_Owns(owner, &idx, &isFree))
			pos = owner->GetPosition();
		else
			pos = TheCamera.GetPosition();
		return;
	}
#endif
	pos = GetComponentMatrix(m_ped, node)->pos;
}

LimbMoveStatus
CPedIK::MoveLimb(LimbOrientation &limb, float targetYaw, float targetPitch, LimbMovementInfo &moveInfo)
{
	LimbMoveStatus result = ONE_ANGLE_COULDNT_BE_SET_EXACTLY;

	// yaw

	if(Abs(limb.yaw-targetYaw) < moveInfo.yawD){
		limb.yaw = targetYaw;
		result = ANGLES_SET_EXACTLY;
	}else{
		if (limb.yaw > targetYaw) {
			limb.yaw -= moveInfo.yawD;
		} else if (limb.yaw < targetYaw) {
			limb.yaw += moveInfo.yawD;
		}
	}

	if (limb.yaw > moveInfo.maxYaw || limb.yaw < moveInfo.minYaw) {
		limb.yaw = clamp(limb.yaw, moveInfo.minYaw, moveInfo.maxYaw);
		result = ANGLES_SET_TO_MAX;
	}

	// pitch

	if (Abs(limb.pitch - targetPitch) < moveInfo.pitchD){
		limb.pitch = targetPitch;
	}else{
		if (limb.pitch > targetPitch) {
			limb.pitch -= moveInfo.pitchD;
		} else if (limb.pitch < targetPitch) {
			limb.pitch += moveInfo.pitchD;
		}
		result = ONE_ANGLE_COULDNT_BE_SET_EXACTLY;
	}

	if (limb.pitch > moveInfo.maxPitch || limb.pitch < moveInfo.minPitch) {
		limb.pitch = clamp(limb.pitch, moveInfo.minPitch, moveInfo.maxPitch);
		result = ANGLES_SET_TO_MAX;
	}
	return result;
}

bool
CPedIK::RestoreGunPosn(void)
{
	LimbMoveStatus limbStatus = MoveLimb(m_torsoOrient, 0.0f, 0.0f, ms_torsoInfo);
	RotateTorso(m_ped->m_pFrames[PED_MID], &m_torsoOrient, false);
	return limbStatus == ANGLES_SET_EXACTLY;
}

bool
CPedIK::LookInDirection(float targetYaw, float targetPitch)
{
	bool success = true;
	float yaw, pitch;
	if (!(m_ped->m_pFrames[PED_HEAD]->flag & AnimBlendFrameData::IGNORE_ROTATION)) {
		m_ped->m_pFrames[PED_HEAD]->flag |= AnimBlendFrameData::IGNORE_ROTATION;
		RwMatrix *m = GetComponentMatrix(m_ped, PED_NECK);
		m_headOrient.yaw = Atan2(-m->at.y, -m->at.x);
		m_headOrient.yaw -= m_ped->m_fRotationCur;
		m_headOrient.yaw = CGeneral::LimitRadianAngle(m_headOrient.yaw);
		float up = clamp(m->up.z, -1.0f, 1.0f);
		m_headOrient.pitch = Atan2(-up, Sqrt(1.0f - SQR(-up)));
	}

	// parent of head is neck
	RwMatrix *m = GetComponentMatrix(m_ped, PED_NECK);
	yaw = CGeneral::LimitRadianAngle(Atan2(-m->at.y, -m->at.x));
	float up = clamp(m->up.z, -1.0f, 1.0f);
	pitch = Atan2(-up, Sqrt(1.0f - SQR(-up)));
	float headYaw = CGeneral::LimitRadianAngle(targetYaw - (yaw + m_torsoOrient.yaw));
	float headPitch = CGeneral::LimitRadianAngle(targetPitch - pitch) * Cos(Min(Abs(headYaw), HALFPI));

	LimbMoveStatus headStatus = MoveLimb(m_headOrient, headYaw, headPitch, ms_headInfo);
	if (headStatus == ANGLES_SET_TO_MAX)
		success = false;

	if (headStatus != ANGLES_SET_EXACTLY && !(m_flags & LOOKAROUND_HEAD_ONLY))
		if (MoveLimb(m_torsoOrient, CGeneral::LimitRadianAngle(targetYaw-m_ped->m_fRotationCur), targetPitch, ms_torsoInfo))
			success = true;

	// This was RotateHead
	RtQuat *q = &m_ped->m_pFrames[PED_HEAD]->hanimFrame->q;
	RtQuatRotate(q, &ZaxisIK, RADTODEG(m_headOrient.pitch), rwCOMBINEREPLACE);
	RtQuatRotate(q, &XaxisIK, RADTODEG(m_headOrient.yaw), rwCOMBINEPRECONCAT);
	m_ped->bDontAcceptIKLookAts = true;

	if (!(m_flags & LOOKAROUND_HEAD_ONLY))
		RotateTorso(m_ped->m_pFrames[PED_MID], &m_torsoOrient, false);
	return success;
}

bool
CPedIK::LookAtPosition(CVector const &pos)
{
	RwV3d *pedpos = &GetComponentMatrix(m_ped, PED_MID)->pos;
	float yawToFace = CGeneral::GetRadianAngleBetweenPoints(
		pos.x, pos.y,
		pedpos->x, pedpos->y);

	float pitchToFace = CGeneral::GetRadianAngleBetweenPoints(
		// BUG? not using pedpos here
		pos.z, (m_ped->GetPosition() - pos).Magnitude2D(),
		pedpos->z, 0.0f);

	return LookInDirection(yawToFace, pitchToFace);
}

bool
CPedIK::PointGunInDirection(float targetYaw, float targetPitch)
{
	bool result = true;
	bool armPointedToGun = false;
	targetYaw = CGeneral::LimitRadianAngle(targetYaw - m_ped->GetForward().Heading());
	m_flags &= ~GUN_POINTED_SUCCESSFULLY;
	m_flags |= LOOKAROUND_HEAD_ONLY;
	if (m_flags & AIMS_WITH_ARM) {
		armPointedToGun = PointGunInDirectionUsingArm(targetYaw, targetPitch);
		targetYaw = CGeneral::LimitRadianAngle(targetYaw - (m_upperArmOrient.yaw + m_lowerArmOrient.yaw));
	}
	if (armPointedToGun) {
		if (m_flags & AIMS_WITH_ARM && m_torsoOrient.yaw * m_upperArmOrient.yaw < 0.0f)
			MoveLimb(m_torsoOrient, 0.0f, m_torsoOrient.pitch, ms_torsoInfo);
	} else {
		// Unused code
		RwMatrix *matrix;
		float yaw, pitch;
		matrix = RwMatrixCreate();
		*matrix = *GetComponentMatrix(m_ped, PED_CLAVICLER);
		ExtractYawAndPitchWorld(matrix, &yaw, &pitch);
		RwMatrixDestroy(matrix);

		if(m_flags & AIMS_WITH_ARM){
			if(targetPitch > 0.0f)
				targetPitch = Max(targetPitch - Abs(targetYaw), 0.0f);
			else
				targetPitch = Min(targetPitch + Abs(targetYaw), 0.0f);
		}
		LimbMoveStatus status = MoveLimb(m_torsoOrient, targetYaw, targetPitch, ms_torsoInfo);
		if (status == ANGLES_SET_TO_MAX)
			result = false;
		else if (status == ANGLES_SET_EXACTLY)
			m_flags |= GUN_POINTED_SUCCESSFULLY;
	}
	RwMatrix *m = GetBoneMatrix(m_ped, BONE_spine);	// BUG: game uses index 2 directly, which happens to be identical to BONE_spine
	RwV3d axis = { 0.0f, 0.0f, 0.0f };
	float axisangle = -CGeneral::LimitRadianAngle(Atan2(-m->at.y, -m->at.x) - m_ped->m_fRotationCur);
	axis.y = -Sin(axisangle);
	axis.z = Cos(axisangle);

	// this was RotateTorso
	RtQuat *q = &m_ped->m_pFrames[PED_MID]->hanimFrame->q;
	RtQuatRotate(q, &axis, RADTODEG(m_torsoOrient.pitch), rwCOMBINEPOSTCONCAT);
	RtQuatRotate(q, &XaxisIK, RADTODEG(m_torsoOrient.yaw), rwCOMBINEPOSTCONCAT);
	m_ped->bDontAcceptIKLookAts = true;

	return result;
}

bool
CPedIK::PointGunInDirectionUsingArm(float targetYaw, float targetPitch)
{
	bool result = false;
	RwMatrix *matrix;
	float yaw, pitch;

	float uaRoll = 45.0f;
	float handRoll = 30.0f;

	matrix = GetComponentMatrix(m_ped, PED_CLAVICLER);
	yaw = CGeneral::LimitRadianAngle(Atan2(matrix->right.y, matrix->right.x) - m_ped->m_fRotationCur);
	pitch = Atan2(matrix->up.z, Sqrt(1.0f - SQR(matrix->up.z)));

	float uaYaw, uaPitch;
	uaYaw = CGeneral::LimitRadianAngle(targetYaw - yaw - DEGTORAD(15.0f));
	uaPitch = CGeneral::LimitRadianAngle(targetPitch - pitch + DEGTORAD(10.0f));
	LimbMoveStatus uaStatus = MoveLimb(m_upperArmOrient, uaYaw, uaPitch, ms_upperArmInfo);
	if (uaStatus == ANGLES_SET_EXACTLY) {
		m_flags |= GUN_POINTED_SUCCESSFULLY;
		result = true;
	}

	if (uaStatus == ANGLES_SET_TO_MAX) {
		float laYaw = uaYaw - m_upperArmOrient.yaw;

		LimbMoveStatus laStatus;
		if (laYaw > 0.0f){
			float rollReduce = laYaw/DEGTORAD(30.0f);
			uaRoll *= 1.0f - Min(rollReduce, 1.0f);
			handRoll *= 1.0f - Min(rollReduce, 1.0f);

			laYaw *= 1.9f;
			laStatus = MoveLimb(m_lowerArmOrient, laYaw, 0.0f, ms_lowerArmInfo);

			// some unused statics here
			float uaPitchAmount = 1.0f - (m_lowerArmOrient.yaw + m_upperArmOrient.yaw) * 0.34f;
			float f1 = ms_upperArmInfo.maxPitch * Max(uaPitchAmount, 0.0f);
			float f2 = 0.2f*m_lowerArmOrient.yaw + m_upperArmOrient.pitch;
			m_upperArmOrient.pitch = Min(f1, f2);
		}else
			laStatus = MoveLimb(m_lowerArmOrient, laYaw, 0.0f, ms_lowerArmInfo);

		if (laStatus == ANGLES_SET_EXACTLY) {
			m_flags |= GUN_POINTED_SUCCESSFULLY;
			result = true;
		}

		// game does this stupidly by going through the clump extension...
		RtQuat *q = &m_ped->m_pFrames[PED_FOREARMR]->hanimFrame->q;
		RtQuatRotate(q, &ZaxisIK, -RADTODEG(m_lowerArmOrient.yaw), rwCOMBINEREPLACE);
		RtQuatRotate(q, &XaxisIK, -RADTODEG(m_lowerArmOrient.pitch), rwCOMBINEPOSTCONCAT);
		m_ped->bDontAcceptIKLookAts = true;
	}

	RtQuat *q = &m_ped->m_pFrames[PED_UPPERARMR]->hanimFrame->q;
	RtQuatRotate(q, &XaxisIK, uaRoll, rwCOMBINEREPLACE);
	RtQuatRotate(q, &YaxisIK, -RADTODEG(m_upperArmOrient.pitch), rwCOMBINEPOSTCONCAT);
	RtQuatRotate(q, &ZaxisIK, -RADTODEG(m_upperArmOrient.yaw+HALFPI), rwCOMBINEPOSTCONCAT);
	m_ped->bDontAcceptIKLookAts = true;

	q = &m_ped->m_pFrames[PED_HANDR]->hanimFrame->q;
	RtQuatRotate(q, &XaxisIK, handRoll, rwCOMBINEPRECONCAT);

	return result;
}

bool
CPedIK::PointGunAtPosition(CVector const& position)
{
	CVector startPoint;
	if (m_ped->GetWeapon()->m_eWeaponType == WEAPONTYPE_SPAS12_SHOTGUN || m_ped->GetWeapon()->m_eWeaponType == WEAPONTYPE_STUBBY_SHOTGUN)
		startPoint = m_ped->GetPosition();
	else {
		RwV3d armPos;
		GetComponentPosition(armPos, PED_UPPERARMR);
		startPoint.x = m_ped->GetPosition().x;
		startPoint.y = m_ped->GetPosition().y;
		startPoint.z = armPos.z;
	}

	return PointGunInDirection(
		CGeneral::GetRadianAngleBetweenPoints(position.x, position.y, startPoint.x, startPoint.y),
		CGeneral::GetRadianAngleBetweenPoints(position.z, Distance2D(m_ped->GetPosition(), position.x, position.y), startPoint.z, 0.0f));
}

bool
CPedIK::RestoreLookAt(void)
{
	bool result = false;
	float yaw, pitch;

	if (m_ped->m_pFrames[PED_HEAD]->flag & AnimBlendFrameData::IGNORE_ROTATION) {
		m_ped->m_pFrames[PED_HEAD]->flag &= (~AnimBlendFrameData::IGNORE_ROTATION);
	} else {
		ExtractYawAndPitchLocalSkinned(m_ped->m_pFrames[PED_HEAD], &yaw, &pitch);
		if (MoveLimb(m_headOrient, yaw, pitch, ms_headRestoreInfo) == ANGLES_SET_EXACTLY)
			result = true;
	}

	// This was RotateHead
	RtQuat *q = &m_ped->m_pFrames[PED_HEAD]->hanimFrame->q;
	RtQuatRotate(q, &XaxisIK, RADTODEG(m_headOrient.yaw), rwCOMBINEREPLACE);
	RtQuatRotate(q, &ZaxisIK, RADTODEG(m_headOrient.pitch), rwCOMBINEPRECONCAT);
	m_ped->bDontAcceptIKLookAts = true;

	if (!(m_flags & LOOKAROUND_HEAD_ONLY))
		MoveLimb(m_torsoOrient, 0.0f, 0.0f, ms_torsoInfo);
	if (!(m_flags & LOOKAROUND_HEAD_ONLY))
		RotateTorso(m_ped->m_pFrames[PED_MID], &m_torsoOrient, false);
	return result;
}

void
CPedIK::ExtractYawAndPitchWorld(RwMatrix *mat, float *yaw, float *pitch)
{
	float f = clamp(DotProduct(mat->up, CVector(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f);
	*yaw = Acos(f);
	if (mat->up.x > 0.0f) *yaw = -*yaw;

	f = clamp(DotProduct(mat->right, CVector(0.0f, 0.0f, 1.0f)), -1.0f, 1.0f);
	*pitch = Acos(f);
	if (mat->up.z > 0.0f) *pitch = -*pitch;
}

void
CPedIK::ExtractYawAndPitchLocal(RwMatrix *mat, float *yaw, float *pitch)
{
	float f = clamp(DotProduct(mat->at, CVector(0.0f, 0.0f, 1.0f)), -1.0f, 1.0f);
	*yaw = Acos(f);
	if (mat->at.y > 0.0f) *yaw = -*yaw;

	f = clamp(DotProduct(mat->right, CVector(1.0f, 0.0f, 0.0f)), -1.0f, 1.0f);
	*pitch = Acos(f);
	if (mat->up.x > 0.0f) *pitch = -*pitch;
}

void
CPedIK::ExtractYawAndPitchLocalSkinned(AnimBlendFrameData *node, float *yaw, float *pitch)
{
	RwMatrix *mat = RwMatrixCreate();
	RtQuatConvertToMatrix(&node->hanimFrame->q, mat);
	ExtractYawAndPitchLocal(mat, yaw, pitch);
	RwMatrixDestroy(mat);
}
