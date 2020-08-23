A basic bitcoin wallet to run on the LILYGO T-Watch 2020

Inspired by Bowser (https://github.com/arcbtc/bowser-bitcoin-hardware-wallet)

T-Watch 2020 has no SD card, so use SPIFFS with watch-hosted webserver(!) to retrive PSBT. Post transaction to server to initiate wallet operations.
More secure option - write operations via SPIFFS directly (https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/spiffs.html)

COMMANDS
Wipe device completely, setup new wallet:
HARD RESET

Restore from a seed backup:
RESTORE husband behind shallow promote....

Sign an Electrum transaction:
SIGN 45505446ff00020000000001016cb....

Tap user button 3x to initiate wallet, otherwise display simple watch interface.