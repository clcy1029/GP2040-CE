#include "addons/reverse.h"
#include "storagemanager.h"
#include "GamepadEnums.h"
#include "GamepadState.h"
#include "gamepad.h"
#include "helper.h"
#include "config.pb.h"

// Timing: one fighting-game frame = 1/60s ~= 16666 us. The input loop runs FAR faster than 60Hz,
// so each motion step is held by the microsecond wall-clock (getMicro()), not by loop ticks.
#define SUPER_FRAME_US        16666
#define SUPER_STEP_FRAMES_MIN 1
#define SUPER_STEP_FRAMES_MAX 2

// Charge moves (46* back-charge, 28* down-charge): the charge direction must be held this long
// (continuous, real-time) before the press, else the move doesn't fire. - clcy
#define CHARGE_FRAMES 45
#define CHARGE_US     ((uint64_t)CHARGE_FRAMES * SUPER_FRAME_US)   // ~750ms

// "Reverse 23626 LP/LK" super = shared opening 21346 + a tail chosen at the end of the opening.
// Steps are absolute dpad bits (no mirror); see the super block in process() for the full logic. - clcy
static const uint16_t SUPER_OPEN[]  = {                       // 21346 (shared opening)
    GAMEPAD_MASK_DOWN,                       // 2
    GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT,   // 1
    GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT,  // 3
    GAMEPAD_MASK_LEFT,                       // 4
    GAMEPAD_MASK_RIGHT,                      // 6
};
static const int SUPER_OPEN_COUNT = (int)(sizeof(SUPER_OPEN) / sizeof(SUPER_OPEN[0]));
static const uint16_t SUPER_T_DIV[] = { GAMEPAD_MASK_DOWN, GAMEPAD_MASK_LEFT, GAMEPAD_MASK_RIGHT }; // 246 -> 21346246 (divert)
static const uint16_t SUPER_T_FWD[] = { GAMEPAD_MASK_DOWN, GAMEPAD_MASK_RIGHT };                    // 26  -> 2134626 (held 4)
static const uint16_t SUPER_T_BCK[] = { GAMEPAD_MASK_DOWN, GAMEPAD_MASK_LEFT };                     // 24  -> 2134624 (held 6)

// Current step of the super (opening, then the chosen tail: 1=246, 2=26, 3=24). False past the end.
static bool superSeq(int step, int tailType, uint16_t& dpad, bool& isLast) {
    if (step < SUPER_OPEN_COUNT) { dpad = SUPER_OPEN[step]; isLast = false; return true; }
    int ti = step - SUPER_OPEN_COUNT;
    const uint16_t* tail; int n;
    switch (tailType) {
        case 1: tail = SUPER_T_DIV; n = 3; break;
        case 2: tail = SUPER_T_FWD; n = 2; break;
        case 3: tail = SUPER_T_BCK; n = 2; break;
        default: return false;
    }
    if (ti >= n) return false;
    dpad = tail[ti]; isLast = (ti == n - 1); return true;
}

// Random step duration in microseconds: uniform [minF, maxF] frames (1 frame = SUPER_FRAME_US).
// Tiny xorshift32 PRNG, lazily seeded from the microsecond clock so the pattern varies per run. - clcy
static inline uint64_t randStepUs(uint32_t& rng, uint8_t minF, uint8_t maxF) {
    if (rng == 0) rng = (uint32_t)getMicro() | 1u;   // seed once (must be nonzero)
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    uint32_t span = (uint32_t)(maxF - minF + 1);
    uint32_t frames = (uint32_t)minF + (rng % span);
    return (uint64_t)frames * SUPER_FRAME_US;
}
static inline uint64_t superRandStepUs(uint32_t& rng) {
    return randStepUs(rng, SUPER_STEP_FRAMES_MIN, SUPER_STEP_FRAMES_MAX);
}

// ---- General hardcoded-motion table (236/214 QCR, 6214/4236 HCB, 623*, 21346*, 22*, 2HP) - clcy ----
// Numpad -> absolute dpad bits (fixed, no mirror): 5 = neutral.
#define D_N  0
#define D_2  GAMEPAD_MASK_DOWN
#define D_1  (GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT)
#define D_3  (GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT)
#define D_4  GAMEPAD_MASK_LEFT
#define D_6  GAMEPAD_MASK_RIGHT
#define D_8  GAMEPAD_MASK_UP
// Attack masks on this board: LP=B1 MP=B2 HP=R1 LK=B3 MK=B4 HK=L1
#define A_LP GAMEPAD_MASK_B1
#define A_MP GAMEPAD_MASK_B2
#define A_HP GAMEPAD_MASK_R1
#define A_LK GAMEPAD_MASK_B3
#define A_MK GAMEPAD_MASK_B4
#define A_HK GAMEPAD_MASK_L1
#define A_ALL (A_LP | A_MP | A_HP | A_LK | A_MK | A_HK)   // all six attacks - clcy

struct MotionStep { uint16_t dpad; uint8_t minF; uint8_t maxF; };
struct MotionDef  { GpioAction action; const MotionStep* steps; uint8_t count; uint16_t defaultEnder; };

static const MotionStep ST_236[]   = { {D_N,2,2}, {D_2,2,3}, {D_3,2,3}, {D_6,2,3} };   // 5236: leading 回中 (neutral 2f) then 236, each dir 2-3f
static const MotionStep ST_214[]   = { {D_N,2,2}, {D_2,2,3}, {D_1,2,3}, {D_4,2,3} };   // 5214: leading 回中 (neutral 2f) then 214, each dir 2-3f
static const MotionStep ST_6214[]  = { {D_6,2,3}, {D_2,2,3}, {D_1,2,3}, {D_4,2,3} };   // 6214 HCB (each step 2-3f)
static const MotionStep ST_4236[]  = { {D_4,2,3}, {D_2,2,3}, {D_3,2,3}, {D_6,2,3} };   // 4236 HCB (each step 2-3f)
static const MotionStep ST_6214_LP[] = { {D_6,1,2}, {D_2,1,2}, {D_1,1,2}, {D_4,2,2} }; // 6214 LP: 6,2,1 each 1-2f, attack on the last dir (4) for a fixed 2f
static const MotionStep ST_4236_LP[] = { {D_4,1,2}, {D_2,1,2}, {D_3,1,2}, {D_6,2,2} }; // 4236 LP: 4,2,3 each 1-2f, attack on the last dir (6) for a fixed 2f
static const MotionStep ST_623[]   = { {D_1,1,2}, {D_3,1,2}, {D_1,1,2}, {D_3,1,2}, {D_1,2,3} };
static const MotionStep ST_623HP[] = { {D_1,1,1}, {D_3,1,1}, {D_1,1,1}, {D_3,1,1}, {D_1,1,3} }; // steps 1f, last 1-3f
static const MotionStep ST_21346[] = { {D_2,1,2}, {D_1,1,2}, {D_3,1,2}, {D_4,1,2}, {D_6,2,3} };
static const MotionStep ST_21346246[] = { {D_2,1,1},{D_1,1,1},{D_3,1,1},{D_4,1,1},{D_6,1,1},{D_2,1,1},{D_4,1,1},{D_6,1,1} }; // all 1 frame
static const MotionStep ST_214236[]   = { {D_2,1,1}, {D_1,1,1}, {D_4,1,1}, {D_2,1,1}, {D_3,1,1}, {D_6,2,2} }; // 214236: each step 1f, last (attack) 2f
static const MotionStep ST_22[]    = { {D_N,1,2}, {D_2,1,2}, {D_N,1,2}, {D_2,2,3} };
static const MotionStep ST_2HP[]   = { {D_2,4,4} };              // single step, 4 frames
static const MotionStep ST_2PP[]   = { {D_2,2,2} };              // 2PP: down + LP+MP, 2 frames
static const MotionStep ST_3K[]    = { {D_N,2,2} };              // KKK: neutral + LK+MK+HK, 2 frames (ignores all input)

static const MotionDef MOTION_DEFS[] = {
    { GpioAction::BUTTON_PRESS_QCR_236,    ST_236,   4, 0 },
    { GpioAction::BUTTON_PRESS_QCR_214,    ST_214,   4, 0 },
    { GpioAction::BUTTON_PRESS_BISON_5236_LK, ST_236, 4, A_LK },
    { GpioAction::BUTTON_PRESS_BISON_5214_LK, ST_214, 4, A_LK },
    { GpioAction::BUTTON_PRESS_6214_HCB,   ST_6214,    4, 0 },
    { GpioAction::BUTTON_PRESS_6214_LP,    ST_6214_LP, 4, A_LP },
    { GpioAction::BUTTON_PRESS_4236_HCB,   ST_4236,    4, 0 },
    { GpioAction::BUTTON_PRESS_4236_LP,    ST_4236_LP, 4, A_LP },
    { GpioAction::BUTTON_PRESS_623_LP,     ST_623,   5, A_LP },
    { GpioAction::BUTTON_PRESS_623_MP,     ST_623,   5, A_MP },
    { GpioAction::BUTTON_PRESS_623_HP,     ST_623HP, 5, A_HP },
    { GpioAction::BUTTON_PRESS_623_LPMP,   ST_623,   5, A_LP | A_MP },
    { GpioAction::BUTTON_PRESS_623_LK,     ST_623,   5, A_LK },
    { GpioAction::BUTTON_PRESS_623_MK,     ST_623,   5, A_MK },
    { GpioAction::BUTTON_PRESS_623_HK,     ST_623,   5, A_HK },
    { GpioAction::BUTTON_PRESS_623_LKMK,   ST_623,   5, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_21346_LP,   ST_21346, 5, A_LP },
    { GpioAction::BUTTON_PRESS_21346_LK,   ST_21346, 5, A_LK },
    { GpioAction::BUTTON_PRESS_21346_HK,   ST_21346, 5, A_HK },
    { GpioAction::BUTTON_PRESS_21346_LKMK, ST_21346, 5, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_21346246_LK, ST_21346246, 8, A_LK },
    { GpioAction::BUTTON_PRESS_21346246_LP, ST_21346246, 8, A_LP },
    { GpioAction::BUTTON_PRESS_214236_LK,   ST_214236, 6, A_LK },
    { GpioAction::BUTTON_PRESS_214236_MK,   ST_214236, 6, A_MK },
    { GpioAction::BUTTON_PRESS_214236_HK,   ST_214236, 6, A_HK },
    { GpioAction::BUTTON_PRESS_214236_LKMK, ST_214236, 6, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_22_LKMK,    ST_22,    4, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_22_LK,      ST_22,    4, A_LK },
    { GpioAction::BUTTON_PRESS_22_LP,      ST_22,    4, A_LP },
    { GpioAction::BUTTON_PRESS_22,         ST_22,    4, 0 },
    { GpioAction::BUTTON_PRESS_2_HP,       ST_2HP,   1, A_HP },
    { GpioAction::BUTTON_PRESS_2PP,        ST_2PP,   1, A_LP | A_MP },
    { GpioAction::BUTTON_PRESS_KKK,        ST_3K,    1, A_LK | A_MK | A_HK },
};
static const int MOTION_COUNT = (int)(sizeof(MOTION_DEFS) / sizeof(MOTION_DEFS[0]));
static_assert(MOTION_COUNT <= REVERSE_MOTION_MAX, "increase REVERSE_MOTION_MAX");

// ---- Directional one-button moves (46 LP/HK/MK, 1/3 HP/HK, 28 HK/LK/LKMK, Air Throw, JMP, KKK) - clcy ----
// Unlike the fixed MOTION_DEFS above, these SAMPLE a held direction at press and act on it. Most MIRROR
// the held horizontal: you hold "back", we output "forward" (hold ←/↙/↖ -> 6, hold →/↘/↗ -> 4; the 1/3
// moves add DOWN -> the ↓-forward diagonal 3/1). Gate options (GATE_*): HORIZ needs a held ←/→, DOWN needs
// a held ↓, NONE never gates (jumps: no direction -> straight up). HORIZ_CHG / DOWN_CHG additionally require a
// real CHARGE (the held ←/→ or ↓ held >= CHARGE_FRAMES ~45f) — used by the 46* (back-charge) and 28* (down-charge).
// The charge is CONSUMED on fire (its timer resets) so you must re-charge before the next one.
// Each plays 1-2 fixed steps, a random [minF,maxF] frames per step;
// the attack fires on its step. A held attack in the move's addCat REPLACES the base (46: same-category;
// 28: any attack); none held -> the base. Lives in process().
#define DSF_UP   0x1u   // step includes UP
#define DSF_FWD  0x2u   // step includes the (mirrored) forward horizontal
#define DSF_DOWN 0x4u   // step includes DOWN (so FWD+DOWN = the 1/3 down-forward diagonal)
#define DSF_FWD_RAW 0x8u  // step includes the held horizontal AS-IS (no mirror, vertical stripped; Anti Air 4MK)

#define GATE_NONE      0    // no gate (jumps: no direction -> straight up)
#define GATE_HORIZ     1    // require a held ←/→ at press (mirror to forward), else don't fire
#define GATE_DOWN      2    // require a held ↓ at press, else don't fire
#define GATE_HORIZ_CHG 3    // GATE_HORIZ + the held ←/→ must be CHARGED >= CHARGE_FRAMES (46* back-charge)
#define GATE_DOWN_CHG  4    // GATE_DOWN  + ↓ must be CHARGED >= CHARGE_FRAMES (28* down-charge)

struct DirStep { uint8_t flags; uint16_t attack; uint8_t minF; uint8_t maxF; };   // attack: buttons this step (0=none); minF/maxF: per-step frames (0 -> use the move's range) - clcy
struct DirMoveDef {
    GpioAction     action;
    uint8_t        gate;     // GATE_NONE / GATE_HORIZ / GATE_DOWN
    uint16_t       addCat;   // pressed attacks of this category added on the attack step (0 = none)
    const DirStep* steps;
    uint8_t        count;
    uint8_t        minF;     // per-step hold = random [minF, maxF] frames
    uint8_t        maxF;
};

static const DirStep ST_46LP[]     = { { DSF_FWD, A_LP } };                                 // forward + LP
static const DirStep ST_46MP[]     = { { DSF_FWD, 0, 1, 1 }, { DSF_FWD, A_MP } };           // 46 MP: 1f forward lead (detect attack), then forward + MP
static const DirStep ST_13HP[]     = { { DSF_FWD | DSF_DOWN, A_HP } };                       // ↓-forward (1/3) + HP
static const DirStep ST_13HK[]     = { { DSF_FWD | DSF_DOWN, A_HK } };                       // ↓-forward (1/3) + HK
static const DirStep ST_28HK[]     = { { DSF_UP, A_HK } };                                   // 8 (up) + HK   (charge: needs held ↓)
static const DirStep ST_28LK[]     = { { DSF_UP, A_LK } };                                   // 8 (up) + LK
static const DirStep ST_28LKMK[]   = { { DSF_UP, A_LK | A_MK } };                            // 8 (up) + LK+MK
static const DirStep ST_AIRTHROW[] = { { DSF_UP | DSF_FWD, 0 }, { DSF_FWD, A_LK | A_LP } };  // jump-in, then LK+LP
static const DirStep ST_JMP[]      = { { DSF_UP | DSF_FWD, 0 }, { DSF_FWD, A_MP } };         // jump-in, then MP
static const DirStep ST_KKK[]      = { { DSF_FWD, A_LK | A_MK | A_HK } };                    // forward + LK+MK+HK (Reversal KKK)
static const DirStep ST_AA4MK[]    = { { DSF_FWD_RAW, A_MK } };                              // held ←/→ as-is (↓ stripped, no mirror) + MK

static const DirMoveDef DIR_MOVE_DEFS[] = {
    { GpioAction::BUTTON_PRESS_46_LP,        GATE_HORIZ_CHG, (uint16_t)(A_LP | A_MP | A_HP), ST_46LP,     1, 2, 3 },
    { GpioAction::BUTTON_PRESS_46_MP,        GATE_HORIZ_CHG, (uint16_t)(A_LP | A_MP | A_HP), ST_46MP,     2, 2, 3 },
    { GpioAction::BUTTON_PRESS_13_HP,        GATE_HORIZ,     0,                              ST_13HP,     1, 3, 4 },
    { GpioAction::BUTTON_PRESS_13_HK,        GATE_HORIZ,     0,                              ST_13HK,     1, 3, 4 },
    { GpioAction::BUTTON_PRESS_28_HK,        GATE_DOWN_CHG,  (uint16_t)A_ALL,                ST_28HK,     1, 2, 3 },
    { GpioAction::BUTTON_PRESS_28_LK,        GATE_DOWN_CHG,  (uint16_t)A_ALL,                ST_28LK,     1, 2, 3 },
    { GpioAction::BUTTON_PRESS_28_LKMK,      GATE_DOWN_CHG,  (uint16_t)A_ALL,                ST_28LKMK,   1, 2, 3 },
    { GpioAction::BUTTON_PRESS_AIR_THROW,    GATE_NONE,      0,                              ST_AIRTHROW, 2, 2, 3 },
    { GpioAction::BUTTON_PRESS_JMP,          GATE_NONE,      0,                              ST_JMP,      2, 2, 3 },
    { GpioAction::BUTTON_PRESS_REVERSAL_KKK, GATE_HORIZ,     0,                              ST_KKK,      1, 2, 3 },
    { GpioAction::BUTTON_PRESS_ANTI_AIR_4MK, GATE_HORIZ,     0,                              ST_AA4MK,    1, 2, 3 },
};
static const int DIR_MOVE_COUNT = (int)(sizeof(DIR_MOVE_DEFS) / sizeof(DIR_MOVE_DEFS[0]));
static_assert(DIR_MOVE_COUNT <= REVERSE_DIRMOVE_MAX, "increase REVERSE_DIRMOVE_MAX");

// 623 HP Charge: dpad sequence 1,3,1,3,1 (each 1-2f random), then HP held while the button stays
// pressed (release to stop). Handled by its own block in process() (the hold phase is unlike the
// play-to-completion motions). - clcy
static const uint16_t CHARGE_623HP_DIRS[] = { D_1, D_3, D_1, D_3, D_1 };
static const int CHARGE_623HP_COUNT = (int)(sizeof(CHARGE_623HP_DIRS) / sizeof(CHARGE_623HP_DIRS[0]));

bool ReverseInput::available() {
    const ReverseOptions& options = Storage::getInstance().getAddonOptions().reverseOptions;
	return options.enabled;
}

void ReverseInput::setup()
{
    // Setup Reverse Input Button
    mapInputReverse = new GamepadButtonMapping(0);
    mapChargeHP = new GamepadButtonMapping(0);
    mapSuperLP = new GamepadButtonMapping(0);
    mapSuperLK = new GamepadButtonMapping(0);
    mapSuperLPNew = new GamepadButtonMapping(0);
    mapSuperLKNew = new GamepadButtonMapping(0);

    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch (pinMappings[pin].action) {
            case GpioAction::BUTTON_PRESS_INPUT_REVERSE: mapInputReverse->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_623_HP_CHARGE: mapChargeHP->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_SUPER_LP: mapSuperLP->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_SUPER_LK: mapSuperLK->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_SUPER_LP_NEW: mapSuperLPNew->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_SUPER_LK_NEW: mapSuperLKNew->pinMask |= 1 << pin; break;
            default:    break;
        }
    }

    // Setup Reverse LED if available
    const ReverseOptions& options = Storage::getInstance().getAddonOptions().reverseOptions;
    pinLED = 0xff;
    if (isValidPin(options.ledPin)) {
        pinLED = options.ledPin;
        gpio_init(pinLED);
        gpio_set_dir(pinLED, GPIO_OUT);
        gpio_put(pinLED, 1);
    }

    actionUp = options.actionUp;
    actionDown = options.actionDown;
    actionLeft = options.actionLeft;
    actionRight = options.actionRight;

    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    mapDpadUp    = gamepad->mapDpadUp;
    mapDpadDown  = gamepad->mapDpadDown;
    mapDpadLeft  = gamepad->mapDpadLeft;
    mapDpadRight = gamepad->mapDpadRight;
    mapButtonB1  = gamepad->mapButtonB1;
    mapButtonB2  = gamepad->mapButtonB2;
    mapButtonL2  = gamepad->mapButtonL2;
    mapButtonR1  = gamepad->mapButtonR1;
    mapButtonB3  = gamepad->mapButtonB3;
    mapButtonB4  = gamepad->mapButtonB4;
    mapButtonL1  = gamepad->mapButtonL1;

    invertXAxis = gamepad->getOptions().invertXAxis;
    invertYAxis = gamepad->getOptions().invertYAxis;

    state = false; // if reverse button is pressed
    stateReverseActive = false; // if the gated Drive Reversal is active (held + a ←/→)

    // super motion state - clcy
    superActive = false;
    superStep = 0;
    superStepStartTime = 0;
    superMirror = false;
    superAttackMask = mapButtonB1->buttonMask | mapButtonB2->buttonMask | mapButtonR1->buttonMask
                    | mapButtonB3->buttonMask | mapButtonB4->buttonMask | mapButtonL1->buttonMask;
    superEnderDefault = 0;
    superEnderMask = 0;
    prevSuperLP = false;
    prevSuperLK = false;
    prevSuperLPNew = false;
    prevSuperLKNew = false;
    superDirPending = false;
    superStepDurationUs = 0;
    superRng = 0;
    superDivert = false;
    superTailType = 0;
    superDirLatch = 0;
    superDivertEnder = 0;

    // General motion-button state + pin masks (scan every motion action) - clcy
    motionActive = false;
    motionWhich = 0;
    motionStep = 0;
    motionStepStartTime = 0;
    motionStepDurationUs = 0;
    for (int i = 0; i < MOTION_COUNT; i++) {
        motionPinMask[i] = 0;
        motionPrev[i] = false;
    }
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        for (int i = 0; i < MOTION_COUNT; i++) {
            if (pinMappings[pin].action == MOTION_DEFS[i].action) {
                motionPinMask[i] |= (1u << pin);
            }
        }
    }

    // Directional-move state + pin masks (46 LP/HK, Air Throw, JMP) - clcy
    dirActive = false;
    dirWhich = 0;
    dirStep = 0;
    dirStepStartTime = 0;
    dirStepDurationUs = 0;
    dirForward = 0;
    dirHeld = 0;
    dirAddedAttack = 0;
    chargeActive = false;
    chargeHold = false;
    chargeStep = 0;
    chargeStepStartTime = 0;
    chargeStepDurationUs = 0;
    chargePrev = false;
    holdStartLeft = 0;
    holdStartRight = 0;
    holdStartDown = 0;
    for (int i = 0; i < DIR_MOVE_COUNT; i++) {
        dirPinMask[i] = 0;
        dirPrev[i] = false;
    }
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        for (int i = 0; i < DIR_MOVE_COUNT; i++) {
            if (pinMappings[pin].action == DIR_MOVE_DEFS[i].action) {
                dirPinMask[i] |= (1u << pin);
            }
        }
    }
}

void ReverseInput::update() {
    Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;

    // stateReverseActive (which drives the dpad-axis inversion in input()) is finalized in process(),
    // since the gated Drive Reversal also needs the held direction. The directional moves below handle
    // their own direction. - clcy
    state = (values & mapInputReverse->pinMask);
}
void ReverseInput::reinit() {
    delete mapInputReverse;
    delete mapChargeHP;
    delete mapSuperLP;
    delete mapSuperLK;
    delete mapSuperLPNew;
    delete mapSuperLKNew;
    setup();
}

uint8_t ReverseInput::input(uint32_t valueMask, uint16_t buttonMask, uint16_t buttonMaskReverse, uint8_t action, bool invertAxis) {
    if (stateReverseActive && action == 2) {
        return 0;
    }
    bool invert = (stateReverseActive && action == 1) ? !invertAxis : invertAxis;
    return (valueMask ? (invert ? buttonMaskReverse : buttonMask) : 0);
}

void ReverseInput::process()
{
    // Update Reverse State
    update();

    Gamepad * gamepad = Storage::getInstance().GetGamepad();

    // temporary raw dpad storage - cliu55
    uint16_t rawDpad = gamepad->state.dpad;
    uint16_t rawButtons = gamepad->state.buttons;   // player's held buttons, before reverse remapping - clcy

    // ---- Charge tracking: continuous hold of 4/6/2 (wall-clock; 0 = released) for the 46*/28* charge gates - clcy ----
    {
        uint64_t nowC = getMicro();
        if (rawDpad & GAMEPAD_MASK_LEFT)  { if (!holdStartLeft)  holdStartLeft  = (nowC ? nowC : 1); } else holdStartLeft  = 0;
        if (rawDpad & GAMEPAD_MASK_RIGHT) { if (!holdStartRight) holdStartRight = (nowC ? nowC : 1); } else holdStartRight = 0;
        if (rawDpad & GAMEPAD_MASK_DOWN)  { if (!holdStartDown)  holdStartDown  = (nowC ? nowC : 1); } else holdStartDown  = 0;
    }

    // Drive Reversal (gated): only acts when a ←/→ is held (no horizontal -> does nothing). It inverts
    // the dpad (via input() below) and presses L2. - clcy
    bool gateHoriz     = (rawDpad & mapDpadLeft->buttonMask) || (rawDpad & mapDpadRight->buttonMask);
    bool reverseActive = state && gateHoriz;
    stateReverseActive = reverseActive;

    gamepad->state.dpad = 0
        | input(gamepad->state.dpad & mapDpadUp->buttonMask,    mapDpadUp->buttonMask,      mapDpadDown->buttonMask,    actionUp,       invertYAxis)
        | input(gamepad->state.dpad & mapDpadDown->buttonMask,  mapDpadDown->buttonMask,    mapDpadUp->buttonMask,      actionDown,     invertYAxis)
        | input(gamepad->state.dpad & mapDpadLeft->buttonMask,  mapDpadLeft->buttonMask,    mapDpadRight->buttonMask,   actionLeft,     invertXAxis)
        | input(gamepad->state.dpad & mapDpadRight->buttonMask, mapDpadRight->buttonMask,   mapDpadLeft->buttonMask,    actionRight,    invertXAxis)
    ;


    if (reverseActive){
        // Drive Reversal (gated) -> press L2 (sf6 drive reversal)
        gamepad->state.buttons |= mapButtonL2->buttonMask;
    }

    // ---- "Reverse 23626 LP/LK" super: shared 21346 opening + a tail chosen at the end - clcy ----
    // Press -> play the shared opening 21346 (2,1,3,4,6). From the press, while it runs, watch for any
    // attack (k or p) and a held direction (4/6). When the opening's "6" finishes, pick the tail (this
    // reuses the already-played 21346, no restart):
    //   any attack  -> +246 = 21346246, ender = the attack(s) you pressed;  hold 4 -> +26 = 2134626;  hold 6 -> +24 = 2134624
    //   (the 26/24 supers end in the button's LK/LP);  no attack + no direction -> stop after 21346.
    // Every step is 1 frame, the final attack step 2-3 frames (random). Held attacks suppressed during the motion.
    {
        Mask_t superGpio = gamepad->debouncedGpio;
        bool superLPpressed = mapSuperLP->pinMask && (superGpio & mapSuperLP->pinMask);
        bool superLKpressed = mapSuperLK->pinMask && (superGpio & mapSuperLK->pinMask);
        bool superLPNewPressed = mapSuperLPNew->pinMask && (superGpio & mapSuperLPNew->pinMask);
        bool superLKNewPressed = mapSuperLKNew->pinMask && (superGpio & mapSuperLKNew->pinMask);
        uint64_t nowUs = getMicro();

        if (!superActive && !motionActive && !dirActive && !chargeActive) {
            bool risingLP = superLPpressed && !prevSuperLP;
            bool risingLK = superLKpressed && !prevSuperLK;
            bool risingLPNew = superLPNewPressed && !prevSuperLPNew;
            bool risingLKNew = superLKNewPressed && !prevSuperLKNew;
            if (risingLP || risingLK || risingLPNew || risingLKNew) {
                bool isLP  = risingLP || risingLPNew;    // LP-family (else LK-family)
                bool isNew = risingLPNew || risingLKNew; // the (NEW) flip variants
                superActive = true;
                superStep = 0;
                superStepStartTime = nowUs;
                superStepDurationUs = SUPER_FRAME_US;       // opening steps are 1 frame
                superTailType = 0;                          // 0 = playing the 21346 opening / tail undecided
                superEnderDefault = isLP ? mapButtonB1->buttonMask : mapButtonB3->buttonMask;   // LP/LK for the direction tails
                superDivertEnder  = isNew ? (isLP ? mapButtonB3->buttonMask : mapButtonB1->buttonMask) : 0;  // (NEW): attack-divert flips (LP->LK, LK->LP); 0 = use the pressed attack
                superEnderMask = rawButtons & superAttackMask;                       // watch attacks (from press)
                superDirLatch  = rawDpad & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT);  // watch direction (from press)
            }
        }

        if (superActive) {
            // From the press through the opening, keep latching any attack and the held direction.
            if (superTailType == 0) {
                superEnderMask |= (rawButtons & superAttackMask);
                uint16_t d = rawDpad & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT);
                if (d) superDirLatch = d;
            }

            if ((nowUs - superStepStartTime) >= superStepDurationUs) {
                superStep++;
                superStepStartTime = nowUs;
                if (superStep == SUPER_OPEN_COUNT) {
                    // 21346 finished -> choose the tail (reuses the opening, no restart):
                    if (superEnderMask) {                               // ANY attack -> divert
                        superTailType = 1;                              // +246 = 21346246
                        if (superDivertEnder) superEnderMask = superDivertEnder;   // (NEW) variants: flip to LP/LK; else keep the pressed attack(s) - clcy
                    } else if (superDirLatch & GAMEPAD_MASK_LEFT) {     // held 4 -> +26 = 2134626
                        superTailType = 2; superEnderMask = superEnderDefault;
                    } else if (superDirLatch & GAMEPAD_MASK_RIGHT) {    // held 6 -> +24 = 2134624
                        superTailType = 3; superEnderMask = superEnderDefault;
                    } else {
                        superActive = false;                            // no attack, no direction -> stop after 21346
                    }
                }
                if (superActive) {
                    uint16_t d; bool isLast;
                    if (!superSeq(superStep, superTailType, d, isLast)) superActive = false;   // past the end
                    else superStepDurationUs = isLast ? randStepUs(superRng, 2, 3) : SUPER_FRAME_US;  // attack step 2-3f random - clcy
                }
            }
            if (superActive) {
                uint16_t d; bool isLast;
                if (superSeq(superStep, superTailType, d, isLast)) {
                    gamepad->state.dpad = d;                            // exclusive (fixed sequence, no mirror)
                    gamepad->state.buttons &= ~superAttackMask;        // suppress held attacks during the motion
                    if (isLast) gamepad->state.buttons |= superEnderMask;   // ender on the last tail step
                }
            }
        }

        prevSuperLP = superLPpressed;
        prevSuperLK = superLKpressed;
        prevSuperLPNew = superLPNewPressed;
        prevSuperLKNew = superLKNewPressed;
    }

    // ---- Hardcoded-motion buttons (236/214 QCR, 6214/4236 HCB, 623*, 21346*, 22*, 2HP) - clcy ----
    // Fixed sequences, play to completion regardless of the stick (no mirror, no direction gate).
    // Per-step length is random (per-motion frame ranges in MOTION_DEFS). Held attacks are suppressed
    // mid-motion. The LAST-step ender reads the attack you're HOLDING AT THE LAST STEP (fresh sample, so a
    // leftover released by then never leaks): defaultEnder==0 (236/214/6214/4236/22) -> that held attack IS
    // the ender (none -> no attack); 21346 LK/HK -> a held kick overrides; 21346 LP -> LP + held punch, but a
    // held kick replaces it (kick only); 22 LP -> a held attack replaces LP; everything else -> the fixed defaultEnder.
    {
        Mask_t mGpio = gamepad->debouncedGpio;
        uint64_t nowUs = getMicro();

        if (!motionActive && !superActive && !dirActive && !chargeActive) {
            for (int i = 0; i < MOTION_COUNT; i++) {
                bool pressed = motionPinMask[i] && (mGpio & motionPinMask[i]);
                if (pressed && !motionPrev[i]) {
                    motionActive = true;
                    motionWhich = i;
                    motionStep = 0;
                    motionStepStartTime = nowUs;
                    motionStepDurationUs = randStepUs(superRng, MOTION_DEFS[i].steps[0].minF, MOTION_DEFS[i].steps[0].maxF);
                    break;   // one motion at a time
                }
            }
        }

        if (motionActive) {
            const MotionDef& md = MOTION_DEFS[motionWhich];
            if ((nowUs - motionStepStartTime) >= motionStepDurationUs) {
                motionStep++;
                motionStepStartTime = nowUs;
                if (motionStep < md.count) {
                    motionStepDurationUs = randStepUs(superRng, md.steps[motionStep].minF, md.steps[motionStep].maxF);
                } else {
                    motionActive = false;
                }
            }
            if (motionActive) {
                gamepad->state.dpad = md.steps[motionStep].dpad;      // exclusive (fixed, no mirror); 5 = neutral = 0
                gamepad->state.buttons &= ~superAttackMask;           // suppress held attacks during the motion
                if (motionStep == (md.count - 1)) {
                    // All "smart" enders read the attack you're HOLDING AT THIS LAST STEP only (fresh
                    // sample, like the QCRs) — a leftover/early press released by now never leaks. - clcy
                    uint16_t pressedNow = rawButtons & superAttackMask;
                    uint16_t ender = md.defaultEnder;
                    if (ender == 0) {
                        ender = pressedNow;                             // no-default (236/214/6214/4236/22): that held attack IS the ender
                    } else if (md.action == GpioAction::BUTTON_PRESS_21346_LK ||
                               md.action == GpioAction::BUTTON_PRESS_21346_HK) {
                        uint16_t kicks = pressedNow & (A_LK | A_MK | A_HK);
                        if (kicks) ender = kicks;                       // 21346 LK/HK: a held kick overrides the default
                    } else if (md.action == GpioAction::BUTTON_PRESS_21346_LP) {
                        uint16_t kicks = pressedNow & (A_LK | A_MK | A_HK);
                        ender = kicks ? kicks : (md.defaultEnder | pressedNow);  // 21346 LP: a held KICK replaces LP (kick only); else LP + held PUNCH (add)
                    } else if (md.action == GpioAction::BUTTON_PRESS_22_LP ||
                               md.action == GpioAction::BUTTON_PRESS_6214_LP ||
                               md.action == GpioAction::BUTTON_PRESS_4236_LP ||
                               md.action == GpioAction::BUTTON_PRESS_BISON_5236_LK ||
                               md.action == GpioAction::BUTTON_PRESS_BISON_5214_LK) {
                        if (pressedNow) ender = pressedNow;             // 22/6214/4236 LP + Bison 5236/5214 LK: held attack(s) REPLACE the default; else default
                    }
                    gamepad->state.buttons |= ender;
                }
            }
        }

        for (int i = 0; i < MOTION_COUNT; i++) {
            motionPrev[i] = motionPinMask[i] && (mGpio & motionPinMask[i]);
        }
    }

    // ---- Directional one-button moves (46 LP/HK, Air Throw, JMP) - clcy ----
    // At press: mirror the held horizontal (hold ←/↙/↖ -> forward 6, hold →/↘/↗ -> forward 4). 46 LP/HK
    // need a held ←/→ (gate); the jumps don't (no direction -> straight up). Then play the move's fixed
    // 1-2 steps (each a random 2-3 frames), firing the attack (+ any pressed same-category attack,
    // sampled at press) on its step. See DIR_MOVE_DEFS. Held attacks are suppressed so they don't leak.
    {
        Mask_t dGpio = gamepad->debouncedGpio;
        uint64_t nowUs = getMicro();
        bool chgL = holdStartLeft  && (nowUs - holdStartLeft)  >= CHARGE_US;   // ← charged >= CHARGE_FRAMES
        bool chgR = holdStartRight && (nowUs - holdStartRight) >= CHARGE_US;   // → charged >= CHARGE_FRAMES
        bool chgD = holdStartDown  && (nowUs - holdStartDown)  >= CHARGE_US;   // ↓ charged >= CHARGE_FRAMES

        if (!dirActive && !motionActive && !superActive && !chargeActive) {
            for (int i = 0; i < DIR_MOVE_COUNT; i++) {
                bool pressed = dirPinMask[i] && (dGpio & dirPinMask[i]);
                if (pressed && !dirPrev[i]) {
                    const DirMoveDef& dm = DIR_MOVE_DEFS[i];
                    uint16_t fwd = 0;                                  // mirror: held back -> forward
                    if (rawDpad & GAMEPAD_MASK_LEFT)       fwd = GAMEPAD_MASK_RIGHT;
                    else if (rawDpad & GAMEPAD_MASK_RIGHT) fwd = GAMEPAD_MASK_LEFT;
                    if (dm.gate == GATE_HORIZ && fwd == 0) continue;                          // need a held ←/→
                    if (dm.gate == GATE_DOWN  && !(rawDpad & GAMEPAD_MASK_DOWN)) continue;     // need a held ↓
                    if (dm.gate == GATE_HORIZ_CHG && (fwd == 0 || !((rawDpad & GAMEPAD_MASK_LEFT) ? chgL : chgR))) continue;  // 46*: held ←/→ charged >= 45f
                    if (dm.gate == GATE_DOWN_CHG  && (!(rawDpad & GAMEPAD_MASK_DOWN) || !chgD)) continue;                     // 28*: held ↓ charged >= 45f
                    dirActive = true;
                    dirWhich = i;
                    dirStep = 0;
                    dirStepStartTime = nowUs;
                    dirStepDurationUs = randStepUs(superRng, dm.steps[dirStep].minF ? dm.steps[dirStep].minF : dm.minF, dm.steps[dirStep].maxF ? dm.steps[dirStep].maxF : dm.maxF);
                    dirForward = fwd;
                    dirHeld = rawDpad & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT);   // raw, no mirror (Anti Air 4MK)
                    dirAddedAttack = dm.addCat ? (rawButtons & dm.addCat) : 0;
                    if (dm.gate == GATE_HORIZ_CHG) {                            // consume the charge -> must re-charge before the next one
                        if (rawDpad & GAMEPAD_MASK_LEFT) holdStartLeft = nowUs; else holdStartRight = nowUs;
                    } else if (dm.gate == GATE_DOWN_CHG) {
                        holdStartDown = nowUs;
                    }
                    break;   // one move at a time
                }
            }
        }

        if (dirActive) {
            const DirMoveDef& dm = DIR_MOVE_DEFS[dirWhich];
            if (dm.addCat) dirAddedAttack |= (rawButtons & dm.addCat);   // live: add attacks pressed mid-move, ASAP - clcy
            if ((nowUs - dirStepStartTime) >= dirStepDurationUs) {
                dirStep++;
                dirStepStartTime = nowUs;
                if (dirStep < dm.count) {
                    dirStepDurationUs = randStepUs(superRng, dm.steps[dirStep].minF ? dm.steps[dirStep].minF : dm.minF, dm.steps[dirStep].maxF ? dm.steps[dirStep].maxF : dm.maxF);
                } else {
                    dirActive = false;
                }
            }
            if (dirActive) {
                const DirStep& s = dm.steps[dirStep];
                uint16_t outDpad = 0;
                if (s.flags & DSF_UP)      outDpad |= GAMEPAD_MASK_UP;
                if (s.flags & DSF_DOWN)    outDpad |= GAMEPAD_MASK_DOWN;
                if (s.flags & DSF_FWD)     outDpad |= dirForward;
                if (s.flags & DSF_FWD_RAW) outDpad |= dirHeld;
                gamepad->state.dpad = outDpad;                    // exclusive (mirror handled at press)
                gamepad->state.buttons &= ~superAttackMask;       // suppress held attacks during the move
                if (s.attack) gamepad->state.buttons |= (dirAddedAttack ? dirAddedAttack : s.attack);   // held attack(s) REPLACE the base; else base
            }
        }

        for (int i = 0; i < DIR_MOVE_COUNT; i++) {
            dirPrev[i] = dirPinMask[i] && (dGpio & dirPinMask[i]);
        }
    }

    // ---- 623 HP Charge: 13131 + HP, then HOLD HP while the button stays pressed - clcy ----
    // Press -> play 1,3,1,3,1 (each 1-2f random), HP on the last step. When the sequence ends, if the
    // button is STILL held, keep HP pressed (the stick passes through) until release. Tap-and-release
    // just gives the one-shot 13131+HP. Mutually exclusive with the other engines.
    {
        Mask_t cGpio = gamepad->debouncedGpio;
        bool chargePressed = mapChargeHP->pinMask && (cGpio & mapChargeHP->pinMask);
        uint64_t nowUs = getMicro();

        if (!chargeActive && !motionActive && !superActive && !dirActive) {
            if (chargePressed && !chargePrev) {
                chargeActive = true;
                chargeHold = false;
                chargeStep = 0;
                chargeStepStartTime = nowUs;
                chargeStepDurationUs = randStepUs(superRng, 1, 2);
            }
        }

        if (chargeActive) {
            if (!chargeHold) {                                  // playing 1,3,1,3,1
                if ((nowUs - chargeStepStartTime) >= chargeStepDurationUs) {
                    chargeStep++;
                    chargeStepStartTime = nowUs;
                    if (chargeStep < CHARGE_623HP_COUNT) {
                        chargeStepDurationUs = randStepUs(superRng, 1, 2);
                    } else {
                        chargeHold = chargePressed;             // still held at the end -> hold HP
                        chargeActive = chargePressed;           // else done (one-shot 13131+HP)
                    }
                }
                if (chargeActive && !chargeHold) {
                    gamepad->state.dpad = CHARGE_623HP_DIRS[chargeStep];   // exclusive during the motion
                    gamepad->state.buttons &= ~superAttackMask;           // suppress held attacks
                    if (chargeStep == CHARGE_623HP_COUNT - 1)
                        gamepad->state.buttons |= A_HP;                   // HP on the last step
                }
            }
            if (chargeActive && chargeHold) {                   // HOLD phase: keep HP while pressed
                if (chargePressed) gamepad->state.buttons |= A_HP;        // stick passes through
                else chargeActive = false;                                // released -> stop
            }
        }

        chargePrev = chargePressed;
    }

    if (pinLED != 0xff) {
        gpio_put(pinLED, !state);
    }
}
