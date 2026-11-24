// board/pinmap.h: розпіновка FocusDesk (ESP32-S3 DevKit)
// Джерело: docs/hardware/pinmap.md. Зміни лише через PR.
#pragma once

// Кільце WS2812 (RMT)
#define PIN_RING_DATA       10

// Енкодер KY-040
#define PIN_ENC_A           15   // CLK
#define PIN_ENC_B           16   // DT
#define PIN_ENC_BTN         17   // SW

// Зумер (LEDC PWM) і додаткова кнопка
#define PIN_BUZZER           4
#define PIN_BTN_AUX          5

// Екран ST7789 (SPI)
#define PIN_LCD_SCLK        12
#define PIN_LCD_MOSI        11   // SDA на модулі
#define PIN_LCD_RST          7
#define PIN_LCD_DC           8
#define PIN_LCD_BL           6
#define PIN_LCD_CS          (-1) // у модуля ST7789 (7 пінів) виводу CS немає

// Датчик LD2410C (UART)
#define LD2410_UART_NUM      1
#define PIN_LD2410_RX       18   // сюди приходить TX датчика
#define PIN_LD2410_TX       21   // звідси йде в RX датчика
#define LD2410_BAUD         256000

