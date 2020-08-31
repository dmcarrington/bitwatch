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
#include <WiFiClient.h>
//#include <WebServer.h>
#include <Electrum.h>
#include "qrcode.h"
#include "gui.h"
#include "SPIFFS.h"
#include "FFat.h"
#include "Bitcoin.h"
#include "Hash.h"
#include "seedwords.h"

//#include <WiFi.h>
//#include <WiFiClient.h>
#include <WiFiAP.h>

#define FILESYSTEM SPIFFS
// You only need to format the filesystem once
#define FORMAT_FILESYSTEM false
#define DBG_OUTPUT_PORT Serial

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
//bool pinConfirmed = false;
bool confirmingPin = false;
lv_obj_t * pinTitle = NULL;
String repeatPincode = "";
lv_obj_t * pinPad = NULL;

String privatekey;
String pubkey;

// PIN generation / entry UI elements
String pincode = "";
String obfuscated = "";
lv_obj_t * label1 = NULL;

int seedCount = 1;
// UI elements for showing seed words
lv_obj_t * seedLabel = NULL;
lv_obj_t * seedBtn = NULL;
lv_obj_t * btnLabel = NULL;
lv_obj_t * wordLabel = NULL;
// String of all generated seed words
String seedgeneratestr = "";
// Current seed word for user to write down
String seedWord = "";
// Display current seed word index
String wordCount = " Word 1";
bool seed_done = false;

lv_obj_t * txinfo = NULL;
lv_obj_t * signTxBtns = NULL;
ElectrumTx tx;

lv_obj_t * resetBtns = NULL;
lv_obj_t * resetInfo = NULL;
lv_obj_t * main_menu = NULL;

lv_obj_t * restoreInfo = NULL;
lv_obj_t * restoreBtns = NULL;
String theSeed = "";

const char *ssid = "yourAP";
const char *password = "yourPassword";
WiFiServer server(80);
String ipaddress = "";

void pinmaker();

void startWebserver(void) {
  //Serial.begin(115200);
  Serial.println();
  Serial.println("Configuring access point...");

  // You can remove the password parameter if you want the AP to be open.
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  ipaddress = myIP.toString();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.begin();

  Serial.println("Server started");

}

//========================================================================
// Show an address as a QR code
//========================================================================
void showAddress(String XXX){
  ttgo->tft->fillScreen(TFT_WHITE);
  XXX.toUpperCase();
 const char* addr = XXX.c_str();
 Serial.println(addr);
  int qrSize = 12;
  int sizes[17] = { 14, 26, 42, 62, 84, 106, 122, 152, 180, 213, 251, 287, 331, 362, 412, 480, 504 };
  int len = String(addr).length();
  for(int i=0; i<17; i++){
    if(sizes[i] > len){
      qrSize = i+1;
      break;
    }
  }
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(qrSize)];
  qrcode_initText(&qrcode, qrcodeData, qrSize-1, ECC_LOW, addr);
  Serial.println(qrSize -1);
 
  float scale = 4;
  int offset_x = 0;
  int offset_y = 40;

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if(qrcode_getModule(&qrcode, x, y)){       
        ttgo->tft->fillRect(offset_x + 15+2+scale*x, offset_y + 2+scale*y, scale, scale, TFT_BLACK);
      }
      else{
        ttgo->tft->fillRect(offset_x + 15+2+scale*x, offset_y + 2+scale*y, scale, scale, TFT_WHITE);
      }
    }
  }
}

//========================================================================
// Show seed words
//========================================================================
void showseed() {
  ttgo->tft->fillScreen(TFT_BLACK);
  ttgo->tft->setCursor(0, 20);
  ttgo->tft->setTextSize(3);
  ttgo->tft->setTextColor(TFT_GREEN);
  ttgo->tft->println("  SHOW SEED");
  ttgo->tft->println("");
  ttgo->tft->setTextColor(TFT_BLUE);
  ttgo->tft->setTextSize(2);
  ttgo->tft->println(savedseed);
  delay(10000);
}

//========================================================================
// Show ZPUB as QR code and export to SPIFFS
//========================================================================
void exportZpub() {
  int str_len = pubkey.length() + 1; 
  char char_array[str_len];
  pubkey.toCharArray(char_array, str_len);
  fs::File file = SPIFFS.open("/bitwatch.txt", FILE_WRITE);
  file.print(char_array);
  file.close();

  ttgo->tft->fillScreen(TFT_BLACK);
  showAddress(pubkey);
  ttgo->tft->setTextColor(TFT_BLACK);
  ttgo->tft->setCursor(0, 0);
  ttgo->tft->setTextSize(2);
  ttgo->tft->println(" EXPORT ZPUB");
  
  ttgo->tft->setTextSize(2);
  ttgo->tft->setCursor(0, 200);
  String msg = "Saved to \n" + ipaddress + "/bitwatch.txt";
  ttgo->tft->println(msg.c_str());
  delay(5000);
}

//========================================================================
// Display public key as QR code and text
//========================================================================
void displayPubkey() {
  HDPublicKey hd(pubkey);
  String pubnumn = "0";
  if(SPIFFS.exists("/num.txt")) {
    fs::File numFile = SPIFFS.open("/num.txt");
    pubnumn = numFile.readStringUntil('\n');
    Serial.println(pubnumn);
    numFile.close();
  }
    
  int pubnum = pubnumn.toInt() + 1;
  fs::File file = SPIFFS.open("/num.txt", FILE_WRITE);
  file.print(pubnum);
  file.close();

  String path = String("m/0/") + pubnum;
  ttgo->tft->fillScreen(TFT_BLACK);
  
  String freshpub = hd.derive(path).address();
  showAddress(freshpub);
  ttgo->tft->setCursor(0, 20);
  ttgo->tft->setTextSize(2);
  ttgo->tft->setTextColor(TFT_BLACK);
  ttgo->tft->println("      PUBKEY");
  ttgo->tft->setCursor(0, 180);
  int i = 0;
  while (i < freshpub.length() + 1){
    ttgo->tft->println(freshpub.substring(i, i + 20));
    i = i + 20;
  }

  delay(10000);
}

//========================================================================
// Repond to confirming a transaction, or cancel signing
//========================================================================
static void sign_tx_event_handler(lv_obj_t * obj, lv_event_t event)
{
  if(event == LV_EVENT_CLICKED)
  {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    if(btn_index == 0) {
      // "sign" button pressed
      lv_obj_del(txinfo);
      lv_obj_del(signTxBtns);

      HDPrivateKey hd(savedseed, passkey);
      HDPrivateKey account = hd.derive("m/84'/0'/0'/");
      Serial.println(account);
      
      tx.sign(account); 
      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 20);
      ttgo->tft->setTextSize(2);
      
      String signedtx = tx;
      Serial.print(signedtx);
      int str_len = signedtx.length() + 1; 
      char char_array[str_len];
      signedtx.toCharArray(char_array, str_len);
      fs::File file = SPIFFS.open("/bitwatch.txt", FILE_WRITE);
      file.print(char_array);
      file.close();

      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 100);
      ttgo->tft->setTextSize(2);

      String msg = "Saved to \n" + ipaddress + "/bitwatch.txt";
      ttgo->tft->println(msg.c_str());
      ttgo->tft->println("");

      delay(3000);
        
    } else if(btn_index == 1) {
      // "cancel button pressed
      lv_obj_del(txinfo);
      lv_obj_del(signTxBtns);
      
    }
  }
}

static const char * sign_tx_map[] = {"Sign", "Cancel"};

//========================================================================
// Submenu for signing a transaction
//========================================================================
void signTransaction() {
  if (sdcommand.substring(0, 4) == "SIGN"){
    String eltx = sdcommand.substring(5, sdcommand.length() + 1);

    

    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setCursor(0, 20);
    ttgo->tft->setTextSize(3);
    ttgo->tft->setTextColor(TFT_GREEN);
    ttgo->tft->println("");
    ttgo->tft->setCursor(0, 90);
    ttgo->tft->println("  Bwahahahaha!");
    ttgo->tft->println("");
    ttgo->tft->println("  Transaction");
    ttgo->tft->println("  found");
  
    delay(3000);

    String txnLabel = "";
    for(int i=0; i < tx.tx.outputsNumber; i++){
      txnLabel =  txnLabel + (tx.tx.txOuts[i].address()) + "\n-> ";
      // Serial can't print uint64_t, so convert to int
      txnLabel = txnLabel + (int(tx.tx.txOuts[i].amount));
      txnLabel = txnLabel + " sat\n";
    }
    txinfo = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(txinfo, NULL, LV_ALIGN_CENTER, 0, -100);
    lv_label_set_text(txinfo, txnLabel.c_str());

    signTxBtns = lv_btnmatrix_create(lv_scr_act(), NULL);
    lv_btnmatrix_set_map(signTxBtns, sign_tx_map);
    lv_obj_set_size(signTxBtns, 240, 40);
    lv_obj_align(signTxBtns, NULL, LV_ALIGN_CENTER, 0, -100);
    lv_obj_set_event_cb(signTxBtns, sign_tx_event_handler);
  }
}

//========================================================================
// handle reset confirmation
//========================================================================
static void reset_btn_event_handler(lv_obj_t * obj, lv_event_t event)
{
  if(event == LV_EVENT_CLICKED)
  {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    if(btn_index == 0) {
      Serial.println("Reset confirmed");
      // confirm reset
      lv_obj_del(resetBtns);
      lv_obj_del(resetInfo);
      lv_obj_set_pos(main_menu, 0, 0);
      seedmaker();
    } else if(btn_index == 1) {
      Serial.println("reset cancelled");
      // cancel reset
      lv_obj_del(resetBtns);
      lv_obj_del(resetInfo);
      lv_obj_set_pos(main_menu, 0, 0);
    }
  }
}  

static const char * reset_btn_map[] = {"Reset", "Cancel", ""};

//========================================================================
// Display reset confirmation
//========================================================================
void confirmReset() {
  lv_obj_set_pos(main_menu, -500, 0);
  resetInfo = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(resetInfo, NULL, LV_ALIGN_CENTER, -30, -60);
  lv_label_set_text(resetInfo, "Device will be reset,\nare you sure?");

  resetBtns = lv_btnmatrix_create(lv_scr_act(), NULL);
  lv_btnmatrix_set_map(resetBtns, reset_btn_map);
  lv_obj_set_size(resetBtns, 240, 40);
  lv_obj_align(resetBtns, NULL, LV_ALIGN_CENTER, 0, 100);
  lv_obj_set_event_cb(resetBtns, reset_btn_event_handler); 
}

//========================================================================
// handle restore confirmation
//========================================================================
static void restore_btn_event_handler(lv_obj_t * obj, lv_event_t event)
{
  if(event == LV_EVENT_CLICKED)
  {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    if(btn_index == 0) {
      Serial.println("restore confirmed");
      // confirm reset
      lv_obj_del(restoreBtns);
      lv_obj_del(restoreInfo);
      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 100);
      ttgo->tft->setTextColor(TFT_GREEN);
      ttgo->tft->setTextSize(2);
      ttgo->tft->println("  Saving seed...");
      delay(2000);
      fs::File file = FFat.open("/key.txt", FILE_WRITE);
      file.print(theSeed + "\n");
      file.close();
      fs::File outfile = SPIFFS.open("/bitwatch.txt", FILE_WRITE);
      outfile.print("");
      outfile.close();
      lv_obj_set_pos(main_menu, 0, 0);
    } else if(btn_index == 1) {
      Serial.println("restore cancelled");
      // cancel reset
      lv_obj_del(restoreBtns);
      lv_obj_del(restoreInfo);
      lv_obj_set_pos(main_menu, 0, 0);
    }
  }
}

static const char * restore_btn_map[] = {"Restore", "Cancel", ""};

//========================================================================
// Sub menu to confirm restore
//========================================================================
void confirmRestoreFromSeed() {
  lv_obj_set_pos(main_menu, -500, 0);
  restoreInfo = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(restoreInfo, NULL, LV_ALIGN_CENTER, -60, -60);
  lv_label_set_text(restoreInfo, "Device will be wiped\nthen restored from seed,\nare you sure?");

  restoreBtns = lv_btnmatrix_create(lv_scr_act(), NULL);
  lv_btnmatrix_set_map(restoreBtns, reset_btn_map);
  lv_obj_set_size(restoreBtns, 240, 40);
  lv_obj_align(restoreBtns, NULL, LV_ALIGN_CENTER, 0, 100);
  lv_obj_set_event_cb(restoreBtns, restore_btn_event_handler); 
}

//========================================================================
// attempt to restore from phrase stored in SPIFFS
//========================================================================
void restoreFromSeed() {
  if(sdcommand.substring(0,7) == "RESTORE"){
    theSeed = sdcommand.substring(8,sdcommand.length());
    confirmRestoreFromSeed();
  } else {
    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setTextSize(2);
    ttgo->tft->setCursor(0, 90);
    ttgo->tft->setTextColor(TFT_RED);
    ttgo->tft->println("'RESTORE *seed phrase*' not found on SPIFFS");
    delay(3000);
  }
}

//========================================================================
// Handle menu button array presses
//========================================================================
static void menu_event_handler(lv_obj_t * obj, lv_event_t event)
{
  if(event == LV_EVENT_CLICKED)
  {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    switch(btn_index){
      case(0):
        Serial.println("Display pubkey");
        displayPubkey();
        break;
      case(1): 
        Serial.println("Sign transaction");
        break;
      case(2):
        Serial.println("Export ZPUB");
        exportZpub();
        break;
      case(3):
        Serial.println("Show Seed");
        showseed();
        break;
      case(4):
        Serial.println("Wipe Device");
        confirmReset();
        break;
      case(5):
        Serial.println("Restore from seed");
        restoreFromSeed();
        break;
      case(6):
        Serial.println("Restart");
        esp_restart();
        break;
    }    
  }
}

static const char * menu_map[] = {"Display Pubkey", "\n",
                                    "Sign Transaction", "\n",
                                    "Export ZPUB", "\n",
                                    "Show Seed", "\n",
                                    "Wipe Device", "\n",
                                    "Restore From Seed", "\n",
                                    "Restart", ""};

void menu_matrix(void)
{
    ttgo->tft->fillScreen(TFT_BLACK);
    main_menu = lv_btnmatrix_create(lv_scr_act(), NULL);
    lv_btnmatrix_set_map(main_menu, menu_map);
    lv_obj_set_size(main_menu, 240, 240);
    lv_obj_align(main_menu, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_event_cb(main_menu, menu_event_handler);
}

//========================================================================
// On each button press, generate the next of 24 seed words.
// When completed, save to seed file in FFAT and SPIFFS partitions
//========================================================================
static void seedmaker_cb(lv_obj_t * obj, lv_event_t event) {
  if(event == LV_EVENT_CLICKED) {
    if(seedCount <= 23){
      seedCount++;
      seedWord = seedwords[random(0,2047)];
      lv_label_set_text(seedLabel, seedWord.c_str());
      seedgeneratestr += " " + seedWord;
      Serial.println(seedgeneratestr);
      wordCount = "Word " + String(seedCount);
      lv_label_set_text(wordLabel, wordCount.c_str());
    } else {
      if(seed_done) {
        lv_obj_del(seedLabel);
        lv_obj_del(wordLabel);
        lv_obj_del(seedBtn);
        pinmaker();
      } else {
        fs::File file = FFat.open("/key.txt", FILE_WRITE);
        file.print(seedgeneratestr.substring(0, seedgeneratestr.length()) + "\n");
        file.close();
        
        file = SPIFFS.open("/bitwatch.txt", FILE_WRITE);
        file.print(seedgeneratestr);
        file.close();
        
        lv_obj_align(seedLabel, NULL, LV_ALIGN_CENTER, -50, -20);
        lv_label_set_text(seedLabel, "Key file saved to\ninternal storage");
        lv_label_set_text(wordLabel, "");
        lv_label_set_text(btnLabel, "Done");
        seed_done = true;
      }
    }
  }
}

//========================================================================
// Loop over a set of 24 randomly-generated seed words
//========================================================================
void seedmaker(){
  ttgo->tft->fillScreen(TFT_BLACK);
  ttgo->tft->setCursor(0, 100);
  ttgo->tft->setTextColor(TFT_GREEN);
  ttgo->tft->setTextSize(2);
  ttgo->tft->println("   Write seed words");
  ttgo->tft->println("   somewhere safe!");
  delay(6000);

  seedBtn = lv_btn_create(lv_scr_act(), NULL);
  lv_obj_set_event_cb(seedBtn, seedmaker_cb);      
  lv_obj_set_size(seedBtn, 200, 40);    
  lv_obj_align(seedBtn, NULL, LV_ALIGN_CENTER, 0, 90);
  btnLabel = lv_label_create(seedBtn, NULL);
  lv_label_set_text(btnLabel, "Next");  

  seedWord = seedwords[random(0,2047)];
  seedgeneratestr = seedWord;
  seedLabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(seedLabel, NULL, LV_ALIGN_CENTER, 0, -20);
  lv_label_set_text(seedLabel, seedWord.c_str());

  wordLabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(wordLabel, NULL, LV_ALIGN_CENTER, 0, -60);
  lv_label_set_text(wordLabel, "Word 1");
  
}

//========================================================================
// Generate wallet keys from mnemonic and password
//========================================================================
void getKeys(String mnemonic, String password)
{
  HDPrivateKey hd(mnemonic, password);

  if(!hd){ // check if it is valid
    Serial.println("   Invalid xpub");
    return;
  }
  
  HDPrivateKey account = hd.derive("m/84'/0'/0'/");
  
  privatekey = account;
  
  pubkey = account.xpub();
}

//========================================================================
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
      getKeys(savedseed, passkey);
      //passkey = "";
      passhide = "";
      confirm = true;
      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 110);
      ttgo->tft->setTextSize(2);
      ttgo->tft->setTextColor(TFT_GREEN);
      ttgo->tft->print("Wallet setup!");
      startWebserver();
      delay(3000);
      lv_obj_del(pinPad);
      lv_obj_del(pinTitle);
      lv_obj_del(label1);
      menu_matrix();
      return;
   }
   else if (savedpinhash != hashed && set == false){
      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 110);
      ttgo->tft->setTextSize(2);
      ttgo->tft->setTextColor(TFT_RED);
      ttgo->tft->print("Reset and try again");
      pincode = "";
      passhide = "";
      delay(3000);
   }
   
  confirm = false;
}

//========================================================================
static void pin_event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        const char * txt = lv_btnmatrix_get_active_btn_text(obj);
        unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
        if(btn_index == 10) {
          // Clearn button pressed
          if(confirmingPin) {
            repeatPincode = "";
          } else {
            pincode = "";
          }
          obfuscated = "";
          lv_label_set_text(label1, obfuscated.c_str());
        } else if (btn_index == 11) {
          if(confirmingPin){
            if(repeatPincode == pincode) {
              // pincode confirmed successfully, set it
              passkey = pincode;          
              confirm = true;
              confirmPin();
            } else {
              // pin did not match, start over
              ttgo->tft->fillScreen(TFT_BLACK);
              ttgo->tft->setCursor(0, 110);
              ttgo->tft->setTextSize(2);
              ttgo->tft->setTextColor(TFT_RED);
              ttgo->tft->print("Try again");
              pincode = "";
              passhide = "";
              obfuscated = "";
              confirmingPin = false;
              delay(3000);
            }
          } else {
            if(setPin) {
              confirmingPin = true;
              obfuscated = "";
              lv_label_set_text(label1, obfuscated.c_str());
              lv_label_set_text(pinTitle, "Confirm PIN");
            } else {
              passkey = pincode;
              confirmPin();
            }
          }
          
        } else {
          // Number button 0-9
          if(confirmingPin) {
            repeatPincode = repeatPincode + String(txt);
          } else {
            pincode = pincode + String(txt);
            printf("pincode = %s\n", pincode.c_str());
          }
          
          obfuscated = obfuscated + "*";
          lv_label_set_text(label1, obfuscated.c_str());
        }

        printf("%s was pressed\n", txt);
    }
}


static const char * btnm_map[] = {"1", "2", "3", "4", "5", "\n",
                                  "6", "7", "8", "9", "0", "\n",
                                  "Clear", "OK", ""};

//========================================================================
void passcode_matrix(void)
{
    ttgo->tft->fillScreen(TFT_BLACK);
    pinPad = lv_btnmatrix_create(lv_scr_act(), NULL);
    lv_btnmatrix_set_map(pinPad, btnm_map);
    lv_btnmatrix_set_btn_width(pinPad, 10, 2);        /*Make "Action1" twice as wide as "Action2"*/
    lv_obj_align(pinPad, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_event_cb(pinPad, pin_event_handler);
    label1 = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_text(label1, pincode.c_str());
    lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, -70);
    pinTitle = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_text(pinTitle, "Enter PIN");
    lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, -90);
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
void pinmaker(){
  ttgo->tft->fillScreen(TFT_BLACK);
  ttgo->tft->setCursor(0, 90);
  ttgo->tft->setTextColor(TFT_GREEN);
  ttgo->tft->println("   Enter pin using");
  ttgo->tft->println("   keypad,");
  ttgo->tft->println("   3 letters at least");
  delay(6000);
  enterpin(true);
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
    Serial.println("Key file exists:");
    fs::File keyfile = FFat.open("/key.txt", FILE_READ);
    savedseed = keyfile.readStringUntil('\n');
    Serial.println(savedseed);
    keyfile.close();
  }
  

  haveCommand = SPIFFS.exists("/bitwatch.txt");
  if(haveCommand) {
    fs::File commandfile = SPIFFS.open("/bitwatch.txt", FILE_READ);
    sdcommand = commandfile.readStringUntil('\n');
    Serial.println(sdcommand);
    commandfile.close();
  }
  
  //filechecker();

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

      WiFiClient client = server.available();   // listen for incoming clients

      if (client) {                             // if you get a client,
        Serial.println("New Client.");           // print a message out the serial port
        String currentLine = "";                // make a String to hold incoming data from the client
        while (client.connected()) {            // loop while the client's connected
          if (client.available()) {             // if there's bytes to read from the client,
            char c = client.read();             // read a byte, then
            Serial.write(c);                    // print it out the serial monitor
            if (c == '\n') {                    // if the byte is a newline character
    
              // if the current line is blank, you got two newline characters in a row.
              // that's the end of the client HTTP request, so send a response:
              if (currentLine.length() == 0) {
                // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                // and a content-type so the client knows what's coming, then a blank line:
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/html");
                client.println();
                // print content of bitwatch.txt
                fs::File file = SPIFFS.open("/bitwatch.txt", FILE_READ);
                String bitwatch = file.readStringUntil('\n');
                client.println(bitwatch);
                file.close();
                client.stop();
              }
            }
          }
        }
        // close the connection:
        Serial.println("Client Disconnected.");
      }

      lv_task_handler();
      delay(5);
      
    }
}
