#ifndef _Reverse_H
#define _Reverse_H

#include "gpaddon.h"

#include "GamepadEnums.h"

#ifndef REVERSE_ENABLED
#define REVERSE_ENABLED 1
#endif

#ifndef PIN_REVERSE
#define PIN_REVERSE     -1
#endif

#ifndef REVERSE_LED_PIN
#define REVERSE_LED_PIN -1
#endif

#ifndef REVERSE_UP_DEFUALT
#define REVERSE_UP_DEFAULT 0
#endif

#ifndef REVERSE_DOWN_DEFAULT
#define REVERSE_DOWN_DEFAULT 2
#endif

#ifndef REVERSE_LEFT_DEFAULT
#define REVERSE_LEFT_DEFAULT 1
#endif

#ifndef REVERSE_RIGHT_DEFUALT
#define REVERSE_RIGHT_DEFAULT 1
#endif

// Reverse Module Name
#define ReverseName "Input Reverse"

// Max number of table-driven hardcoded-motion buttons (see MOTION_DEFS in reverse.cpp) - clcy
#define REVERSE_MOTION_MAX 48

// Max number of table-driven directional one-button moves (see DIR_MOVE_DEFS in reverse.cpp) - clcy
#define REVERSE_DIRMOVE_MAX 16

class ReverseInput : public GPAddon {
public:
    virtual bool available();
    virtual void setup();       // Reverse Button Setup
    virtual void preprocess() {}
    virtual void process();     // Reverse process
    virtual void postprocess(bool sent) {}
    virtual void reinit();
    virtual std::string name() { return ReverseName; }
private:
    void update();
    uint8_t input(uint32_t valueMask, uint16_t buttonMask, uint16_t buttonMaskReverse, uint8_t action, bool invertAxis);

    bool state;

    bool stateReverseActive;

    uint8_t pinLED;

    GamepadButtonMapping *mapDpadUp;
    GamepadButtonMapping *mapDpadDown;
    GamepadButtonMapping *mapDpadLeft;
    GamepadButtonMapping *mapDpadRight;
    GamepadButtonMapping *mapInputReverse;

    //usable buttons
    GamepadButtonMapping *mapButtonB1;
    GamepadButtonMapping *mapButtonB2;
    GamepadButtonMapping *mapButtonL2;
    GamepadButtonMapping *mapButtonB3;
    GamepadButtonMapping *mapButtonB4;
    GamepadButtonMapping *mapButtonR1;
    GamepadButtonMapping *mapButtonL1;

    bool invertXAxis;
    bool invertYAxis;

    // 0 - Ignore, 1 - Enabled, 2 - Neutral
    uint8_t actionUp;
    uint8_t actionDown;
    uint8_t actionLeft;
    uint8_t actionRight;

    // One-button super motion (timed sequence, auto-mirror by held direction) - clcy
    GamepadButtonMapping *mapSuperLP;   // 23626 (or mirror 21424) + LP (B1)
    GamepadButtonMapping *mapSuperLK;   // 23626 (or mirror 21424) + LK (B3)
    GamepadButtonMapping *mapSuperLPNew;   // (NEW) Reverse 23626 LP: on the attack-divert, ender flips to LK - clcy
    GamepadButtonMapping *mapSuperLKNew;   // (NEW) Reverse 23626 LK: on the attack-divert, ender flips to LP - clcy
    GamepadButtonMapping *mapSuper23626LP;  // (NEW) 23626 LP: like mapSuperLPNew but tail follows direction (no reverse) - clcy
    GamepadButtonMapping *mapSuper23626LK;  // (NEW) 23626 LK: like mapSuperLKNew but tail follows direction (no reverse) - clcy
    bool superActive;
    int  superStep;
    uint64_t superStepStartTime;
    bool superMirror;          // true when holding right -> mirror rightward motion to leftward
    uint16_t superAttackMask;     // OR of the 6 attack buttons (LP/MP/HP/LK/MK/HK)
    uint16_t superEnderDefault;   // ender if no attack held (B1 for Super-LP, B3 for Super-LK)
    uint16_t superEnderMask;      // attack(s) held during the buffer -> overrides the default ender
    bool prevSuperLP;
    bool prevSuperLK;
    bool prevSuperLPNew;
    bool prevSuperLKNew;
    bool prevSuper23626LP;
    bool prevSuper23626LK;
    bool superNoReverse;       // (NEW) 23626: tail follows the held direction (hold 4->24, hold 6->26) instead of reversing it - clcy
    bool superDirPending;      // late buffer: pressed before a direction -> side decided when step 0 ends
    uint64_t superStepDurationUs;  // current step's hold time (randomized 1-2 frames)
    uint32_t superRng;             // xorshift PRNG state for the per-step length randomness
    bool superDivert;              // (legacy — unused by the current super)
    int  superTailType;            // super tail: 0=opening, 1=246 (divert), 2=26 (held 4), 3=24 (held 6)
    uint16_t superDirLatch;        // super: ←/→ latched during the 21346 opening
    uint16_t superDivertEnder;     // (NEW) super variants: attack-divert ender (flipped LP/LK); 0 = use the pressed attack - clcy

    // General hardcoded-motion buttons (236/214/623*/21346*/22*/28*/2HP): fixed sequences, no
    // mirror/gate, play to completion; a pressed attack overrides the per-motion default ender,
    // fired on the last step. Table-driven (see MOTION_DEFS in reverse.cpp). - clcy
    uint32_t motionPinMask[REVERSE_MOTION_MAX];
    bool     motionPrev[REVERSE_MOTION_MAX];
    bool     motionActive;
    int      motionWhich;          // index into MOTION_DEFS
    int      motionStep;
    uint64_t motionStepStartTime;
    uint64_t motionStepDurationUs;

    // Directional one-button moves (46 LP/HK, Air Throw, JMP): at press, sample the held horizontal
    // and MIRROR it (held back -> output forward); 46 LP/HK gate on a held ←/→, the jumps don't.
    // Play 1-2 fixed steps (jump then attack for the air moves), each a random 2-3 frames; the attack
    // fires on its step, plus any pressed same-category attack (46 LP: punches, 46 HK: kicks).
    // Table-driven (see DIR_MOVE_DEFS in reverse.cpp). - clcy
    uint32_t dirPinMask[REVERSE_DIRMOVE_MAX];
    bool     dirPrev[REVERSE_DIRMOVE_MAX];
    bool     dirActive;
    int      dirWhich;             // index into DIR_MOVE_DEFS
    int      dirStep;
    uint64_t dirStepStartTime;
    uint64_t dirStepDurationUs;
    uint16_t dirForward;           // mirrored forward horizontal sampled at press (0 = none held)
    uint16_t dirHeld;              // raw held horizontal (no mirror, sampled at press) — for Anti Air 4MK
    uint16_t dirAddedAttack;       // pressed same-category attack(s) sampled at press

    // 623 HP Charge: play 13131+HP, then HOLD HP while the button stays pressed (release to stop). - clcy
    GamepadButtonMapping *mapChargeHP;
    bool     chargeActive;
    bool     chargeHold;           // true once 13131 is done and we're holding HP until release
    int      chargeStep;
    uint64_t chargeStepStartTime;
    uint64_t chargeStepDurationUs;
    bool     chargePrev;

    // (Luke) 5 HP event sequence: HP(3f) -> neutral wait(21f, pass user input through) -> neutral MK+MP(3f);
    // any attack pressed during the wait cancels the MK+MP tail. Own block in process(). - clcy
    GamepadButtonMapping *mapLuke5HP;
    bool     luke5Active;
    uint8_t  luke5Phase;           // 0=HP, 1=wait(pass-through), 2=MK+MP tail
    uint64_t luke5PhaseStart;
    bool     luke5CancelTail;      // an attack was pressed during the wait -> skip the MK+MP tail
    bool     prevLuke5;

    // Continuous-hold timestamps for 4/6/2 (wall-clock; 0 = released). The 46*/28* charge gates check
    // these against CHARGE_FRAMES (real ~45-frame charge). - clcy
    uint64_t holdStartLeft;
    uint64_t holdStartRight;
    uint64_t holdStartDown;
};

#endif // _Reverse_H_