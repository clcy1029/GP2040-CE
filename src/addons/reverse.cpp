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

    R1PressedLast = false;
    newPressR1 = false;
    R1Hold = false;
    R1NewPressAge = 0;
}

void ReverseInput::preprocess(){

    Gamepad * gamepad = Storage::getInstance().GetGamepad();

    //if R1 is pressed
    bool R1PressedNow = (gamepad->mapButtonR1->pinMask && (gamepad->debouncedGpio & gamepad->mapButtonR1->pinMask));
    
    //if Extra1 is pressed
    bool Extra1PressedNow   = (mapReverseExtra1->pinMask && (gamepad->debouncedGpio & mapReverseExtra1->pinMask));
    
    // if R1 is newly pressed
    newPressR1 = R1PressedNow && !R1PressedLast;

    // 更新上一 tick 状态
    R1PressedLast = R1PressedNow;

    // 更新新按 R1 的 age
    if (newPressR1) {
        R1NewPressAge = 0;  // 刚新按下，重置计数
    } else if (R1NewPressAge <= 33332) {
        R1NewPressAge++;    // 每 tick 自增
    } else {
        R1NewPressAge = 0;  // 超过 16666 tick，重置
    }

    //只有在 Extra1 被按下时才更新 R1Hold 状态
    if (Extra1PressedNow){
        if (newPressR1){
            // R1 is newly pressed
            newPressR1 = true;
            R1PressedLast = R1PressedNow; //true
            R1Hold = true;
        }
        else if ((!newPressR1) && R1PressedNow){ 
            // R1 is being held but not newly pressed
            newPressR1 = false;
            R1PressedLast = R1PressedNow; //true

            if (R1NewPressAge < 33332) {
                R1Hold = true;
            }
        }
        else {
            // R1 is not holding now
            R1PressedLast = R1PressedNow; //false
            newPressR1 = false;
            R1Hold = false;
        }
    }
    else {
        // Extra1 is not pressed, reset R1Hold
        R1Hold = false;
    }
};

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

        if (R1Hold) {
             gamepad->state.buttons |= mapButtonR1->buttonMask;
        }
        gamepad->state.buttons |= mapButtonB1->buttonMask;

        // uint16_t otherButtons =
        // gamepad->state.buttons & ~mapButtonB1->buttonMask;

        // if (otherButtons == 0) {
        //     gamepad->state.buttons |= mapButtonB1->buttonMask;
        // }

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

    if (pinLED != 0xff) {
        gpio_put(pinLED, !state);
    }
}
