/*
   Copyright (c) 2020-2022 David Carrington

   Port of Bowser wallet https://github.com/arcbtc/bowser-bitcoin-hardware-wallet to LILYGO T-Watch

*/

#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
using WebServerClass = WebServer;
fs::SPIFFSFS &FlashFS = SPIFFS;
#define FORMAT_ON_FAIL true

#include <AutoConnect.h>
#include <SPI.h>

#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include <soc/rtc.h>
#include "esp_wifi.h"
#include <ArduinoJson.h>
#include <Electrum.h>
#include "qrcoded.h"
#include "gui.h"

#include "Bitcoin.h"
#include "Hash.h"
#include "seedwords.h"
#include "watch.h"

#define PARAM_FILE "/elements.json"
#define PARAM_JSON_SIZE 1024
#define PSBT_FILE "/psbt.json"

// watch instance
TTGOClass *ttgo;

// Event handlers
QueueHandle_t g_event_queue_handle = NULL;
EventGroupHandle_t g_event_group = NULL;
EventGroupHandle_t isr_group = NULL;
bool decoy = true;
bool irq = false;

// PIN code elements
lv_obj_t *pinTitle = NULL;
lv_obj_t *pinPad = NULL;

// wallet elements
String privatekey;
String pubkey = "";

// Global variables read from config file
String pin;
String apPassword = "ToTheMoon1"; //default WiFi AP password
String seedphrase;
String psbt;
String signedTransaction;

// PIN generation / entry UI elements
String pincode = "";
String obfuscated = "";
lv_obj_t *label1 = NULL;

// Wallet reset UI
lv_obj_t *resetBtns = NULL;
lv_obj_t *resetInfo = NULL;
lv_obj_t *main_menu = NULL;

// custom access point pages
// Init / Restore wallet
static const char PAGE_INIT[] PROGMEM = R"(
{
  "uri": "/newwallet",
  "title": "Bitwatch Config",
  "menu": true,
  "element": [
    {
      "name": "text",
      "type": "ACText",
      "value": "Bitwatch options",
      "style": "font-family:Arial;font-size:16px;font-weight:400;color:#191970;margin-botom:15px;"
    },
    {
      "name": "pin",
      "type": "ACInput",
      "label": "PIN code for Bitwatch wallet",
      "apply": "number"
    },
    {
      "name": "password",
      "type": "ACInput",
      "label": "Password for Bitwatch AP WiFi",
      "apply": "password"
    },
    {
      "name": "seedphrase",
      "type": "ACInput",
      "label": "Wallet Seed Phrase",
    },
    {
      "name": "load",
      "type": "ACSubmit",
      "value": "Load",
      "uri": "/newwallet"
    },
    {
      "name": "save",
      "type": "ACSubmit",
      "value": "Save",
      "uri": "/save"
    },
    {
      "name": "adjust_width",
      "type": "ACElement",
      "value": "<script type='text/javascript'>window.onload=function(){var t=document.querySelectorAll('input[]');for(i=0;i<t.length;i++){var e=t[i].getAttribute('placeholder');e&&t[i].setAttribute('size',e.length*.8)}};</script>"
    }
  ]
 }
)";

// Save wallet
static const char PAGE_SAVEWALLET[] PROGMEM = R"(
{
  "uri": "/save",
  "title": "Elements",
  "menu": false,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "format": "Elements have been saved to %s",
      "style": "font-family:Arial;font-size:18px;font-weight:400;color:#191970"
    },
    {
      "name": "validated",
      "type": "ACText",
      "style": "color:red"
    },
    {
      "name": "echo",
      "type": "ACText",
      "style": "font-family:monospace;font-size:small;white-space:pre;"
    },
    {
      "name": "ok",
      "type": "ACSubmit",
      "value": "OK",
      "uri": "/newwallet"
    }
  ]
}
)";

static const char PAGE_ENTERPSBT[] PROGMEM = R"(
{
  "uri": "/enterpsbt",
  "title": "Submit PSBT",
  "menu": true,
  "element": [
    {
      "name": "text",
      "type": "ACText",
      "value": "Submit PSBT",
      "style": "font-family:Arial;font-size:16px;font-weight:400;color:#191970;margin-botom:15px;"
    },
    {
      "name": "psbt",
      "type": "ACInput",
      "label": "Paste PSBT here - hex only",
    },
    {
      "name": "save",
      "type": "ACSubmit",
      "value": "Sign",
      "uri": "/savepsbt"
    },
    {
      "name": "adjust_width",
      "type": "ACElement",
      "value": "<script type='text/javascript'>window.onload=function(){var t=document.querySelectorAll('input[]');for(i=0;i<t.length;i++){var e=t[i].getAttribute('placeholder');e&&t[i].setAttribute('size',e.length*.8)}};</script>"
    }
  ]
})";

static const char PAGE_SAVEPSBT[] PROGMEM = R"(
{
  "uri": "/savepsbt",
  "title": "PSBT",
  "menu": false,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "format": "PSBT has been saved to %s",
      "style": "font-family:Arial;font-size:18px;font-weight:400;color:#191970"
    },
    {
      "name": "validated",
      "type": "ACText",
      "style": "color:red"
    },
    {
      "name": "echo",
      "type": "ACText",
      "style": "font-family:monospace;font-size:small;white-space:pre;"
    },
    {
      "name": "ok",
      "type": "ACSubmit",
      "value": "OK",
      "uri": "/enterpsbt"
    }
  ]
})";

// portal and config
WebServerClass server;
AutoConnect portal(server);
AutoConnectConfig config;
AutoConnectAux initAux;
AutoConnectAux zpubAux;
AutoConnectAux saveWalletAux;
AutoConnectAux submittxAux;
AutoConnectAux savepsbtAux;

// Prints the content of a file to the Serial
void printFile(const char *filename) {
  // Open file for reading
  File file = FlashFS.open(filename);
  if (!file) {
    Serial.println(F("Failed to read file"));
    return;
  }

  // Extract each characters by one by one
  while (file.available()) {
    Serial.print((char)file.read());
  }
  Serial.println();

  // Close the file
  file.close();
}

//========================================================================
// Show an address as a QR code
//========================================================================
void showAddress(String XXX) {
  ttgo->tft->fillScreen(TFT_WHITE);
  XXX.toUpperCase();
  const char *addr = XXX.c_str();
  Serial.println(addr);
  int qrSize = 12;
  int sizes[17] = { 14, 26, 42, 62, 84, 106, 122, 152, 180, 213, 251, 287, 331, 362, 412, 480, 504 };
  int len = String(addr).length();
  for (int i = 0; i < 17; i++) {
    if (sizes[i] > len) {
      qrSize = i + 1;
      break;
    }
  }
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(qrSize)];
  qrcode_initText(&qrcode, qrcodeData, qrSize - 1, ECC_LOW, addr);
  Serial.println(qrSize - 1);

  float scale = 4;
  int offset_x = 0;
  int offset_y = 40;

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        ttgo->tft->fillRect(offset_x + 15 + 2 + scale * x, offset_y + 2 + scale * y, scale, scale, TFT_BLACK);
      } else {
        ttgo->tft->fillRect(offset_x + 15 + 2 + scale * x, offset_y + 2 + scale * y, scale, scale, TFT_WHITE);
      }
    }
  }
}

//========================================================================
// Show ZPUB as QR code
//========================================================================
void exportZpub() {
  int str_len = pubkey.length() + 1;
  char char_array[str_len];
  pubkey.toCharArray(char_array, str_len);

  ttgo->tft->fillScreen(TFT_BLACK);
  showAddress(pubkey);
  ttgo->tft->setTextColor(TFT_BLACK);
  ttgo->tft->setCursor(0, 0);
  ttgo->tft->setTextSize(2);
  ttgo->tft->println(" EXPORT ZPUB");

  ttgo->tft->setTextSize(1);
  ttgo->tft->setCursor(0, 220);
  delay(10000);
}

//========================================================================
// Display public key as QR code and text
//========================================================================
void displayPubkey() {
  HDPublicKey hd(pubkey);
  String pubnumn = "0";
  if (FlashFS.exists("/num.txt")) {
    fs::File numFile = FlashFS.open("/num.txt");
    pubnumn = numFile.readStringUntil('\n');
    Serial.println(pubnumn);
    numFile.close();
  }

  int pubnum = pubnumn.toInt() + 1;
  fs::File file = FlashFS.open("/num.txt", FILE_WRITE);
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
  while (i < freshpub.length() + 1) {
    ttgo->tft->println(freshpub.substring(i, i + 20));
    i = i + 20;
  }

  delay(10000);
}

//========================================================================
// Signs a PSBT, returns signed transaction for broadcast
//========================================================================
String signTransaction(String psbt) {
  ElectrumTx tx;
  int len_parsed = tx.parse(psbt);
  if (len_parsed == 0) {
    Serial.println("Failed to parse tx " + psbt);
    return "";
  }
  Serial.println(seedphrase);
  HDPrivateKey hd(seedphrase, pin);
  HDPrivateKey account = hd.derive("m/84'/0'/0'/");
  Serial.println(account);

  tx.sign(account);
  return tx;
}

//========================================================================
// Submenu for signing a transaction
//========================================================================
void signTransaction() {
  server.on("/", []() {
      String content = "<h1>Bitwatch</br>PSBT Signing Portal</h1>";
      content += AUTOCONNECT_LINK(COG_24);
      server.send(200, "text/html", content);
    });

    submittxAux.load(FPSTR(PAGE_ENTERPSBT));
    submittxAux.on([](AutoConnectAux &aux, PageArgument &arg) {
      File file = FlashFS.open(PSBT_FILE, "r");
      if (file)
      {
        aux.loadElement(file, "psbt");
        file.close();
      } else {
        Serial.println("Failed to open params for submittxAux");
      }

      if (portal.where() == "/enterpsbt")
      {
        File file = FlashFS.open(PSBT_FILE, "r");
        if (file)
        {
          aux.loadElement(file, "psbt");
          file.close();
        }
      }

      return String();
    });    

    savepsbtAux.load(FPSTR(PAGE_SAVEPSBT));
    savepsbtAux.on([](AutoConnectAux &aux, PageArgument &arg) {
      aux["caption"].value = PSBT_FILE;
      File file = FlashFS.open(PSBT_FILE, "w+");

      if (file)
      {
        // save as a loadable set for parameters.
        submittxAux.saveElement(file, {"psbt"});
        file.close();
      }

      file = FlashFS.open(PSBT_FILE, "r");
      if(file)
      {
        // read value back from file and actually sign the transaction
        StaticJsonDocument<PARAM_JSON_SIZE> doc;
        DeserializationError error = deserializeJson(doc, file.readString());
        if(error) 
        {
          Serial.println("Error decoding psbt file!");
        } else {
          const char * psbtChar = doc["value"];
          psbt = String(psbtChar);
          Serial.println("Read psbt = " + psbt);
          signedTransaction = signTransaction(psbt);
          Serial.println("Signed transaction: " + signedTransaction);

          aux["echo"].value = signedTransaction;
          file.close();
        }
      } else {
        Serial.println("failed to open PSBT_FILE for writing");
        aux["echo"].value = "Filesystem failed to open.";
      }

      return String();
    });

    // start access point
    portalLaunch();

    config.immediateStart = true;
    config.ticker = true;
    config.apid = "Bitwatch-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    config.psk = apPassword;
    config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET;
    config.title = "Bitwatch";

    portal.join({submittxAux, savepsbtAux});
    portal.config(config);
    portal.begin();
    while (true)
    {
      portal.handleClient();
    }
}

//========================================================================
// handle reset confirmation
//========================================================================
static void reset_btn_event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    if (btn_index == 0) {
      Serial.println("Reset confirmed");
      // confirm reset
      lv_obj_del(resetBtns);
      lv_obj_del(resetInfo);
      lv_obj_set_pos(main_menu, 0, 0);
      
      // Delete params file and restart
      FlashFS.remove(PARAM_FILE);
      esp_restart();
    } else if (btn_index == 1) {
      Serial.println("reset cancelled");
      // cancel reset
      lv_obj_del(resetBtns);
      lv_obj_del(resetInfo);
      lv_obj_set_pos(main_menu, 0, 0);
    }
  }
}

static const char *reset_btn_map[] = { "Reset", "Cancel", "" };

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
// Handle menu button array presses
//========================================================================
static void menu_event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    switch (btn_index) {
      case (0):
        Serial.println("Display pubkey");
        displayPubkey();
        break;
      case (1):
        Serial.println("Sign transaction");
        signTransaction();
        break;
      case (2):
        Serial.println("Export ZPUB");
        exportZpub();
        break;
      case (3):
        Serial.println("Settings");
        startConfigPortal();
        break;
      case (4):
        Serial.println("Wipe Device");
        confirmReset();
        break;
      case (6):
        Serial.println("Restart");
        esp_restart();
        break;
    }
  }
}

static const char *menu_map[] = { "Receive", "\n",
                                  "Sign Transaction", "\n",
                                  "Show ZPUB", "\n",
                                  "Settings", "\n",
                                  "Wipe Device", "\n",
                                  "Restart", "" };

//========================================================================
// Main menu for wallet
//========================================================================
void menu_matrix(void) {
  ttgo->tft->fillScreen(TFT_BLACK);
  main_menu = lv_btnmatrix_create(lv_scr_act(), NULL);
  lv_btnmatrix_set_map(main_menu, menu_map);
  lv_obj_set_size(main_menu, 240, 240);
  lv_obj_align(main_menu, NULL, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_event_cb(main_menu, menu_event_handler);
}

//========================================================================
// Loop over a set of 24 randomly-generated seed words
//========================================================================
void seedmaker() {

  // Delete existing file, otherwise the configuration is appended to the file
  FlashFS.remove(PARAM_FILE);
  
  seedphrase = seedwords[random(0, 2047)];
  for (int i = 0; i < 23; i++)
  {
    seedphrase = seedphrase + " " + seedwords[random(0, 2047)];
  }
  Serial.println("Created seedphrase " + seedphrase);
  
  File file = FlashFS.open(PARAM_FILE, "w+");
  if (!file)
  {
    Serial.println(F("Failed to create file"));
    return;
  }

  // Create a placeholder param file matching pattern used by AutoConnect
  DynamicJsonDocument doc(PARAM_JSON_SIZE);

  doc[0]["name"] = "pin";
  doc[0]["type"] = "ACInput";
  doc[0]["value"] = "1234";
  doc[0]["label"] = "PIN code for Bitwatch wallet";
  doc[0]["pattern"] = "";
  doc[0]["placeholder"] = "1234";
  doc[0]["style"] = "";
  doc[0]["apply"] = "number";

  doc[1]["name"] = "password";
  doc[1]["type"] = "ACInput";
  doc[1]["value"] = "ToTheMoon1";
  doc[1]["label"] = "Password for Bitwatch AP WiFi";
  doc[1]["pattern"] = "";
  doc[1]["placeholder"] = "WIFI password";
  doc[1]["style"] = "";
  doc[1]["apply"] = "password";
  
  doc[2]["name"] = "seedphrase";
  doc[2]["type"] = "ACInput";
  doc[2]["value"] = seedphrase;
  doc[2]["label"] = "Wallet Seed Phrase";
  doc[2]["pattern"] = "";
  doc[2]["placeholder"] = "word1 word2 word3 word4 word5 word6 word7 word8 word9 word10 word11 word12";
  doc[2]["style"] = "";
  doc[2]["apply"] = "text";
  
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  } else {
    Serial.println("written seed phrase to file");
  }
  
  file.close();
}

//========================================================================
// Generate wallet keys from mnemonic and password
//========================================================================
void getKeys(String mnemonic, String password) {
  HDPrivateKey hd(mnemonic, password);

  if (!hd) {  // check if it is valid
    Serial.println("   Invalid xpub");
    return;
  }

  HDPrivateKey account = hd.derive("m/84'/0'/0'/");

  privatekey = account;

  pubkey = account.xpub();
}

//========================================================================
static void confirmPin() {

  if (pincode == pin) {
    Serial.println("PIN ok");
    getKeys(seedphrase, pin);
    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setCursor(0, 110);
    ttgo->tft->setTextSize(2);
    ttgo->tft->setTextColor(TFT_GREEN);
    ttgo->tft->print(" Wallet Loaded!");
    delay(1000);
    lv_obj_del(pinPad);
    lv_obj_del(pinTitle);
    lv_obj_del(label1);
    menu_matrix();
    return;
  } else {
    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setCursor(0, 110);
    ttgo->tft->setTextSize(2);
    ttgo->tft->setTextColor(TFT_RED);
    ttgo->tft->print("Reset and try again");
    pincode = "";
    obfuscated = "";
    delay(3000);
  }
}

//========================================================================
static void pin_event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event == LV_EVENT_VALUE_CHANGED) {
    const char *txt = lv_btnmatrix_get_active_btn_text(obj);
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    if (btn_index == 10) {
      // Clear button pressed
      pincode = "";
      obfuscated = "";
      lv_label_set_text(label1, obfuscated.c_str());
    } else if (btn_index == 11) {
      // Verify pin and start wallet if correct
      confirmPin();
    } else {
      // Number button 0-9
      pincode = pincode + String(txt);
      printf("pincode = %s\n", pincode.c_str());
      
      obfuscated = obfuscated + "*";
      lv_label_set_text(label1, obfuscated.c_str());
    }

    printf("%s was pressed\n", txt);
  }
}


static const char *btnm_map[] = { "1", "2", "3", "4", "5", "\n",
                                  "6", "7", "8", "9", "0", "\n",
                                  "Clear", "OK", "" };
//========================================================================
// Display PIN entry pad
//========================================================================
void enterpin() {
  ttgo->tft->fillScreen(TFT_BLACK);
  pinPad = lv_btnmatrix_create(lv_scr_act(), NULL);
  lv_btnmatrix_set_map(pinPad, btnm_map);
  lv_btnmatrix_set_btn_width(pinPad, 10, 2); /*Make "Action1" twice as wide as "Action2"*/
  lv_obj_align(pinPad, NULL, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_event_cb(pinPad, pin_event_handler);
  label1 = lv_label_create(lv_scr_act(), NULL);
  lv_label_set_text(label1, pincode.c_str());
  lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, -70);
  pinTitle = lv_label_create(lv_scr_act(), NULL);
  lv_label_set_text(pinTitle, "Enter PIN");
  lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, -90);
}

//========================================================================
// Display "AP Launched" message while AP is active
//========================================================================
void portalLaunch()
{
  ttgo->tft->fillScreen(TFT_BLACK);
  ttgo->tft->setTextColor(TFT_PURPLE, TFT_BLACK);
  ttgo->tft->setTextSize(3);
  ttgo->tft->setCursor(20, 50);
  ttgo->tft->println("AP LAUNCHED");
  ttgo->tft->setTextColor(TFT_WHITE, TFT_BLACK);
  ttgo->tft->setCursor(0, 75);
  ttgo->tft->setTextSize(2);
  ttgo->tft->println(" WHEN FINISHED RESET");
}

//========================================================================
// Start AP for configuring wallet
//========================================================================
void startConfigPortal()
{
  // handle access point traffic
    server.on("/", []() {
      String content = "<h1>Bitwatch</br>Stealth Bitcoin hardware wallet</h1>";
      content += AUTOCONNECT_LINK(COG_24);
      server.send(200, "text/html", content);
    });

    initAux.load(FPSTR(PAGE_INIT));
    initAux.on([](AutoConnectAux &aux, PageArgument &arg) {
      File param = FlashFS.open(PARAM_FILE, "r");
      if (param)
      {
        aux.loadElement(param, {"pin", "password", "seedphrase"});
        param.close();
      } else {
        Serial.println("Failed to open params for initAux");
      }

      if (portal.where() == "/newwallet")
      {
        File param = FlashFS.open(PARAM_FILE, "r");
        if (param)
        {
          aux.loadElement(param, {"pin", "password", "seedphrase"});
          param.close();
        }
      }

      return String();
    });

    String zpubPage = "{\"title\": \"ZPUB\",\"uri\": \"/zpub\",\"menu\": true,\"element\": [{\"name\": \"zpub\",\"type\": \"ACText\", \"value\": \"" + pubkey + "\", \"style\": \"display:inline;word-wrap:break-word;font-weight:bold;margin-right:3px\"}]}";
    zpubAux.load(FPSTR(zpubPage.c_str()));
    saveWalletAux.on([](AutoConnectAux &aux, PageArgument &arg) {
      aux["caption"].value = "ZPUB";

      return String();
    });
    

    saveWalletAux.load(FPSTR(PAGE_SAVEWALLET));
    saveWalletAux.on([](AutoConnectAux &aux, PageArgument &arg) {
      aux["caption"].value = PARAM_FILE;
      File param = FlashFS.open(PARAM_FILE, "w");

      if (param)
      {
        // save as a loadable set for parameters.
        initAux.saveElement(param, {"pin", "password", "seedphrase"});
        param.close();

        // read the saved elements again to display.
        param = FlashFS.open(PARAM_FILE, "r");
        aux["echo"].value = param.readString();
        param.close();
        printFile(PARAM_FILE);
      }
      else
      {
        aux["echo"].value = "Filesystem failed to open.";
      }

      return String();
    });

    // start access point
    portalLaunch();

    config.immediateStart = true;
    config.ticker = true;
    config.apid = "Bitwatch-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    config.psk = apPassword;
    config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET;
    config.title = "Bitwatch";

    portal.join({initAux, zpubAux, saveWalletAux});
    portal.config(config);
    portal.begin();
    while (true)
    {
      portal.handleClient();
    }
}

//=======================================================================
// Try to start the wallet. If one is not found, create a new one and start the config portal
//========================================================================
void startupWallet() {
  //FlashFS.begin(FORMAT_ON_FAIL);
  if(!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }

  bool needInit = true;

  // uncomment if PARAM_FILE gets corrupted or needs to be removed for testing
  //FlashFS.remove(PARAM_FILE);

  // get the saved details and store in global variables
  File paramFile = FlashFS.open(PARAM_FILE, "r");
  Serial.println("reading PARAM_FILE");
  
  if (paramFile)
  {
    StaticJsonDocument<PARAM_JSON_SIZE> doc;
    DeserializationError error = deserializeJson(doc, paramFile.readString());
    if(error) 
    {
      Serial.println("Error decoding param file, will recreate");
    } else {
      printFile(PARAM_FILE);
      const JsonObject pinRoot = doc[0];
      char pinChar[64];
      strlcpy(pinChar, pinRoot["value"], sizeof(pinChar));
      pin = String(pinChar);
      Serial.println("Read pin = " + pin);
  
      const JsonObject passRoot = doc[1];
      const char *apPasswordChar = passRoot["value"];
      const char *apNameChar = passRoot["name"];
      if (String(apPasswordChar) != "" && String(apNameChar) == "password")
      {
        apPassword = apPasswordChar;
      }
      Serial.println("Read password = " + apPassword);
  
      const JsonObject seedRoot = doc[2];
      char seedphraseChar[2500];
      strlcpy(seedphraseChar, seedRoot["value"], sizeof(seedphraseChar));
      seedphrase = String(seedphraseChar);
      if (seedphrase != "")
      {
        needInit = false;
        
      }
      Serial.println("Read seedphrase = " + seedphrase);
    }
  }else {
      Serial.println("failed to open param file");
  }

  paramFile.close();

  // general WiFi setting
  config.autoReset = false;
  config.autoReconnect = true;
  config.reconnectInterval = 1; // 30s
  config.beginTimeout = 10000UL;

  // start portal if we decide we need to initialise the config
  if (needInit)
  {
    // Initialise a new wallet seed phrase
    seedmaker();
    
    startConfigPortal();
  } else {
    enterpin();
  }
}


//=======================================================================

void setup() {
  Serial.begin(115200);

  Serial.println(THIS_VERSION_STR);

  //Create a program that allows the required message objects and group flags
  g_event_queue_handle = xQueueCreate(20, sizeof(uint8_t));
  g_event_group = xEventGroupCreate();
  isr_group = xEventGroupCreate();

  ttgo = TTGOClass::getWatch();

  //Initialize TWatch
  ttgo->begin();

  ttgo->openBL();

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
  attachInterrupt(
    BMA423_INT1, [] {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      EventBits_t bits = xEventGroupGetBitsFromISR(isr_group);
      if (bits & WATCH_FLAG_SLEEP_MODE) {
        // Use an XEvent when waking from low energy sleep mode.
        xEventGroupSetBitsFromISR(isr_group, WATCH_FLAG_SLEEP_EXIT | WATCH_FLAG_BMA_IRQ, &xHigherPriorityTaskWoken);
      } else {
        // Use the XQueue mechanism when we are already awake.
        uint8_t data = Q_EVENT_BMA_INT;
        xQueueSendFromISR(g_event_queue_handle, &data, &xHigherPriorityTaskWoken);
      }

      if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
      }
    },
    RISING);

  // Connection interrupted to the specified pin
  pinMode(AXP202_INT, INPUT);
  attachInterrupt(
    AXP202_INT, [] {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      EventBits_t bits = xEventGroupGetBitsFromISR(isr_group);
      if (bits & WATCH_FLAG_SLEEP_MODE) {
        // Use an XEvent when waking from low energy sleep mode.
        xEventGroupSetBitsFromISR(isr_group, WATCH_FLAG_SLEEP_EXIT | WATCH_FLAG_AXP_IRQ, &xHigherPriorityTaskWoken);
      } else {
        // Use the XQueue mechanism when we are already awake.
        uint8_t data = Q_EVENT_AXP_INT;
        xQueueSendFromISR(g_event_queue_handle, &data, &xHigherPriorityTaskWoken);
      }
      if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
      }
      irq = true;
    },
    FALLING);

  // Turn on the IRQ used
  ttgo->power->adc1Enable(AXP202_BATT_VOL_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1, AXP202_ON);
  ttgo->power->enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_CHARGING_FINISHED_IRQ, AXP202_ON);
  ttgo->power->clearIRQ();

  // Capture start time to check when we can start the wallet
  ttgo->tft->fillScreen(TFT_BLACK);
  int starttime = millis();
  int power_button_press = 0;
  while (millis() - starttime < 5000 && decoy == true) {
    if (irq) {
      irq = false;
      ttgo->power->readIRQ();
      if (ttgo->power->isPEKShortPressIRQ()) {
        power_button_press++;
        if (power_button_press == 3) {
          decoy = false;
        }
      }
      ttgo->power->clearIRQ();
    }
  }

  if (decoy == false) {
    // Display wallet
    startupWallet();
  } else {
    startupWatch();
  }
}

void loop() {


  static uint32_t start = 0;
  if (decoy) {
    loopWatch();

  } else {
    //portal.handleClient();
    lv_task_handler();
    delay(5);
  }
}
