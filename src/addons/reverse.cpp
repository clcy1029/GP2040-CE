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
#define SUPER_FRAME_US    16666
#define SUPER_STEP_FRAMES 2
#define SUPER_STEP_US     (SUPER_FRAME_US * SUPER_STEP_FRAMES)

struct SuperStep { uint16_t dpad; bool fireButton; };
static const SuperStep SUPER_STEPS[] = {
    { GAMEPAD_MASK_DOWN,                      false }, // 2  down
    { GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT, false }, // 3  down-forward
    { GAMEPAD_MASK_RIGHT,                     false }, // 6  forward
    { GAMEPAD_MASK_DOWN,                      false }, // 2  down
    { GAMEPAD_MASK_RIGHT,                     true  }, // 6  forward + button
};
static const int SUPER_STEP_COUNT = (int)(sizeof(SUPER_STEPS) / sizeof(SUPER_STEPS[0]));

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
    superButtonMask = 0;
    prevSuperLP = false;
    prevSuperLK = false;
    superDirPending = false;
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
        // Extra Button 3 for up b4 button , for J mk
        gamepad->state.buttons |= mapButtonB4->buttonMask;
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

    // ---- One-button super motion (timed sequence, auto-mirror by held direction) - clcy ----
    // Hold 4/back (left side) + Super button -> 2 3 6 2 6 + button (motion goes right).
    // Hold 6/back (right side) + Super button -> mirror 2 1 4 2 4 + button (motion goes left).
    // Late buffer: if pressed with NO horizontal held, the first "2" still plays; the side is
    // then sampled when that step ends (so you can press the button a hair before the stick).
    {
        Mask_t superGpio = gamepad->debouncedGpio;
        bool superLPpressed = mapSuperLP->pinMask && (superGpio & mapSuperLP->pinMask);
        bool superLKpressed = mapSuperLK->pinMask && (superGpio & mapSuperLK->pinMask);
        uint64_t nowUs = getMicro();

        if (!superActive) {
            bool risingLP = superLPpressed && !prevSuperLP;
            bool risingLK = superLKpressed && !prevSuperLK;
            if (risingLP || risingLK) {
                superActive = true;
                superStep = 0;
                superStepStartTime = nowUs;
                superButtonMask = risingLP ? mapButtonB1->buttonMask : mapButtonB3->buttonMask;
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
            if ((nowUs - superStepStartTime) >= SUPER_STEP_US) {
                superStep++;
                superStepStartTime = nowUs;

                // Late buffer resolves at the end of the first "2": sample the stick now to
                // pick forward (正摇) vs mirror (反摇). Still nothing held -> cancel the rest.
                if (superDirPending && superStep == 1) {
                    bool heldLeft  = rawDpad & GAMEPAD_MASK_LEFT;
                    bool heldRight = rawDpad & GAMEPAD_MASK_RIGHT;
                    if (heldLeft || heldRight) {
                        superMirror = heldRight;
                        superDirPending = false;
                    } else {
                        superActive = false;            // no direction even after the buffer
                    }
                }

                if (superStep >= SUPER_STEP_COUNT) {
                    superActive = false;
                }
            }
            if (superActive) {
                const SuperStep& st = SUPER_STEPS[superStep];
                uint16_t outDpad = 0;
                if (st.dpad & GAMEPAD_MASK_DOWN) outDpad |= GAMEPAD_MASK_DOWN;
                bool stepLeft  = st.dpad & GAMEPAD_MASK_LEFT;
                bool stepRight = st.dpad & GAMEPAD_MASK_RIGHT;
                if (superMirror) { bool tmp = stepLeft; stepLeft = stepRight; stepRight = tmp; }
                if (stepLeft)  outDpad |= GAMEPAD_MASK_LEFT;
                if (stepRight) outDpad |= GAMEPAD_MASK_RIGHT;
                gamepad->state.dpad = outDpad;          // exclusive: the motion owns the stick
                if (st.fireButton) {
                    gamepad->state.buttons |= superButtonMask;
                }
            }
        }

        prevSuperLP = superLPpressed;
        prevSuperLK = superLKpressed;
    }

    if (pinLED != 0xff) {
        gpio_put(pinLED, !state);
    }
}
