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
#define REVERSE_MOTION_MAX 32

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

    //extra two buttons' state
    bool stateReverseExtra1;
    bool stateReverseExtra2;
    bool stateReverseExtra3;
    bool stateReverseExtra4;
    bool stateReverseExtra5;

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

    //add two extra buttons
    GamepadButtonMapping *mapReverseExtra1;
    GamepadButtonMapping *mapReverseExtra2;
    GamepadButtonMapping *mapReverseExtra3;
    GamepadButtonMapping *mapReverseExtra4;
    GamepadButtonMapping *mapReverseExtra5;
    
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
    bool superActive;
    int  superStep;
    uint64_t superStepStartTime;
    bool superMirror;          // true when holding right -> mirror rightward motion to leftward
    uint16_t superAttackMask;     // OR of the 6 attack buttons (LP/MP/HP/LK/MK/HK)
    uint16_t superEnderDefault;   // ender if no attack held (B1 for Super-LP, B3 for Super-LK)
    uint16_t superEnderMask;      // attack(s) held during the buffer -> overrides the default ender
    bool prevSuperLP;
    bool prevSuperLK;
    bool superDirPending;      // late buffer: pressed before a direction -> side decided when step 0 ends
    uint64_t superStepDurationUs;  // current step's hold time (randomized 1-2 frames)
    uint32_t superRng;             // xorshift PRNG state for the per-step length randomness
    bool superDivert;              // a kick pressed during the opening -> morph the 23626 super into 21346246+kick

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
    uint16_t motionEnderMask;      // attacks captured during the sequence
};

#endif // _Reverse_H_