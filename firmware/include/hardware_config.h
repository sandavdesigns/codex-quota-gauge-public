#pragma once

// Shared by demo and device builds. Override with -D build flags if required.
#ifndef GAUGE_TFT_SCK
#define GAUGE_TFT_SCK 4
#endif
#ifndef GAUGE_TFT_MOSI
#define GAUGE_TFT_MOSI 6
#endif
#ifndef GAUGE_TFT_CS
#define GAUGE_TFT_CS 7
#endif
#ifndef GAUGE_TFT_DC
#define GAUGE_TFT_DC 1
#endif
#ifndef GAUGE_TFT_RST
#define GAUGE_TFT_RST 0
#endif
#ifndef GAUGE_TFT_ROTATION
#define GAUGE_TFT_ROTATION 0
#endif
#ifndef GAUGE_SPI_HZ
#define GAUGE_SPI_HZ 20000000UL
#endif
#ifndef GAUGE_SETUP_BUTTON
#define GAUGE_SETUP_BUTTON 9
#endif

// German local time, with automatic European summer/winter time.
#ifndef GAUGE_TIMEZONE
#define GAUGE_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#endif
