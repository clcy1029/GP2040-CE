#include "addons/input_macro.h"
#include "storagemanager.h"
#include "GamepadState.h"

#include "hardware/gpio.h"

bool InputMacro::available() {
    // Macro Button initialized by void Gamepad::setup()
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
            case GpioAction::BUTTON_PRESS_MACRO_1:
            case GpioAction::BUTTON_PRESS_MACRO_2:
            case GpioAction::BUTTON_PRESS_MACRO_3:
            case GpioAction::BUTTON_PRESS_MACRO_4:
            case GpioAction::BUTTON_PRESS_MACRO_5:
            case GpioAction::BUTTON_PRESS_MACRO_6:
            case GpioAction::BUTTON_PRESS_MACRO_7:
            case GpioAction::BUTTON_PRESS_MACRO_8:
            case GpioAction::BUTTON_PRESS_MACRO_9:
            case GpioAction::BUTTON_PRESS_MACRO_10:
                return true;
            default:
                break;
        }
    }
    return false;
}

void InputMacro::setup() {
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    macroButtonMask = 0;
    memset(macroPinMasks, 0, sizeof(macroPinMasks));
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
                macroButtonMask = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_1:
                macroPinMasks[0] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_2:
                macroPinMasks[1] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_3:
                macroPinMasks[2] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_4:
                macroPinMasks[3] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_5:
                macroPinMasks[4] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroPinMasks[5] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_7:
                macroPinMasks[6] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_8:
                macroPinMasks[7] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_9:
                macroPinMasks[8] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_10:
                macroPinMasks[9] = 1 << pin;
                break;
            default:
                break;
        }
    }

    inputMacroOptions = &Storage::getInstance().getAddonOptions().macroOptions;
    if (inputMacroOptions->macroBoardLedEnabled && isValidPin(BOARD_LED_PIN)) {
        gpio_init(BOARD_LED_PIN);
        gpio_set_dir(BOARD_LED_PIN, GPIO_OUT);
        boardLedEnabled = true;
    } else {
        boardLedEnabled = false;
    }
    boardLedEnabled = false;
    prevMacroInputPressed = false;
    reset();
}


void InputMacro::reset() {
    macroPosition = -1; // this is cur index of which macro (0-9) button is pressed, usally = pressedMacro
    pressedMacro = -1; // this is the index of which macro (0-9) button is pressed
    isMacroRunning = false;
    macroStartTime = 0;
    macroInputPosition = 0; // this the index for a single macro serial 
    isMacroTriggerHeld = false;
    macroInputHoldTime = INPUT_HOLD_US;
    deferredButtons = 0;
    hasDeferredButtons = false;
    if (boardLedEnabled) {
        gpio_put(BOARD_LED_PIN, 0);
    }

    newPressB1 = false;
    newPressB2 = false;
    newPressB3 = false;
    newPressB4 = false;
    newPressR1 = false;
    newPressL1 = false;

    B1PressedLast = false;
    B2PressedLast = false;
    B3PressedLast = false;
    B4PressedLast = false;
    R1PressedLast = false;
    L1PressedLast = false;

}

void InputMacro::restart(Macro& macro) {
    macroStartTime = currentMicros;
    macroInputPosition = 0; // this the index for a single macro serial 
    MacroInput& newMacroInput = macro.macroInputs[macroInputPosition];
    uint32_t newMacroInputDuration = newMacroInput.duration + newMacroInput.waitDuration;
    macroInputHoldTime = newMacroInputDuration <= 0 ? INPUT_HOLD_US : newMacroInputDuration;
}

void InputMacro::checkMacroPress() {
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    Mask_t allPins = gamepad->debouncedGpio;

    // Go through our macro list
    pressedMacro = -1;
    for(int i = 0; i < MAX_MACRO_LIMIT; i++) {
        if ( inputMacroOptions->macroList[i].enabled == false ) // Skip disabled macros
            continue;
        Macro * macro = &inputMacroOptions->macroList[i];
        if ( macro->useMacroTriggerButton ) {
            // Use Gamepad Button for Macro Trigger
            if ((allPins & macroButtonMask) &&
                ((gamepad->state.buttons & macro->macroTriggerButton) ||
                    (gamepad->state.dpad & (macro->macroTriggerButton >> 16))) ) {
                pressedMacro = i;
                break;
            }
        } else if ( allPins & macroPinMasks[i] ) {
            // Use Pin Manager for Macro Trigger
            pressedMacro = i;
            break;
        }
    }
}

void InputMacro::checkMacroAction() {
    bool macroInputPressed = (pressedMacro != -1); // Was any macro input pressed?

    // Is our pressed macro button different from our current macro AND no macro is running?
    if ( pressedMacro != macroPosition && !isMacroRunning ) {
        macroPosition = pressedMacro; // move our position to that macro
    }

    // check if macro button is newly pressed
    bool newPress = macroInputPressed && (prevMacroInputPressed ^ macroInputPressed);

    // Check to see if we should change the current macro (or turn off based on input)
    if ( inputMacroOptions->macroList[macroPosition].macroType == ON_PRESS ) {
        // START Macro: On Press or On Hold Repeat
        if (!isMacroRunning ) {
            isMacroTriggerHeld = newPress;
        }
    } else if ( inputMacroOptions->macroList[macroPosition].macroType == ON_HOLD_REPEAT ) {
        isMacroTriggerHeld = macroInputPressed;
    } else if ( inputMacroOptions->macroList[macroPosition].macroType == ON_TOGGLE ) {
        //isMacroTriggerHeld = macroInputPressed;
        if (!isMacroRunning ) {
            isMacroTriggerHeld = newPress;
        } else if (isMacroRunning && newPress) {
            // STOP Macro: Toggle on new press
            reset(); // Stop Macro: Toggle
            prevMacroInputPressed = macroInputPressed;
            return;
        }
    }

    prevMacroInputPressed = macroInputPressed;
    if (!isMacroRunning && isMacroTriggerHeld) {
        // New Macro to run
        macroPosition = pressedMacro; // Set current macro
        Macro& macro = inputMacroOptions->macroList[macroPosition];
        MacroInput& macroInput = macro.macroInputs[macroInputPosition];
        uint32_t macroInputDuration = macroInput.duration + macroInput.waitDuration;
        macroInputHoldTime = macroInputDuration <= 0 ? INPUT_HOLD_US : macroInputDuration;
        isMacroRunning = true;
        macroStartTime = getMicro(); // current time
    }
}

void InputMacro::runCurrentMacro() {

    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    
    //if B1 is pressed
    bool B1PressedNow = (gamepad->mapButtonB1->pinMask && (gamepad->debouncedGpio & gamepad->mapButtonB1->pinMask));
    // if B1 is newly pressed
    newPressB1 = B1PressedNow && !B1PressedLast;
    // 更新上一 tick 状态
    B1PressedLast = B1PressedNow;

    //if B2 is pressed
    bool B2PressedNow = (gamepad->mapButtonB2->pinMask && (gamepad->debouncedGpio & gamepad->mapButtonB2->pinMask));
    // if B2 is newly pressed
    newPressB2 = B2PressedNow && !B2PressedLast;
    // 更新上一 tick 状态
    B2PressedLast = B2PressedNow;

    //if B1 is pressed
    bool B3PressedNow = (gamepad->mapButtonB3->pinMask && (gamepad->debouncedGpio & gamepad->mapButtonB3->pinMask));
    // if B3 is newly pressed
    newPressB3 = B3PressedNow && !B3PressedLast;
    // 更新上一 tick 状态
    B3PressedLast = B3PressedNow;

    //if B4 is pressed
    bool B4PressedNow = (gamepad->mapButtonB4->pinMask && (gamepad->debouncedGpio & gamepad->mapButtonB4->pinMask));
    // if B4 is newly pressed
    newPressB4 = B4PressedNow && !B4PressedLast;
    // 更新上一 tick 状态
    B4PressedLast = B4PressedNow;

    //if R1 is pressed
    bool R1PressedNow = (gamepad->mapButtonR1->pinMask && (gamepad->debouncedGpio & gamepad->mapButtonR1->pinMask));
    // if R1 is newly pressed
    newPressR1 = R1PressedNow && !R1PressedLast;
    // 更新上一 tick 状态
    R1PressedLast = R1PressedNow;

    //if L1 is pressed
    bool L1PressedNow = (gamepad->mapButtonL1->pinMask && (gamepad->debouncedGpio & gamepad->mapButtonL1->pinMask));
    // if L1 is newly pressed
    newPressL1 = L1PressedNow && !L1PressedLast;
    // 更新上一 tick 状态
    L1PressedLast = L1PressedNow;
    

    // Do nothing if macro is not currently running
    if (!isMacroRunning ||
            macroPosition == -1)
        return;

    Macro& macro = inputMacroOptions->macroList[macroPosition];

    // Stop Macro if released (ON PRESS & ON HOLD REPEAT)
    if (inputMacroOptions->macroList[macroPosition].macroType == ON_HOLD_REPEAT &&
            !isMacroTriggerHeld ) {
        reset();
        return;
    }

    MacroInput& macroInput = macro.macroInputs[macroInputPosition];
    
    currentMicros = getMicro();

    // ---------- NEW EXCLUSIVE / INTERRUPTIBLE SEMANTICS ----------


    

    if (true) {

        if (macro.useMacroTriggerButton) {
            gamepad->state.dpad &= ~(macro.macroTriggerButton >> 16);
            gamepad->state.buttons &= ~macro.macroTriggerButton;
        }
        
        // exclusive = true, interruptible = false
        if (!macro.interruptible) {
            // fully ignore user input
            gamepad->state.dpad = 0;
            gamepad->state.buttons = 0;
        }

        // exclusive = true, interruptible = true
        else {
            // ignore dpad
            gamepad->state.dpad = 0;
            
            if (newPressB1){
                deferredButtons |= GAMEPAD_MASK_B1;
                hasDeferredButtons = true;
            }
            if (newPressB2){
                deferredButtons |= GAMEPAD_MASK_B2;
                hasDeferredButtons = true;
            }
            if (newPressB3){
                deferredButtons |= GAMEPAD_MASK_B3;
                hasDeferredButtons = true;
            }
            if (newPressB4){
                deferredButtons |= GAMEPAD_MASK_B4;
                hasDeferredButtons = true;
            }
            if (newPressR1){
                deferredButtons |= GAMEPAD_MASK_R1;
                hasDeferredButtons = true;
            }
            if (newPressL1){
                deferredButtons |= GAMEPAD_MASK_L1;
                hasDeferredButtons = true;
            }

            // not let buttons effective
            gamepad->state.buttons = 0;
        }
    }
    // exclusive = false, keep normal behavior of interruptible
    else {
        if (macro.useMacroTriggerButton) {
            gamepad->state.dpad &= ~(macro.macroTriggerButton >> 16);
            gamepad->state.buttons &= ~macro.macroTriggerButton;
        }
        if (macro.interruptible &&
            (gamepad->state.buttons != 0 || gamepad->state.dpad != 0)) {
            reset();
            return;
        }
    }


    // Have we elapsed the input hold time?
    if ((currentMicros - macroStartTime) >= macroInputHoldTime) {
        macroStartTime = currentMicros;
        macroInputPosition++;
        
        if (macroInputPosition >= (macro.macroInputs_count)) {
            if ( macro.macroType == ON_PRESS ) {
                reset(); // On press = no more macro
            } else {
                restart(macro); // On Hold-Repeat or On Toggle = start macro again
            }
        } 
        else {
            MacroInput& newMacroInput = macro.macroInputs[macroInputPosition];
            uint32_t newMacroInputDuration = newMacroInput.duration + newMacroInput.waitDuration;
            macroInputHoldTime = newMacroInputDuration <= 0 ? INPUT_HOLD_US : newMacroInputDuration;
        }
    }

    // Check if we should still hold this macro input based on duration
    if ((currentMicros - macroStartTime) <= macroInput.duration) {
        uint32_t buttonMask = macroInput.buttonMask;
        if (buttonMask & GAMEPAD_MASK_DU) {
            gamepad->state.dpad |= GAMEPAD_MASK_UP;
        }
        if (buttonMask & GAMEPAD_MASK_DD) {
            gamepad->state.dpad |= GAMEPAD_MASK_DOWN;
        }
        if (buttonMask & GAMEPAD_MASK_DL) {
            gamepad->state.dpad |= GAMEPAD_MASK_LEFT;
        }
        if (buttonMask & GAMEPAD_MASK_DR) {
            gamepad->state.dpad |= GAMEPAD_MASK_RIGHT;
        }
        gamepad->state.buttons |= buttonMask;
        
        // do a special checking if current chip clock is with the last macro
        if (macroInputPosition == (macro.macroInputs_count - 1 )) {
            // if last macro input, and ON PRESS, add deferred buttons on it
            if ( macro.macroType == ON_PRESS ) {
                //if deferred buttons exist, apply them now 
                if (hasDeferredButtons) {
                    gamepad->state.buttons |= deferredButtons;
                }
            }
        }

        // Macro LED is on if we're currently running and inputs are doing something (wait-timers turn it off)
        if (boardLedEnabled) {
            gpio_put(BOARD_LED_PIN, (gamepad->state.dpad || gamepad->state.buttons) ? 1 : 0);
        }
    }
}

void InputMacro::preprocess()
{
    FocusModeOptions * focusModeOptions = &Storage::getInstance().getAddonOptions().focusModeOptions;
    if (focusModeOptions->enabled && focusModeOptions->macroLockEnabled) {
        Gamepad * gamepad = Storage::getInstance().GetGamepad();
        // Override Toggle Pressed OR focus mode pin is set
        if (focusModeOptions->overrideEnabled ||
            (gamepad->mapFocusMode->pinMask && (gamepad->debouncedGpio & gamepad->mapFocusMode->pinMask))) {
            return;
        }
    }

    checkMacroPress();
    checkMacroAction();
    runCurrentMacro();
}

void InputMacro::reinit() {
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    macroButtonMask = 0;
    memset(macroPinMasks, 0, sizeof(macroPinMasks));
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
                macroButtonMask = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_1:
                macroPinMasks[0] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_2:
                macroPinMasks[1] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_3:
                macroPinMasks[2] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_4:
                macroPinMasks[3] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_5:
                macroPinMasks[4] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroPinMasks[5] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_7:
                macroPinMasks[6] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_8:
                macroPinMasks[7] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_9:
                macroPinMasks[8] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_10:
                macroPinMasks[9] = 1 << pin;
                break;
            default:
                break;
        }
    }
}
