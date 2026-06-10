// reTerminal E1001 pin map.
// Sources: Seeed Arduino cookbook (EPD SPI) and ESPHome advanced cookbook
// (buttons, LED, buzzer, battery ADC, I2C).
#pragma once

// 7.5" 800x480 UC8179 ePaper on HSPI
#define EPD_SCK_PIN 7
#define EPD_MOSI_PIN 9
#define EPD_CS_PIN 10
#define EPD_DC_PIN 11
#define EPD_RES_PIN 12
#define EPD_BUSY_PIN 13

// I2C bus: SHT4x (0x44), PCF8563 RTC (0x51), SY6974B charger (0x6A)
#define I2C_SDA_PIN 19
#define I2C_SCL_PIN 20

// Buttons, active low with internal pullup. All are RTC-capable GPIOs (< 22)
// so they can be ext1 deep-sleep wake sources.
#define BTN_GREEN_PIN 3   // top "refresh" button -> hold for maintenance mode
#define BTN_RIGHT_PIN 4
#define BTN_LEFT_PIN 5

#define LED_GREEN_PIN 6   // active low
#define BUZZER_PIN 45     // passive piezo, LEDC PWM

// Battery voltage through a 1:2 divider
#define BAT_ADC_PIN 1
#define BAT_DIVIDER 2.0f
