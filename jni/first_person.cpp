#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>

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
static void  (*RestoreWithJumpCut)(void*) = nullptr;
static int   (*IsTouchDown)(int) = nullptr;

static const unsigned BONE_HEAD      = 6;
static const int      SWITCH_JUMPCUT = 2;
static const int      SCRIPT_CONTROL = 1;
static const float    FORWARD_OFFSET = 0.12f;
static const float    UP_OFFSET      = 0.05f;
static const float    LOOK_DISTANCE  = 20.0f;
static const int      WARMUP_FRAMES  = 300;

#define LOGPATH "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/fpv_log.txt"

static void FLog(const char* msg)
{
    FILE* f = fopen(LOGPATH, "a");
    if (f) { fputs(msg, f); fputc('\n', f); fclose(f); }
}
static void FLogSym(const char* name, void* p)
{
    char b[160];
    snprintf(b, sizeof b, "%s = %p", name, p);
    FLog(b);
}

static int  g_frames = 0;
static bool g_enabled = true;
static bool g_prev3 = false;
static bool g_hookRan = false, g_seenPed = false, g_seenRw = false, g_warm = false, g_applied = false, g_inCar = false, g_seenTouch = false;

static bool TouchDown(int i)
{
    if (!IsTouchDown) return false;
    int r = IsTouchDown(i);
    return (r & 0xFF) != 0;
}

DECL_HOOK(void, CCamera_Process, void* self)
{
    CCamera_Process(self);

    if (!g_hookRan) { g_hookRan = true; FLog("hook: CCamera::Process called for the first time"); }

    if (!g_seenTouch && TouchDown(0)) { g_seenTouch = true; FLog("touch: first touch detected"); }
    bool three = TouchDown(0) && TouchDown(1) && TouchDown(2);
    if (three && !g_prev3) {
        g_enabled = !g_enabled;
        FLog(g_enabled ? "toggle: first person ON" : "toggle: first person OFF");
        if (!g_enabled && RestoreWithJumpCut) RestoreWithJumpCut(self);
    }
    g_prev3 = three;
    if (!g_enabled) return;

    if (!FindPlayerPed || !GetBonePosition || !SetCamPosFixed || !TakeControlNoEntity) return;

    void* ped = FindPlayerPed(-1);
    if (!ped) { g_frames = 0; return; }
    if (!g_seenPed) { g_seenPed = true; FLog("hook: player ped found"); }

    void* rwObject = *(void**)((uintptr_t)ped + 0x18);
    if (!rwObject) { g_frames = 0; return; }
    if (!g_seenRw) { g_seenRw = true; FLog("hook: player model is ready"); }

    if (g_frames < WARMUP_FRAMES) { g_frames++; return; }
    if (!g_warm) { g_warm = true; FLog("hook: warm-up finished"); }

    if (FindPlayerVehicle && FindPlayerVehicle(-1, false)) {
        if (!g_inCar) { g_inCar = true; FLog("hook: player is in a vehicle, skipping"); }
        return;
    }
    g_inCar = false;

    RwV3d head;
    GetBonePosition(ped, head, BONE_HEAD, true);

    float h  = FindPlayerHeading ? FindPlayerHeading(-1) : 0.0f;
    float fx = -sinf(h), fy = cosf(h);

    CVector pos  = { head.x + fx * FORWARD_OFFSET, head.y + fy * FORWARD_OFFSET, head.z + UP_OFFSET };
    CVector look = { pos.x + fx * LOOK_DISTANCE,   pos.y + fy * LOOK_DISTANCE,   pos.z };
    CVector up   = { 0.0f, 0.0f, 1.0f };

    SetCamPosFixed(self, pos, up);
    TakeControlNoEntity(self, look, SWITCH_JUMPCUT, SCRIPT_CONTROL);

    if (!g_applied) {
        g_applied = true;
        char b[200];
        snprintf(b, sizeof b, "hook: camera applied. head=(%.2f, %.2f, %.2f) heading=%.2f", head.x, head.y, head.z, h);
        FLog(b);
    }
}

extern "C" void OnModLoad()
{
    FLog("---- OnModLoad reached ----");
    logger->SetTag("GTASA-FPV");
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");
    FLogSym("libGTASA base", (void*)pGTASA);
    if (!pGTASA || !hGTASA) { FLog("ERROR: libGTASA.so not found"); return; }

    SET_TO(FindPlayerPed,       aml->GetSym(hGTASA, "_Z13FindPlayerPedi"));
    SET_TO(FindPlayerVehicle,   aml->GetSym(hGTASA, "_Z17FindPlayerVehicleib"));
    SET_TO(GetBonePosition,     aml->GetSym(hGTASA, "_ZN4CPed15GetBonePositionER5RwV3djb"));
    SET_TO(SetCamPosFixed,      aml->GetSym(hGTASA, "_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_"));
    SET_TO(FindPlayerHeading,   aml->GetSym(hGTASA, "_Z17FindPlayerHeadingi"));
    SET_TO(TakeControlNoEntity, aml->GetSym(hGTASA, "_ZN7CCamera19TakeControlNoEntityERK7CVectorsi"));
    SET_TO(RestoreWithJumpCut,  aml->GetSym(hGTASA, "_ZN7CCamera18RestoreWithJumpCutEv"));
    // SET_TO(IsTouchDown,         aml->GetSym(hGTASA, "_ZN15CTouchInterface11IsTouchDownEi"));

    FLogSym("FindPlayerPed",       (void*)FindPlayerPed);
    FLogSym("FindPlayerVehicle",   (void*)FindPlayerVehicle);
    FLogSym("GetBonePosition",     (void*)GetBonePosition);
    FLogSym("SetCamPosFixed",      (void*)SetCamPosFixed);
    FLogSym("FindPlayerHeading",   (void*)FindPlayerHeading);
    FLogSym("TakeControlNoEntity", (void*)TakeControlNoEntity);
    FLogSym("RestoreWithJumpCut",  (void*)RestoreWithJumpCut);
    FLogSym("IsTouchDown",         (void*)IsTouchDown);

    uintptr_t camProcess = aml->GetSym(hGTASA, "_ZN7CCamera7ProcessEv");
    FLogSym("CCamera::Process", (void*)camProcess);
    if (!camProcess) { FLog("ERROR: CCamera::Process symbol missing, mod disabled"); return; }
    HOOK(CCamera_Process, camProcess);

    FLog("hook installed OK");
}
