#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
static void*    (*GetPad)(int) = nullptr;
static uint32_t (*PadLookLR)(void*) = nullptr;
static uint32_t (*PadLookUD)(void*) = nullptr;

// ================= قيم قابلة للتعديل =================
static const float SENS_X   = 0.04f;   // سرعة الدوران يمين/يسار (قلّلها لتبطئ)
static const float SENS_Y   = 0.03f;   // سرعة النظر فوق/تحت
static const bool  INVERT_X = false;   // اجعلها true إذا كان اتجاه اليمين/اليسار معكوساً
static const bool  INVERT_Y = false;   // اجعلها true إذا كان فوق/تحت معكوساً
// ======================================================

static const unsigned BONE_HEAD      = 6;
static const int      SWITCH_JUMPCUT = 2;
static const int      SCRIPT_CONTROL = 1;
static const float    FORWARD_OFFSET = 0.12f;
static const float    UP_OFFSET      = 0.05f;
static const float    LOOK_DISTANCE  = 20.0f;
static const float    PITCH_LIMIT    = 1.3f;
static const int      WARMUP_FRAMES  = 300;

#define LOGPATH "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/fpv_log.txt"

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

static int   g_frames = 0;
static bool  g_enabled = true;
static bool  g_prev3 = false;
static bool  g_needInit = true;
static float g_yaw = 0.0f, g_pitch = 0.0f;
static int   g_lookLogs = 0;
static bool  g_hookRan = false, g_seenPed = false, g_seenRw = false, g_warm = false, g_applied = false, g_inCar = false;

static bool TouchDown(int i)
{
    if (!IsTouchDown) return false;
    int r = IsTouchDown(i);
    return (r & 0xFF) != 0;
}

// the game function may return a float or a small integer; handle both
static float ReadLook(uint32_t raw)
{
    if (raw == 0) return 0.0f;
    float f;
    memcpy(&f, &raw, 4);
    if (fabsf(f) > 1e-6f && fabsf(f) < 1000.0f) return f;
    return (float)(int16_t)(raw & 0xFFFF) / 128.0f;
}

DECL_HOOK(void, CCamera_Process, void* self)
{
    CCamera_Process(self);

    if (!g_hookRan) { g_hookRan = true; FLog("hook: CCamera::Process called for the first time"); }

    bool three = TouchDown(0) && TouchDown(1) && TouchDown(2);
    if (three && !g_prev3) {
        g_enabled = !g_enabled;
        g_needInit = true;
        FLog(g_enabled ? "toggle: first person ON" : "toggle: first person OFF");
        if (!g_enabled && RestoreWithJumpCut) RestoreWithJumpCut(self);
    }
    g_prev3 = three;
    if (!g_enabled) return;

    if (!FindPlayerPed || !GetBonePosition || !SetCamPosFixed || !TakeControlNoEntity) return;

    void* ped = FindPlayerPed(0);
    if (!ped) { g_frames = 0; g_needInit = true; return; }
    if (!g_seenPed) { g_seenPed = true; FLog("hook: player ped found"); }

    void* rwObject = *(void**)((uintptr_t)ped + 0x18);
    if (!rwObject) { g_frames = 0; g_needInit = true; return; }
    if (!g_seenRw) { g_seenRw = true; FLog("hook: player model is ready"); }

    if (g_frames < WARMUP_FRAMES) { g_frames++; return; }
    if (!g_warm) { g_warm = true; FLog("hook: warm-up finished"); }

    if (FindPlayerVehicle && FindPlayerVehicle(0, false)) {
        if (!g_inCar) { g_inCar = true; FLog("hook: player is in a vehicle, skipping"); }
        g_needInit = true;
        return;
    }
    g_inCar = false;

    RwV3d head;
    GetBonePosition(ped, head, BONE_HEAD, true);

    if (g_needInit) {
        g_yaw = FindPlayerHeading ? FindPlayerHeading(0) : 0.0f;
        g_pitch = 0.0f;
        g_needInit = false;
    }

    // ---- look input from the game's own screen-drag handling ----
    void* pad = GetPad ? GetPad(0) : nullptr;
    if (pad && PadLookLR && PadLookUD) {
        uint32_t rawLR = PadLookLR(pad);
        uint32_t rawUD = PadLookUD(pad);
        float lr = ReadLook(rawLR);
        float ud = ReadLook(rawUD);

        if ((rawLR || rawUD) && g_lookLogs < 40) {
            g_lookLogs++;
            char b[200];
            snprintf(b, sizeof b, "look raw LR=0x%08X UD=0x%08X -> LR=%.4f UD=%.4f", rawLR, rawUD, lr, ud);
            FLog(b);
        }

        g_yaw   += (INVERT_X ? 1.0f : -1.0f) * lr * SENS_X;
        g_pitch += (INVERT_Y ? -1.0f : 1.0f) * ud * SENS_Y;
        if (g_pitch >  PITCH_LIMIT) g_pitch =  PITCH_LIMIT;
        if (g_pitch < -PITCH_LIMIT) g_pitch = -PITCH_LIMIT;
    }

    float cp = cosf(g_pitch);
    float fx = -sinf(g_yaw) * cp;
    float fy =  cosf(g_yaw) * cp;
    float fz =  sinf(g_pitch);

    CVector pos  = { head.x + fx * FORWARD_OFFSET, head.y + fy * FORWARD_OFFSET, head.z + UP_OFFSET };
    CVector look = { pos.x + fx * LOOK_DISTANCE,   pos.y + fy * LOOK_DISTANCE,   pos.z + fz * LOOK_DISTANCE };
    CVector up   = { 0.0f, 0.0f, 1.0f };

    SetCamPosFixed(self, pos, up);
    TakeControlNoEntity(self, look, SWITCH_JUMPCUT, SCRIPT_CONTROL);

    if (!g_applied) {
        g_applied = true;
        char b[200];
        snprintf(b, sizeof b, "hook: camera applied. head=(%.2f, %.2f, %.2f) yaw=%.2f", head.x, head.y, head.z, g_yaw);
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
    SET_TO(IsTouchDown,         aml->GetSym(hGTASA, "_ZN15CTouchInterface11IsTouchDownEi"));
    SET_TO(GetPad,              aml->GetSym(hGTASA, "_ZN4CPad6GetPadEi"));
    SET_TO(PadLookLR,           aml->GetSym(hGTASA, "_ZN4CPad19LookAroundLeftRightEv"));
    SET_TO(PadLookUD,           aml->GetSym(hGTASA, "_ZN4CPad16LookAroundUpDownEv"));

    FLogSym("GetPad",     (void*)GetPad);
    FLogSym("PadLookLR",  (void*)PadLookLR);
    FLogSym("PadLookUD",  (void*)PadLookUD);
    FLogSym("IsTouchDown", (void*)IsTouchDown);

    uintptr_t camProcess = aml->GetSym(hGTASA, "_ZN7CCamera7ProcessEv");
    FLogSym("CCamera::Process", (void*)camProcess);
    if (!camProcess) { FLog("ERROR: CCamera::Process symbol missing, mod disabled"); return; }
    HOOK(CCamera_Process, camProcess);

    FLog("hook installed OK");
}
