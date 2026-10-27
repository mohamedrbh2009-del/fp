#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

MYMODCFG(net.local.gtasa.firstperson, GTASA First Person, 1.1, local)
BEGIN_DEPLIST()
END_DEPLIST()

struct CVector { float x, y, z; };
struct RwV3d   { float x, y, z; };

static uintptr_t g_gtasaBase = 0;
static void* g_gtasaHandle = nullptr;

static void*  (*FindPlayerPed)(int) = nullptr;
static void*  (*FindPlayerVehicle)(int, bool) = nullptr;
static void   (*GetBonePosition)(void*, RwV3d&, unsigned, bool) = nullptr;
static void   (*SetCamPosFixed)(void*, const CVector&, const CVector&) = nullptr;
static void   (*TakeControlNoEntity)(void*, const CVector&, short, int) = nullptr;
static float  (*FindPlayerHeading)(int) = nullptr;
static void   (*RestoreWithJumpCut)(void*) = nullptr;
static int    (*IsTouchDown)(int) = nullptr;
static void*  (*GetPad)(int) = nullptr;
static uint32_t (*PadLookLR)(void*) = nullptr;
static uint32_t (*PadLookUD)(void*) = nullptr;

// ---------------- User tuning ----------------
static constexpr float SENS_X = 0.0015f;
static constexpr float SENS_Y = 0.0015f;
static constexpr bool INVERT_X = false;
static constexpr bool INVERT_Y = false;

static constexpr unsigned BONE_HEAD = 6;
static constexpr short CAMERA_SWITCH_JUMPCUT = 2;
static constexpr int SCRIPT_CONTROL = 1;

static constexpr float HEAD_FORWARD_OFFSET = 0.10f;
static constexpr float HEAD_UP_OFFSET = 0.025f;
static constexpr float LOOK_DISTANCE = 20.0f;
static constexpr float PITCH_LIMIT = 1.25f;

// Smoothing: 0 = no smoothing; larger values follow more slowly.
static constexpr float POSITION_SMOOTHING = 0.42f;
static constexpr float ANGLE_SMOOTHING = 0.35f;
static constexpr int WARMUP_FRAMES = 90;

#define LOGPATH "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/fpv_log.txt"

static void Log(const char* message)
{
    FILE* f = std::fopen(LOGPATH, "a");
    if (!f) return;
    std::fputs(message, f);
    std::fputc('\n', f);
    std::fclose(f);
}

static void LogSymbol(const char* name, const void* ptr)
{
    char line[160];
    std::snprintf(line, sizeof(line), "%s = %p", name, ptr);
    Log(line);
}

static float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

static float Smooth(float current, float target, float amount)
{
    return current + (target - current) * amount;
}

// The original code's raw look values may be float-packed or integer-packed.
// This decoder is retained only as a compatibility probe; confirm the actual
// return type/meaning for the target libGTASA.so before treating it as final.
static float DecodeLook(uint32_t raw)
{
    if (raw == 0) return 0.0f;

    float asFloat = 0.0f;
    std::memcpy(&asFloat, &raw, sizeof(asFloat));

    if (std::isfinite(asFloat) && std::fabs(asFloat) > 1.0e-6f &&
        std::fabs(asFloat) < 1000.0f)
        return asFloat;

    return static_cast<float>(static_cast<int16_t>(raw & 0xFFFFu)) / 128.0f;
}

static int g_frames = 0;
static int g_lookLogs = 0;
static bool g_enabled = true;
static bool g_prevThreeTouches = false;
static bool g_needInit = true;
static bool g_warmLogged = false;
static bool g_cameraLogged = false;
static bool g_inVehicle = false;

static float g_yaw = 0.0f;
static float g_pitch = 0.0f;
static bool g_hasCameraPosition = false;
static CVector g_smoothedPosition = {0.0f, 0.0f, 0.0f};

static bool TouchDown(int index)
{
    if (!IsTouchDown) return false;
    return (IsTouchDown(index) & 0xFF) != 0;
}

static float AngleDelta(float from, float to)
{
    float d = std::fmod(to - from + 3.14159265358979323846f,
                        6.28318530717958647692f);
    if (d < 0.0f) d += 6.28318530717958647692f;
    return d - 3.14159265358979323846f;
}

static float SmoothAngle(float current, float target, float amount)
{
    return current + AngleDelta(current, target) * amount;
}

DECL_HOOK(void, CCamera_Process, void* self)
{
    CCamera_Process(self);

    // Three simultaneous touches toggle FP mode. If this conflicts with the
    // game's UI on your device, replace this gesture with a proper touch hook.
    const bool threeTouches = TouchDown(0) && TouchDown(1) && TouchDown(2);
    if (threeTouches && !g_prevThreeTouches)
    {
        g_enabled = !g_enabled;
        g_needInit = true;
        g_hasCameraPosition = false;
        Log(g_enabled ? "toggle: first person ON" : "toggle: first person OFF");

        if (!g_enabled && RestoreWithJumpCut)
            RestoreWithJumpCut(self);
    }
    g_prevThreeTouches = threeTouches;

    if (!g_enabled) return;

    if (!FindPlayerPed || !GetBonePosition || !SetCamPosFixed ||
        !TakeControlNoEntity)
        return;

    void* ped = FindPlayerPed(0);
    if (!ped)
    {
        g_frames = 0;
        g_needInit = true;
        g_hasCameraPosition = false;
        return;
    }

    // Preserve the project's original model-ready check for the target build.
    void* rwObject = *reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(ped) + 0x18);
    if (!rwObject)
    {
        g_frames = 0;
        g_needInit = true;
        g_hasCameraPosition = false;
        return;
    }

    if (g_frames < WARMUP_FRAMES)
    {
        ++g_frames;
        return;
    }

    if (!g_warmLogged)
    {
        g_warmLogged = true;
        Log("camera warm-up finished");
    }

    if (FindPlayerVehicle && FindPlayerVehicle(0, false))
    {
        if (!g_inVehicle) Log("vehicle detected: FP camera paused");
        g_inVehicle = true;
        g_needInit = true;
        g_hasCameraPosition = false;
        return;
    }
    g_inVehicle = false;

    RwV3d head = {0.0f, 0.0f, 0.0f};
    GetBonePosition(ped, head, BONE_HEAD, true);

    if (g_needInit)
    {
        g_yaw = FindPlayerHeading ? FindPlayerHeading(0) : 0.0f;
        g_pitch = 0.0f;
        g_needInit = false;
    }

    // Uses the game's existing look-axis functions. This does NOT by itself
    // guarantee "drag anywhere"; that requires verified screen-touch deltas.
    void* pad = GetPad ? GetPad(0) : nullptr;
    if (pad && PadLookLR && PadLookUD)
    {
        const uint32_t rawLR = PadLookLR(pad);
        const uint32_t rawUD = PadLookUD(pad);
        const float lr = DecodeLook(rawLR);
        const float ud = DecodeLook(rawUD);

        if ((rawLR != 0 || rawUD != 0) && g_lookLogs < 40)
        {
            ++g_lookLogs;
            char line[180];
            std::snprintf(line, sizeof(line),
                          "look raw LR=0x%08X UD=0x%08X decoded LR=%.5f UD=%.5f",
                          rawLR, rawUD, lr, ud);
            Log(line);
        }

        g_yaw += (INVERT_X ? 1.0f : -1.0f) * lr * SENS_X;
        g_pitch += (INVERT_Y ? -1.0f : 1.0f) * ud * SENS_Y;
        g_pitch = Clamp(g_pitch, -PITCH_LIMIT, PITCH_LIMIT);
    }

    // Compute the target camera position from the current head bone every frame.
    const float cp = std::cos(g_pitch);
    const float fx = -std::sin(g_yaw) * cp;
    const float fy =  std::cos(g_yaw) * cp;
    const float fz =  std::sin(g_pitch);

    const CVector targetPos = {
        head.x + fx * HEAD_FORWARD_OFFSET,
        head.y + fy * HEAD_FORWARD_OFFSET,
        head.z + HEAD_UP_OFFSET
    };

    if (!g_hasCameraPosition)
    {
        g_smoothedPosition = targetPos;
        g_hasCameraPosition = true;
    }
    else
    {
        g_smoothedPosition.x = Smooth(g_smoothedPosition.x, targetPos.x,
                                      POSITION_SMOOTHING);
        g_smoothedPosition.y = Smooth(g_smoothedPosition.y, targetPos.y,
                                      POSITION_SMOOTHING);
        g_smoothedPosition.z = Smooth(g_smoothedPosition.z, targetPos.z,
                                      POSITION_SMOOTHING);
    }

    const CVector lookAt = {
        g_smoothedPosition.x + fx * LOOK_DISTANCE,
        g_smoothedPosition.y + fy * LOOK_DISTANCE,
        g_smoothedPosition.z + fz * LOOK_DISTANCE
    };
    const CVector up = {0.0f, 0.0f, 1.0f};

    SetCamPosFixed(self, g_smoothedPosition, up);
    TakeControlNoEntity(self, lookAt, CAMERA_SWITCH_JUMPCUT, SCRIPT_CONTROL);

    if (!g_cameraLogged)
    {
        g_cameraLogged = true;
        char line[180];
        std::snprintf(line, sizeof(line),
                      "camera active: head=(%.2f, %.2f, %.2f) yaw=%.3f pitch=%.3f",
                      head.x, head.y, head.z, g_yaw, g_pitch);
        Log(line);
    }
}

extern "C" void OnModLoad()
{
    Log("---- GTASA First Person 1.1 load ----");
    logger->SetTag("GTASA-FPV");

    g_gtasaBase = aml->GetLib("libGTASA.so");
    g_gtasaHandle = aml->GetLibHandle("libGTASA.so");
    LogSymbol("libGTASA base", reinterpret_cast<void*>(g_gtasaBase));

    if (!g_gtasaBase || !g_gtasaHandle)
    {
        Log("ERROR: libGTASA.so not found");
        return;
    }

    SET_TO(FindPlayerPed,
           aml->GetSym(g_gtasaHandle, "_Z13FindPlayerPedi"));
    SET_TO(FindPlayerVehicle,
           aml->GetSym(g_gtasaHandle, "_Z17FindPlayerVehicleib"));
    SET_TO(GetBonePosition,
           aml->GetSym(g_gtasaHandle, "_ZN4CPed15GetBonePositionER5RwV3djb"));
    SET_TO(SetCamPosFixed,
           aml->GetSym(g_gtasaHandle, "_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_"));
    SET_TO(FindPlayerHeading,
           aml->GetSym(g_gtasaHandle, "_Z17FindPlayerHeadingi"));
    SET_TO(TakeControlNoEntity,
           aml->GetSym(g_gtasaHandle, "_ZN7CCamera19TakeControlNoEntityERK7CVectorsi"));
    SET_TO(RestoreWithJumpCut,
           aml->GetSym(g_gtasaHandle, "_ZN7CCamera18RestoreWithJumpCutEv"));
    SET_TO(IsTouchDown,
           aml->GetSym(g_gtasaHandle, "_ZN15CTouchInterface11IsTouchDownEi"));
    SET_TO(GetPad,
           aml->GetSym(g_gtasaHandle, "_ZN4CPad6GetPadEi"));
    SET_TO(PadLookLR,
           aml->GetSym(g_gtasaHandle, "_ZN4CPad19LookAroundLeftRightEv"));
    SET_TO(PadLookUD,
           aml->GetSym(g_gtasaHandle, "_ZN4CPad16LookAroundUpDownEv"));

    LogSymbol("FindPlayerPed", reinterpret_cast<void*>(FindPlayerPed));
    LogSymbol("GetBonePosition", reinterpret_cast<void*>(GetBonePosition));
    LogSymbol("GetPad", reinterpret_cast<void*>(GetPad));
    LogSymbol("PadLookLR", reinterpret_cast<void*>(PadLookLR));
    LogSymbol("PadLookUD", reinterpret_cast<void*>(PadLookUD));
    LogSymbol("IsTouchDown", reinterpret_cast<void*>(IsTouchDown));

    const uintptr_t camProcess =
        reinterpret_cast<uintptr_t>(
            aml->GetSym(g_gtasaHandle, "_ZN7CCamera7ProcessEv"));

    LogSymbol("CCamera::Process", reinterpret_cast<void*>(camProcess));
    if (!camProcess)
    {
        Log("ERROR: CCamera::Process symbol missing; hook not installed");
        return;
    }

    HOOK(CCamera_Process, camProcess);
    Log("hook installed");
}
