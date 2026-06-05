#include "addons/reverse.h"
#include "storagemanager.h"
#include "GamepadEnums.h"
#include "GamepadState.h"
#include "gamepad.h"
#include "helper.h"
#include "config.pb.h"

// ---- One-button super motion (timed, auto-mirror by held direction) - clcy ----
// Canonical motion is authored for "character on the LEFT side" (forward = right): 2 3 6 2 6.
// When the player is instead holding right, LEFT/RIGHT are swapped to produce the mirror 2 1 4 2 4.
// Timing: one fighting-game frame = 1/60s ~= 16666 us. The input loop runs FAR faster than 60Hz,
// so each step is held by the microsecond wall-clock (getMicro()), not by loop ticks.
#define SUPER_FRAME_US        16666
#define SUPER_STEP_FRAMES_MIN 1
#define SUPER_STEP_FRAMES_MAX 2

struct SuperStep { uint16_t dpad; bool fireButton; };
static const SuperStep SUPER_STEPS[] = {
    { GAMEPAD_MASK_DOWN,                      false }, // 2  down
    { GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT, false }, // 3  down-forward
    { GAMEPAD_MASK_RIGHT,                     false }, // 6  forward
    { GAMEPAD_MASK_DOWN,                      false }, // 2  down
    { GAMEPAD_MASK_RIGHT,                     true  }, // 6  forward + button
};
static const int SUPER_STEP_COUNT = (int)(sizeof(SUPER_STEPS) / sizeof(SUPER_STEPS[0]));

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

// ---- General hardcoded-motion table (236/214/623*/21346*/22*/28*/2HP) - clcy ----
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

struct MotionStep { uint16_t dpad; uint8_t minF; uint8_t maxF; };
struct MotionDef  { GpioAction action; const MotionStep* steps; uint8_t count; uint16_t defaultEnder; };

static const MotionStep ST_236[]   = { {D_2,1,2}, {D_3,1,2}, {D_6,2,3} };
static const MotionStep ST_214[]   = { {D_2,1,2}, {D_1,1,2}, {D_4,2,3} };
static const MotionStep ST_623[]   = { {D_1,1,2}, {D_3,1,2}, {D_1,1,2}, {D_3,1,2}, {D_1,2,3} };
static const MotionStep ST_623HP[] = { {D_1,1,1}, {D_3,1,1}, {D_1,1,1}, {D_3,1,1}, {D_1,1,3} }; // steps 1f, last 1-3f
static const MotionStep ST_21346[] = { {D_2,1,2}, {D_1,1,2}, {D_3,1,2}, {D_4,1,2}, {D_6,2,3} };
static const MotionStep ST_21346246[] = { {D_2,1,1},{D_1,1,1},{D_3,1,1},{D_4,1,1},{D_6,1,1},{D_2,1,1},{D_4,1,1},{D_6,1,1} }; // all 1 frame
static const int SUPER_DIVERT_COUNT = (int)(sizeof(ST_21346246) / sizeof(ST_21346246[0])); // super kick-divert plays ST_21346246
static const MotionStep ST_22[]    = { {D_N,1,2}, {D_2,1,2}, {D_N,1,2}, {D_2,2,3} };
static const MotionStep ST_28[]    = { {D_2,1,1}, {D_8,2,3} };   // 2 is 1 frame, then 8 + attack (2-3f)
static const MotionStep ST_2HP[]   = { {D_2,4,4} };              // single step, 4 frames

static const MotionDef MOTION_DEFS[] = {
    { GpioAction::BUTTON_PRESS_QCR_236,    ST_236,   3, 0 },
    { GpioAction::BUTTON_PRESS_QCR_214,    ST_214,   3, 0 },
    { GpioAction::BUTTON_PRESS_623_LP,     ST_623,   5, A_LP },
    { GpioAction::BUTTON_PRESS_623_HP,     ST_623HP, 5, A_HP },
    { GpioAction::BUTTON_PRESS_623_LPMP,   ST_623,   5, A_LP | A_MP },
    { GpioAction::BUTTON_PRESS_623_LK,     ST_623,   5, A_LK },
    { GpioAction::BUTTON_PRESS_623_HK,     ST_623,   5, A_HK },
    { GpioAction::BUTTON_PRESS_623_LKMK,   ST_623,   5, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_21346_LK,   ST_21346, 5, A_LK },
    { GpioAction::BUTTON_PRESS_21346_HK,   ST_21346, 5, A_HK },
    { GpioAction::BUTTON_PRESS_21346_LKMK, ST_21346, 5, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_21346246_LK, ST_21346246, 8, A_LK },
    { GpioAction::BUTTON_PRESS_21346246_LP, ST_21346246, 8, A_LP },
    { GpioAction::BUTTON_PRESS_22_LKMK,    ST_22,    4, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_22,         ST_22,    4, 0 },
    { GpioAction::BUTTON_PRESS_28_HK,      ST_28,    2, A_HK },
    { GpioAction::BUTTON_PRESS_28_LK,      ST_28,    2, A_LK },
    { GpioAction::BUTTON_PRESS_28_LKMK,    ST_28,    2, A_LK | A_MK },
    { GpioAction::BUTTON_PRESS_2_HP,       ST_2HP,   1, A_HP },
};
static const int MOTION_COUNT = (int)(sizeof(MOTION_DEFS) / sizeof(MOTION_DEFS[0]));
static_assert(MOTION_COUNT <= REVERSE_MOTION_MAX, "increase REVERSE_MOTION_MAX");

bool ReverseInput::available() {
    const ReverseOptions& options = Storage::getInstance().getAddonOptions().reverseOptions;
	return options.enabled;
}

void ReverseInput::setup()
{
    // Setup Reverse Input Button
    mapInputReverse = new GamepadButtonMapping(0);
    mapReverseExtra1 = new GamepadButtonMapping(0);
    mapReverseExtra2 = new GamepadButtonMapping(0);
    mapReverseExtra3 = new GamepadButtonMapping(0);
    mapReverseExtra4 = new GamepadButtonMapping(0);
    mapReverseExtra5 = new GamepadButtonMapping(0);
    mapSuperLP = new GamepadButtonMapping(0);
    mapSuperLK = new GamepadButtonMapping(0);

    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch (pinMappings[pin].action) {
            case GpioAction::BUTTON_PRESS_INPUT_REVERSE: mapInputReverse->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_REVERSE_EXTRA_1: mapReverseExtra1->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_REVERSE_EXTRA_2: mapReverseExtra2->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_REVERSE_EXTRA_3: mapReverseExtra3->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_REVERSE_EXTRA_4: mapReverseExtra4->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_REVERSE_EXTRA_5: mapReverseExtra5->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_SUPER_LP: mapSuperLP->pinMask |= 1 << pin; break;
            case GpioAction::BUTTON_PRESS_SUPER_LK: mapSuperLK->pinMask |= 1 << pin; break;
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
    stateReverseExtra1 = false; // if reverse extra1 button is pressed
    stateReverseExtra2 = false; // if reverse extra2 button is pressed
    stateReverseExtra3 = false; // if reverse extra3 button is pressed
    stateReverseExtra4 = false; // if reverse extra4 button is pressed
    stateReverseExtra5 = false; // if reverse extra5 button is pressed
    stateReverseActive = false; // if any of above buttons is pressed

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
    superDirPending = false;
    superStepDurationUs = 0;
    superRng = 0;
    superDivert = false;

    // General motion-button state + pin masks (scan every motion action) - clcy
    motionActive = false;
    motionWhich = 0;
    motionStep = 0;
    motionStepStartTime = 0;
    motionStepDurationUs = 0;
    motionEnderMask = 0;
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
}

void ReverseInput::update() {
    Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;

    state = (values & mapInputReverse->pinMask);
    stateReverseExtra1 = (values & mapReverseExtra1->pinMask);
    stateReverseExtra2 = (values & mapReverseExtra2->pinMask);
    stateReverseExtra3 = (values & mapReverseExtra3->pinMask);
    stateReverseExtra4 = (values & mapReverseExtra4->pinMask);
    stateReverseExtra5 = (values & mapReverseExtra5->pinMask);

    // unified reverse state
    stateReverseActive = state || stateReverseExtra1 || stateReverseExtra2 || stateReverseExtra3 || stateReverseExtra4 || stateReverseExtra5;
}
void ReverseInput::reinit() {
    delete mapInputReverse;
    delete mapReverseExtra1;
    delete mapReverseExtra2;
    delete mapReverseExtra3;
    delete mapReverseExtra4;
    delete mapReverseExtra5;
    delete mapSuperLP;
    delete mapSuperLK;
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

    gamepad->state.dpad = 0
        | input(gamepad->state.dpad & mapDpadUp->buttonMask,    mapDpadUp->buttonMask,      mapDpadDown->buttonMask,    actionUp,       invertYAxis)
        | input(gamepad->state.dpad & mapDpadDown->buttonMask,  mapDpadDown->buttonMask,    mapDpadUp->buttonMask,      actionDown,     invertYAxis)
        | input(gamepad->state.dpad & mapDpadLeft->buttonMask,  mapDpadLeft->buttonMask,    mapDpadRight->buttonMask,   actionLeft,     invertXAxis)
        | input(gamepad->state.dpad & mapDpadRight->buttonMask, mapDpadRight->buttonMask,   mapDpadLeft->buttonMask,    actionRight,    invertXAxis)
    ;


    if (state){
        // Reverse Input Button for Reverse + L2 (sf6 drive reversal)
        gamepad->state.buttons |= mapButtonL2->buttonMask;
    }
    else if (stateReverseExtra1){
        // Extra Button 1 for B1 button, for 46 lp 
        gamepad->state.buttons |= mapButtonB1->buttonMask;
    }
    else if (stateReverseExtra2){
        // Extra Button 2 for B1 R1 button , for 46 lp hp
        gamepad->state.buttons |= mapButtonR1->buttonMask;
        gamepad->state.buttons |= mapButtonB1->buttonMask;
    }
    else if (stateReverseExtra3){
        // Extra Button 3 for up + B2 (MP / 中拳), for jump MP - clcy
        gamepad->state.buttons |= mapButtonB2->buttonMask;
        gamepad->state.dpad |= mapDpadUp->buttonMask;
    }
    else if (stateReverseExtra4){
        // Extra Button 4 for B3 button , for 3 or 1 hp 
        bool hasHorizontal =
        (rawDpad & mapDpadLeft->buttonMask) ||
        (rawDpad & mapDpadRight->buttonMask);

        if (hasHorizontal) {
            gamepad->state.buttons |= mapButtonR1->buttonMask;
            gamepad->state.dpad    |= mapDpadDown->buttonMask;
        }
    }
    else if (stateReverseExtra5){
        // Extra Button 5 for B1 B3 button , for air throw
        gamepad->state.buttons |= mapButtonB1->buttonMask;
        gamepad->state.buttons |= mapButtonB3->buttonMask;
        gamepad->state.dpad |= mapDpadUp->buttonMask;
    }

    // ---- "Reverse 23626 LP/LK" super (timed, auto-mirror, cross-category divert) - clcy ----
    // Press -> opening "2" plays. During that buffer we sample direction (4/6) AND attacks. An attack
    // of the OPPOSITE category to the button diverts (HIGHEST priority, any direction incl. neutral):
    //   Reverse-LP + a KICK  -> 21346246 LK ;   Reverse-LK + a PUNCH -> 21346246 LP
    //   (the remaining 1 3 4 6 2 4 6, 1 frame each, last 2 frames, no mirror).
    // Otherwise normal super: direction 4 -> 2 3 6 2 6 + ender; 6 -> mirror 2 1 4 2 4; no direction -> cancel.
    // Held attacks are suppressed during the motion so they don't leak as a stray normal.
    {
        Mask_t superGpio = gamepad->debouncedGpio;
        bool superLPpressed = mapSuperLP->pinMask && (superGpio & mapSuperLP->pinMask);
        bool superLKpressed = mapSuperLK->pinMask && (superGpio & mapSuperLK->pinMask);
        uint64_t nowUs = getMicro();

        if (!superActive && !motionActive) {
            bool risingLP = superLPpressed && !prevSuperLP;
            bool risingLK = superLKpressed && !prevSuperLK;
            if (risingLP || risingLK) {
                superActive = true;
                superDivert = false;
                superStep = 0;
                superStepStartTime = nowUs;
                superStepDurationUs = superRandStepUs(superRng);
                superEnderDefault = risingLP ? mapButtonB1->buttonMask : mapButtonB3->buttonMask;
                superEnderMask = rawButtons & superAttackMask;   // attack already held at press
                bool heldLeft  = rawDpad & GAMEPAD_MASK_LEFT;
                bool heldRight = rawDpad & GAMEPAD_MASK_RIGHT;
                if (heldLeft || heldRight) {
                    superMirror = heldRight;            // direction known at press -> commit now
                    superDirPending = false;
                } else {
                    superMirror = false;                // tentative; step 0 ("down") is side-agnostic
                    superDirPending = true;             // decide when the first step ends (late buffer)
                }
            }
        }

        if (superActive) {
            if ((nowUs - superStepStartTime) >= superStepDurationUs) {
                superStep++;
                superStepStartTime = nowUs;

                if (superStep == 1) {
                    // Resolve at the end of the opening "2". A pressed attack of the OPPOSITE category
                    // to the button diverts into 21346246 (HIGHEST priority, any direction incl. neutral):
                    //   Reverse-LP + a KICK  -> 21346246 LK ;   Reverse-LK + a PUNCH -> 21346246 LP.
                    superEnderMask |= (rawButtons & superAttackMask);
                    bool isLPsuper = (superEnderDefault == A_LP);   // LP super (B1) vs LK super (B3)
                    uint16_t pressedPunch = superEnderMask & (A_LP | A_MP | A_HP);
                    uint16_t pressedKick  = superEnderMask & (A_LK | A_MK | A_HK);
                    if (isLPsuper && pressedKick) {
                        superDivert = true; superEnderMask = A_LK;   // LP super + kick -> 21346246 LK
                    } else if (!isLPsuper && pressedPunch) {
                        superDivert = true; superEnderMask = A_LP;   // LK super + punch -> 21346246 LP
                    }
                    if (superDivert) {
                        superMirror = false;            // 21346246 is fixed, no mirror
                        superDirPending = false;
                    } else {
                        // normal 23626 super: finalize the ender, then pick the side from the stick
                        if (superEnderMask == 0) superEnderMask = superEnderDefault;
                        if (superDirPending) {
                            bool heldLeft  = rawDpad & GAMEPAD_MASK_LEFT;
                            bool heldRight = rawDpad & GAMEPAD_MASK_RIGHT;
                            if (heldLeft || heldRight) {
                                superMirror = heldRight;
                                superDirPending = false;
                            } else {
                                superActive = false;    // no divert, no direction -> cancel
                            }
                        }
                    }
                }

                // next step's duration + completion depend on which sequence we're playing
                if (superDivert) {
                    superStepDurationUs = (superStep >= SUPER_DIVERT_COUNT - 1) ? (2 * SUPER_FRAME_US) : SUPER_FRAME_US;
                    if (superStep >= SUPER_DIVERT_COUNT) superActive = false;
                } else {
                    superStepDurationUs = superRandStepUs(superRng);
                    if (superStep >= SUPER_STEP_COUNT) superActive = false;
                }
            }
            if (superActive) {
                if (superDivert) {
                    // 21346246 + kick: fixed motion (no mirror); the kick fires on the last step
                    gamepad->state.dpad = ST_21346246[superStep].dpad;
                    gamepad->state.buttons &= ~superAttackMask;
                    if (superStep == (SUPER_DIVERT_COUNT - 1)) {
                        gamepad->state.buttons |= superEnderMask;
                    }
                } else {
                    const SuperStep& st = SUPER_STEPS[superStep];
                    // collect any attack pressed during the opening "2" so the ender can be e.g. HP
                    if (superStep == 0) {
                        superEnderMask |= (rawButtons & superAttackMask);
                    }
                    uint16_t outDpad = 0;
                    if (st.dpad & GAMEPAD_MASK_DOWN) outDpad |= GAMEPAD_MASK_DOWN;
                    bool stepLeft  = st.dpad & GAMEPAD_MASK_LEFT;
                    bool stepRight = st.dpad & GAMEPAD_MASK_RIGHT;
                    if (superMirror) { bool tmp = stepLeft; stepLeft = stepRight; stepRight = tmp; }
                    if (stepLeft)  outDpad |= GAMEPAD_MASK_LEFT;
                    if (stepRight) outDpad |= GAMEPAD_MASK_RIGHT;
                    gamepad->state.dpad = outDpad;          // exclusive: the motion owns the stick
                    gamepad->state.buttons &= ~superAttackMask;   // suppress held attacks during the motion
                    if (st.fireButton) {
                        gamepad->state.buttons |= (superEnderMask ? superEnderMask : superEnderDefault);
                    }
                }
            }
        }

        prevSuperLP = superLPpressed;
        prevSuperLK = superLKpressed;
    }

    // ---- Hardcoded-motion buttons (236/214/623*/21346*/22*/28*/2HP) - clcy ----
    // Fixed sequences, play to completion regardless of the stick (no mirror, no direction gate).
    // Per-step length is a random 1-2 frames, with per-motion overrides in MOTION_DEFS (e.g. 623 HP
    // steps = 1 frame + last 1-3; 2 HP = 4 frames). Any attack held at trigger OR pressed during the
    // sequence (B1/B2/R1/B3/B4/L1) OVERRIDES the per-motion default ender and fires on the LAST step;
    // held attacks are suppressed mid-motion so they don't leak early.
    {
        Mask_t mGpio = gamepad->debouncedGpio;
        uint64_t nowUs = getMicro();

        if (!motionActive && !superActive) {
            for (int i = 0; i < MOTION_COUNT; i++) {
                bool pressed = motionPinMask[i] && (mGpio & motionPinMask[i]);
                if (pressed && !motionPrev[i]) {
                    motionActive = true;
                    motionWhich = i;
                    motionStep = 0;
                    motionStepStartTime = nowUs;
                    motionStepDurationUs = randStepUs(superRng, MOTION_DEFS[i].steps[0].minF, MOTION_DEFS[i].steps[0].maxF);
                    motionEnderMask = rawButtons & superAttackMask;   // attack already held at trigger
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
                motionEnderMask |= (rawButtons & superAttackMask);    // capture attacks pressed mid-sequence
                gamepad->state.dpad = md.steps[motionStep].dpad;      // exclusive (fixed, no mirror); 5 = neutral = 0
                gamepad->state.buttons &= ~superAttackMask;           // suppress held attacks during the motion
                if (motionStep == (md.count - 1)) {
                    uint16_t ender = md.defaultEnder;
                    if (ender == 0) {
                        ender = motionEnderMask;                        // no-default (236/214/22): any attack
                    } else if (md.action == GpioAction::BUTTON_PRESS_21346_LK ||
                               md.action == GpioAction::BUTTON_PRESS_21346_HK) {
                        uint16_t kicks = motionEnderMask & (A_LK | A_MK | A_HK);
                        if (kicks) ender = kicks;                       // 21346 LK/HK: a pressed KICK overrides the default
                    }
                    gamepad->state.buttons |= ender;                  // see ender rule above
                }
            }
        }

        for (int i = 0; i < MOTION_COUNT; i++) {
            motionPrev[i] = motionPinMask[i] && (mGpio & motionPinMask[i]);
        }
    }

    if (pinLED != 0xff) {
        gpio_put(pinLED, !state);
    }
}
