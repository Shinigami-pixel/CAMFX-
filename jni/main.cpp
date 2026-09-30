#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/isautils.h>
#include <mod/config.h>
#include <csetjmp>
#include <csignal>
#include <cstring>
#include <chrono>

MYMODCFGNAME(net.thirtyfoxmc.camerafx, CameraFX, 0.9, Senji, CameraFX) // config file: configs/CameraFX.ini — GUID stays net.thirtyfoxmc.camerafx for AML's own mod identification

ISAUtils* sautils = NULL;
static eTypeOfSettings g_tab = SetType_Mods; // replaced with our own tab on load
static volatile bool g_active = false;       // guards every ScriptCommand call

// ---------------------------------------------------------------------------
// Crash containment - every CALLSCM below goes through this: if a call
// segfaults, it's caught, skipped for that frame, and the game keeps
// running instead of crashing. Falls through to whatever handler was
// previously installed (AML's own crash logger) for anything outside this
// wrapper's protected window.
// ---------------------------------------------------------------------------
static sigjmp_buf g_jumpBuf;
static volatile sig_atomic_t g_inProtectedCall = 0;
static struct sigaction g_prevSigSegvAction;
static volatile sig_atomic_t g_haveInstalledGuard = 0;

static void SafeCallSignalHandler(int sig, siginfo_t* info, void* context)
{
    if (g_inProtectedCall) siglongjmp(g_jumpBuf, 1);
    else { sigaction(SIGSEGV, &g_prevSigSegvAction, NULL); raise(sig); }
}

static void InstallSignalGuard()
{
    if (g_haveInstalledGuard) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = SafeCallSignalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_prevSigSegvAction);
    g_haveInstalledGuard = 1;
}

template<typename... Args>
static int SafeScriptCommand(const SCRIPT_COMMAND* cmd, Args... args)
{
    g_inProtectedCall = 1;
    if (sigsetjmp(g_jumpBuf, 1) == 0)
    {
        int result = sautils->ScriptCommand(cmd, args...);
        g_inProtectedCall = 0;
        return result;
    }
    g_inProtectedCall = 0;
    return 0;
}

#undef CALLSCM
#define CALLSCM(__name, ...) SafeScriptCommand(&scm_##__name _VA_ARGS(__VA_ARGS__))

DEFOPCODE(01F5, GET_PLAYER_CHAR,             iv);   // (playerIdx) -> charHandle
DEFOPCODE(06AC, GET_CHAR_SPEED,              iv);   // (charHandle) -> speed
DEFOPCODE(0818, IS_CHAR_IN_AIR,              i);    // (charHandle)
DEFOPCODE(04AD, IS_CHAR_IN_WATER,            i);    // (charHandle)
DEFOPCODE(0965, IS_CHAR_SWIMMING,            i);    // (charHandle) - dedicated swim-task check, distinct from IS_CHAR_IN_WATER
DEFOPCODE(0A0C, IS_PLAYER_USING_JETPACK,     i);    // (playerIdx)
DEFOPCODE(0256, IS_PLAYER_PLAYING,           i);    // (playerIdx)
DEFOPCODE(0801, GET_CAMERA_FOV,              v);    // () -> fov
DEFOPCODE(0922, CAMERA_SET_LERP_FOV,         ffib); // (from, to, timeMs, ease)
DEFOPCODE(0931, CAMERA_PERSIST_FOV,          b);    // (state)
DEFOPCODE(0470, GET_CURRENT_CHAR_WEAPON,     iv);   // (charHandle) -> weaponType
DEFOPCODE(0782, GET_WEAPONTYPE_SLOT,         iv);   // (weaponType) -> slot
DEFOPCODE(00DF, IS_CHAR_IN_ANY_CAR,          i);    // (charHandle)
DEFOPCODE(047A, IS_CHAR_ON_ANY_BIKE,         i);    // (charHandle)
DEFOPCODE(04C8, IS_CHAR_IN_FLYING_VEHICLE,   i);    // (charHandle)
DEFOPCODE(0A51, IS_WIDGET_PRESSED,           i);    // (widgetId)
DEFOPCODE(09E7, IS_PLAYER_CONTROL_ON,        i);    // (playerIdx) -> bool (return value)
DEFOPCODE(068C, IS_PLAYER_TARGETTING_ANYTHING, i);  // (playerIdx) -> bool, works for both free-aim and lock-aim

// Verified from enums.json (WeaponType / WeaponSlot / WidgetId) — not guesses.
static const int WEAPON_PARACHUTE   = 46;
static const int WEAPON_SNIPER      = 34;  // excluded from Weapon FOV — has its own native scope zoom
static const int SLOT_SHOTGUNS      = 4;
static const int SLOT_ASSAULT       = 6;
static const int SLOT_RIFLES        = 7;   // includes both Rifle (33) and Sniper (34)
static const int WIDGET_ACCELERATE  = 2;   // WidgetId.Accelerate
static const int WIDGET_BRAKE       = 3;   // WidgetId.Brake
static const int WIDGET_SPRINT      = 31;  // WidgetId.ButtonSprint

enum class Lang { EN, JA, ES, ID, FR, PT, DE, RU, COUNT };
static Lang g_lang = Lang::EN;

enum class S {
    Author, SprintZoom, FallZoom, ParachuteZoom, VehicleZoom,
    SprintOutT, SprintInT, FallOutT, FallInT, ParachuteT, VehicleT,
    SprintThresh, VehicleZoomThresh, VehicleThresh, BlendTransition, BlendTagline, WeaponFov, COUNT
};

static const char* kLangNames[(int)Lang::COUNT] = {
    "English", "日本語", "Español", "Indonesia", "Français", "Português", "Deutsch", "Русский"
};

static const char* kStrings[(int)Lang::COUNT][(int)S::COUNT] = {
    /* EN */ { "Author", "Sprint Zoom", "Fall Zoom", "Parachute Zoom", "Vehicle Zoom", "Sprint Zoom-Out Time", "Sprint Zoom-In Time", "Fall Zoom-Out Time", "Fall Zoom-In Time", "Parachute Zoom Time", "Vehicle Zoom Time", "Sprint Speed Threshold", "Vehicle Zoom Threshold", "Vehicle Speed Threshold", "Blend Transition", "Smooth Blend Transitions", "Weapon FOV Effect" },
    /* JA */ { "作者", "スプリントズーム", "落下ズーム", "パラシュートズーム", "車両ズーム", "スプリントズームアウト時間", "スプリントズームイン時間", "落下ズームアウト時間", "落下ズームイン時間", "パラシュートズーム時間", "車両ズーム時間", "スプリント速度しきい値", "車両ズームしきい値", "車両速度しきい値", "ブレンド遷移", "スムーズなブレンド遷移", "武器FOV効果" },
    /* ES */ { "Autor", "Zoom de Sprint", "Zoom de Caída", "Zoom de Paracaídas", "Zoom de Vehículo", "Tiempo de Zoom-Out de Sprint", "Tiempo de Zoom-In de Sprint", "Tiempo de Zoom-Out de Caída", "Tiempo de Zoom-In de Caída", "Tiempo de Zoom de Paracaídas", "Tiempo de Zoom de Vehículo", "Umbral de Velocidad de Sprint", "Umbral de Zoom de Vehículo", "Umbral de Velocidad de Vehículo", "Transición de Mezcla", "Transiciones de Mezcla Suaves", "Efecto FOV de Arma" },
    /* ID */ { "Penulis", "Zoom Sprint", "Zoom Jatuh", "Zoom Parasut", "Zoom Kendaraan", "Waktu Zoom-Out Sprint", "Waktu Zoom-In Sprint", "Waktu Zoom-Out Jatuh", "Waktu Zoom-In Jatuh", "Waktu Zoom Parasut", "Waktu Zoom Kendaraan", "Ambang Kecepatan Sprint", "Ambang Zoom Kendaraan", "Ambang Kecepatan Kendaraan", "Transisi Blend", "Transisi Blend yang Halus", "Efek FOV Senjata" },
    /* FR */ { "Auteur", "Zoom Sprint", "Zoom de Chute", "Zoom Parachute", "Zoom Véhicule", "Temps de Zoom Arrière Sprint", "Temps de Zoom Avant Sprint", "Temps de Zoom Arrière Chute", "Temps de Zoom Avant Chute", "Temps de Zoom Parachute", "Temps de Zoom Véhicule", "Seuil de Vitesse Sprint", "Seuil de Zoom Véhicule", "Seuil de Vitesse Véhicule", "Transition de Fondu", "Transitions de Fondu Fluides", "Effet FOV d'Arme" },
    /* PT */ { "Autor", "Zoom de Corrida", "Zoom de Queda", "Zoom de Paraquedas", "Zoom de Veículo", "Tempo de Zoom-Out de Corrida", "Tempo de Zoom-In de Corrida", "Tempo de Zoom-Out de Queda", "Tempo de Zoom-In de Queda", "Tempo de Zoom de Paraquedas", "Tempo de Zoom de Veículo", "Limite de Velocidade de Corrida", "Limite de Zoom de Veículo", "Limite de Velocidade de Veículo", "Transição de Mesclagem", "Transições de Mesclagem Suaves", "Efeito FOV de Arma" },
    /* DE */ { "Autor", "Sprint-Zoom", "Fall-Zoom", "Fallschirm-Zoom", "Fahrzeug-Zoom", "Sprint-Auszoom-Zeit", "Sprint-Einzoom-Zeit", "Fall-Auszoom-Zeit", "Fall-Einzoom-Zeit", "Fallschirm-Zoom-Zeit", "Fahrzeug-Zoom-Zeit", "Sprint-Geschwindigkeitsschwelle", "Fahrzeug-Zoom-Schwelle", "Fahrzeug-Geschwindigkeitsschwelle", "Überblendungsübergang", "Sanfte Überblendungsübergänge", "Waffen-FOV-Effekt" },
    /* RU */ { "Автор", "Зум спринта", "Зум падения", "Зум парашюта", "Зум транспорта", "Время зума спринта (выход)", "Время зума спринта (вход)", "Время зума падения (выход)", "Время зума падения (вход)", "Время зума парашюта", "Время зума транспорта", "Порог скорости спринта", "Порог зума транспорта", "Порог скорости транспорта", "Плавный переход", "Плавные переходы масштабирования", "Эффект FOV оружия" },
};

static void OnLanguageChanged(int, int newVal, void*)
{
    g_lang = (Lang)newVal;
}

static struct
{
    int sprintZoomX10      = 850;
    int fallZoomX10        = 1100;
    int parachuteZoomX10   = 950;
    int sprintZoomOutTime  = 700;
    int sprintZoomInTime   = 500;
    int fallZoomOutTime    = 3000;
    int fallZoomInTime     = 500;
    int parachuteZoomTime  = 800;
    int vehicleZoomX10     = 900;
    int vehicleZoomTime    = 800;
    int vehicleZoomInTime  = 600;
    int vehicleZoomThresholdX10 = 80;
    int vehicleSpeedThresholdX10 = 200;
    int charSpeedX10       = 65;
    int blendTransition    = 1;
    int weaponFovEnabled   = 1;
} g_settings;

static const float kWeaponFovZoom = 60.0f;
static const int   kWeaponFovTime = 400;

static const char* DrawFloatX10(int v, void*)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", v / 10.0f);
    return buf;
}
static const char* DrawMs(int v, void*)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%dms", v);
    return buf;
}
static const char* kOnOffBlue[2] = { "~b~OFF", "~b~ON" };

struct BoundSetting { int* value; ConfigEntry* entry; };
static BoundSetting g_bound[16];
static int g_boundCount = 0;

static BoundSetting* BindInt(const char* key, int* target)
{
    ConfigEntry* e = cfg->Bind(key, *target, "Settings");
    if (e) *target = e->GetInt();
    BoundSetting* slot = &g_bound[g_boundCount++];
    slot->value = target;
    slot->entry = e;
    return slot;
}

static void OnIntSettingChanged(int, int newVal, void* pData)
{
    BoundSetting* b = (BoundSetting*)pData;
    *b->value = newVal;
    if (b->entry)
    {
        b->entry->SetInt(newVal);
        cfg->Save();
    }
}

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------
enum class ZoomState { NEUTRAL, SPRINT, FALL, PARACHUTE, VEHICLE, WEAPON };
static ZoomState g_zoomState = ZoomState::NEUTRAL;
static int64_t   g_zoomStateEnteredMs = 0;
static float     g_storedFov = 0.0f;   // FOV to restore to — captured fresh at the moment each NEUTRAL→active transition starts
static bool      g_wasPlaying = false;
static float     g_vehicleMaxSpeedSeen = 0.0f;   // self-calibrating "recent peak speed" reference
static float     g_smoothedVehicleRatio = 0.0f;  // rate-limited version of the raw ratio - this is what actually drives FOV
static int64_t   g_lastVehicleRetargetMs = 0;

static int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Main per-frame hook
// ---------------------------------------------------------------------------
static void OnPlayerUpdate(uintptr_t /*info*/)
{
    if (!g_active || !sautils) return;

    if (!CALLSCM(IS_PLAYER_PLAYING, 0))
    {
        g_wasPlaying = false;
        return;
    }
    if (!g_wasPlaying)
    {
        g_zoomState = ZoomState::NEUTRAL;
    }
    g_wasPlaying = true;

    int playerChar = 0;
    CALLSCM(GET_PLAYER_CHAR, 0, &playerChar);

    bool playerControlOn = CALLSCM(IS_PLAYER_CONTROL_ON, 0) != 0;

    float speed = 0.0f;
    CALLSCM(GET_CHAR_SPEED, playerChar, &speed);

    bool inCar = CALLSCM(IS_CHAR_IN_ANY_CAR, playerChar) != 0;
    bool onBike = CALLSCM(IS_CHAR_ON_ANY_BIKE, playerChar) != 0;
    bool inFlyingVehicle = CALLSCM(IS_CHAR_IN_FLYING_VEHICLE, playerChar) != 0;
    bool inAnyVehicle = inCar || onBike || inFlyingVehicle;

    bool inWater = false, isSwimming = false, inAir = false, usingJetpack = false;
    bool hasParachute = false;
    bool hasHeavyWeapon = false;
    bool holdingSniper = false;
    bool isAiming = false;
    if (!inAnyVehicle)
    {
        inWater        = CALLSCM(IS_CHAR_IN_WATER, playerChar) != 0;
        isSwimming      = CALLSCM(IS_CHAR_SWIMMING, playerChar) != 0;
        inAir           = CALLSCM(IS_CHAR_IN_AIR, playerChar) != 0;
        usingJetpack    = CALLSCM(IS_PLAYER_USING_JETPACK, 0) != 0;
        isAiming        = CALLSCM(IS_PLAYER_TARGETTING_ANYTHING, 0) != 0;

        int weaponType = 0;
        CALLSCM(GET_CURRENT_CHAR_WEAPON, playerChar, &weaponType);
        hasParachute = (weaponType == WEAPON_PARACHUTE);
        holdingSniper = (weaponType == WEAPON_SNIPER);

        int weaponSlot = 0;
        CALLSCM(GET_WEAPONTYPE_SLOT, weaponType, &weaponSlot);
        // Shotguns excluded pending investigation of a reported no-damage
        // issue while aimed with one during a mission - can't confirm this
        // mod is actually the cause, but persisting FOV during aim+fire is
        // the only plausible mechanism I can identify, and shotguns'
        // pellet-spread calculation seems like the most likely to be
        // sensitive to it if anything is. Rifles/assault weapons unaffected.
        hasHeavyWeapon = (weaponSlot == SLOT_ASSAULT || weaponSlot == SLOT_RIFLES)
                          && (weaponType != WEAPON_SNIPER);
    }

    float charSpeedThreshold = g_settings.charSpeedX10 / 10.0f;
    float sprintZoom          = g_settings.sprintZoomX10 / 10.0f;
    float fallZoom             = g_settings.fallZoomX10 / 10.0f;
    float parachuteZoom        = g_settings.parachuteZoomX10 / 10.0f;
    float vehicleZoom          = g_settings.vehicleZoomX10 / 10.0f;
    int   easeFlag              = g_settings.blendTransition ? 1 : 0;

    float sprintStopThreshold = charSpeedThreshold * 0.7f;
    float sprintSpeedGate = (g_zoomState == ZoomState::SPRINT) ? sprintStopThreshold : charSpeedThreshold;

    // FIX (0.9 - sprint button overlapping jetpack and gliding backpack):
    // same button-reuse pattern already fixed for swimming - the touch HUD
    // repurposes the sprint button's screen position for jetpack controls
    // and for the parachute/glider, so pressing it there was being read as
    // "wants to sprint". Not querying the sprint widget at all while a
    // jetpack or parachute is active is the same fix that worked for water.
    bool sprintWidgetRawHeld = !inAnyVehicle && !inWater && !isSwimming
                                && !usingJetpack && !hasParachute
                                && CALLSCM(IS_WIDGET_PRESSED, WIDGET_SPRINT) != 0;

    static int64_t s_sprintLastHeldMs = 0;
    int64_t sprintNowMs = NowMs();
    if (sprintWidgetRawHeld) s_sprintLastHeldMs = sprintNowMs;
    bool sprintWidgetHeld = sprintWidgetRawHeld || (sprintNowMs - s_sprintLastHeldMs < 200);

    // Debounced airborne signal for sprint's purposes only - stepping off a
    // fence/cliff without jumping still flips IS_CHAR_IN_AIR for a few
    // frames; only a sustained stretch counts as "really in the air" here.
    static int64_t s_inAirSinceMs = 0;
    int64_t airNowMs = NowMs();
    if (inAir) { if (s_inAirSinceMs == 0) s_inAirSinceMs = airNowMs; }
    else { s_inAirSinceMs = 0; }
    bool inAirSustained = (s_inAirSinceMs != 0) && (airNowMs - s_inAirSinceMs >= 300);

    bool isSprinting  = sprintWidgetHeld && !inAirSustained && (speed > sprintSpeedGate);
    bool isParachute  = inAir && hasParachute && !inAnyVehicle;
    bool isFalling      = inAir && !usingJetpack && !hasParachute && !inAnyVehicle && (speed > 25.0f);
    bool isWeaponFov = g_settings.weaponFovEnabled && hasHeavyWeapon && isAiming && !sprintWidgetHeld
                       && !inWater && !isSwimming && !inAir && !inAnyVehicle;

    bool accelHeld = inAnyVehicle && CALLSCM(IS_WIDGET_PRESSED, WIDGET_ACCELERATE) != 0;
    bool brakeHeld  = inAnyVehicle && CALLSCM(IS_WIDGET_PRESSED, WIDGET_BRAKE) != 0;
    bool wantVehicle = inAnyVehicle;

    if (speed > g_vehicleMaxSpeedSeen) g_vehicleMaxSpeedSeen = speed;
    else if (inAnyVehicle && g_vehicleMaxSpeedSeen > 0.01f) g_vehicleMaxSpeedSeen *= 0.999f;
    float vehicleSpeedRatio = (inAnyVehicle && g_vehicleMaxSpeedSeen > 0.01f) ? (speed / g_vehicleMaxSpeedSeen) : 0.0f;
    if (vehicleSpeedRatio > 1.0f) vehicleSpeedRatio = 1.0f;
    if (vehicleSpeedRatio < 0.0f) vehicleSpeedRatio = 0.0f;

    static int64_t s_lastSmoothMs = 0;
    int64_t smoothNowMs = NowMs();
    if (s_lastSmoothMs == 0) s_lastSmoothMs = smoothNowMs;
    int64_t elapsedMs = smoothNowMs - s_lastSmoothMs;
    s_lastSmoothMs = smoothNowMs;
    if (elapsedMs > 250) elapsedMs = 250;
    const float kVehicleRatioRampMs = 2500.0f;
    // FIX (0.9 - re-diagnosed): the brake-specific 2.5x-faster ramp was
    // the actual cause of the reported stutter, not Blend Transition.
    // Braking from full speed already drops real speed very fast on its
    // own; compounding that with an *also*-accelerated smoothing rate
    // produced large per-tick target jumps specifically in the high-speed
    // braking case (small at low speed, since the ratio's total range of
    // change is small there too - matching exactly what was reported: no
    // stutter at low speed, stutter braking hard from full speed). Using
    // one constant rate regardless of braking keeps every tick's step
    // size bounded the same way in both directions.
    float maxStep = (float)elapsedMs / kVehicleRatioRampMs;
    if (!inAnyVehicle)
    {
        g_smoothedVehicleRatio = 0.0f;
    }
    else if (g_smoothedVehicleRatio < vehicleSpeedRatio)
    {
        g_smoothedVehicleRatio += maxStep;
        if (g_smoothedVehicleRatio > vehicleSpeedRatio) g_smoothedVehicleRatio = vehicleSpeedRatio;
    }
    else if (g_smoothedVehicleRatio > vehicleSpeedRatio)
    {
        g_smoothedVehicleRatio -= maxStep;
        if (g_smoothedVehicleRatio < vehicleSpeedRatio) g_smoothedVehicleRatio = vehicleSpeedRatio;
    }

    ZoomState wantState = ZoomState::NEUTRAL;
    if (!holdingSniper && playerControlOn)
    {
        if (isSprinting)       wantState = ZoomState::SPRINT;
        else if (isParachute) wantState = ZoomState::PARACHUTE;
        else if (wantVehicle)  wantState = ZoomState::VEHICLE;
        else if (isWeaponFov) wantState = ZoomState::WEAPON;
        else if (isFalling)   wantState = ZoomState::FALL;
    }

    ZoomState effectiveWantState = wantState;
    if (wantState != ZoomState::NEUTRAL && g_zoomState != ZoomState::NEUTRAL && wantState != g_zoomState)
    {
        if (wantState == ZoomState::VEHICLE || g_zoomState == ZoomState::VEHICLE)
            effectiveWantState = ZoomState::NEUTRAL;
    }

    const int64_t kSprintMinCommitMs = 250;
    if (g_zoomState == ZoomState::SPRINT && effectiveWantState != ZoomState::SPRINT
        && (NowMs() - g_zoomStateEnteredMs) < kSprintMinCommitMs)
    {
        effectiveWantState = ZoomState::SPRINT;
    }

    if (effectiveWantState != g_zoomState)
    {
        float curFov = 0.0f;
        CALLSCM(GET_CAMERA_FOV, &curFov);

        bool enteringFromNeutral = (effectiveWantState != ZoomState::NEUTRAL && g_zoomState == ZoomState::NEUTRAL);
        bool curFovSane = (curFov > 1.0f && curFov < 170.0f);

        if (!enteringFromNeutral || curFovSane)
        {
            if (effectiveWantState != ZoomState::NEUTRAL)
            {
                if (enteringFromNeutral)
                    g_storedFov = curFov;

                float target; int timeMs;
                switch (effectiveWantState)
                {
                    case ZoomState::SPRINT:    target = sprintZoom;    timeMs = g_settings.sprintZoomOutTime; break;
                    case ZoomState::FALL:      target = fallZoom;      timeMs = g_settings.fallZoomOutTime;   break;
                    case ZoomState::PARACHUTE: target = parachuteZoom; timeMs = g_settings.parachuteZoomTime; break;
                    case ZoomState::WEAPON:    target = kWeaponFovZoom; timeMs = kWeaponFovTime;              break;
                    case ZoomState::VEHICLE:   target = curFov + (vehicleZoom - curFov) * g_smoothedVehicleRatio; timeMs = g_settings.vehicleZoomTime; break;
                    default:                    target = curFov;        timeMs = 500; break;
                }
                if (effectiveWantState != ZoomState::VEHICLE)
                    CALLSCM(CAMERA_SET_LERP_FOV, curFov, target, timeMs, easeFlag);
                CALLSCM(CAMERA_PERSIST_FOV, 1);
            }
            else
            {
                int returnTime;
                switch (g_zoomState)
                {
                    case ZoomState::PARACHUTE: returnTime = g_settings.parachuteZoomTime; break;
                    case ZoomState::WEAPON:    returnTime = kWeaponFovTime;               break;
                    case ZoomState::VEHICLE:   returnTime = brakeHeld ? (g_settings.vehicleZoomInTime / 2)
                                                                        : g_settings.vehicleZoomInTime;         break;
                    case ZoomState::FALL:      returnTime = g_settings.fallZoomInTime;    break;
                    default:                    returnTime = g_settings.sprintZoomInTime; break;
                }
                CALLSCM(CAMERA_SET_LERP_FOV, curFov, g_storedFov, returnTime, easeFlag);
                CALLSCM(CAMERA_PERSIST_FOV, 0);
            }

            g_zoomState = effectiveWantState;
            g_zoomStateEnteredMs = NowMs();
        }
    }

    if (g_zoomState == ZoomState::VEHICLE && effectiveWantState == ZoomState::VEHICLE)
    {
        int64_t nowMs2 = NowMs();
        if (nowMs2 - g_lastVehicleRetargetMs >= 150)
        {
            g_lastVehicleRetargetMs = nowMs2;
            float curFov = 0.0f;
            CALLSCM(GET_CAMERA_FOV, &curFov);
            if (curFov > 1.0f && curFov < 170.0f)
            {
                float target = g_storedFov + (vehicleZoom - g_storedFov) * g_smoothedVehicleRatio;
                CALLSCM(CAMERA_SET_LERP_FOV, curFov, target, 350, 0 /* linear - avoids ease-curve restart kink */);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Native settings tab.
// ---------------------------------------------------------------------------
static void OnSprintZoomChanged(int, int nv, void*) { g_settings.sprintZoomX10 = nv; }
static void SetupNativeSettingsTab()
{
    g_tab = sautils->AddSettingsTab("Camera~b~FX");

    sautils->AddButton(g_tab, "~b~Author: Senji", NULL);
    sautils->AddButton(g_tab, "CameraFX 0.9 (Final)", NULL);
    const char** T = kStrings[(int)g_lang];
    sautils->AddClickableItem(g_tab, "Language", (int)g_lang, 0, (int)Lang::COUNT - 1,
        kLangNames, OnLanguageChanged);

    sautils->AddSliderItem(g_tab, T[(int)S::SprintZoom], g_settings.sprintZoomX10, 0, 2000,
        OnIntSettingChanged, DrawFloatX10, BindInt("SprintZoom", &g_settings.sprintZoomX10));
    sautils->AddSliderItem(g_tab, T[(int)S::FallZoom], g_settings.fallZoomX10, 0, 2000,
        OnIntSettingChanged, DrawFloatX10, BindInt("FallZoom", &g_settings.fallZoomX10));
    sautils->AddSliderItem(g_tab, T[(int)S::ParachuteZoom], g_settings.parachuteZoomX10, 0, 2000,
        OnIntSettingChanged, DrawFloatX10, BindInt("ParachuteZoom", &g_settings.parachuteZoomX10));
    sautils->AddSliderItem(g_tab, T[(int)S::VehicleZoom], g_settings.vehicleZoomX10, 0, 2000,
        OnIntSettingChanged, DrawFloatX10, BindInt("VehicleZoom", &g_settings.vehicleZoomX10));
    sautils->AddSliderItem(g_tab, T[(int)S::SprintOutT], g_settings.sprintZoomOutTime, 0, 10000,
        OnIntSettingChanged, DrawMs, BindInt("SprintZoomOutTime", &g_settings.sprintZoomOutTime));
    sautils->AddSliderItem(g_tab, T[(int)S::SprintInT], g_settings.sprintZoomInTime, 0, 10000,
        OnIntSettingChanged, DrawMs, BindInt("SprintZoomInTime", &g_settings.sprintZoomInTime));
    sautils->AddSliderItem(g_tab, T[(int)S::FallOutT], g_settings.fallZoomOutTime, 0, 10000,
        OnIntSettingChanged, DrawMs, BindInt("FallZoomOutTime", &g_settings.fallZoomOutTime));
    sautils->AddSliderItem(g_tab, T[(int)S::FallInT], g_settings.fallZoomInTime, 0, 10000,
        OnIntSettingChanged, DrawMs, BindInt("FallZoomInTime", &g_settings.fallZoomInTime));
    sautils->AddSliderItem(g_tab, T[(int)S::ParachuteT], g_settings.parachuteZoomTime, 0, 10000,
        OnIntSettingChanged, DrawMs, BindInt("ParachuteZoomTime", &g_settings.parachuteZoomTime));
    sautils->AddSliderItem(g_tab, T[(int)S::VehicleT], g_settings.vehicleZoomTime, 0, 10000,
        OnIntSettingChanged, DrawMs, BindInt("VehicleZoomTime", &g_settings.vehicleZoomTime));
    sautils->AddSliderItem(g_tab, "Vehicle Zoom-In Time", g_settings.vehicleZoomInTime, 0, 10000,
        OnIntSettingChanged, DrawMs, BindInt("VehicleZoomInTime", &g_settings.vehicleZoomInTime));
    sautils->AddSliderItem(g_tab, T[(int)S::SprintThresh], g_settings.charSpeedX10, 0, 500,
        OnIntSettingChanged, DrawFloatX10, BindInt("SprintSpeedThreshold", &g_settings.charSpeedX10));
    sautils->AddClickableItem(g_tab, T[(int)S::BlendTransition], g_settings.blendTransition, 0, 1,
        kOnOffBlue, OnIntSettingChanged, BindInt("BlendTransition", &g_settings.blendTransition));
    sautils->AddClickableItem(g_tab, T[(int)S::WeaponFov], g_settings.weaponFovEnabled, 0, 1,
        kOnOffBlue, OnIntSettingChanged, BindInt("WeaponFovEnabled", &g_settings.weaponFovEnabled));

    sautils->AddPlayerUpdateListener(OnPlayerUpdate, true);

    g_active = true;
    logger->Info("CameraFX v0.9 (native) loaded — settings under its own CameraFX tab, persisted to ini.");
}

ON_MOD_PRELOAD()
{
    logger->SetTag("CameraFX");
    InstallSignalGuard();
}

ON_MOD_LOAD()
{
    sautils = (ISAUtils*)GetInterface("SAUtils");
    if (!sautils)
    {
        logger->Error("SAUtils interface not found — is SAUtils installed and loaded before CameraFX?");
        return;
    }
    SetupNativeSettingsTab();
}

ON_MOD_UNLOAD()
{
    g_active = false;
}
