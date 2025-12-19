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

    //add two extra buttons
    GamepadButtonMapping *mapReverseExtra1;
    GamepadButtonMapping *mapReverseExtra2;
    
    bool invertXAxis;
    bool invertYAxis;

    // 0 - Ignore, 1 - Enabled, 2 - Neutral
    uint8_t actionUp;
    uint8_t actionDown;
    uint8_t actionLeft;
    uint8_t actionRight;
};

#endif // _Reverse_H_