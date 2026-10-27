#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <math.h>

MYMODCFG(net.local.gtasa.firstperson, GTASA First Person, 1.0, local)
BEGIN_DEPLIST()
END_DEPLIST()

struct CVector { float x, y, z; };
struct RwV3d   { float x, y, z; };

uintptr_t pGTASA = 0;
void*     hGTASA = nullptr;

static void* (*FindPlayerPed)(int) = nullptr;
static void* (*FindPlayerVehicle)(int, bool) = nullptr;
static void  (*GetBonePosition)(void*, RwV3d&, unsigned, bool) = nullptr;
static void  (*SetCamPosFixed)(void*, const CVector&, const CVector&) = nullptr;
static void  (*TakeControlNoEntity)(void*, const CVector&, short, int) = nullptr;
static float (*FindPlayerHeading)(int) = nullptr;
static void* pTheCamera = nullptr;

static const unsigned BONE_HEAD      = 6;
static const int      SWITCH_JUMPCUT = 2;
static const int      SCRIPT_CONTROL = 1;
static const float    FORWARD_OFFSET = 0.12f;
static const float    UP_OFFSET      = 0.05f;
static const float    LOOK_DISTANCE  = 20.0f;

DECL_HOOK(void, CCamera_Process, void* self)
{
    CCamera_Process(self);

    if (!FindPlayerPed || !GetBonePosition || !SetCamPosFixed || !TakeControlNoEntity) return;

    void* ped = FindPlayerPed(-1);
    if (!ped) return;
    if (FindPlayerVehicle && FindPlayerVehicle(-1, false)) return;

    RwV3d head;
    GetBonePosition(ped, head, BONE_HEAD, true);

    float h  = FindPlayerHeading ? FindPlayerHeading(-1) : 0.0f;
    float fx = -sinf(h), fy = cosf(h);

    CVector pos  = { head.x + fx * FORWARD_OFFSET, head.y + fy * FORWARD_OFFSET, head.z + UP_OFFSET };
    CVector look = { pos.x + fx * LOOK_DISTANCE,   pos.y + fy * LOOK_DISTANCE,   pos.z };
    CVector up   = { 0.0f, 0.0f, 1.0f };

    SetCamPosFixed(self, pos, up);
    TakeControlNoEntity(self, look, SWITCH_JUMPCUT, SCRIPT_CONTROL);
}

extern "C" void OnModLoad()
{
    logger->SetTag("GTASA-FPV");
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");
    if (!pGTASA || !hGTASA) { logger->Error("libGTASA.so not found"); return; }

    SET_TO(FindPlayerPed,       aml->GetSym(hGTASA, "_Z13FindPlayerPedi"));
    SET_TO(FindPlayerVehicle,   aml->GetSym(hGTASA, "_Z17FindPlayerVehicleib"));
    SET_TO(GetBonePosition,     aml->GetSym(hGTASA, "_ZN4CPed15GetBonePositionER5RwV3djb"));
    SET_TO(SetCamPosFixed,      aml->GetSym(hGTASA, "_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_"));
    SET_TO(FindPlayerHeading,   aml->GetSym(hGTASA, "_Z17FindPlayerHeadingi"));
    SET_TO(TakeControlNoEntity, aml->GetSym(hGTASA, "_ZN7CCamera19TakeControlNoEntityERK7CVectorsi"));
    SET_TO(pTheCamera,          aml->GetSym(hGTASA, "TheCamera"));

    uintptr_t camProcess = aml->GetSym(hGTASA, "_ZN7CCamera7ProcessEv");
    if (!camProcess) { logger->Error("CCamera::Process symbol missing, mod disabled"); return; }
   // HOOK(CCamera_Process, camProcess);

    logger->Info("First person loaded");
}
