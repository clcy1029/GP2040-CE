# GP2040 Configuration for Raspberry Pi Pico

Basic pin setup for a stock Raspberry Pi Pico. Combine with a simple GPIO breakout/screw terminal board for an easy DIY arcade stick.

## Build

Env Setup: https://gp2040-ce.info/development/build-environment?_highlight=build#building

Set Environment Variables
```
cd /Users/noraba/Explore/GP2040-CE
export GP2040_BOARDCONFIG=PicoClcy
```

Build
```
mkdir build 
cd build
cmake ..
make
```

Delete builds
```
cd /Users/noraba/Explore/GP2040-CE
rm -rf ./build
```

## Main Pin Mapping Configuration

| RP2040 Pin | Action                        | 
|------------|-------------------------------|
#define GPIO_PIN_02 | GpioAction::BUTTON_PRESS_UP     
#define GPIO_PIN_03 | GpioAction::BUTTON_PRESS_DOWN   
#define GPIO_PIN_04 | GpioAction::BUTTON_PRESS_RIGHT  
#define GPIO_PIN_05 | GpioAction::BUTTON_PRESS_LEFT   
#define GPIO_PIN_06 | GpioAction::BUTTON_PRESS_B4     
#define GPIO_PIN_07 | GpioAction::BUTTON_PRESS_L1     
#define GPIO_PIN_10 | GpioAction::BUTTON_PRESS_B2     
#define GPIO_PIN_11 | GpioAction::BUTTON_PRESS_R1     
#define GPIO_PIN_12 | GpioAction::BUTTON_PRESS_R2
#define GPIO_PIN_15 | GpioAction::BUTTON_PRESS_L2     
#define GPIO_PIN_16 | GpioAction::BUTTON_PRESS_S1     
#define GPIO_PIN_17 | GpioAction::BUTTON_PRESS_S2     
#define GPIO_PIN_19 | GpioAction::BUTTON_PRESS_B3     
#define GPIO_PIN_20 | GpioAction::BUTTON_PRESS_L3     
#define GPIO_PIN_21 | GpioAction::BUTTON_PRESS_R3     
#define GPIO_PIN_28 | GpioAction::BUTTON_PRESS_B1     