#include "addons/reverse.h"
#include "storagemanager.h"
#include "GamepadEnums.h"
#include "helper.h"
#include "config.pb.h"

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
        // Extra Button 2 for B1 B2 button , for 46 lp mp
        gamepad->state.buttons |= mapButtonB2->buttonMask;
        gamepad->state.buttons |= mapButtonB1->buttonMask;
    }
    else if (stateReverseExtra3){
        // Extra Button 3 for R1 button , for 46 hp
        gamepad->state.buttons |= mapButtonR1->buttonMask;
    }
    else if (stateReverseExtra4){
        // Extra Button 4 for B3 button , for 28 lk 
        gamepad->state.buttons |= mapButtonB3->buttonMask;
        gamepad->state.dpad |= mapDpadUp->buttonMask;
    }
    else if (stateReverseExtra5){
        // Extra Button 5 for B1 B3 button , for air throw
        gamepad->state.buttons |= mapButtonB1->buttonMask;
        gamepad->state.buttons |= mapButtonB3->buttonMask;
        gamepad->state.dpad |= mapDpadUp->buttonMask;
    }

    if (pinLED != 0xff) {
        gpio_put(pinLED, !state);
    }
}
