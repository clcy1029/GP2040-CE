#include "addons/focus_mode.h"
#include "storagemanager.h"
#include "hardware/gpio.h"

bool FocusModeAddon::available() {
    const FocusModeOptions& options = Storage::getInstance().getAddonOptions().focusModeOptions;
    return options.enabled;
}

void FocusModeAddon::setup() {
    macroStep = 0;
    branchLeft = false;
    stepStartTime = 0;
    macroRunning = false;
    deferredButtons = 0;
    hasDeferredButtons = false;
    focusPressedLast = false;
}

void FocusModeAddon::preprocess() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    const FocusModeOptions& options = Storage::getInstance().getAddonOptions().focusModeOptions;

    uint64_t now = getMicro();

    // ===== 已在跑宏：推进 step =====
    if (macroRunning) {
        if (now - stepStartTime >= INPUT_HOLD_US * 2) {  // 每 step 2 帧
            macroStep++;
            stepStartTime = now;

            if (macroStep >= 3) {
                // 宏结束，重置
                macroRunning = false;
                macroStep = 0;
                branchLeft = false;
                stepStartTime = 0;
                deferredButtons = 0;
                hasDeferredButtons = false;
            }
        }
        return;
    }

    // ===== 未跑宏：检测 trigger pin =====
    bool focusPressedNow = (gamepad->mapFocusMode->pinMask &&
                            (gamepad->debouncedGpio & gamepad->mapFocusMode->pinMask));

    bool newPress = focusPressedNow && !focusPressedLast;
    focusPressedLast = focusPressedNow;  // 更新状态

    if (!newPress) return;  // 只在按下瞬间触发

    // ===== 判断左右分支（只要包含即可）=====
    uint32_t dpad = gamepad->state.dpad;
    if (dpad & GAMEPAD_MASK_LEFT) {
        branchLeft = true;
    } else if (dpad & GAMEPAD_MASK_RIGHT) {
        branchLeft = false;
    } else {
        // 没有左右输入，不触发宏
        return;
    }

    // ===== 启动宏 =====
    macroRunning = true;
    macroStep = 0;
    stepStartTime = now;

    // ===== 暂存用户按钮 =====
    if (gamepad->state.buttons != 0) {
        deferredButtons |= gamepad->state.buttons;
        hasDeferredButtons = true;
    }
}

void FocusModeAddon::process() {
    if (!macroRunning)
        return;

    Gamepad* gamepad = Storage::getInstance().GetGamepad();

    // 清空 dpad，仅由宏控制
    gamepad->state.dpad = 0;

    switch (macroStep) {
        case 0:
            // Down
            gamepad->state.dpad = GAMEPAD_MASK_DOWN;
            break;

        case 1:
            // Down + Left / Right
            gamepad->state.dpad = GAMEPAD_MASK_DOWN |
                (branchLeft ? GAMEPAD_MASK_LEFT : GAMEPAD_MASK_RIGHT);
            break;

        case 2:
            // Left / Right
            gamepad->state.dpad =
                branchLeft ? GAMEPAD_MASK_LEFT : GAMEPAD_MASK_RIGHT;

            // ===== 附加 deferred buttons =====
            if (hasDeferredButtons) {
                gamepad->state.buttons |= deferredButtons;
            }
            break;

        default:
            break;
    }
}
