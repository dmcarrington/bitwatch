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

#include "watch.h"
#include <WiFi.h>

extern QueueHandle_t g_event_queue_handle;
extern EventGroupHandle_t g_event_group;
extern EventGroupHandle_t isr_group;
extern TTGOClass *ttgo;

lv_icon_battery_t batState = LV_ICON_CALCULATION;
unsigned int screenTimeout = DEFAULT_SCREEN_TIMEOUT;

bool lenergy = false;



void setupNetwork()
{
    WiFi.mode(WIFI_STA);
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        xEventGroupClearBits(g_event_group, G_EVENT_WIFI_CONNECTED);
        setCpuFrequencyMhz(CPU_FREQ_NORM);
    }, WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        uint8_t data = Q_EVENT_WIFI_SCAN_DONE;
        xQueueSend(g_event_queue_handle, &data, portMAX_DELAY);
    }, WiFiEvent_t::SYSTEM_EVENT_SCAN_DONE);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        xEventGroupSetBits(g_event_group, G_EVENT_WIFI_CONNECTED);
    }, WiFiEvent_t::SYSTEM_EVENT_STA_CONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        wifi_connect_status(true);
    }, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);
}

void low_energy()
{
    if (ttgo->bl->isOn()) {
        log_i("low_energy() - BL is on");
        xEventGroupSetBits(isr_group, WATCH_FLAG_SLEEP_MODE);
        
        if (screenTimeout != DEFAULT_SCREEN_TIMEOUT)
        {
          torchOff();
        }

        ttgo->closeBL();
        ttgo->stopLvglTick();
        ttgo->bma->enableStepCountInterrupt(false);
        ttgo->displaySleep();

        if (!WiFi.isConnected()) {
            log_i("low_energy() - WiFi is off entering 10MHz mode");
            delay(250);
            lenergy = true;
            WiFi.mode(WIFI_OFF);
            setCpuFrequencyMhz (CPU_FREQ_MIN); 
            gpio_wakeup_enable ((gpio_num_t)AXP202_INT, GPIO_INTR_LOW_LEVEL);
            gpio_wakeup_enable ((gpio_num_t)BMA423_INT1, GPIO_INTR_HIGH_LEVEL);
            esp_sleep_enable_gpio_wakeup ();
            esp_light_sleep_start();
        }
    } else {
        log_i("low_energy() - BL is off");
        ttgo->startLvglTick();
        ttgo->displayWakeup();
        ttgo->rtc->syncToSystem();
        updateStepCounter(ttgo->bma->getCounter());
        updateBatteryLevel();
        updateBatteryIcon(batState);
        updateTime();
        lv_disp_trig_activity(NULL);
        ttgo->openBL();
        ttgo->bma->enableStepCountInterrupt(true);
        screenTimeout = DEFAULT_SCREEN_TIMEOUT;
    }
}

void startupWatch() {
    //Check if the RTC clock matches, if not, use compile time
    ttgo->rtc->check();

    //Synchronize time to system time
    ttgo->rtc->syncToSystem();

    //Setting up the network
    setupNetwork();
  

    //Execute your own GUI interface
    setupGui();

    //Clear lvgl counter
    lv_disp_trig_activity(NULL);

    //When the initialization is complete, turn on the backlight
    //ttgo->openBL();

    /*
     * Setup the axes for the TWatch-2020-V1 for the tilt detection.
     */
    struct bma423_axes_remap remap_data;

    remap_data.x_axis = 0;
    remap_data.x_axis_sign = 1;
    remap_data.y_axis = 1;
    remap_data.y_axis_sign = 0;
    remap_data.z_axis  = 2;
    remap_data.z_axis_sign  = 1;

    ttgo->bma->set_remap_axes(&remap_data);

    /*
     * Enable the double tap wakeup.
     */
    ttgo->bma->enableWakeupInterrupt (true);
  }


void loopWatch() {
   bool  rlst;
   uint8_t data;
   
  // An XEvent signifies that there has been a wakeup interrupt, bring the CPU out of low energy mode
  EventBits_t  bits = xEventGroupGetBits(isr_group);
  if (bits & WATCH_FLAG_SLEEP_EXIT) {
      if (lenergy) {
          lenergy = false;
          setCpuFrequencyMhz (CPU_FREQ_NORM);
      }

      low_energy();

      if (bits & WATCH_FLAG_BMA_IRQ) {
      log_i("WATCH_FLAG_BMA_IRQ");
          do {
              rlst =  ttgo->bma->readInterrupt();
          } while (!rlst);
          xEventGroupClearBits(isr_group, WATCH_FLAG_BMA_IRQ);
      }
      if (bits & WATCH_FLAG_AXP_IRQ) {
      log_i("WATCH_FLAG_AXP_IRQ");
          ttgo->power->readIRQ();
          ttgo->power->clearIRQ();
          xEventGroupClearBits(isr_group, WATCH_FLAG_AXP_IRQ);
      }
      xEventGroupClearBits(isr_group, WATCH_FLAG_SLEEP_EXIT);
      xEventGroupClearBits(isr_group, WATCH_FLAG_SLEEP_MODE);
  }
  if ((bits & WATCH_FLAG_SLEEP_MODE)) {
      //! No event processing after entering the information screen
      return;
  }

  //! Normal polling
  if (xQueueReceive(g_event_queue_handle, &data, 5 / portTICK_RATE_MS) == pdPASS) {
      switch (data) {
      case Q_EVENT_BMA_INT:
      log_i("Q_EVENT_BMA_IRQ");

          do {
              rlst =  ttgo->bma->readInterrupt();
          } while (!rlst);

          if (ttgo->bma->isDoubleClick()) {
          if (screenTimeout == DEFAULT_SCREEN_TIMEOUT)
          {
              screenTimeout--;
              ttgo->setBrightness(255);
          }
          else
          {
              screenTimeout = 5 * 60 * 1000;
              torchOn();
              setCpuFrequencyMhz (CPU_FREQ_MIN);
          }
          }
          
          //! setp counter
          if (ttgo->bma->isStepCounter()) {
              updateStepCounter(ttgo->bma->getCounter());
          }
          break;
      case Q_EVENT_AXP_INT:
      log_i("Q_EVENT_AXP_INT");

          ttgo->power->readIRQ();
          if (ttgo->power->isVbusPlugInIRQ()) {
              batState = LV_ICON_CHARGE;
              updateBatteryIcon(LV_ICON_CHARGE);
          }
          if (ttgo->power->isVbusRemoveIRQ()) {
              batState = LV_ICON_CALCULATION;
              updateBatteryIcon(LV_ICON_CALCULATION);
          }
          if (ttgo->power->isChargingDoneIRQ()) {
              batState = LV_ICON_CALCULATION;
              updateBatteryIcon(LV_ICON_CALCULATION);
          }
          if (ttgo->power->isPEKShortPressIRQ()) {
              ttgo->power->clearIRQ();
              low_energy();
              return;
          }
          ttgo->power->clearIRQ();
          break;
      case Q_EVENT_WIFI_SCAN_DONE: {
          int16_t len =  WiFi.scanComplete();
          for (int i = 0; i < len; ++i) {
              wifi_list_add(WiFi.SSID(i).c_str());
          }
          break;
      }
      default:
          break;
      }

  }
  if (lv_disp_get_inactive_time(NULL) < screenTimeout) {
      lv_task_handler();
  } else {
      low_energy();
  }
}
