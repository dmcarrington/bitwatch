/*
   Copyright (c) 2020 David Carrington

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
#define KEY_FILE "/thekey.txt"

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
String pubkey;

// Global variables read from config file
String pin;
String apPassword = "ToTheMoon1"; //default WiFi AP password
String seedphrase;
String psbt;

// PIN generation / entry UI elements
String pincode = "";
String obfuscated = "";
lv_obj_t *label1 = NULL;

// Transaction signing
lv_obj_t *txinfo = NULL;
lv_obj_t *signTxBtns = NULL;
ElectrumTx tx;

// Wallet reset UI
lv_obj_t *resetBtns = NULL;
lv_obj_t *resetInfo = NULL;
lv_obj_t *main_menu = NULL;




// custom access point pages
static const char PAGE_RESTORE[] PROGMEM = R"(
)";

static const char PAGE_INIT[] PROGMEM = R"(
  "uri": "/newWallet",
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
      "value": "1234"
    },
    {
      "name": "password",
      "type": "ACInput",
      "label": "Password for Bitwatch AP WiFi",
      "value": "ToTheMoon1"
    },
    {
      "name": "seedphrase",
      "type": "ACInput",
      "label": "Wallet Seed Phrase",
      "value": "seedphrase"
    },
    {
      "name": "load",
      "type": "ACSubmit",
      "value": "Load",
      "uri": "/newWallet"
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
      "uri": "/newWallet"
    }
  ]
}
)";

static const char PAGE_VIEWWALLET[] PROGMEM = R"(
)";

static const char PAGE_SUBMITTX[] PROGMEM = R"(
)";

static const char PAGE_SIGNEDTX[] PROGMEM = R"(
)";

// portal and config
WebServerClass server;
AutoConnect portal(server);
AutoConnectConfig config;
AutoConnectAux restoreAux;
AutoConnectAux initAux;
AutoConnectAux saveWalletAux;
AutoConnectAux viewWalletAux;
AutoConnectAux submittxAux;
AutoConnectAux signedtxAux;

//========================================================================
// Start a wireless access point for us to connect to to read our files
//========================================================================
/*void startWebserver(void) {
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
}*/

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
// Show seed words
//========================================================================
/*void showseed() {
  ttgo->tft->fillScreen(TFT_BLACK);
  ttgo->tft->setCursor(0, 20);
  ttgo->tft->setTextSize(3);
  ttgo->tft->setTextColor(TFT_GREEN);
  ttgo->tft->println("  SHOW SEED");
  ttgo->tft->println("");
  ttgo->tft->setTextColor(TFT_BLUE);
  ttgo->tft->setTextSize(2);
  ttgo->tft->println(savedseed);
  delay(30000);
}*/

//========================================================================
// Show ZPUB as QR code and export to SPIFFS
//========================================================================
void exportZpub() {
  int str_len = pubkey.length() + 1;
  char char_array[str_len];
  pubkey.toCharArray(char_array, str_len);
  fs::File file = FlashFS.open("/bitwatch.txt", FILE_WRITE);
  file.print(char_array);
  file.close();

  ttgo->tft->fillScreen(TFT_BLACK);
  showAddress(pubkey);
  ttgo->tft->setTextColor(TFT_BLACK);
  ttgo->tft->setCursor(0, 0);
  ttgo->tft->setTextSize(2);
  ttgo->tft->println(" EXPORT ZPUB");

  ttgo->tft->setTextSize(1);
  ttgo->tft->setCursor(0, 220);
  //String msg = "Saved to \n" + ipaddress + "/bitwatch.txt";
  //ttgo->tft->println(msg.c_str());
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
// Repond to confirming a transaction, or cancel signing
//========================================================================
static void sign_tx_event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    if (btn_index == 0) {
      // "sign" button pressed
      lv_obj_del(txinfo);
      lv_obj_del(signTxBtns);

      Serial.println(seedphrase);
      HDPrivateKey hd(seedphrase, pin);
      HDPrivateKey account = hd.derive("m/84'/0'/0'/");
      Serial.println(account);

      tx.sign(account);
      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 20);
      ttgo->tft->setTextSize(2);

      String signedtx = tx;
      Serial.println("Signedtx: ");
      Serial.print(signedtx);
      int str_len = signedtx.length() + 1;
      char char_array[str_len];
      signedtx.toCharArray(char_array, str_len);
      fs::File file = FlashFS.open("/bitwatch.txt", FILE_WRITE);
      file.println(signedtx.c_str());
      file.close();

      ttgo->tft->fillScreen(TFT_BLACK);
      ttgo->tft->setCursor(0, 100);
      ttgo->tft->setTextSize(2);

      //String msg = "Saved to \n" + ipaddress + "/bitwatch.txt";
      //ttgo->tft->println(msg.c_str());
      //ttgo->tft->println("");
      //lv_obj_set_pos(main_menu, 0, 0);
      delay(3000);

    } else if (btn_index == 1) {
      // "cancel button pressed
      lv_obj_del(txinfo);
      lv_obj_del(signTxBtns);
      lv_obj_set_pos(main_menu, 0, 0);
    }
  }
}

static const char *sign_tx_map[] = { "Sign", "Cancel", "" };

//========================================================================
// Submenu for signing a transaction
//========================================================================
void signTransaction() {
  //if (sdcommand.substring(0, 4) == "SIGN") {
    String eltx = psbt; //sdcommand.substring(5, sdcommand.length() + 1);
    Serial.println(eltx);

    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setCursor(0, 20);
    ttgo->tft->setTextSize(3);
    ttgo->tft->setTextColor(TFT_GREEN);
    ttgo->tft->println("");
    ttgo->tft->setCursor(0, 90);
    ttgo->tft->println(" Bwahahahaha!");
    ttgo->tft->println("");
    ttgo->tft->println(" Transaction");
    ttgo->tft->println(" found");

    delay(3000);

    // hide the main menu
    lv_obj_set_pos(main_menu, -500, 0);

    int len_parsed = tx.parse(eltx);
    if (len_parsed == 0) {
      ttgo->tft->println("Can't parse tx");
      delay(3000);
      lv_obj_set_pos(main_menu, 0, 0);
      Serial.println("no valid transaction found");
      return;
    }
    String txnLabel = "";
    Serial.println(tx.toString());
    for (int i = 0; i < tx.tx.outputsNumber; i++) {
      txnLabel = txnLabel + (tx.tx.txOuts[i].address()) + "\n-> ";
      // Serial can't print uint64_t, so convert to int
      txnLabel = txnLabel + (int(tx.tx.txOuts[i].amount));
      txnLabel = txnLabel + " sat\n";
    }
    Serial.println(txnLabel);
    txinfo = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(txinfo, NULL, LV_ALIGN_CENTER, -100, -100);
    lv_label_set_text(txinfo, txnLabel.c_str());

    signTxBtns = lv_btnmatrix_create(lv_scr_act(), NULL);
    lv_btnmatrix_set_map(signTxBtns, sign_tx_map);
    lv_obj_set_size(signTxBtns, 240, 40);
    lv_obj_align(signTxBtns, NULL, LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_event_cb(signTxBtns, sign_tx_event_handler);
  //}
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
// handle restore confirmation
//========================================================================
/*static void restore_btn_event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    unsigned int btn_index = lv_btnmatrix_get_active_btn(obj);
    if (btn_index == 0) {
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
      fs::File file = SPIFFS.open("/key.txt", FILE_WRITE);
      file.print(theSeed + "\n");
      file.close();
      fs::File outfile = SPIFFS.open("/bitwatch.txt", FILE_WRITE);
      outfile.print("");
      outfile.close();
      lv_obj_set_pos(main_menu, 0, 0);
    } else if (btn_index == 1) {
      Serial.println("restore cancelled");
      // cancel reset
      lv_obj_del(restoreBtns);
      lv_obj_del(restoreInfo);
      lv_obj_set_pos(main_menu, 0, 0);
    }
  }
}

static const char *restore_btn_map[] = { "Restore", "Cancel", "" };*/

//========================================================================
// Sub menu to confirm restore
//========================================================================
/*void confirmRestoreFromSeed() {
  lv_obj_set_pos(main_menu, -500, 0);
  restoreInfo = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(restoreInfo, NULL, LV_ALIGN_CENTER, -60, -60);
  lv_label_set_text(restoreInfo, "Device will be wiped\nthen restored from seed,\nare you sure?");

  restoreBtns = lv_btnmatrix_create(lv_scr_act(), NULL);
  lv_btnmatrix_set_map(restoreBtns, reset_btn_map);
  lv_obj_set_size(restoreBtns, 240, 40);
  lv_obj_align(restoreBtns, NULL, LV_ALIGN_CENTER, 0, 100);
  lv_obj_set_event_cb(restoreBtns, restore_btn_event_handler);
}*/

//========================================================================
// attempt to restore from phrase stored in SPIFFS
//========================================================================
void restoreFromSeed() {
  /*if (sdcommand.substring(0, 7) == "RESTORE") {
    theSeed = sdcommand.substring(8, sdcommand.length());
    confirmRestoreFromSeed();
  } else {*/
    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setTextSize(2);
    ttgo->tft->setCursor(0, 90);
    ttgo->tft->setTextColor(TFT_RED);
    ttgo->tft->println("'RESTORE *seed phrase*' not found on SPIFFS");
  /*  delay(3000);
  }*/
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
        Serial.println("Show Seed");
        //showseed();
        break;
      case (4):
        Serial.println("Wipe Device");
        confirmReset();
        break;
      case (5):
        Serial.println("Restore from seed");
        restoreFromSeed();
        break;
      case (6):
        Serial.println("Restart");
        esp_restart();
        break;
    }
  }
}

static const char *menu_map[] = { "Display Pubkey", "\n",
                                  "Sign Transaction", "\n",
                                  "Export ZPUB", "\n",
                                  "Show Seed", "\n",
                                  "Wipe Device", "\n",
                                  "Restore From Seed", "\n",
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
/*void pinmaker() {
  ttgo->tft->fillScreen(TFT_BLACK);
  ttgo->tft->setCursor(0, 90);
  ttgo->tft->setTextColor(TFT_GREEN);
  ttgo->tft->println("  Enter pin using");
  ttgo->tft->println("  keypad,");
  delay(6000);
  enterpin(true);
}*/

//========================================================================
// On each button press, generate the next of 24 seed words.
// When completed, save to seed file in FFAT and SPIFFS partitions
//========================================================================
/*static void seedmaker_cb(lv_obj_t *obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    if (seedCount <= 23) {
      seedCount++;
      seedWord = seedwords[random(0, 2047)];
      lv_label_set_text(seedLabel, seedWord.c_str());
      seedgeneratestr += " " + seedWord;
      Serial.println(seedgeneratestr);
      wordCount = "Word " + String(seedCount);
      lv_label_set_text(wordLabel, wordCount.c_str());
    } else {
      if (seed_done) {
        lv_obj_del(seedLabel);
        lv_obj_del(wordLabel);
        lv_obj_del(seedBtn);
        pinmaker();
      } else {
        fs::File file = SPIFFS.open("/key.txt", FILE_WRITE);
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
}*/

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
// Loop over a set of 24 randomly-generated seed words
//========================================================================
void seedmaker() {
  /*ttgo->tft->fillScreen(TFT_BLACK);
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

  seedWord = seedwords[random(0, 2047)];
  seedgeneratestr = seedWord;
  seedLabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(seedLabel, NULL, LV_ALIGN_CENTER, 0, -20);
  lv_label_set_text(seedLabel, seedWord.c_str());

  wordLabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(wordLabel, NULL, LV_ALIGN_CENTER, 0, -60);
  lv_label_set_text(wordLabel, "Word 1");*/
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

  StaticJsonDocument<512> doc;
  doc["pin"] = "1234";
  doc["password"] = "ToTheMoon1";
  doc["seedphrase"] = seedphrase;
  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  Serial.println("written seed phrase to file");


  file.close();

  //printFile(PARAM_FILE);
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
  /*bool set = setPin;
  if (set == true) {
    uint8_t newpasskeyresult[32];
    sha256(passkey, newpasskeyresult);
    hashed = toHex(newpasskeyresult, 32);

    fs::File file = SPIFFS.open("/pass.txt", FILE_WRITE);
    file.print(hashed + "\n");
    file.close();
  }

  fs::File otherfile = SPIFFS.open("/pass.txt");
  savedpinhash = otherfile.readStringUntil('\n');
  otherfile.close();

  uint8_t passkeyresult[32];
  sha256(passkey, passkeyresult);
  hashed = toHex(passkeyresult, 32);
  Serial.println(savedpinhash);
  Serial.println(hashed);*/

  if (/*savedpinhash == hashed || set == true*/ pincode == pin) {
    Serial.println("PIN ok");
    getKeys(seedphrase, pin);
    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setCursor(0, 110);
    ttgo->tft->setTextSize(2);
    ttgo->tft->setTextColor(TFT_GREEN);
    ttgo->tft->print(" Wallet Loaded!");
    //startWebserver();
    delay(1000);
    lv_obj_del(pinPad);
    lv_obj_del(pinTitle);
    lv_obj_del(label1);
    menu_matrix();
    return;
  } else /*if (savedpinhash != hashed && set == false)*/ {
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
void passcode_matrix(void) {
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
void enterpin() {
  passcode_matrix();
}

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

//=======================================================================

void startupWallet() {
  //FlashFS.begin(FORMAT_ON_FAIL);
  if(!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }

  bool needInit = true;

  // get the saved details and store in global variables
  File paramFile = FlashFS.open(PARAM_FILE, "r");
  Serial.println("reading PARAM_FILE");
  
  if (paramFile)
  {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, paramFile.readString());
    if(error) 
    {
      Serial.println("Error decoding param file, will recreate");
    } else {
      printFile(PARAM_FILE);
      const JsonObject pinRoot = doc["pin"];
      char pinChar[64];
      strlcpy(pinChar, doc["pin"], sizeof(pinChar));
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
  
      const JsonObject maRoot = doc["seedphrase"];
      char seedphraseChar[2500];
      strlcpy(seedphraseChar, doc["seedphrase"], sizeof(seedphraseChar));
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
      }

      if (portal.where() == "/newWallet")
      {
        aux.loadElement(param, {"pin", "password", "seedphrase"});
        param.close();
      }

      return String();
    });

    saveWalletAux.load(FPSTR(PAGE_SAVEWALLET));
    saveWalletAux.on([](AutoConnectAux &aux, PageArgument &arg) {
      aux["caption"].value = PARAM_FILE;
      File param = FlashFS.open(PARAM_FILE, "w");

      if (param)
      {
        // save as a loadable set for parameters.
        initAux.saveElement(param, {"pin", "password"});
        param.close();

        // read the saved elements again to display.
        param = FlashFS.open(PARAM_FILE, "r");
        aux["echo"].value = param.readString();
        param.close();
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

    portal.join({initAux, saveWalletAux});
    portal.config(config);
    portal.begin();
    while (true)
    {
      portal.handleClient();
    }
  } else {
    enterpin();
  }

  /*bool haveKey = true;
  bool haveCommand = true;*/

  //Checks if the user has an account or is forcing a reset
  /*haveKey = SPIFFS.exists("/key.txt");
  if (haveKey) {
    Serial.println("Key file exists:");
    fs::File keyfile = SPIFFS.open("/key.txt", FILE_READ);
    savedseed = keyfile.readStringUntil('\n');
    Serial.println(savedseed);
    keyfile.close();
  }

  // Extract any command we have been given in the command file
  haveCommand = SPIFFS.exists("/bitwatch.txt");
  if (haveCommand) {
    fs::File commandfile = SPIFFS.open("/bitwatch.txt", FILE_READ);
    sdcommand = commandfile.readStringUntil('\n');
    Serial.println(sdcommand);
    commandfile.close();
  }

  if (sdcommand == "HARD RESET") {
    seedmaker();
  } else if (sdcommand.substring(0, 7) == "RESTORE") {
    restoreFromSeed();
    enterpin(true);
  } else {
    enterpin(false);
  }*/
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
    /*WiFiClient client = server.available();  // listen for incoming clients

    if (client) {                     // if you get a client,
      Serial.println("New Client.");  // print a message out the serial port
      String currentLine = "";        // make a String to hold incoming data from the client
      while (client.connected()) {    // loop while the client's connected
        if (client.available()) {     // if there's bytes to read from the client,
          char c = client.read();     // read a byte, then
          Serial.write(c);            // print it out the serial monitor
          if (c == '\n') {            // if the byte is a newline character

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
    }*/

    lv_task_handler();
    delay(5);
  }
}
