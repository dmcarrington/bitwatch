/*
 * Copyright (c) 2020 David Carrington
 * 
 * Decoy watch application
 * 
 * Based on agoodWatch by Alex Goodyear
 * 
 * Press power button 3x on startup to enter wallet, otherwise standard watch will be displayed.
 *
 * Original header comment below ...
 * Copyright (c) 2020 Alex Goodyear
 * 
 * agoodWatch will always work with my fork of the LilyGo TTGO_TWatch_Library at
 * https://github.com/AlexGoodyear/TTGO_TWatch_Library
 * 
 * Derived from the SimpleWatch example in https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library
*/

#include "config.h"
#include "gui.h"

#define G_EVENT_VBUS_PLUGIN         _BV(0)
#define G_EVENT_VBUS_REMOVE         _BV(1)
#define G_EVENT_CHARGE_DONE         _BV(2)

#define G_EVENT_WIFI_SCAN_START     _BV(3)
#define G_EVENT_WIFI_SCAN_DONE      _BV(4)
#define G_EVENT_WIFI_CONNECTED      _BV(5)
#define G_EVENT_WIFI_BEGIN          _BV(6)
#define G_EVENT_WIFI_OFF            _BV(7)

enum {
    Q_EVENT_WIFI_SCAN_DONE,
    Q_EVENT_WIFI_CONNECT,
    Q_EVENT_BMA_INT,
    Q_EVENT_AXP_INT,
} ;

#define WATCH_FLAG_SLEEP_MODE   _BV(1)
#define WATCH_FLAG_SLEEP_EXIT   _BV(2)
#define WATCH_FLAG_BMA_IRQ      _BV(3)
#define WATCH_FLAG_AXP_IRQ      _BV(4)

void loopWatch();
void startupWatch();
