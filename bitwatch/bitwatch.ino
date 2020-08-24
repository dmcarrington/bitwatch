/*
 * Copyright (c) 2020 David Carrington
 * 
 * Based on agoodWatch by Alex Goodyear
 * 
 * Press power button on startup to enter wallet, otherwise standard watch will be displayed.
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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include <soc/rtc.h>
#include "esp_wifi.h"
#include <WiFi.h>
#include "gui.h"
#include "SPIFFS.h"
#include "FFat.h"
#include "Bitcoin.h"
#include "Hash.h"


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

QueueHandle_t g_event_queue_handle = NULL;
EventGroupHandle_t g_event_group = NULL;
EventGroupHandle_t isr_group = NULL;
bool lenergy = false;
TTGOClass *ttgo;
lv_icon_battery_t batState = LV_ICON_CALCULATION;
unsigned int screenTimeout = DEFAULT_SCREEN_TIMEOUT;
bool decoy = true;
bool irq = false;
bool confirm = false;
bool walletDone = false;

String sdcommand;
String savedseed;
String passkey = "";
String passhide = "";
String hashed = "";
String savedpinhash;
bool setPin = false;

String pincode = "";
String obfuscated = "";
lv_obj_t * label1 = NULL;

static void confirmPin() {
  bool set = setPin;
  if (set == true){
     uint8_t newpasskeyresult[32];
     sha256(passkey, newpasskeyresult);
     hashed = toHex(newpasskeyresult,32);
        
     fs::File file = FFat.open("/pass.txt", FILE_WRITE);
     file.print(hashed + "\n");
     file.close();
   }
     
   fs::File otherfile = FFat.open("/pass.txt");
   savedpinhash = otherfile.readStringUntil('\n');
   otherfile.close();
       
   uint8_t passkeyresult[32];
   sha256(passkey, passkeyresult);
   hashed = toHex(passkeyresult,32);
   Serial.println(savedpinhash);
   Serial.println(hashed);
     
   if(savedpinhash == hashed || set == true ){
      Serial.println("PIN ok");
      //getKeys(savedseed, passkey);
      passkey = "";
      passhide = "";
      confirm = true;
      return;
   }
   else if (savedpinhash != hashed && set == false){
      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 110);
      ttgo->tft->setTextSize(2);
      ttgo->tft->setTextColor(TFT_RED);
      ttgo->tft->print("   Reset and try again");
      passkey = "";
      passhide = "";
      delay(3000);
   }
   
  confirm = false;
}

static void event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        const char * txt = lv_btnmatrix_get_active_btn_text(obj);
        if(txt == "Clear") {
          pincode = "";
          obfuscated = "";
          lv_label_set_text(label1, obfuscated.c_str());
        } else if (txt == "Set") {
          passkey = pincode;          
          confirm = true;
          confirmPin();
        }
        
        if(isdigit((int)txt[0])) {
          pincode = pincode + String(txt);
          printf("pincode = %s\n", pincode.c_str());
          obfuscated = obfuscated + "*";
          
          
          lv_label_set_text(label1, obfuscated.c_str());
        }

        printf("%s was pressed\n", txt);
    }
}


static const char * btnm_map[] = {"1", "2", "3", "4", "5", "\n",
                                  "6", "7", "8", "9", "0", "\n",
                                  "Clear", "Set", ""};


void passcode_matrix(void)
{
   
    lv_obj_t * btnm1 = lv_btnmatrix_create(lv_scr_act(), NULL);
    lv_btnmatrix_set_map(btnm1, btnm_map);
    lv_btnmatrix_set_btn_width(btnm1, 10, 2);        /*Make "Action1" twice as wide as "Action2"*/
    lv_obj_align(btnm1, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_event_cb(btnm1, event_handler);
    label1 = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_text(label1, pincode.c_str());
    lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, -70);
}

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

//========================================================================
void enterpin(bool set){

  confirm = false;
  setPin = set;
  passcode_matrix();
}

//========================================================================
void filechecker(){
  SPIFFS.open("/bitwatch.txt", FILE_READ);

}

//========================================================================
void seedmaker(){
  fs::File keyfile = FFat.open("/key.txt", FILE_WRITE);
  keyfile.print("test\n");
  keyfile.close();
}

//=======================================================================

void startupWallet() {
  if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
  } 
      
  if(!FFat.begin(true)) {
    Serial.println("An error has occurred while mounting FFAT");
    return;
  }
  
  bool haveKey = true;
  bool haveCommand = true;
  
  //Checks if the user has an account or is forcing a reset
  haveKey = FFat.exists("/key.txt");
  if(haveKey) {
    fs::File keyfile = FFat.open("/key.txt", FILE_READ);
    savedseed = keyfile.readStringUntil('\n');
    keyfile.close();
  }
  

  haveCommand = SPIFFS.exists("/bitwatch.txt");
  if(haveCommand) {
    fs::File commandfile = SPIFFS.open("/bitwatch.txt", FILE_READ);
    sdcommand = commandfile.readStringUntil('\n');
    commandfile.close();
  }
  
  filechecker();

  if(sdcommand == "HARD RESET"){
    seedmaker();  
    //pinmaker();
  }
  else if(sdcommand.substring(0,7) == "RESTORE"){
    //restorefromseed(sdcommand.substring(8,sdcommand.length()));
    enterpin(true);
  }
  else{
    enterpin(false);
  }

  /*ttgo->tft->fillScreen(TFT_BLUE);
  ttgo->tft->drawString("Start wallet here", 25, 100);
  if(haveKey) {
    ttgo->tft->drawString("Key file found!", 25, 120);
    ttgo->tft->drawString(savedseed, 35, 130);
  } else {
    ttgo->tft->drawString("Key file NOT found!", 25, 120);
  }

  if(haveCommand) {
    ttgo->tft->drawString("Command file found!", 25, 140);
    ttgo->tft->drawString(sdcommand, 35, 150);
  } else {
    ttgo->tft->drawString("Command file NOT found!", 25, 140);
  }
  Serial.println("waiting 30s");
  delay(30000);
  Serial.println("delay finished");
  walletDone = true;    */
}


//=======================================================================

void setup()
{
    Serial.begin (115200);

    Serial.println (THIS_VERSION_STR);
    
    //Create a program that allows the required message objects and group flags
    g_event_queue_handle = xQueueCreate(20, sizeof(uint8_t));
    g_event_group = xEventGroupCreate();
    isr_group = xEventGroupCreate();

    ttgo = TTGOClass::getWatch();

    //Initialize TWatch
    ttgo->begin();

    ttgo->openBL();

    // Turn on the IRQ used
    //ttgo->power->adc1Enable(AXP202_BATT_VOL_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1, AXP202_ON);
    //ttgo->power->enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_CHARGING_FINISHED_IRQ, AXP202_ON);
    //ttgo->power->clearIRQ();

    // Turn off unused power
    ttgo->power->setPowerOutPut(AXP202_EXTEN, AXP202_OFF);
    ttgo->power->setPowerOutPut(AXP202_DCDC2, AXP202_OFF);
    ttgo->power->setPowerOutPut(AXP202_LDO3, AXP202_OFF);
    ttgo->power->setPowerOutPut(AXP202_LDO4, AXP202_OFF);

    //Initialize lvgl
    ttgo->lvgl_begin();

    //Initialize bma423
    ttgo->bma->begin();

    //Enable BMA423 interrupt
    ttgo->bma->attachInterrupt();

    //Connection interrupted to the specified pin
    pinMode(BMA423_INT1, INPUT);
    attachInterrupt(BMA423_INT1, [] {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        EventBits_t  bits = xEventGroupGetBitsFromISR(isr_group);
        if (bits & WATCH_FLAG_SLEEP_MODE)
        {
            // Use an XEvent when waking from low energy sleep mode.
            xEventGroupSetBitsFromISR(isr_group, WATCH_FLAG_SLEEP_EXIT | WATCH_FLAG_BMA_IRQ, &xHigherPriorityTaskWoken);
        } else
        {
            // Use the XQueue mechanism when we are already awake.
            uint8_t data = Q_EVENT_BMA_INT;
            xQueueSendFromISR(g_event_queue_handle, &data, &xHigherPriorityTaskWoken);
        }

        if (xHigherPriorityTaskWoken)
        {
            portYIELD_FROM_ISR ();
        }

    }, RISING);

    // Connection interrupted to the specified pin
    pinMode(AXP202_INT, INPUT);
    attachInterrupt(AXP202_INT, [] {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        EventBits_t  bits = xEventGroupGetBitsFromISR(isr_group);
        if (bits & WATCH_FLAG_SLEEP_MODE)
        {
            // Use an XEvent when waking from low energy sleep mode.
            xEventGroupSetBitsFromISR(isr_group, WATCH_FLAG_SLEEP_EXIT | WATCH_FLAG_AXP_IRQ, &xHigherPriorityTaskWoken);
        } else
        {
            // Use the XQueue mechanism when we are already awake.
            uint8_t data = Q_EVENT_AXP_INT;
            xQueueSendFromISR(g_event_queue_handle, &data, &xHigherPriorityTaskWoken);
        }
        if (xHigherPriorityTaskWoken)
        {
            portYIELD_FROM_ISR ();
        }
        irq = true;
    }, FALLING);

    // Turn on the IRQ used
    ttgo->power->adc1Enable(AXP202_BATT_VOL_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1, AXP202_ON);
    ttgo->power->enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_CHARGING_FINISHED_IRQ, AXP202_ON);
    ttgo->power->clearIRQ();

    // Capture start time to check when we can start the wallet
    ttgo->tft->fillScreen(TFT_RED);
    int starttime = millis();
    while (millis() - starttime < 5000 && decoy == true) {
      ttgo->tft->drawString("waiting for keypress", 25, 100);
      if(irq) {
        irq = false;
        ttgo->power->readIRQ();
        if (ttgo->power->isPEKShortPressIRQ()) {
          decoy = false;
        }
        ttgo->power->clearIRQ();
      }
    }
    
    if(decoy == false){
      // Display wallet
      startupWallet();
    } else {

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
}

void loop()
{
    bool  rlst;
    uint8_t data;
    static uint32_t start = 0;
    if(decoy) {

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
    } else {
      if(walletDone) {
        Serial.println("Got walletDone, restarting");
        
        esp_restart();
      }

      lv_task_handler();
      delay(5);
      
    }
}
