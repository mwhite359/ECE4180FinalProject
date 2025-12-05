#pragma once
#include <LovyanGFX.hpp>

// Custom display class for ESP32-C6 + ST7796
class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Panel_ST7796     _panel;
    lgfx::Bus_SPI          _bus;
    lgfx::Light_PWM        _light;


    LGFX(void) {
        // SPI BUS SETUP
        auto cfg = _bus.config();
        cfg.spi_host = SPI2_HOST;  // ESP32-C6 uses SPI2 (HSPI)
        cfg.spi_mode = 0;
        cfg.freq_write = 40000000;   // 40 MHz
        cfg.freq_read  = 16000000;
        cfg.spi_3wire  = false;      // MISO present
        cfg.use_lock   = true;

        //SPI Pins
        cfg.pin_sclk = 21;
        cfg.pin_mosi = 19;
        cfg.pin_miso = 20;

        cfg.pin_dc   = 4;

        _bus.config(cfg);
        _panel.setBus(&_bus);

        // PANEL SETUP
        auto pcfg = _panel.config();

        // uLCD pins
        pcfg.pin_cs  = 18;
        pcfg.pin_rst = 5;
        pcfg.pin_busy = -1;

        pcfg.memory_width  = 320;
        pcfg.memory_height = 480;
        pcfg.panel_width   = 320;
        pcfg.panel_height  = 480;

        pcfg.offset_x = 0;
        pcfg.offset_y = 0;

        pcfg.rgb_order = false;   
        pcfg.invert    = false;

        pcfg.readable  = true;
        pcfg.dlen_16bit = false;
        pcfg.bus_shared = true;

        _panel.config(pcfg);

        // used for backlight pin, optional
        auto lcfg = _light.config();
        lcfg.pin_bl = -1;   // currently not used
        _light.config(lcfg);
        _panel.setLight(&_light);

        setPanel(&_panel);
    }
};
