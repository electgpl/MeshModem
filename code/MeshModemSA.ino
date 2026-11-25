/* ============================================================================
   ELECTGPL — MeshModemSA v3.9  (Mesh_Modem_Web_v3_TCXO_BLE v3.7 + spectrum analyzer + diagnostics)
   LoRa node compatible with the Meshtastic® network, with rebroadcasting (hops),
   web chat, AT commands and a LoRa spectrum analyzer on the same web server
   ----------------------------------------------------------------------------
   SPECTRUM ANALYZER (http://<ip>/spec , AT+SPEC)
     An extra tool. The modem is still exactly v3.7: everything related to the
     mesh is unchanged while the analyzer is off (power-on state).
     EXCLUSIVE mode: the SX1262 is a single receiver. While sweeping, the node does
     NOT receive, transmit or rebroadcast mesh packets. On entry the rebroadcast
     queue is dropped; NodeInfo requests made meanwhile stay pending and are sent
     on return. On exit the radio is re-initialized with the same sequence as
     setup() (reset through RST + begin), so it returns to the power-on state.
     Measurement (same as LoRaSA_ESP32S3_SX1262): modem in GFSK, the channel
     filter (RxBw 4.8 to 467 kHz) is the RBW; per point SetRfFrequency -> SetRx
     continuous -> settling -> N x GetRssiInst. Peak detector and average in
     linear power. The TCXO on DIO3 and the RF switch on DIO2 are preserved.
     Returns to the mesh by itself: with AT+SPEC=0, when the page is closed, after
     SA_LEASE_MS without data requests from the web, or when SA_MAX_MS expires.
     The sweep runs in slices of SA_SLICE_US per loop() pass: web, BLE and UART
     keep responding during the measurement.
     /api/spec returns binary data (32 B header + peak + average, 1 B per
     point = -2*P[dBm]). The peak is the maximum accumulated since the previous
     read: bursts are not lost even if the page polls more slowly.
     Noise floor ~ -174 + 10log(RBW) + NF: with RBW 117 kHz ~ -117 dBm. Nodes the
     modem decodes below that floor (SNR < 0) are not visible.
   ----------------------------------------------------------------------------
   Board  : ESP32 LoRa Zero rev 1.0 (ELECTGPL, Raspberry Pi Zero form factor)
            ESP32-S3 without internal flash or PSRAM + external GD25Q128 flash (16 MB).
            SX1262 with 32 MHz TCXO powered from DIO3 at 1.8 V, SX1262 DC-DC regulator (L1 15 uH),
            BGS12WN6E6327 switch driven by DIO2 (ANT_SW_CTRL), IPD915 filter, U.FL.
            SX1262: NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14
            Power: USB-C or 5 V from the 2x20 header (ORed by LM66200) -> buck-boost
            TPS631000 to 3.3 V. No battery, no fuel gauge and no GPIO LEDs
            (D2 is the power LED, always on from 3.3 V). BOOT (GPIO0) and RESET buttons.
            Assembled PCBA: TPS2116 instead of the LM66200 and TG2520SMN TCXO instead of
            the X1G0054210205 in the schematic.
   IDE    : ESP32S3 Dev Module · Serial 115200 8N1 · line ending CR, LF or CR+LF
            Terminal over the native USB-C: "USB CDC On Boot: Enabled"; with
            "Disabled", the serial port is U0TXD/U0RXD (GPIO43/44) on the 2x20 header.
   Libs   : RadioLib 7.x or 8.x (the only external one) + WiFi, WebServer, DNSServer,
            ESPmDNS, Preferences and LittleFS (all from the ESP32 core). Own AES-128.
   Flash  : 16 MB (see BLUETOOTH LE for the partition scheme); the message log uses
            the data partition with LittleFS.
   Compat.: Meshtastic 2.x, default primary channel (public PSK "AQ=="),
            text broadcast + NodeInfo. Rebroadcasts mesh packets following the
            rules of the official firmware (managed flooding), in 3 modes.

   BLUETOOTH LE (Nordic UART Service)
     Third interface, besides UART and web: the same AT commands and the same
     responses (+RX, +NODE, +ECO...). Works without WiFi. Name "MeshModem-XXXX".
     Works with nRF Connect, Serial Bluetooth Terminal, Bluefruit Connect, a Web
     Bluetooth page (Chrome) or Python with bleak. Writing requires pairing with
     the BLE_PIN PIN (or AT+BLEPIN). Terminate each command with CR or LF.
     Arduino IDE: Flash Size 16MB, PSRAM Disabled (the Zero has no PSRAM) and one of these two options:
       a) Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)", without partitions.csv in the folder
       b) Partition Scheme "Custom" with partitions.csv (2 x 4 MB app, ~7.9 MB of message log)
     The message log mounts whichever partition exists ("spiffs" or "ffat").

   WIFI: OWN NETWORK FOR USE IN THE FIELD (AT+WIFIMODE)
     AUTO (default): tries the home network; if it does not connect within 20 s,
          it brings up its own network "MeshModem-XXXX" (WPA2, password AP_PASS /
          AT+APPASS) with the full chat at http://192.168.4.1 . No internet needed:
          the phone keeps using mobile data. /setup lets you enter the home network.
     STA: home network only (without credentials: configuration portal).
     AP : always its own network.
     Without NTP, the time is taken from the browser or the BLE chat on connection.

   WIFI
     With no saved credentials, or holding BOOT (GPIO0) for 5 s, it brings up the
     captive portal: open network "MeshModem-XXXX", page at http://192.168.4.1
     On the home network: http://meshmodem.local or the IP reported by ATI.
     Everything that happens on the web is mirrored on the serial terminal and vice versa.

   ECHOES (implicit ACK)
     When another node rebroadcasts one of our messages, the modem hears the echo
     (same from and id, with the relay_node of the repeater). It is reported as
     +ECO:id,relay=XX,name,rssi,snr,hop_limit and on the web next to the message:
     "relayed by N". hop_limit = HOP-1 means a direct neighbor; lower means one hop further.
     RETRIES (AT+RETRY, default 2): like ReliableRouter in the official firmware (3 attempts
     for a broadcast), an own text that gets no echo is sent again: the SAME frame, same id,
     after the getRetransmissionMsec() time (2 x airtime + contention + 4.5 s, ~5 s on
     MediumFast). Nodes that already have it drop it as a duplicate, so nobody sees it twice.
     The first echo cancels the remaining attempts. Texts go out with want_ack = 1, as the
     official app sends them (a broadcast with want_ack is never ACKed: the echo is the ACK).
     Each retry: +RETX:id,k/N. Without echoes after the last attempt: +ECO:id,0 ("no relay heard").

   DIAGNOSTICS (AT+DIAG)
     Counters since boot or since AT+DIAG=0, without leaving the mesh:
     - Noise floor: GetRssiInst sampled every 50 ms while in RX, P10 and P50 of the
       histogram (P10 ~ floor without traffic). Use it to compare conditions (PC USB vs
       power bank, antenna, place); the absolute value carries the SX1262 RSSI tolerance.
     - CHUTIL: % of the last minute with the channel busy (RX frames + own TX), TXUTIL: %
       of the last hour transmitting. Same definitions as AirTime in the official firmware.
     - RX: frames OK, CRC errors, damaged headers (many = demodulating at the limit),
       frames of other channels, duplicates.
     - SNR of valid frames (min/avg/max) and MARGIN = average SNR - SF limit (-2.5 x (SF - 4)).
     - TX: own packets, retries, how many got an echo, rebroadcasts.
     AT+NI=1 sends NodeInfo with want_response: nodes that hear it answer with their own
     NodeInfo addressed to this node (+HEARDBY). The official firmware throttles those
     replies (one per requester every 12 h, and not if it sent its own NodeInfo recently),
     so silence does not prove that nobody hears you.

   REBROADCAST (AT+RELAY)
     0 = mute (CLIENT_MUTE): does not rebroadcast.
     1 = client (CLIENT): rebroadcasts after an SNR-based delay and cancels if
         another node rebroadcasts first. This is the default.
     2 = router (ROUTER): always rebroadcasts, with priority. Only for a well
         placed node (height, good antenna).
     Rebroadcasts any packet with a valid CRC and hop_limit > 0, even if it cannot
     decrypt it (other channels, other nodes' DMs), like the firmware's ALL mode.

   MESSAGE LOG
     Received and sent messages are stored in flash (LittleFS) with date and time
     (NTP). They survive reboots and power loss. They are shown on the web, with
     AT+LOG, and erased with AT+LOGCLR. Every boot is logged with its reset reason.

   CONFIGURATION
     Changes (AT or web) take effect immediately. AT+SAVE stores them in flash
     (NVS); without AT+SAVE they are lost on reboot. AT&F restores the CONFIG block.

   AT COMMANDS
     AT                      Communication test
     AT+HELP                 This help
     ATI / AT+INFO?          Full configuration and current status
     AT+NODE=!aabbccdd       32-bit NodeNum in hex (8 digits)
     AT+NAME=long[,short]    Long name (<=39) and short name (<=4)
     AT+PRESET=?             List of presets
     AT+PRESET=MediumFast    Preset by name or number (0..8)
     AT+BAND=915,928         Band edges in MHz (ANZ 915,928 / US 902,928 /
                             EU_868 869.4,869.65 / BR_902 902,907.5)
     AT+FREQ=926.125         Manual frequency in MHz; AT+FREQ=0 = automatic slot
     AT+HOP=3                Max hops for packets originated by this node (0..7)
     AT+RELAY=1              Rebroadcast: 0 mute, 1 client, 2 router
     AT+PWR=20               TX power in dBm (-9..22)
     AT+SEND=text            Text broadcast on the primary channel (<=232 bytes)
     AT+NI                   Sends NodeInfo
     AT+NI=1                 NodeInfo asking for replies: who hears this node (+HEARDBY)
     AT+RETRY=0..3           Retransmissions of own texts without echo (stored; 0 = off)
     AT+RETRY?               Current value
     AT+DIAG / AT+DIAG=0     Link diagnostics / reset its counters
     AT+NODES                Heard nodes
     AT+SAVE                 Stores the radio configuration in flash
     AT+WIFI=ssid,password   Stores WiFi credentials and connects
     AT+WIFI?                WiFi status
     AT+WIFIMODE=AUTO|STA|AP WiFi mode (stored): home with fallback / home only / own network
     AT+APPASS=password      Own network password (8 to 63 characters; empty = open)
     AT+TIME=epoch           Sets the time (UTC seconds) when there is no NTP
     AT+PORTAL               Brings up the WiFi configuration portal
     AT+BLE=0|1  AT+BLE?     Bluetooth off / on (stored) / status
     AT+BLEPIN=123456        Pairing PIN (6 digits; 0 = no PIN), applied on reboot
     AT+SPEC=1|0  AT+SPEC?   Spectrum analyzer on (mesh suspended) / mesh / status
     AT+SPECCFG=f0,f1,step,rbw,n   Sweep: MHz, MHz, kHz, kHz, samples per point (1..64)
     AT+LOG=20               Last N log entries (AT+LOG = 20)
     AT+LOGCLR               Erases the message log
     AT+RAW=1                Trace of everything received (0 = off)
     AT&F                    CONFIG block values (erases the stored configuration)
     AT+RST                  Reboots the ESP32

   UNSOLICITED RESPONSES
     +RX:!from,!to,rssi,snr,hops,text         +NODE:!from,rssi,snr,short,long
     +TX:id     +ECO:id,relay=XX,name,rssi,snr,hop_limit     +ECO:id,0 (no relay heard)
     +RETX:id,k/N  retry k of N     +HEARDBY:!from,rssi,snr,short  NodeInfo addressed to this node
     +RAW:len,status,rssi,snr,hash[,!from,id,hop_limit/hop_start,next_hop,relay] (with AT+RAW=1)
     +RELAY:!from,id,hop_limit (with AT+RAW=1)     +WIFI:UP,ssid,url     +WIFI:DOWN     +WIFI:PORTAL,net,ip
     [web] command  /  [ble] command           command received from the web or over BLE
     +BLE:CONNECTED / +BLE:DISCONNECTED        Bluetooth connection changes
     +SPEC:ON,... / +SPEC:OFF,reason,RADIO=e   analyzer entry / exit
   ============================================================================ */
#include <RadioLib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "esp_partition.h"
#include <vector>
#include <time.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#if !defined(CONFIG_NIMBLE_ENABLED)
#error "This sketch uses the NimBLE stack (ESP32-S3 with arduino-esp32 3.x)"
#endif
#include <host/ble_hs.h>
// The BLE security API changed in arduino-esp32 3.3.1: BLESecurity::setPassKey() appears and
// BLEServerCallbacks::onPassKeyRequest() goes away. In 3.3.0, BLESecurity also fails to link.
#define BLE_SEC_NEW_API (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 1))

// ======================= CONFIG (factory defaults) ==========================
#define NODE_ID     0xE1EC1001UL       // Own NodeNum: unique per board, != 0 and != 0xFFFFFFFF
#define LONG_NAME   "HackMeshWeb"
#define SHORT_NAME  "HMW"
#define PRESET_IDX  3                  // 3 = MediumFast (see PRESETS[])
#define BAND_START  915.0f             // ANZ
#define BAND_END    928.0f
#define FREQ_MAN    0.0f               // 0 = automatic slot
#define HOP_LIMIT   3
#define TX_DBM      22                 // SX1262 maximum, as a typical Meshtastic node
#define RETRY_DEF   2                  // retries of own texts without echo (official: 3 attempts)
#define RELAY_MODE  1                  // 0 mute, 1 client, 2 router
#define HOSTNAME    "meshmodem"        // http://meshmodem.local
#define TZ_STR      "<-03>3"           // Argentina, UTC-3 without daylight saving time
#define PORTAL_HOLD_MS 5000            // BOOT held this long = WiFi portal
#define AP_PASS     "meshmodem"        // WPA2 password of the own network (>= 8 characters)
#define STA_TIMEOUT_MS 20000           // AUTO: time to join the home network before bringing up the own network
#define BLE_ENABLE  1                  // Bluetooth on by default
#define BLE_PIN     123456             // BLE pairing PIN (0 = no PIN, anyone can write)
#define LOG_MAX     (384UL * 1024UL)   // size per log file (rotated to .old)
// Spectrum analyzer
#define SA_LEASE_MS 20000UL            // no /api/spec requests for this long: back to the mesh
#define SA_MAX_MS   (30UL * 60UL * 1000UL)   // maximum length of an analyzer session
#define SA_DEF_STEP 50.0               // default sweep: the node's band (BAND), step [kHz]
#define SA_DEF_RBW  117.3f             // RBW [kHz] (snapped to the nearest valid value)
#define SA_DEF_NS   4                  // RSSI samples per point

// ======================= Hardware (ESP32 LoRa Zero rev 1.0) ==================
#define PIN_CS     8                   // NSS
#define PIN_SCK    9
#define PIN_MOSI  10
#define PIN_MISO  11
#define PIN_RST   12
#define PIN_BUSY  13
#define PIN_DIO1  14
// No PIN_RFSW: the RF switch is driven by the SX1262 through DIO2 (DIO2_RFSW true)
#define PIN_BOOT   0                   // BOOT button (SW1)
// The Zero has no fuel gauge, charger or GPIO LEDs. The battery and LED code inherited from
// the Batt variant is compiled but inactive with HAS_BATT/HAS_LEDS at 0: the I2C bus is not
// initialized and no GPIO is touched. Do not change these values on this board.
#define HAS_BATT   0
#define HAS_LEDS   0
#define PIN_SDA    4                   // unused with HAS_BATT 0
#define PIN_SCL    5
#define MAX17048_ADDR 0x36
#define BATT_PERIOD_MS 10000           // fuel gauge read period
#define BATT_LOW_PCT   15              // low battery warning
#define PIN_LED_RED   47               // unused with HAS_LEDS 0
#define PIN_LED_GREEN 48
#define PIN_PGOOD     -1               // unused: -1 = no power source detection
#define TCXO_V   1.8f                  // TCXO Y1 powered from DIO3 at 1.8 V (0 = crystal)
#define DIO2_RFSW true                 // DIO2 (ANT_SW_CTRL) drives the BGS12WN6E6327

// ======================= Meshtastic constants ===============================
const uint8_t PSK[16] = {0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,      // default PSK
                         0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01};     // ("AQ==")
struct Preset { const char* name; float bw; uint8_t sf, cr; };         // name = channel name
const Preset PRESETS[] = {
  {"ShortTurbo", 500, 7, 5}, {"ShortFast", 250, 7, 5}, {"ShortSlow", 250, 8, 5},
  {"MediumFast", 250, 9, 5}, {"MediumSlow", 250, 10, 5}, {"LongTurbo", 500, 11, 8},
  {"LongFast", 250, 11, 5}, {"LongMod", 125, 11, 8}, {"LongSlow", 125, 12, 8}};
const int NPRE = sizeof(PRESETS) / sizeof(*PRESETS);
#define PORT_TEXT     1
#define PORT_NODEINFO 4
#define NI_PERIOD_MS  (3UL * 3600UL * 1000UL)                          // NodeInfo every 3 h

const char HELP[] =
  "AT  AT+HELP  ATI (configuration and status)\r\n"
  "AT+NODE=!aabbccdd     NodeNum (8 hex digits)\r\n"
  "AT+NAME=long[,short]  long name (<=39) and short name (<=4)\r\n"
  "AT+PRESET=?|name|n    modem preset\r\n"
  "AT+BAND=f0,f1         region band in MHz (ANZ 915,928 / US 902,928)\r\n"
  "AT+FREQ=MHz|0         manual frequency / 0 = automatic slot\r\n"
  "AT+HOP=0..7           max hops\r\n"
  "AT+RELAY=0|1|2        rebroadcast: mute / client / router\r\n"
  "AT+PWR=-9..22         TX power in dBm\r\n"
  "AT+SEND=text          text broadcast\r\n"
  "AT+NI  AT+NODES       send NodeInfo / heard nodes\r\n"
  "AT+NI=1               NodeInfo asking for replies (who hears me)\r\n"
  "AT+RETRY=0..3  AT+RETRY?  retries of own texts without echo\r\n"
  "AT+DIAG  AT+DIAG=0    link diagnostics / reset counters\r\n"
  "AT+SAVE               store radio configuration in flash\r\n"
  "AT+WIFI=ssid,password WiFi credentials   AT+WIFI?  status\r\n"
  "AT+WIFIMODE=AUTO|STA|AP  home with fallback / home only / own network\r\n"
  "AT+APPASS=password    own network password (empty = open)\r\n"
  "AT+PORTAL             WiFi portal (or BOOT 5 s)\r\n"
  "AT+BLE=0|1  AT+BLE?   Bluetooth off / on / status\r\n"
  "AT+BLEPIN=nnnnnn      BLE PIN (0 = no PIN), applied on reboot\r\n"
  "AT+SPEC=1|0  AT+SPEC? spectrum analyzer (suspends the mesh) / status\r\n"
  "AT+SPECCFG=f0,f1,step,rbw,n  sweep: MHz,MHz,kHz,kHz,samples\r\n"
  "AT+LOG=n  AT+LOGCLR   message log / erase log\r\n"
  "AT+RAW=0|1            RX trace\r\n"
  "AT&F  AT+RST          factory defaults / reboot\r\n";

// ======================= State ==============================================
struct Cfg { uint32_t magic, node; char ln[40], sn[5]; uint8_t preset, hop; int8_t pwr; float f0, f1, fov; uint8_t relay; };
const Cfg DEF = {0x4D4D5733, NODE_ID, LONG_NAME, SHORT_NAME, PRESET_IDX, HOP_LIMIT, TX_DBM, BAND_START, BAND_END, FREQ_MAN, RELAY_MODE};
Cfg cfg = DEF;
struct NodeE { uint32_t id; char sn[5]; char ln[40]; float rssi, snr; uint32_t seen; };   // heard node
NodeE nodes[32];

SPIClass spiLora(FSPI);
SX1262 radio = new Module(PIN_CS, PIN_DIO1, PIN_RST, PIN_BUSY, spiLora);
uint32_t nCh, slot; uint8_t chHash; float freq;
uint64_t seen[32]; uint8_t seenIdx; bool rawMode;                  // duplicates: from<<32 | id
float slotMs;                                                        // contention slot (ms)

// Rebroadcast queue: raw packets waiting for their turn
struct Pend { bool used; uint32_t from, id, due; uint8_t len, buf[255]; };
Pend pend[6];
uint32_t stRelayed, stCanceled, stDropped;                           // rebroadcast statistics
uint8_t retryMax = RETRY_DEF;                                        // AT+RETRY (NVS "retry")

// Diagnostics (AT+DIAG): counters since boot or since AT+DIAG=0
struct Diag {
  uint32_t t0, rxOk, rxCrc, rxHdr, rxErr, rxForeign, rxDup;          // received frames
  uint32_t txOwn, txRetx, ownEcho, ownNoEcho, relay0;                // own transmissions
  uint32_t snrN; float snrMin, snrMax, snrSum;                       // SNR of valid frames
  uint32_t nfN, nf[256];                                             // GetRssiInst histogram, index = -2*dBm
} dg;
uint32_t cuBin[6], cuLast, txBin[60], txLast;                        // airtime: 6 x 10 s, 60 x 60 s
const char* RELAY_NAME[] = {"MUTE", "CLIENT", "ROUTER"};

// Spectrum analyzer: while specOn is set, the radio does not belong to the mesh
bool specOn = false;                                                 // analyzer mode active
bool niPending = false;                                              // NodeInfo requested during the analyzer
int radioErr = -1;                                                   // result of the radio initialization

Preferences nvs;
WebServer server(80);
DNSServer dns;
String wSsid, wPass;
bool portal = false, wifiUp = false;
bool apMode = false, staEverUp = false;                              // own network active / home network ever joined
uint8_t wifiMode = 0;                                                // 0 AUTO, 1 STA, 2 AP
const char* WMODE[] = {"AUTO", "STA", "AP"};
String apPass;
uint32_t tStaStart = 0;
uint32_t tWifiPending = 0;                                           // deferred reconnection (web)

// Single output: everything goes to the UART and, during a web request, also to the HTTP response
// BLE state shared with the output: the Tee copies the output into the notification buffer
volatile bool bleConn = false;                                       // changed by the NimBLE task
bool bleOn = true;
String bleOut;                                                       // output pending notification (loop only)
#define BLE_OUT_MAX 8192

class Tee : public Print {
public:
  String* cap = nullptr;
  size_t write(uint8_t c) override {
    Serial.write(c); if (cap) *cap += (char)c;
    if (bleConn && bleOut.length() < BLE_OUT_MAX) bleOut += (char)c;
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    Serial.write(b, n); if (cap) cap->concat((const char*)b, n);
    if (bleConn && bleOut.length() + n <= BLE_OUT_MAX) bleOut.concat((const char*)b, n);
    return n;
  }
} io;

// ======================= Events for the web (ring buffer) ===================
#define NEV 48
String evBuf[NEV]; uint32_t evSeq = 0;

uint32_t nowTs() { time_t t = time(nullptr); return t > 1700000000 ? (uint32_t)t : 0; }

String jStr(const char* s, size_t n) {                               // escaped JSON string
  String o = "\""; o.reserve(n + 8);
  for (size_t i = 0; i < n; i++) {
    uint8_t c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o + "\"";
}
String jStr(const char* s) { return jStr(s, strlen(s)); }
String hexId(uint32_t id) { char b[10]; snprintf(b, sizeof(b), "!%08x", (unsigned)id); return b; }

void pushEv(const String& body) {
  evBuf[evSeq % NEV] = "{\"s\":" + String(evSeq) + ",\"ts\":" + String(nowTs()) +
                       ",\"up\":" + String(millis() / 1000) + "," + body + "}";
  evSeq++;
}
void sysEv(const String& txt) { pushEv("\"t\":\"sys\",\"txt\":" + jStr(txt.c_str())); }

// ======================= Message log in flash (LittleFS) ====================
#define LOG_FILE "/log.jsonl"                                        // one JSON line per event
#define LOG_OLD  "/log.old"
bool fsOk = false;

void logWrite(const String& line) {
  if (!fsOk) return;
  File f = LittleFS.open(LOG_FILE, "a");
  if (!f) return;
  f.print(line); f.print('\n');
  size_t sz = f.size(); f.close();
  if (sz > LOG_MAX) { LittleFS.remove(LOG_OLD); LittleFS.rename(LOG_FILE, LOG_OLD); }   // rotation
}

void logEv(const String& body) { pushEv(body); logWrite(evBuf[(evSeq - 1) % NEV]); }   // event + flash

// Last valid lines of a file (reads at most the last 'win' bytes)
void readTail(const char* path, size_t win, std::vector<String>& out) {
  File f = LittleFS.open(path, "r");
  if (!f) return;
  size_t sz = f.size(), start = sz > win ? sz - win : 0;
  f.seek(start);
  String buf = f.readString(); f.close();
  int a = 0;
  if (start) { a = buf.indexOf('\n') + 1; if (a <= 0) return; }        // drops the truncated line
  while (a < (int)buf.length()) {
    int b = buf.indexOf('\n', a); if (b < 0) b = buf.length();
    String l = buf.substring(a, b); l.trim();
    if (l.startsWith("{") && l.endsWith("}")) out.push_back(l);      // skips lines damaged by a power cut
    a = b + 1;
  }
}

void logTail(size_t n, std::vector<String>& out) {
  readTail(LOG_FILE, 24 * 1024, out);
  if (out.size() < n) { std::vector<String> old; readTail(LOG_OLD, 24 * 1024, old); out.insert(out.begin(), old.begin(), old.end()); }
  if (out.size() > n) out.erase(out.begin(), out.end() - n);
}

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "software reset";
    case ESP_RST_PANIC:    return "exception (panic)";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_USB:      return "USB";
    case ESP_RST_DEEPSLEEP:return "deep sleep";
    default:               return "unknown";
  }
}

// ======================= Heard nodes table ==================================
NodeE* nodeGet(uint32_t id) {
  NodeE* old = &nodes[0];
  for (auto& n : nodes) { if (n.id == id) return &n; if (n.seen < old->seen) old = &n; }
  memset(old, 0, sizeof(NodeE)); old->id = id;                       // replaces the oldest one
  return old;
}

// ======================= AES-128 (encryption only; CTR needs no decryption) =
const uint8_t SBOX[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
uint8_t rk[176];                                                     // 11 round keys

uint8_t xt(uint8_t x) { return (x << 1) ^ ((x >> 7) * 0x1B); }       // x*2 in GF(2^8)

void aesKeyExpand(const uint8_t* key) {                              // FIPS-197 §5.2
  memcpy(rk, key, 16);
  for (int i = 16, rcon = 1; i < 176; i += 4) {
    uint8_t t[4]; memcpy(t, rk + i - 4, 4);
    if (i % 16 == 0) {                                               // RotWord + SubWord + Rcon
      uint8_t u = t[0];
      t[0] = SBOX[t[1]] ^ rcon; t[1] = SBOX[t[2]]; t[2] = SBOX[t[3]]; t[3] = SBOX[u];
      rcon = xt(rcon);
    }
    for (int j = 0; j < 4; j++) rk[i + j] = rk[i + j - 16] ^ t[j];
  }
}

void aesBlock(uint8_t* s) {                                          // encrypts 16 bytes in place
  for (int j = 0; j < 16; j++) s[j] ^= rk[j];
  for (int r = 1; r <= 10; r++) {
    uint8_t t[16];
    for (int c = 0; c < 4; c++)                                      // SubBytes + ShiftRows
      for (int w = 0; w < 4; w++) t[4 * c + w] = SBOX[s[4 * ((c + w) & 3) + w]];
    if (r < 10)
      for (int c = 0; c < 4; c++) {                                  // MixColumns
        uint8_t* a = t + 4 * c; uint8_t x = a[0] ^ a[1] ^ a[2] ^ a[3], a0 = a[0];
        a[0] ^= x ^ xt(a[0] ^ a[1]); a[1] ^= x ^ xt(a[1] ^ a[2]);
        a[2] ^= x ^ xt(a[2] ^ a[3]); a[3] ^= x ^ xt(a[3] ^ a0);
      }
    for (int j = 0; j < 16; j++) s[j] = t[j] ^ rk[16 * r + j];      // AddRoundKey
  }
}

// AES-CTR (encrypts and decrypts). Counter block = id(8 B LE) | from(4 B LE) | 0(4 B),
// incremented as a 128-bit big-endian integer (same as mbedtls_aes_crypt_ctr).
void ctr(uint8_t* d, size_t n, uint32_t id, uint32_t from) {
  uint8_t cb[16] = {0}, ks[16];
  memcpy(cb, &id, 4); memcpy(cb + 8, &from, 4);
  for (size_t i = 0; i < n; i++) {
    if ((i & 15) == 0) {
      memcpy(ks, cb, 16); aesBlock(ks);
      for (int k = 15; k >= 0 && ++cb[k] == 0; k--) {}
    }
    d[i] ^= ks[i & 15];
  }
}

// ======================= Channel and radio (same as RadioInterface.cpp) =====
uint32_t djb2(const char* s) { uint32_t h = 5381; while (*s) h = (h << 5) + h + (uint8_t)*s++; return h; }

void derive() {
  const Preset& p = PRESETS[cfg.preset];
  chHash = 0;
  for (const char* c = p.name; *c; c++) chHash ^= (uint8_t)*c;     // xorHash(name)
  for (uint8_t b : PSK) chHash ^= b;                                // ^ xorHash(PSK)
  nCh = (uint32_t)floorf((cfg.f1 - cfg.f0) / (p.bw / 1000.0f) + 1e-4f); if (!nCh) nCh = 1;
  slot = djb2(p.name) % nCh;
  freq = cfg.fov > 0 ? cfg.fov : cfg.f0 + p.bw / 2000.0f + slot * (p.bw / 1000.0f);
  // Slot = CAD (2 symbols + 0.5) + propagation, switching and MAC (7.6 ms): computeSlotTimeMsec()
  slotMs = 2.5f * (float)(1UL << p.sf) / p.bw + 7.6f;
}

// ======================= RadioLib 7.x / 8.x compatibility ==================
// RadioLib 8 changed the API: begin() takes a ConfigLoRa_t, and frequency and bandwidth
// change from MHz/kHz as float to Hz as uint32_t. These functions isolate the difference.
#if RADIOLIB_VERSION_MAJOR >= 8
int radioSetFreq(float mhz) { return radio.setFrequency((uint32_t)(mhz * 1e6f + 0.5f)); }
int radioSetBw(float khz)   { return radio.setBandwidth((uint32_t)(khz * 1000.0f + 0.5f)); }
RadioLibTCXOVoltage_t tcxoEnum(float v) {                            // TCXO voltage through DIO3
  if (v <= 0) return VoltageNone;
  const float t[] = {1.6f, 1.7f, 1.8f, 2.2f, 2.4f, 2.7f, 3.0f, 3.3f};
  int k = 0; for (int i = 1; i < 8; i++) if (fabsf(t[i] - v) < fabsf(t[k] - v)) k = i;
  return (RadioLibTCXOVoltage_t)k;                                   // Voltage1V6 = 0 ... Voltage3V3 = 7
}
int radioBegin(float mhz, float khz, uint8_t sf, uint8_t cr, int8_t pwr) {
  radio.tcxoVoltage = tcxoEnum(TCXO_V);
  radio.useRegulatorLDO = false;                                     // SX1262 DC-DC
  ConfigLoRa_t c;
  c.frequency = (uint32_t)(mhz * 1e6f + 0.5f); c.bandwidth = (uint32_t)(khz * 1000.0f + 0.5f);
  c.spreadingFactor = sf; c.codingRate = cr; c.syncWord = 0x2B; c.power = pwr; c.preambleLength = 16;
  return radio.begin(c);
}
#else
int radioSetFreq(float mhz) { return radio.setFrequency(mhz); }
int radioSetBw(float khz)   { return radio.setBandwidth(khz); }
int radioBegin(float mhz, float khz, uint8_t sf, uint8_t cr, int8_t pwr) {
  return radio.begin(mhz, khz, sf, cr, 0x2B, pwr, 16, TCXO_V);       // sync word 0x2B, preamble 16
}
#endif

int applyRadio() {
  derive();
  if (specOn) return 0;                                              // analyzer: applied when returning to the mesh
  const Preset& p = PRESETS[cfg.preset];
  int e = radio.standby();
  if (!e) e = radioSetFreq(freq);
  if (!e) e = radioSetBw(p.bw);
  if (!e) e = radio.setSpreadingFactor(p.sf);
  if (!e) e = radio.setCodingRate(p.cr);
  if (!e) e = radio.setOutputPower(cfg.pwr);
  radio.startReceive();
  return e;
}

// ======================= Minimal protobuf ===================================
size_t pbVarint(uint8_t* b, uint32_t v) { size_t k = 0; do { b[k] = v & 0x7F; v >>= 7; if (v) b[k] |= 0x80; k++; } while (v); return k; }

bool pbReadVarint(const uint8_t* b, size_t L, size_t& i, uint32_t& v) {
  v = 0;
  for (int sh = 0; i < L && sh < 70; sh += 7) {                    // accepts 64-bit varints (truncated)
    uint8_t c = b[i++]; if (sh < 32) v |= (uint32_t)(c & 0x7F) << sh;
    if (!(c & 0x80)) return true;
  }
  return false;
}

// Next field: f = field number, v = value (varint/fixed32) or length, s = data for wire type 2
bool pbNext(const uint8_t* b, size_t L, size_t& i, uint32_t& f, uint32_t& v, const uint8_t*& s) {
  uint32_t key; s = nullptr;
  if (i >= L || !pbReadVarint(b, L, i, key)) return false;
  f = key >> 3;
  switch (key & 7) {
    case 0: return pbReadVarint(b, L, i, v);
    case 2: if (!pbReadVarint(b, L, i, v) || i + v > L) return false; s = b + i; i += v; return true;
    case 5: if (i + 4 > L) return false; memcpy(&v, b + i, 4); i += 4; return true;
    case 1: if (i + 8 > L) return false; i += 8; return true;
    default: return false;
  }
}

// ======================= Battery (MAX17048): inactive on the Zero (HAS_BATT 0)
bool battOk = false;
float battSoc = 0, battV = 0, battRate = 0;                          // %, V, %/h

uint16_t maxRead16(uint8_t reg) {                                    // 16-bit registers, big-endian
  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom((uint8_t)MAX17048_ADDR, (uint8_t)2) != 2) return 0xFFFF;
  uint16_t hi = Wire.read();
  return (hi << 8) | Wire.read();
}

void battRead() {
  if (!battOk) return;
  uint16_t v = maxRead16(0x02), s = maxRead16(0x04), c = maxRead16(0x16);
  if (v == 0xFFFF || s == 0xFFFF) return;                            // failed read: keeps the previous value
  float prev = battSoc;
  battV    = v * 78.125e-6f;                                         // VCELL: 78.125 uV/LSB
  battSoc  = s / 256.0f;                                             // SOC: 1/256 %/LSB
  battRate = (int16_t)c * 0.208f;                                    // CRATE: 0.208 %/h per LSB, signed
  if (prev >= BATT_LOW_PCT && battSoc < BATT_LOW_PCT) sysEv("Low battery: " + String(battSoc, 0) + " %");
}

void battInit() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.beginTransmission(MAX17048_ADDR);
  battOk = Wire.endTransmission() == 0;
  if (battOk) { battSoc = 100; battRead(); }                         // 100 avoids a false warning at boot
}

// Power source. With /PGOOD it is immediate. Without /PGOOD the MAX17048 rate is used:
// on battery there is always discharge (around -2 to -5 %/h with the modem active); on USB the
// cell charges or stays still. The chip averages the rate, so the change takes minutes.
bool usbPower() {
  static bool usb = true;
  if (PIN_PGOOD >= 0) return digitalRead(PIN_PGOOD) == LOW;
  if (!battOk) return true;                                          // no fuel gauge: USB assumed
  if (usb && battRate < -1.5f) usb = false;                          // hysteresis between -1.5 and -0.8 %/h
  else if (!usb && battRate > -0.8f) usb = true;
  return usb;
}

void battStatus() {
  if (!battOk) { io.printf("+BATT:NOT_DETECTED,%s\r\n", usbPower() ? "USB" : "BATTERY"); return; }
  io.printf("+BATT:%.1f%%,%.3fV,%+.1f%%/h,%s,%s\r\n", min(battSoc, 100.0f), battV, battRate,
            battRate > 0.5f ? "CHARGING" : battRate < -0.5f ? "DISCHARGING" : "STABLE",
            usbPower() ? "USB" : "BATTERY");
}

// ======================= LEDs: inactive on the Zero (HAS_LEDS 0) ============
bool ledsOn = true;
uint16_t unread = 0;                                                 // unread text messages

// ======================= Echoes of own messages (implicit ACK) ==============
struct OwnTx {                                                       // own packet: echoes and retries
  uint32_t id, t, wait; uint8_t n, rb[8]; bool done;                 // rb = distinct relays that rebroadcast it
  uint8_t left, tries, len, frm[255];                                // retries left, attempts, frame as sent
};
OwnTx own[8]; uint8_t ownIdx; uint32_t lastTxId;
uint32_t tMsgLed = 0;                                                // arrival of the last message

void ledsLoop() {
  uint32_t t = millis();
  bool red = false, green = false;
  if (ledsOn) {
    red = usbPower() ? true : (t % 3000) < 60;                       // steady on USB, blink on battery
    if (tMsgLed && t - tMsgLed < 600) green = ((t - tMsgLed) / 100) % 2 == 0;   // 3 fast blinks
    else if (unread) green = (t % 2000) < 80;                        // reminder: unread messages
  }
  digitalWrite(PIN_LED_RED, red ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
}

// ======================= Airtime (AirTime.cpp in the official firmware) =====
uint32_t toaMs(size_t n) { return (uint32_t)((radio.getTimeOnAir(n) + 500) / 1000); }

void airRoll() {                                                     // clears the bins that expired
  uint32_t c = millis() / 10000, h = millis() / 60000;
  if (c - cuLast >= 6) memset(cuBin, 0, sizeof(cuBin)); else while (cuLast != c) cuBin[++cuLast % 6] = 0;
  if (h - txLast >= 60) memset(txBin, 0, sizeof(txBin)); else while (txLast != h) txBin[++txLast % 60] = 0;
  cuLast = c; txLast = h;
}

void airLog(uint32_t ms, bool tx) {                                  // every frame heard or sent counts as busy
  airRoll();
  cuBin[cuLast % 6] += ms;
  if (tx) txBin[txLast % 60] += ms;
}

float chUtil() { airRoll(); uint32_t s = 0; for (uint32_t v : cuBin) s += v; return s / 600.0f; }    // % of 60 s
float txUtil() { airRoll(); uint32_t s = 0; for (uint32_t v : txBin) s += v; return s / 36000.0f; }  // % of 1 h

// Wait for an echo before retrying: RadioInterface::getRetransmissionMsec()
//   2 x airtime + (2^CW + 2 x CWmax + 2^((CWmin + CWmax) / 2)) x slot + 4500 ms, CW from the channel use
uint32_t retxMs(size_t n) {
  long cw = constrain(map((long)chUtil(), 0, 100, 3, 8), 3, 8);
  return 2 * toaMs(n) + (uint32_t)(((1UL << cw) + 2 * 8 + (1UL << 5)) * slotMs) + 4500;
}

// ======================= TX =================================================
bool txRaw(uint8_t* pkt, size_t n) {                                 // CAD + transmission of a ready frame
  if (specOn) return false;                                          // analyzer active: no transmission
  for (int i = 0; i < 5 && radio.scanChannel() == RADIOLIB_LORA_DETECTED; i++)
    delay(100 + esp_random() % 400);
  int e = radio.transmit(pkt, n);
  radio.startReceive();
  if (e == RADIOLIB_ERR_NONE) airLog(toaMs(n), true);
  return e == RADIOLIB_ERR_NONE;
}

bool sendData(uint8_t port, const uint8_t* pl, size_t len, bool wantResp) {
  uint8_t pkt[256]; uint32_t to = 0xFFFFFFFF, id = esp_random() | 1;
  uint8_t* d = pkt + 16; size_t k = 0;
  d[k++] = 0x08; d[k++] = port;                                     // Data.portnum (field 1, varint)
  d[k++] = 0x12; k += pbVarint(d + k, len);                         // Data.payload (field 2, bytes)
  if (16 + k + len + (wantResp ? 2 : 0) > 255) return false;
  memcpy(d + k, pl, len); k += len;
  if (wantResp) { d[k++] = 0x18; d[k++] = 0x01; }                   // Data.want_response (field 3, varint)
  bool retry = port == PORT_TEXT && retryMax > 0;                    // only texts are retried, as in the app
  memcpy(pkt, &to, 4); memcpy(pkt + 4, &cfg.node, 4); memcpy(pkt + 8, &id, 4);
  pkt[12] = (cfg.hop << 5) | (retry ? 0x08 : 0) | cfg.hop;          // hop_start | want_ack | hop_limit
  pkt[13] = chHash; pkt[14] = 0; pkt[15] = cfg.node & 0xFF;         // channel hash, next_hop, relay_node
  ctr(d, k, id, cfg.node);
  seen[seenIdx++ & 31] = ((uint64_t)cfg.node << 32) | id;           // ignore the repeaters' echoes
  bool ok = txRaw(pkt, 16 + k);
  if (ok) {
    io.printf("+TX:%08x\r\n", (unsigned)id);
    lastTxId = id;
    OwnTx& o = own[ownIdx++ & 7]; o.id = id; o.t = millis(); o.n = 0; o.done = false;   // to recognize its echoes
    o.left = retry ? retryMax : 0; o.tries = 1; o.len = 16 + k; memcpy(o.frm, pkt, o.len);
    o.wait = retxMs(o.len);
    dg.txOwn++;
  }
  return ok;
}

// Own packets: retry while there is no echo, then report the result (once per packet)
void ownLoop() {
  for (auto& o : own) {
    if (!o.id || o.done) continue;
    uint32_t el = millis() - o.t, win = o.wait > 15000 ? o.wait : 15000;
    if (!o.n && o.left && el >= o.wait) {                            // no echo yet: same frame, same id
      o.left--; o.tries++; o.t = millis();
      if (txRaw(o.frm, o.len)) {
        dg.txRetx++;
        io.printf("+RETX:%08x,%u/%u\r\n", (unsigned)o.id, o.tries, o.tries + o.left);
        pushEv("\"t\":\"retx\",\"id\":\"" + String(o.id, HEX) + "\",\"k\":" + String(o.tries) +
               ",\"of\":" + String(o.tries + o.left));
      }
    } else if (el > win && (o.n || !o.left)) {
      o.done = true;
      if (o.n) dg.ownEcho++;
      else {
        dg.ownNoEcho++;
        io.printf("+ECO:%08x,0\r\n", (unsigned)o.id);
        pushEv("\"t\":\"noeco\",\"id\":\"" + String(o.id, HEX) + "\",\"k\":" + String(o.tries));
      }
    }
  }
}

bool sendNodeInfoReq(bool want) {                                   // protobuf User; want = want_response
  if (specOn) { niPending = true; return false; }                    // sent when returning to the mesh
  String id = hexId(cfg.node);
  uint8_t u[64]; size_t n = 0;
  auto str = [&](uint8_t tag, const char* s) { u[n++] = tag; u[n++] = strlen(s); memcpy(u + n, s, strlen(s)); n += strlen(s); };
  str(0x0A, id.c_str()); str(0x12, cfg.ln); str(0x1A, cfg.sn);      // id, long_name, short_name
  u[n++] = 0x28; u[n++] = 0xFF; u[n++] = 0x01;                      // hw_model = PRIVATE_HW (255)
  const uint8_t ROLE[] = {1, 0, 2};                                  // CLIENT_MUTE, CLIENT, ROUTER
  if (ROLE[cfg.relay]) { u[n++] = 0x38; u[n++] = ROLE[cfg.relay]; }  // role (field 7; CLIENT = 0 is omitted)
  u[n++] = 0x48; u[n++] = 0x01;                                      // is_unmessagable = true: no DMs
  return sendData(PORT_NODEINFO, u, n, want);
}

bool sendNodeInfo() { return sendNodeInfoReq(false); }

// ======================= Rebroadcast (managed flooding) =====================
// Same contention window as RadioInterface::getTxDelayMsecWeighted():
//   CW = map(SNR, -20..10 dB, 3..8)
//   client: 2*CWmax*slot + random(0, 2^CW)*slot   (low SNR = short wait)
//   router: random(0, 2*CW)*slot                  (always ahead of the clients)
void scheduleRelay(const uint8_t* p, size_t n, float snr, uint32_t from, uint32_t id) {
  Pend* q = nullptr;
  for (auto& x : pend) if (!x.used) { q = &x; break; }
  if (!q) { stDropped++; return; }                                   // queue full
  memcpy(q->buf, p, n); q->len = n; q->from = from; q->id = id;
  q->buf[12] = (p[12] & 0xF8) | ((p[12] & 0x07) - 1);                // hop_limit - 1 (hop_start and flags unchanged)
  q->buf[14] = 0;                                                    // next_hop: no preference
  q->buf[15] = cfg.node & 0xFF;                                      // relay_node: this node
  long cw = constrain(map((long)snr, -20, 10, 3, 8), 3, 9);
  uint32_t d = cfg.relay == 2 ? random(0, 2 * cw) * slotMs
                              : 2 * 8 * slotMs + random(0, 1L << cw) * slotMs;
  q->due = millis() + d; q->used = true;
}

void cancelRelay(uint32_t from, uint32_t id) {                       // another node already rebroadcast it
  if (cfg.relay != 1) return;                                        // the router does not cancel
  for (auto& x : pend) if (x.used && x.from == from && x.id == id) { x.used = false; stCanceled++; }
}

void serviceRelay() {
  if (specOn) return;
  for (auto& x : pend) {
    if (!x.used || (int32_t)(millis() - x.due) < 0) continue;
    x.used = false;
    if (txRaw(x.buf, x.len)) {
      stRelayed++;
      if (rawMode) io.printf("+RELAY:!%08x,%08x,%u\r\n", (unsigned)x.from, (unsigned)x.id, x.buf[12] & 7);
    }
    return;                                                          // one per loop pass
  }
}

// ======================= RX =================================================
void receive() {
  if (specOn) return;                                                // the radio belongs to the analyzer
  if (digitalRead(PIN_DIO1) != HIGH) return;                        // DIO1 high = end of RX
  uint8_t p[256]; size_t n = radio.getPacketLength(); if (n > 255) n = 255;
  int st = radio.readData(p, n); float rssi = radio.getRSSI(), snr = radio.getSNR();
  radio.startReceive();
  airLog(toaMs(n), false);
  bool good = st == RADIOLIB_ERR_NONE && n >= 16;
  if (rawMode) {
    io.printf("+RAW:%u,%d,%.0f,%.1f,%02X", (unsigned)n, st, rssi, snr, n > 13 ? p[13] : 0);
    if (good) {                                                      // clear header: who, hops, relay
      uint32_t f, i; memcpy(&f, p + 4, 4); memcpy(&i, p + 8, 4);
      io.printf(",!%08x,%08x,%u/%u,%02X,%02X", (unsigned)f, (unsigned)i, p[12] & 7, p[12] >> 5, p[14], p[15]);
    }
    io.print("\r\n");
  }
  if (!good) {
    if (st == RADIOLIB_ERR_CRC_MISMATCH) dg.rxCrc++;
    else if (st == RADIOLIB_ERR_LORA_HEADER_DAMAGED) dg.rxHdr++;
    else dg.rxErr++;
    return;
  }
  dg.rxOk++; dg.snrN++; dg.snrSum += snr;
  if (dg.snrN == 1 || snr < dg.snrMin) dg.snrMin = snr;
  if (dg.snrN == 1 || snr > dg.snrMax) dg.snrMax = snr;
  uint32_t to, from, id; memcpy(&to, p, 4); memcpy(&from, p + 4, 4); memcpy(&id, p + 8, 4);
  if (from == cfg.node) {                                            // echo: another node rebroadcast one of ours
    uint8_t rb = p[15], hlE = p[12] & 7;
    for (auto& o : own) if (o.id == id && rb != (cfg.node & 0xFF)) {
      for (uint8_t k = 0; k < o.n; k++) if (o.rb[k] == rb) return;   // that repeater was already counted
      if (o.n < sizeof(o.rb)) o.rb[o.n++] = rb;
      const char* who = "";                                          // name of the node whose NodeNum ends in rb
      for (auto& ne : nodes) if (ne.id && (ne.id & 0xFF) == rb && ne.sn[0]) { who = ne.sn; break; }
      io.printf("+ECO:%08x,relay=%02X,%s,%.0f,%.1f,%u\r\n", (unsigned)id, rb, who, rssi, snr, hlE);
      char b[8]; snprintf(b, sizeof(b), "%02X", rb);
      pushEv("\"t\":\"eco\",\"id\":\"" + String(id, HEX) + "\",\"rb\":\"" + b + "\",\"sn\":" + jStr(who) +
             ",\"n\":" + String(o.n) + ",\"hl\":" + String(hlE) + ",\"rssi\":" + String(rssi, 0));
    }
    return;
  }
  uint64_t key = ((uint64_t)from << 32) | id;
  for (uint64_t s : seen) if (s == key) { dg.rxDup++; cancelRelay(from, id); return; }   // duplicate: already rebroadcast
  seen[seenIdx++ & 31] = key;
  int hl = p[12] & 7, hs = p[12] >> 5, hops = hs ? hs - hl : -1;
  // Rebroadcast: like NextHopRouter::perhapsRebroadcast(). Applies to any channel (no decryption needed)
  if (cfg.relay && to != cfg.node && hl > 0 && id && (p[14] == 0 || p[14] == (cfg.node & 0xFF)))
    scheduleRelay(p, n, snr, from, id);
  if (p[13] != chHash) { dg.rxForeign++; return; }                   // other channel: only rebroadcast
  if (n < 20) return;
  uint8_t* d = p + 16; size_t L = n - 16; ctr(d, L, id, from);
  uint32_t port = 0, f, v; const uint8_t *s, *pl = nullptr; size_t pn = 0, i = 0;
  while (i < L) {
    if (!pbNext(d, L, i, f, v, s)) return;                          // different PSK or corrupt frame
    if (f == 1 && !s) port = v; else if (f == 2 && s) { pl = s; pn = v; }
  }
  if (!pl) return;
  NodeE* ne = nodeGet(from); ne->rssi = rssi; ne->snr = snr; ne->seen = millis() | 1;
  String meta = "\"from\":\"" + hexId(from) + "\",\"rssi\":" + String(rssi, 0) + ",\"snr\":" + String(snr, 1);
  if (port == PORT_TEXT) {
    unread++; tMsgLed = millis() | 1;                                // green LED
    io.printf("+RX:!%08x,!%08x,%.0f,%.1f,%d,", (unsigned)from, (unsigned)to, rssi, snr, hops);
    io.write(pl, pn); io.print("\r\n");
    char rb[3]; snprintf(rb, sizeof(rb), "%02X", p[15]);             // relay_node: last byte of who delivered it
    logEv("\"t\":\"rx\"," + meta + ",\"to\":\"" + hexId(to) + "\",\"hops\":" + String(hops) + ",\"rb\":\"" + rb + "\"" +
           ",\"sn\":" + jStr(ne->sn) + ",\"txt\":" + jStr((const char*)pl, pn));
  } else if (port == PORT_NODEINFO) {
    size_t j = 0;
    while (j < pn && pbNext(pl, pn, j, f, v, s)) {
      if (f == 2 && s) { size_t c = min((size_t)v, sizeof(ne->ln) - 1); memcpy(ne->ln, s, c); ne->ln[c] = 0; }
      if (f == 3 && s) { size_t c = min((size_t)v, sizeof(ne->sn) - 1); memcpy(ne->sn, s, c); ne->sn[c] = 0; }
    }
    io.printf("+NODE:!%08x,%.0f,%.1f,%s,%s\r\n", (unsigned)from, rssi, snr, ne->sn, ne->ln);
    pushEv("\"t\":\"node\"," + meta + ",\"sn\":" + jStr(ne->sn) + ",\"ln\":" + jStr(ne->ln));
    if (to == cfg.node) {                                            // addressed to us: that node hears us
      io.printf("+HEARDBY:!%08x,%.0f,%.1f,%s\r\n", (unsigned)from, rssi, snr, ne->sn);
      sysEv(String(ne->sn[0] ? ne->sn : hexId(from).c_str()) + " (" + hexId(from) + ") hears this node: NodeInfo reply, SNR " +
            String(snr, 1) + " dB");
    }
  } else if (rawMode) io.printf("+PKT:!%08x,%u,%u\r\n", (unsigned)from, (unsigned)port, (unsigned)pn);
}

// ======================= WiFi ===============================================
void saveCfg() { nvs.putBytes("cfg", &cfg, sizeof(cfg)); }

void startPortal() {
  portal = true; wifiUp = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);                                            // STA active to scan networks
  char ap[24]; snprintf(ap, sizeof(ap), "MeshModem-%04X", (unsigned)(cfg.node & 0xFFFF));
  WiFi.softAP(ap);
  dns.start(53, "*", WiFi.softAPIP());                               // every name resolves to the portal
  server.close(); server.begin();
  io.printf("+WIFI:PORTAL,%s,%s\r\n", ap, WiFi.softAPIP().toString().c_str());
  sysEv(String("WiFi portal active: network ") + ap);
}

// Own network with the chat (not the portal): to use the modem without the home network
void startAP() {
  portal = false; apMode = true; wifiUp = false; dns.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);                                            // STA free to scan from /setup
  char ap[24]; snprintf(ap, sizeof(ap), "MeshModem-%04X", (unsigned)(cfg.node & 0xFFFF));
  WiFi.softAP(ap, apPass.length() >= 8 ? apPass.c_str() : nullptr);
  MDNS.end(); MDNS.begin(HOSTNAME); MDNS.addService("http", "tcp", 80);
  server.close(); server.begin();
  io.printf("+WIFI:AP,%s,http://%s/ (password %s)\r\n", ap, WiFi.softAPIP().toString().c_str(),
            apPass.length() >= 8 ? "YES" : "NO");
  sysEv(String("Own network active: ") + ap + ", http://192.168.4.1");
}

void wifiStart() {
  if (portal) { dns.stop(); WiFi.softAPdisconnect(true); portal = false; }
  if (apMode) { WiFi.softAPdisconnect(true); apMode = false; }
  if (wifiMode == 2) { startAP(); return; }
  if (!wSsid.length()) { if (wifiMode == 0) startAP(); else startPortal(); return; }
  wifiUp = false; tStaStart = millis();
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wSsid.c_str(), wPass.c_str());
  server.close(); server.begin();
  io.printf("+WIFI:CONNECTING,%s\r\n", wSsid.c_str());
}

void wifiLoop() {
  if (tWifiPending && millis() - tWifiPending > 1500) { tWifiPending = 0; wifiStart(); }
  if (portal) { dns.processNextRequest(); return; }
  if (apMode) return;
  bool c = WiFi.status() == WL_CONNECTED;
  if (!c && wifiMode == 0 && !staEverUp && millis() - tStaStart > STA_TIMEOUT_MS) {   // AUTO: home network not found
    io.printf("+WIFI:NOT_FOUND,%s\r\n", wSsid.c_str());
    startAP(); return;
  }
  if (c && !wifiUp) {
    wifiUp = true; staEverUp = true;
    MDNS.begin(HOSTNAME); MDNS.addService("http", "tcp", 80);
    configTzTime(TZ_STR, "pool.ntp.org", "time.google.com");
    String ip = WiFi.localIP().toString();
    io.printf("+WIFI:UP,%s,http://%s/ (http://%s.local/)\r\n", wSsid.c_str(), ip.c_str(), HOSTNAME);
    sysEv("WiFi connected to " + wSsid + ", IP " + ip);
  } else if (!c && wifiUp) {
    wifiUp = false; MDNS.end();
    io.print("+WIFI:DOWN\r\n");
  }
}

void bootButton() {
  static uint32_t tDown = 0;
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!tDown) tDown = millis() | 1;
    else if (!portal && millis() - tDown >= PORTAL_HOLD_MS) startPortal();
  } else tDown = 0;
}

void wifiStatus() {
  if (portal) io.printf("+WIFI:PORTAL,%s,%s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
  else if (apMode) io.printf("+WIFI:AP,%s,%s,CLIENTS=%u,MODE=%s\r\n", WiFi.softAPSSID().c_str(),
                             WiFi.softAPIP().toString().c_str(), (unsigned)WiFi.softAPgetStationNum(), WMODE[wifiMode]);
  else if (wifiUp) io.printf("+WIFI:UP,%s,%s,%d dBm,http://%s.local/\r\n", wSsid.c_str(),
                             WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), HOSTNAME);
  else io.printf("+WIFI:%s,%s\r\n", wSsid.length() ? "CONNECTING" : "NOT_CONFIGURED", wSsid.c_str());
}

// ======================= Bluetooth LE (Nordic UART Service) =================
#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"            // the phone writes here
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"            // the modem notifies here
BLEServer* bleSrv = nullptr;
BLECharacteristic* bleTx = nullptr;
uint32_t blePin = BLE_PIN;
volatile uint16_t bleMtu = 23;
volatile bool bleEvConn = false, bleEvDisc = false;                  // events for the loop
char bleName[24];

// Input: ring buffer protected by a spinlock (written by the NimBLE task, read by the loop)
portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;
char bleIn[512]; volatile uint16_t bleInH = 0, bleInT = 0;

class BleSrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConn = true; bleEvConn = true; }
  void onDisconnect(BLEServer*) override { bleConn = false; bleMtu = 23; bleEvDisc = true; }
  void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t mtu) override { bleMtu = mtu; }
#if !BLE_SEC_NEW_API
  uint32_t onPassKeyRequest() override { return blePin; }            // 3.3.0: PIN through a callback
#endif
};

class BleRxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    portENTER_CRITICAL(&bleMux);
    for (size_t i = 0; i < v.length(); i++) {
      uint16_t n = (bleInH + 1) % sizeof(bleIn);
      if (n == bleInT) break;                                        // ring full: dropped
      bleIn[bleInH] = v[i]; bleInH = n;
    }
    portEXIT_CRITICAL(&bleMux);
  }
};

void bleInit() {
  snprintf(bleName, sizeof(bleName), "MeshModem-%04X", (unsigned)(cfg.node & 0xFFFF));
  BLEDevice::init(bleName);
  BLEDevice::setMTU(247);                                            // up to 244 bytes per notification
  if (blePin) {
    // The modem "displays" a fixed passkey (DISPLAY_ONLY) and the phone enters it.
    // MITM + Secure Connections + bonding.
#if BLE_SEC_NEW_API
    BLESecurity::setPassKey(true, blePin);                           // static passkey
    BLESecurity::setCapability(BLE_HS_IO_DISPLAY_ONLY);
    BLESecurity::setAuthenticationMode(true, true, true);            // bonding, MITM, SC
#else
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;                   // 3.3.0: Security Manager set directly;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_mitm = 1; ble_hs_cfg.sm_sc = 1;   // the PIN comes from onPassKeyRequest()
#endif
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  }
  bleSrv = BLEDevice::createServer();
  bleSrv->setCallbacks(new BleSrvCb());
  BLEService* svc = bleSrv->createService(NUS_SERVICE);
  bleTx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);   // NimBLE adds the CCCD
  uint32_t wp = BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR;
  if (blePin) wp |= BLECharacteristic::PROPERTY_WRITE_AUTHEN;        // writing requires pairing with PIN
  BLECharacteristic* rx = svc->createCharacteristic(NUS_RX, wp);
  rx->setCallbacks(new BleRxCb());
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE);                                  // to filter by service (Web Bluetooth)
  adv->setScanResponse(true);
  if (bleOn) BLEDevice::startAdvertising();
}

void bleSetOn(bool on) {
  bleOn = on;
  if (!bleSrv) { if (on) bleInit(); return; }
  if (on) BLEDevice::startAdvertising();
  else {
    BLEDevice::stopAdvertising();
    for (auto& kv : bleSrv->getPeerDevices(false)) bleSrv->disconnect(kv.first);
    bleConn = false; bleOut = "";
  }
}

void bleStatus() {
  io.printf("+BLE:%s,%s,%s,MTU=%u,PIN=%s\r\n", bleOn ? "ON" : "OFF", bleConn ? "CONNECTED" : "IDLE",
            bleName[0] ? bleName : "-", (unsigned)bleMtu, blePin ? "YES" : "NO");
}

void at(String s);

void bleLoop() {
  if (bleEvConn) { bleEvConn = false; bleOut = ""; io.print("+BLE:CONNECTED\r\n"); sysEv("Bluetooth connected"); }
  if (bleEvDisc) {
    bleEvDisc = false; bleOut = "";
    io.print("+BLE:DISCONNECTED\r\n");
    if (bleOn) { delay(50); BLEDevice::startAdvertising(); }         // advertises again
  }
  // Input: lines terminated with CR/LF; without a terminator, executed after 300 ms of silence
  static String line; static uint32_t tLast = 0;
  while (true) {
    char c; bool got = false;
    portENTER_CRITICAL(&bleMux);
    if (bleInT != bleInH) { c = bleIn[bleInT]; bleInT = (bleInT + 1) % sizeof(bleIn); got = true; }
    portEXIT_CRITICAL(&bleMux);
    if (!got) break;
    tLast = millis();
    if (c == '\r' || c == '\n') {
      if (line.length()) { io.printf("[ble] %s\r\n", line.c_str()); unread = 0; at(line); }
      line = "";
    } else if (line.length() < 240) line += c;
  }
  if (line.length() && millis() - tLast > 300) {
    io.printf("[ble] %s\r\n", line.c_str()); unread = 0; at(line); line = "";
  }
  // Output: one notification of up to MTU-3 bytes every ~8 ms
  static uint32_t tTx = 0;
  if (bleConn && bleTx && bleOut.length() && millis() - tTx >= 8) {
    size_t n = min((size_t)(bleMtu > 23 ? bleMtu - 3 : 20), (size_t)bleOut.length());
    bleTx->setValue((uint8_t*)bleOut.c_str(), n);
    bleTx->notify();
    bleOut.remove(0, n);
    tTx = millis();
  }
}

// ======================= Spectrum analyzer ==================================
// Measurement path over direct SPI (like LoRaSA_ESP32S3_SX1262), independent of the
// RadioLib version. RadioLib is only used to return to the mesh (specExit).
#define SA_MAX_PTS      2048
#define SA_MAX_NS       64
#define SA_SLICE_US     8000           // sweep time per loop pass (web/BLE keep being served)
#define SA_SPI_HZ       8000000        // SPI harmonics at 904/912/920/928 MHz (self-generated spurs)
#define SA_SETTLE_BASE  150            // settling after SetRx: base + K/RBW [us] (validate with a CW source)
#define SA_SETTLE_K     8.0e6
#define SA_CAL_SEG_MHZ  32.0           // wider spans: image calibration per segment
#define SA_HDR          32             // /api/spec binary header
#define SA_OP_CLR_IRQ   0x02
#define SA_OP_DIO_IRQ   0x08
#define SA_OP_RSSI_INST 0x15
#define SA_OP_STANDBY   0x80
#define SA_OP_SET_RX    0x82
#define SA_OP_RF_FREQ   0x86
#define SA_OP_PKT_TYPE  0x8A
#define SA_OP_MOD_PAR   0x8B
#define SA_OP_CAL_IMG   0x98
#define SA_STDBY_RC     0x00
#define SA_STDBY_XOSC   0x01

struct SaBw { float khz; uint8_t code; };                            // GFSK RxBw (datasheet table 13-48)
const SaBw SA_BW[] = {
  {4.8f, 0x1F}, {5.8f, 0x17}, {7.3f, 0x0F}, {9.7f, 0x1E}, {11.7f, 0x16}, {14.6f, 0x0E}, {19.5f, 0x1D},
  {23.4f, 0x15}, {29.3f, 0x0D}, {39.0f, 0x1C}, {46.9f, 0x14}, {58.6f, 0x0C}, {78.2f, 0x1B}, {93.8f, 0x13},
  {117.3f, 0x0B}, {156.2f, 0x1A}, {187.2f, 0x12}, {234.3f, 0x0A}, {312.0f, 0x19}, {373.6f, 0x11}, {467.0f, 0x09}};
const int SA_NBW = sizeof(SA_BW) / sizeof(*SA_BW);

struct SaCfg { double f0, step; float rbw; uint8_t bwc, ns, nseg; uint16_t n; uint32_t settle, gap; } sa;
uint8_t saPk[SA_MAX_PTS], saAv[SA_MAX_PTS];                          // sweep in progress (1 B = -2*dBm)
uint8_t saAcc[SA_MAX_PTS], saOut[SA_MAX_PTS];                        // accumulated peak / average of the last sweep
uint8_t saBuf[SA_HDR + 2 * SA_MAX_PTS];                              // /api/spec response
float saLin[256];                                                    // saLin[r] = 10^(-r/20), relative power
uint32_t saSeq, saTus, saT0, saLease, saStart;
uint16_t saI, saNacc; int16_t saSeg; bool saFault;
SPISettings saSpi(SA_SPI_HZ, MSBFIRST, SPI_MODE0);

bool saWaitBusy(uint32_t us) {
  uint32_t t0 = micros();
  while (digitalRead(PIN_BUSY)) if ((uint32_t)(micros() - t0) > us) return false;
  return true;
}

// BUSY rises a few hundred ns after NSS; the 1 us delay avoids sampling a stale BUSY
bool saCmd(uint8_t op, const uint8_t* p, uint8_t n) {
  if (!saWaitBusy(30000)) { saFault = true; return false; }
  spiLora.beginTransaction(saSpi);
  digitalWrite(PIN_CS, LOW);
  spiLora.transfer(op);
  for (uint8_t i = 0; i < n; i++) spiLora.transfer(p[i]);
  digitalWrite(PIN_CS, HIGH);
  spiLora.endTransaction();
  delayMicroseconds(1);
  return true;
}

int saRssi() {                                                       // GetRssiInst: P[dBm] = -raw/2
  if (!saWaitBusy(30000)) { saFault = true; return -1; }
  spiLora.beginTransaction(saSpi);
  digitalWrite(PIN_CS, LOW);
  spiLora.transfer(SA_OP_RSSI_INST);
  spiLora.transfer(0x00);                                            // status
  uint8_t r = spiLora.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  spiLora.endTransaction();
  delayMicroseconds(1);
  return r;
}

void saStandby(uint8_t m) { saCmd(SA_OP_STANDBY, &m, 1); }

void saFreq(double mhz) {                                            // synthesizer step 32 MHz/2^25
  uint32_t frf = (uint32_t)llround(mhz * 1e6 * 33554432.0 / 32000000.0);
  uint8_t p[4] = {(uint8_t)(frf >> 24), (uint8_t)(frf >> 16), (uint8_t)(frf >> 8), (uint8_t)frf};
  saCmd(SA_OP_RF_FREQ, p, 4);
}

void saRx() { const uint8_t p[3] = {0xFF, 0xFF, 0xFF}; saCmd(SA_OP_SET_RX, p, 3); }   // continuous RX

// Image calibration from STDBY_RC; with a TCXO the chip powers it through DIO3 (SetDIO3AsTCXOCtrl
// was configured by RadioLib) and honors its start-up time with BUSY high.
void saCalImage(double fmin, double fmax) {
  saStandby(SA_STDBY_RC);
  int a = (int)floor(fmin / 4.0), b = (int)floor(fmax / 4.0) + 1;
  if (a < 0) a = 0;
  if (b > 255) b = 255;
  if (b <= a) b = a + 1;
  uint8_t p[2] = {(uint8_t)a, (uint8_t)b};
  saCmd(SA_OP_CAL_IMG, p, 2);
  if (!saWaitBusy(50000)) saFault = true;
  saStandby(SA_STDBY_XOSC);
}

double saSegLo(int s) { return sa.f0 + (sa.n - 1) * sa.step / 1000.0 * s / sa.nseg; }

// Modem in GFSK: the RxBw is the RBW. BR 4.8 kb/s and Fdev 5 kHz do not affect the power measurement.
void saRadioSetup() {
  saFault = false;
  saStandby(SA_STDBY_RC);
  uint8_t pt = 0x00; saCmd(SA_OP_PKT_TYPE, &pt, 1);                  // PACKET_TYPE_GFSK
  uint8_t mp[8] = {0x03, 0x41, 0x55,                                 // BR = 32*32e6/4800 = 0x034155
                   0x00, sa.bwc,                                     // no pulse shaping, RxBw
                   0x00, 0x14, 0x7B};                                // Fdev = 5 kHz * 2^25 / 32e6
  saCmd(SA_OP_MOD_PAR, mp, 8);
  uint8_t irq[8] = {0}; saCmd(SA_OP_DIO_IRQ, irq, 8);                // no interrupts on DIO1
  uint8_t clr[2] = {0xFF, 0xFF}; saCmd(SA_OP_CLR_IRQ, clr, 2);
  saSeg = -1;
  if (sa.nseg == 1) { saCalImage(sa.f0, sa.f0 + (sa.n - 1) * sa.step / 1000.0); saSeg = 0; }
  else saStandby(SA_STDBY_XOSC);
  saI = 0; saNacc = 0;
}

bool saConfig(double f0, double f1, double step, float rbw, int ns) {
  if (f0 < 150.0 || f1 > 960.0 || f1 <= f0 || step < 1.0 || ns < 1 || ns > SA_MAX_NS) return false;
  long n = (long)floor((f1 - f0) * 1000.0 / step + 1e-6) + 1;
  if (n < 2 || n > SA_MAX_PTS) return false;
  int k = 0;
  for (int i = 1; i < SA_NBW; i++) if (fabsf(SA_BW[i].khz - rbw) < fabsf(SA_BW[k].khz - rbw)) k = i;
  sa.f0 = f0; sa.step = step; sa.n = (uint16_t)n; sa.ns = (uint8_t)ns;
  sa.rbw = SA_BW[k].khz; sa.bwc = SA_BW[k].code;
  double hz = sa.rbw * 1000.0, gap = 1e6 / hz - 15.0;                // ~15 us are already spent in the SPI read
  sa.settle = (uint32_t)(SA_SETTLE_BASE + SA_SETTLE_K / hz);
  sa.gap = gap > 0 ? (uint32_t)gap : 0;
  sa.nseg = (uint8_t)max(1, (int)ceil((n - 1) * step / 1000.0 / SA_CAL_SEG_MHZ));
  if (specOn) saRadioSetup();                                        // live change: restarts the sweep
  return true;
}

void saInit() {
  for (int r = 0; r < 256; r++) saLin[r] = powf(10.0f, -r / 20.0f);
  if (!saConfig(cfg.f0, cfg.f1, SA_DEF_STEP, SA_DEF_RBW, SA_DEF_NS))   // default: the node's band
    saConfig(902.0, 928.0, SA_DEF_STEP, SA_DEF_RBW, SA_DEF_NS);
}

void specStatus() {
  uint32_t el = millis() - saStart;
  long left = specOn && el < SA_MAX_MS ? (long)((SA_MAX_MS - el) / 1000) : 0;
  io.printf("+SPEC:%s,%.3f,%.3f,%.1f,%.1f,%u,%u,SETTLE=%luus,SWEEPS=%lu,LEFT=%lds\r\n",
            specOn ? "ON" : "OFF", sa.f0, sa.f0 + (sa.n - 1) * sa.step / 1000.0, sa.step, sa.rbw,
            (unsigned)sa.ns, (unsigned)sa.n, (unsigned long)sa.settle, (unsigned long)saSeq, left);
}

void specExit(const char* why);

bool specEnter() {
  if (specOn) { saLease = millis(); return true; }                   // already active: renews the lease
  if (radioErr) { io.printf("+SPEC:ERROR,RADIO=%d\r\n", radioErr); return false; }
  receive();                                                         // picks up a packet already in the FIFO
  for (auto& x : pend) if (x.used) { x.used = false; stDropped++; }  // pending rebroadcasts: dropped
  for (auto& o : own) o.done = true;                                 // no false "no echo" report
  specOn = true; saStart = saLease = millis(); saSeq = 0; saTus = 0;
  saRadioSetup();
  if (saFault) { specExit("SPI/BUSY failure"); return false; }
  specStatus();
  sysEv("Spectrum analyzer active: the mesh is suspended");
  return true;
}

// Back to the mesh: the same sequence as setup() (begin resets the chip through RST)
void specExit(const char* why) {
  if (!specOn) return;
  specOn = false;
  derive();
  const Preset& p = PRESETS[cfg.preset];
  int e = radioBegin(freq, p.bw, p.sf, p.cr, cfg.pwr);
  radio.setDio2AsRfSwitch(DIO2_RFSW);
  radio.setCurrentLimit(140);
  radio.setRxBoostedGainMode(true);
  radio.startReceive();
  radioErr = e;
  io.printf("+SPEC:OFF,%s,RADIO=%d\r\n", why, e);
  sysEv(String("Analyzer stopped (") + why + "): the node is back on the mesh");
  if (niPending) { niPending = false; if (!e) sendNodeInfo(); }      // NodeInfo requested meanwhile
}

// One slice of the sweep per loop pass (SA_SLICE_US, at least one point)
void specLoop() {
  uint32_t ms = millis();
  if (saFault)                     { specExit("SPI/BUSY failure"); return; }
  if (ms - saLease > SA_LEASE_MS)  { specExit("no web requests"); return; }
  if (ms - saStart > SA_MAX_MS)    { specExit("maximum time"); return; }
  if (saI == 0) saT0 = micros();
  uint32_t tc = micros();
  do {
    if (sa.nseg > 1) {
      int16_t s = (int16_t)(((uint32_t)saI * sa.nseg) / sa.n);
      if (s != saSeg) { saCalImage(saSegLo(s), saSegLo(s + 1)); saSeg = s; }
    }
    saStandby(SA_STDBY_XOSC);
    saFreq(sa.f0 + saI * sa.step / 1000.0);
    saRx();
    delayMicroseconds(sa.settle);
    int rmin = 255; float acc = 0.0f;
    for (uint8_t k = 0; k < sa.ns; k++) {
      int r = saRssi(); if (r < 0) r = 255;
      if (r < rmin) rmin = r;                                        // lower raw = more power (peak)
      acc += saLin[r];                                               // average in linear power
      if (sa.gap && k + 1 < sa.ns) delayMicroseconds(sa.gap);
    }
    saPk[saI] = (uint8_t)rmin;
    saAv[saI] = (uint8_t)constrain((int)lroundf(-20.0f * log10f(acc / sa.ns)), 0, 255);
    saI++;
  } while (saI < sa.n && (uint32_t)(micros() - tc) < SA_SLICE_US);
  if (saI < sa.n) return;
  saTus = micros() - saT0;                                           // real duration (includes the rest of the loop)
  for (uint16_t i = 0; i < sa.n; i++) {
    saOut[i] = saAv[i];
    saAcc[i] = saNacc ? min(saAcc[i], saPk[i]) : saPk[i];            // maximum since the last web read
  }
  if (saNacc < 0xFFFF) saNacc++;
  saSeq++; saI = 0;
}

// ======================= AT commands ========================================
void info() {
  const Preset& p = PRESETS[cfg.preset];
  io.printf("+NODE:!%08x\r\n+NAME:%s,%s\r\n", (unsigned)cfg.node, cfg.ln, cfg.sn);
  io.printf("+PRESET:%s,BW=%.1f,SF=%u,CR=4/%u,HASH=%02X\r\n", p.name, p.bw, p.sf, p.cr, chHash);
  io.printf("+BAND:%.3f,%.3f\r\n+FREQ:%.4f,%s,SLOT=%u/%u\r\n", cfg.f0, cfg.f1, freq,
            cfg.fov > 0 ? "MANUAL" : "AUTO", (unsigned)slot + 1, (unsigned)nCh);
  io.printf("+HOP:%u\r\n+PWR:%d\r\n+RETRY:%u\r\n+RAW:%d\r\n", cfg.hop, cfg.pwr, retryMax, rawMode);
  battStatus();
  io.printf("+RELAY:%s,RELAYED=%lu,CANCELED=%lu,DROPPED=%lu,SLOT=%.1fms\r\n", RELAY_NAME[cfg.relay],
            (unsigned long)stRelayed, (unsigned long)stCanceled, (unsigned long)stDropped, slotMs);
  io.printf("+BOOT:%s,UPTIME=%lus\r\n", resetReason(), (unsigned long)(millis() / 1000));
  if (fsOk) io.printf("+LOG:%u bytes used of %u\r\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  else io.print("+LOG:NO_FLASH\r\n");
  bleStatus();
  wifiStatus();
  specStatus();
}

void diagReset() { memset(&dg, 0, sizeof(dg)); dg.t0 = millis(); dg.relay0 = stRelayed; }

// Noise floor: GetRssiInst every 50 ms while in RX (frames in flight barely move the P10)
void nfSample() {
  static uint32_t t;
  if (specOn || radioErr || millis() - t < 50) return;
  t = millis();
  if (digitalRead(PIN_DIO1) == HIGH) return;                         // a frame is waiting in the FIFO
  int r = (int)(-2.0f * radio.getRSSI(false) + 0.5f);
  if (r < 0 || r > 255) return;
  dg.nf[r]++; dg.nfN++;
}

float nfPct(float q) {                                               // q-quantile of the histogram [dBm]
  uint32_t target = (uint32_t)(dg.nfN * q), acc = 0;
  for (int r = 255; r >= 0; r--) { acc += dg.nf[r]; if (acc > target) return -0.5f * r; }
  return 0;
}

void diag() {
  const Preset& p = PRESETS[cfg.preset];
  float lim = -2.5f * (p.sf - 4);                                    // LoRa demodulation limit: SF7 -7.5 ... SF12 -20 dB
  io.printf("+DIAG:TIME=%lus,%s,%.4fMHz,PWR=%d,RETRY=%u\r\n", (unsigned long)((millis() - dg.t0) / 1000), p.name, freq,
            cfg.pwr, retryMax);
  if (dg.nfN) io.printf("+DIAG:NOISE,P10=%.1fdBm,P50=%.1fdBm,N=%lu\r\n", nfPct(0.1f), nfPct(0.5f), (unsigned long)dg.nfN);
  else io.print("+DIAG:NOISE,NO_DATA\r\n");
  io.printf("+DIAG:AIRTIME,CHUTIL=%.1f%%,TXUTIL=%.2f%%\r\n", chUtil(), txUtil());
  io.printf("+DIAG:RX,OK=%lu,CRC=%lu,HEADER=%lu,ERR=%lu,OTHER_CH=%lu,DUP=%lu\r\n", (unsigned long)dg.rxOk,
            (unsigned long)dg.rxCrc, (unsigned long)dg.rxHdr, (unsigned long)dg.rxErr, (unsigned long)dg.rxForeign,
            (unsigned long)dg.rxDup);
  if (dg.snrN) {
    float avg = dg.snrSum / dg.snrN;
    io.printf("+DIAG:SNR,MIN=%.1f,AVG=%.1f,MAX=%.1f,LIMIT=%.1f,MARGIN=%.1fdB\r\n", dg.snrMin, avg, dg.snrMax, lim, avg - lim);
  } else io.print("+DIAG:SNR,NO_DATA\r\n");
  io.printf("+DIAG:TX,OWN=%lu,RETX=%lu,ECHOED=%lu,NO_ECHO=%lu,RELAYED=%lu\r\n", (unsigned long)dg.txOwn,
            (unsigned long)dg.txRetx, (unsigned long)dg.ownEcho, (unsigned long)dg.ownNoEcho,
            (unsigned long)(stRelayed - dg.relay0));
}

void listNodes() {
  for (auto& n : nodes) if (n.id)
    io.printf("+NODES:!%08x,%s,%s,%.0f,%.1f,%lus\r\n", (unsigned)n.id, n.sn, n.ln, n.rssi, n.snr,
              (unsigned long)((millis() - n.seen) / 1000));
}

void at(String s) {
  s.trim(); if (!s.length()) return;
  String u = s; u.toUpperCase();
  int eq = s.indexOf('='); String a = eq >= 0 ? s.substring(eq + 1) : ""; a.trim();
  bool ok = true;

  if (specOn && (u.startsWith("AT+SEND=") || u.startsWith("AT+NI"))) {   // analyzer active: no transmission
    io.print("+SPEC:ACTIVE,mesh suspended (AT+SPEC=0 to return)\r\nERROR\r\n");
    return;
  }

  if (u == "AT") {}
  else if (u == "AT+HELP" || u == "AT+HELP?") io.print(HELP);
  else if (u == "ATI" || u == "AT+INFO?") info();
  else if (u.startsWith("AT+SEND=")) {
    ok = a.length() > 0 && sendData(PORT_TEXT, (const uint8_t*)a.c_str(), a.length(), false);
    if (ok) logEv("\"t\":\"tx\",\"id\":\"" + String(lastTxId, HEX) + "\",\"sn\":" + jStr(cfg.sn) + ",\"txt\":" + jStr(a.c_str()));
  }
  else if (u == "AT+NI" || u == "AT+NI=0") ok = sendNodeInfo();
  else if (u == "AT+NI=1") ok = sendNodeInfoReq(true);
  else if (u == "AT+RETRY?") io.printf("+RETRY:%u\r\n", retryMax);
  else if (u.startsWith("AT+RETRY=")) {
    ok = a.length() == 1 && a[0] >= '0' && a[0] <= '3';
    if (ok) { retryMax = a[0] - '0'; nvs.putUChar("retry", retryMax); }
  }
  else if (u == "AT+DIAG" || u == "AT+DIAG?") diag();
  else if (u == "AT+DIAG=0") diagReset();
  else if (u == "AT+NODES") listNodes();
  else if (u.startsWith("AT+NODE=")) {
    if (a[0] == '!') a.remove(0, 1);
    uint32_t v = strtoul(a.c_str(), nullptr, 16);
    ok = a.length() == 8 && v && v != 0xFFFFFFFF;
    if (ok) { cfg.node = v; sendNodeInfo(); }
  }
  else if (u.startsWith("AT+NAME=")) {
    int c = a.indexOf(','); String ln = c >= 0 ? a.substring(0, c) : a, sn = c >= 0 ? a.substring(c + 1) : a.substring(0, 4);
    ln.trim(); sn.trim();
    ok = ln.length() > 0 && ln.length() < sizeof(cfg.ln) && sn.length() > 0 && sn.length() < sizeof(cfg.sn);
    if (ok) { strcpy(cfg.ln, ln.c_str()); strcpy(cfg.sn, sn.c_str()); sendNodeInfo(); }
  }
  else if (u == "AT+PRESET=?") { for (int i = 0; i < NPRE; i++) io.printf("+PRESET:%d,%s\r\n", i, PRESETS[i].name); }
  else if (u.startsWith("AT+PRESET=")) {
    int k = -1;
    for (int i = 0; i < NPRE; i++) if (a.equalsIgnoreCase(PRESETS[i].name)) k = i;
    if (k < 0 && isDigit(a[0])) k = a.toInt();
    ok = k >= 0 && k < NPRE; if (ok) { cfg.preset = k; ok = !applyRadio(); sendNodeInfo(); }
  }
  else if (u.startsWith("AT+BAND=")) {
    float f0 = a.toFloat(), f1 = a.substring(a.indexOf(',') + 1).toFloat();
    ok = a.indexOf(',') > 0 && f0 >= 150 && f1 <= 960 && f1 > f0;
    if (ok) { cfg.f0 = f0; cfg.f1 = f1; ok = !applyRadio(); }
  }
  else if (u.startsWith("AT+FREQ=")) {
    float f = a.toFloat(); ok = f == 0 || (f >= 150 && f <= 960);
    if (ok) { cfg.fov = f; ok = !applyRadio(); }
  }
  else if (u.startsWith("AT+HOP=")) { int h = a.toInt(); ok = isDigit(a[0]) && h <= 7; if (ok) cfg.hop = h; }
  else if (u.startsWith("AT+PWR=")) { int pw = a.toInt(); ok = pw >= -9 && pw <= 22; if (ok) { cfg.pwr = pw; ok = !applyRadio(); } }
  else if (u.startsWith("AT+RAW=")) rawMode = a.toInt();
  else if (u == "AT+RELAY?") io.printf("+RELAY:%s\r\n", RELAY_NAME[cfg.relay]);
  else if (u == "AT+BATT?" || u == "AT+BATT") battStatus();
  else if (u.startsWith("AT+LED=")) { ok = a == "0" || a == "1"; if (ok) ledsOn = a == "1"; }
  else if (u.startsWith("AT+RELAY=")) {
    int r = a.toInt(); ok = isDigit(a[0]) && r <= 2;
    if (ok) { cfg.relay = r; if (!r) for (auto& x : pend) x.used = false; sendNodeInfo(); }   // announces the role
  }
  else if (u == "AT+SAVE") { saveCfg(); sysEv("Configuration saved to flash"); }
  else if (u == "AT+WIFI?") wifiStatus();
  else if (u.startsWith("AT+WIFI=")) {
    int c = a.indexOf(',');
    ok = c > 0;
    if (ok) {
      wSsid = a.substring(0, c); wPass = a.substring(c + 1);
      nvs.putString("ssid", wSsid); nvs.putString("pass", wPass);
      tWifiPending = millis() | 1;                                   // connects after replying
    }
  }
  else if (u == "AT+PORTAL") startPortal();
  else if (u == "AT+WIFIMODE?") io.printf("+WIFIMODE:%s\r\n", WMODE[wifiMode]);
  else if (u.startsWith("AT+WIFIMODE=")) {
    String m = a; m.toUpperCase(); int k = -1;
    for (int i = 0; i < 3; i++) if (m == WMODE[i] || m == String(i)) k = i;
    ok = k >= 0;
    if (ok) { wifiMode = k; nvs.putUChar("wmode", k); staEverUp = false; tWifiPending = millis() | 1; }
  }
  else if (u.startsWith("AT+APPASS=")) {
    ok = a.length() == 0 || (a.length() >= 8 && a.length() <= 63);
    if (ok) { apPass = a; nvs.putString("appass", a); if (apMode) tWifiPending = millis() | 1; }
  }
  else if (u.startsWith("AT+TIME=")) {
    uint32_t t = strtoul(a.c_str(), nullptr, 10);
    ok = t > 1700000000UL;
    if (ok && !nowTs()) { struct timeval tv = { (time_t)t, 0 }; settimeofday(&tv, nullptr); sysEv("Time taken from the phone"); }
  }
  else if (u == "AT+BLE?") bleStatus();
  else if (u.startsWith("AT+BLE=")) {
    ok = a == "0" || a == "1";
    if (ok) { nvs.putUChar("ble", a == "1"); io.print("OK\r\n"); bleSetOn(a == "1"); return; }   // OK before disconnecting
  }
  else if (u.startsWith("AT+BLEPIN=")) {
    long v = a.toInt(); ok = isDigit(a[0]) && (v == 0 || (v >= 100000 && v <= 999999));
    if (ok) { blePin = v; nvs.putULong("blepin", v); io.print("+BLEPIN:applied on reboot (AT+RST)\r\n"); }
  }
  else if (u == "AT+SPEC?") specStatus();
  else if (u.startsWith("AT+SPEC=")) {
    if (a == "1") ok = specEnter();
    else if (a == "0") specExit("AT+SPEC=0");
    else ok = false;
  }
  else if (u.startsWith("AT+SPECCFG=")) {
    double f0, f1, st; float rb; int ns;
    ok = sscanf(a.c_str(), "%lf,%lf,%lf,%f,%d", &f0, &f1, &st, &rb, &ns) == 5 && saConfig(f0, f1, st, rb, ns);
    if (ok) specStatus();
  }
  else if (u == "AT+LOG" || u.startsWith("AT+LOG=")) {
    int n = u == "AT+LOG" ? 20 : a.toInt();
    ok = fsOk && n > 0 && n <= 500;
    if (ok) { std::vector<String> v; logTail(n, v); for (auto& l : v) io.printf("+LOG:%s\r\n", l.c_str()); }
  }
  else if (u == "AT+LOGCLR") {
    ok = fsOk;
    if (ok) { LittleFS.remove(LOG_FILE); LittleFS.remove(LOG_OLD); sysEv("Message log erased"); }
  }
  else if (u == "AT&F") { cfg = DEF; nvs.remove("cfg"); retryMax = RETRY_DEF; nvs.remove("retry"); applyRadio(); sendNodeInfo(); }
  else if (u == "AT+RST") { io.print("OK\r\n"); Serial.flush(); delay(100); ESP.restart(); }
  else ok = false;

  io.print(ok ? "OK\r\n" : "ERROR\r\n");
}

// ======================= Embedded web pages =================================
const char PAGE_PORTAL[] = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>MeshModemSA: WiFi</title>
<style>
:root{--bg:#0f1626;--card:#172138;--ink:#e7ecf5;--mut:#a3aec4;--line:#2b3753;--acc:#3cc3c4;color-scheme:dark}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:420px;margin:0 auto;padding:28px 18px}
h1{font-size:1.4rem;margin:0 0 4px}p{color:var(--mut);margin:0 0 18px}
form{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:18px;display:grid;gap:12px}
label{font-size:.85rem;font-weight:600;color:var(--mut);display:grid;gap:5px}
input{font:inherit;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink)}
button{font:inherit;font-weight:600;padding:11px;border:0;border-radius:8px;background:var(--acc);color:#0d1322;cursor:pointer}
#nets{display:grid;gap:4px;max-height:220px;overflow:auto}
#nets button{background:transparent;color:var(--ink);border:1px solid var(--line);text-align:left;display:flex;justify-content:space-between;font-weight:500}
small{color:var(--mut)}
</style></head><body><main>
<h1>Connect to your WiFi</h1><p>Choose your home network. The modem keeps working over LoRa and the serial terminal in the meantime.</p>
<form method="post" action="/wifi">
<div id="nets"><small>Scanning networks…</small></div>
<label>Network (SSID)<input name="ssid" id="ssid" required maxlength="32" autocomplete="off"></label>
<label>Password<input name="pass" type="password" maxlength="63"></label>
<button type="submit">Save and connect</button>
</form></main>
<script>
fetch('/scan').then(r=>r.json()).then(l=>{const d=document.getElementById('nets');d.innerHTML='';
const seen=new Set();l.sort((a,b)=>b.r-a.r).forEach(n=>{if(!n.s||seen.has(n.s))return;seen.add(n.s);
const b=document.createElement('button');b.type='button';b.innerHTML='<span></span><small></small>';
b.firstChild.textContent=n.s;b.lastChild.textContent=n.r+' dBm';b.onclick=()=>{document.getElementById('ssid').value=n.s};d.appendChild(b)});
if(!seen.size)d.innerHTML='<small>No networks found. Type the name manually.</small>'}).catch(()=>{});
</script></body></html>)HTML";

const char PAGE_MAIN[] = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"><title>MeshModemSA</title>
<style>
:root{--bg:#0d1322;--card:#151e33;--ink:#e7ecf5;--mut:#a3aec4;--line:#283450;--acc:#3cc3c4;--me:#a893ff;--sys:#76819a;--warn:#f2b53c;
--bad:#ff6b7a;--log:#10182a;--mono:ui-monospace,"Cascadia Mono",Consolas,"DejaVu Sans Mono",monospace;color-scheme:dark}
.bat{display:inline-block;width:22px;height:11px;border:1.5px solid var(--mut);border-radius:2px;position:relative;margin-right:6px;vertical-align:-1px}
.bat::after{content:"";position:absolute;right:-4px;top:2.5px;width:2px;height:3px;background:var(--mut);border-radius:0 1px 1px 0}
.bat i{position:absolute;left:1px;top:1px;bottom:1px;border-radius:1px}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
padding:env(safe-area-inset-top,0) 0 env(safe-area-inset-bottom,0)}
.app{height:100%;display:grid;grid-template-rows:auto 1fr;max-width:1200px;margin:0 auto}
header{display:flex;flex-wrap:wrap;align-items:center;gap:8px 14px;padding:12px 16px;border-bottom:1px solid var(--line)}
header h1{font-size:1.05rem;margin:0;margin-right:auto}
.pill{font-size:.8rem;padding:3px 10px;border-radius:999px;background:var(--card);border:1px solid var(--line);color:var(--mut);white-space:nowrap}
.pill b{color:var(--ink);font-weight:600}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--acc);margin-right:6px;vertical-align:1px}
.dot.off{background:var(--warn)}
.body{display:grid;grid-template-columns:1fr 300px;min-height:0}
.chat{display:grid;grid-template-rows:1fr auto;min-height:0;border-right:1px solid var(--line)}
#log{overflow-y:auto;padding:12px 16px;background:var(--log);font:14px/1.55 var(--mono);word-wrap:break-word}
.l{white-space:pre-wrap}
.t{color:var(--sys)}
.n{font-weight:700}
.me{color:var(--me)}
.s{color:var(--sys)}
.m{color:var(--sys);font-size:.78em;margin-left:6px}
form.in{display:flex;gap:8px;padding:10px 12px;border-top:1px solid var(--line);background:var(--card)}
form.in input{flex:1;min-width:0;font:15px var(--mono);padding:10px 12px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink)}
button{font:inherit;font-weight:600;border:0;border-radius:8px;padding:9px 14px;background:var(--acc);color:#0d1322;cursor:pointer}
button.sec{background:transparent;color:var(--ink);border:1px solid var(--line)}
button:disabled{opacity:.5;cursor:default}
#cnt{align-self:center;font:12px var(--mono);color:var(--sys);min-width:34px;text-align:right}
aside{overflow-y:auto;padding:14px 16px;display:grid;gap:18px;align-content:start}
aside h2{font-size:.95rem;margin:0 0 8px}
.nodes{display:grid;gap:6px}
.node{display:grid;grid-template-columns:auto 1fr;gap:0 8px;font-size:.85rem}
.node .sn{font:700 13px var(--mono);grid-row:span 2;align-self:center;min-width:44px}
.node .d{color:var(--mut);font-size:.78rem}
.cfg{display:grid;gap:10px}
.cfg label{display:grid;gap:4px;font-size:.78rem;font-weight:600;color:var(--mut)}
.cfg input,.cfg select{font:inherit;font-size:.9rem;padding:7px 9px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--ink);width:100%}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.btns{display:flex;flex-wrap:wrap;gap:8px}
.hint{font-size:.78rem;color:var(--mut);margin:0}
.lnk{font-weight:600;font-size:.95rem;padding:9px 14px;border:1px solid var(--line);border-radius:8px;color:var(--ink);text-decoration:none}
button:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid var(--me);outline-offset:1px}
[hidden]{display:none!important}
.cfg input,.cfg select{font-weight:400}
@media(max-width:760px){.app{height:auto;min-height:100%}.body{display:block}
.chat{height:72vh;height:72dvh;border-right:0}aside{border-top:1px solid var(--line);overflow:visible}}
</style></head><body><div class="app">
<header><h1>MeshModemSA</h1>
<span class="pill" id="p-me">—</span><span class="pill" id="p-rf">—</span><span class="pill" id="p-rly">—</span><span class="pill" id="p-bat" hidden>—</span><span class="pill" id="p-ble" hidden>—</span><span class="pill" id="p-net"><span class="dot"></span>—</span><a class="pill" href="/spec" style="text-decoration:none;color:var(--acc)">Spectrum</a></header>
<div class="body">
<section class="chat">
<div id="log" aria-live="polite"></div>
<form class="in" id="f-in" autocomplete="off">
<input id="msg" maxlength="232" placeholder="Message to the channel, or /AT+... for a command">
<span id="cnt">0</span><button id="b-send">Send</button></form>
</section>
<aside>
<div><h2>Message log</h2><div class="btns"><a class="lnk" href="/log.jsonl">Download</a><button type="button" class="sec" id="b-clr">Clear log</button></div>
<p class="hint" style="margin-top:6px">Stored in the modem's flash; it survives reboots and power loss.</p></div>
<div><h2>Heard nodes</h2><div class="nodes" id="nodes"><p class="hint">No nodes heard yet.</p></div></div>
<div><h2>Configuration</h2>
<form class="cfg" id="f-cfg">
<div class="row2"><label>Short name<input id="c-sn" maxlength="4"></label><label>NodeNum<input id="c-node" maxlength="9"></label></div>
<label>Long name<input id="c-ln" maxlength="39"></label>
<label>Preset<select id="c-preset"></select></label>
<label>Region<select id="c-reg">
<option value="915,928">ANZ (915 to 928 MHz)</option><option value="902,928">US (902 to 928 MHz)</option>
<option value="902,907.5">BR_902 (902 to 907.5 MHz)</option><option value="869.4,869.65">EU_868 (869.4 to 869.65 MHz)</option>
<option value="custom">Other band</option></select></label>
<div class="row2" id="r-band" hidden><label>Start [MHz]<input id="c-f0" type="number" step="0.001"></label><label>Stop [MHz]<input id="c-f1" type="number" step="0.001"></label></div>
<label>Manual frequency [MHz], 0 = automatic<input id="c-fov" type="number" step="0.0001" min="0"></label>
<label>Rebroadcast<select id="c-relay"><option value="0">Mute (does not relay)</option><option value="1">Client (default)</option><option value="2">Router (always relays)</option></select></label>
<div class="row2"><label>Hops (0 to 7)<input id="c-hop" type="number" min="0" max="7"></label><label>Power [dBm]<input id="c-pwr" type="number" min="-9" max="22"></label></div>
<div class="btns"><button type="submit">Apply</button><button type="button" class="sec" id="b-save">Save to flash</button><button type="button" class="sec" id="b-ni">Send NodeInfo</button></div>
<p class="hint">"Apply" changes the configuration immediately. To keep it after a reboot, use "Save to flash".</p>
<p class="hint"><a href="/setup" style="color:var(--acc)">Set up the home WiFi network</a></p>
</form></div>
</aside></div></div>
<script>
const $=id=>document.getElementById(id);
let since=0,info=null,myId='';
const names={};
function hue(s){let h=0;for(const c of s)h=(h*31+c.charCodeAt(0))%360;return h}
function hhmm(ts,up){if(!ts&&up!==undefined){const h=Math.floor(up/3600),m=Math.floor(up/60)%60;return '+'+h+'h'+String(m).padStart(2,'0')}
const d=ts?new Date(ts*1000):new Date(),n=new Date(),hm=d.toTimeString().slice(0,5);
return d.toDateString()===n.toDateString()?hm:String(d.getDate()).padStart(2,'0')+'/'+String(d.getMonth()+1).padStart(2,'0')+' '+hm}
function line(parts){const log=$('log'),stick=log.scrollTop+log.clientHeight>=log.scrollHeight-30;
const div=document.createElement('div');div.className='l';
for(const [txt,cls,style] of parts){const sp=document.createElement('span');sp.textContent=txt;if(cls)sp.className=cls;if(style)sp.style.cssText=style;div.appendChild(sp)}
log.appendChild(div);while(log.children.length>500)log.removeChild(log.firstChild);if(stick)log.scrollTop=log.scrollHeight;return div}
const txEl={};
function rbName(rb){rb=rb.toLowerCase();for(const k in names)if(k.endsWith(rb))return names[k];return rb}
function nick(id,sn){const n=sn||names[id]||id;return['<'+n+'>','n','color:hsl('+hue(id)+' 60% 50%)']}
function addEv(e,hist){const t=['['+hhmm(e.ts,e.up)+'] ','t'];
if(e.t==='rx'){if(e.sn)names[e.from]=e.sn;
const via=e.hops===0?', direct':(e.rb&&e.rb!=='00'?', via '+rbName(e.rb):'');
const meta=e.rssi+' dBm, '+e.snr+' dB'+(e.hops>=0?', '+e.hops+(e.hops===1?' hop':' hops'):'')+via+(e.to!=='!ffffffff'?', to '+e.to:'');
line([t,nick(e.from,e.sn),[' '+e.txt],[meta,'m']])}
else if(e.t==='tx'){const d=line([t,['<'+e.sn+'>','n me'],[' '+e.txt],[hist?'':'waiting for echo…','m eco']]);
if(e.id&&!hist)txEl[e.id]={sp:d.lastChild,who:[]}}
else if(e.t==='retx'){const x=txEl[e.id];if(x&&!x.who.length)x.sp.textContent='no echo yet, retry '+e.k+'/'+e.of+'…'}
else if(e.t==='noeco'){const x=txEl[e.id];if(x&&!x.who.length){x.sp.textContent='✗ no relay heard'+(e.k>1?' after '+e.k+' attempts':'');x.sp.style.color='var(--warn)'}}
else if(e.t==='eco'){const x=txEl[e.id];const w=(e.rb==='00'?'a node with firmware older than 2.6':(e.sn||e.rb))+(info&&e.hl<info.hop-1?' (2nd hop)':'');
if(x){if(x.who.includes(w))return;x.who.push(w);x.sp.textContent='✓ relayed by '+x.who.length+': '+x.who.join(', ');x.sp.style.color='var(--acc)'}
else line([t,['-- echo of your message '+e.id+' relayed by '+w,'s']])}
else if(e.t==='node'){if(e.sn)names[e.from]=e.sn;line([t,['*** '+(e.sn||e.from)+' ('+(e.ln||'no name')+') '+e.from+' announced itself, '+e.rssi+' dBm','s']])}
else line([t,['-- '+e.txt,'s']])}
async function poll(){try{const j=await(await fetch('/api/ev?since='+since+'&seen='+(document.visibilityState==='visible'?1:0),{cache:'no-store'})).json();
if(j.next<since){since=0;line([['['+hhmm(0)+'] ','t'],['-- The modem rebooted; the message log is still stored','s','color:var(--warn)']]);setTimeout(poll,300);return}
j.ev.forEach(e=>addEv(e));since=j.next;online(true)}catch(e){online(false)}setTimeout(poll,1000)}
function online(ok){const p=$('p-net');if(!ok){p.innerHTML='<span class="dot off"></span>no connection to the modem';return}
if(info)p.innerHTML='<span class="dot"></span>'+(info.ssid||'')+(info.wrssi?' '+info.wrssi+' dBm':'')}
async function api(cmd){const r=await fetch('/api/at',{method:'POST',body:new URLSearchParams({cmd})});return r.text()}
function ago(s){return s<60?s+' s ago':s<3600?Math.round(s/60)+' min ago':Math.round(s/3600)+' h ago'}
async function loadInfo(fill){try{info=await(await fetch('/api/info',{cache:'no-store'})).json()}catch(e){return}
myId=info.node;$('p-me').innerHTML='<b></b> '+info.node;$('p-me').firstChild.textContent=info.sn;
$('p-rf').innerHTML='<b>'+info.pname+'</b> '+info.freq.toFixed(3)+' MHz, '+info.hop+' hops';
$('p-rly').innerHTML='<b>'+['Mute','Client','Router'][info.relay]+'</b> '+info.rly+' relayed, '+info.rlc+' canceled';
const pq=$('p-ble');pq.hidden=!info.ble;pq.innerHTML='<b>BLE</b> '+(info.ble===2?'connected':'waiting');
const pb=$('p-bat');pb.hidden=!info.batt;
if(info.batt){const s=Math.max(0,Math.min(100,info.soc)),col=s<15?'var(--bad)':s<40?'var(--warn)':'var(--acc)';
const st=info.usb?(info.crate>0.5?' USB ▲ charging':' USB'):' on battery';
pb.innerHTML='<span class="bat"><i style="width:'+(s*0.18).toFixed(1)+'px;background:'+col+'"></i></span><b>'+Math.round(s)+' %</b> '+info.vbat.toFixed(2)+' V'+st;
pb.title='Rate: '+info.crate.toFixed(1)+' %/h'}
online(true);
const nl=$('nodes');const ns=info.nodes.sort((a,b)=>a.ago-b.ago);
if(ns.length){nl.innerHTML='';ns.forEach(n=>{if(n.sn)names[n.id]=n.sn;const d=document.createElement('div');d.className='node';
d.innerHTML='<span class="sn"></span><span></span><span class="d"></span>';d.children[0].textContent=n.sn||'?';
d.children[0].style.color='hsl('+hue(n.id)+' 60% 50%)';d.children[1].textContent=n.ln||n.id;
d.children[2].textContent=n.id+', '+n.rssi+' dBm, '+n.snr+' dB, '+ago(n.ago);nl.appendChild(d)})}
if(fill){$('c-sn').value=info.sn;$('c-ln').value=info.ln;$('c-node').value=info.node;
const sel=$('c-preset');if(!sel.options.length)info.presets.forEach((p,i)=>sel.add(new Option(p,i)));sel.value=info.preset;
const band=info.f0.toFixed(3).replace(/\.?0+$/,'')+','+info.f1.toFixed(3).replace(/\.?0+$/,'');
const opt=[...$('c-reg').options].find(o=>o.value===band);$('c-reg').value=opt?band:'custom';
$('c-f0').value=info.f0;$('c-f1').value=info.f1;$('r-band').hidden=!!opt;
$('c-fov').value=info.fov;$('c-hop').value=info.hop;$('c-pwr').value=info.pwr;$('c-relay').value=info.relay}}
$('c-reg').onchange=()=>{const v=$('c-reg').value;$('r-band').hidden=v!=='custom';if(v!=='custom'){const[a,b]=v.split(',');$('c-f0').value=a;$('c-f1').value=b}};
async function run(cmd){const r=(await api(cmd)).trim();const ok=/OK$/.test(r);
line([['['+hhmm(0)+'] ','t'],['-- '+cmd+(ok?'':' → ERROR'),'s',ok?'':'color:var(--warn)']]);return r}
$('f-cfg').onsubmit=async e=>{e.preventDefault();const b=e.submitter;b.disabled=true;const c=[];
const sn=$('c-sn').value.trim(),ln=$('c-ln').value.trim();
if(sn!==info.sn||ln!==info.ln)c.push('AT+NAME='+ln+','+sn);
if($('c-node').value.trim().toLowerCase()!==info.node)c.push('AT+NODE='+$('c-node').value.trim());
if(+$('c-preset').value!==info.preset)c.push('AT+PRESET='+$('c-preset').value);
const f0=+$('c-f0').value,f1=+$('c-f1').value;if(Math.abs(f0-info.f0)>1e-4||Math.abs(f1-info.f1)>1e-4)c.push('AT+BAND='+f0+','+f1);
if(Math.abs(+$('c-fov').value-info.fov)>1e-5)c.push('AT+FREQ='+(+$('c-fov').value));
if(+$('c-hop').value!==info.hop)c.push('AT+HOP='+$('c-hop').value);
if(+$('c-pwr').value!==info.pwr)c.push('AT+PWR='+$('c-pwr').value);
if(+$('c-relay').value!==info.relay)c.push('AT+RELAY='+$('c-relay').value);
if(!c.length)line([['['+hhmm(0)+'] ','t'],['-- No changes to apply','s']]);
for(const x of c)await run(x);await loadInfo(true);b.disabled=false};
$('b-save').onclick=()=>run('AT+SAVE');
$('b-ni').onclick=()=>run('AT+NI');
$('msg').oninput=()=>$('cnt').textContent=new TextEncoder().encode($('msg').value).length;
$('f-in').onsubmit=async e=>{e.preventDefault();const v=$('msg').value.trim();if(!v)return;
const b=$('b-send');b.disabled=true;
if(v[0]==='/'){const cmd=v.slice(1);const r=await api(cmd);line([['['+hhmm(0)+'] ','t'],['> '+cmd,'s']]);
r.trim().split(/\r?\n/).forEach(x=>line([['        ','t'],[x,'s']]));loadInfo(/^AT\+(NAME|NODE|PRESET|BAND|FREQ|HOP|PWR)|^AT&F/i.test(cmd))}
else if(new TextEncoder().encode(v).length>232)line([['['+hhmm(0)+'] ','t'],['-- The message exceeds 232 bytes','s','color:var(--warn)']]);
else{const r=await api('AT+SEND='+v);if(!/OK\s*$/.test(r))line([['['+hhmm(0)+'] ','t'],['-- Could not transmit','s','color:var(--warn)']])}
$('msg').value='';$('cnt').textContent='0';b.disabled=false;$('msg').focus()};
$('b-clr').onclick=async()=>{if(!confirm('Erase the whole message log stored in the modem?'))return;await run('AT+LOGCLR');$('log').textContent=''};
async function loadLog(){try{const j=await(await fetch('/api/log?n=300',{cache:'no-store'})).json();
j.log.forEach(e=>{if(e.t==='sys')line([['['+hhmm(e.ts,e.up)+'] ','t'],['-- '+e.txt,'s']]);else addEv(e,true)});
if(j.log.length)line([['','t'],['---------- live ----------','s']]);since=j.next}catch(e){}}
loadInfo(true).then(()=>{if(info&&!info.time)api('AT+TIME='+Math.floor(Date.now()/1000))}).then(loadLog).then(poll);setInterval(()=>loadInfo(false),10000);
</script></body></html>)HTML";

// Spectrum analyzer (/spec): spectrum, max hold, average, waterfall and per-slot occupancy
const char PAGE_SPEC[] = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"><title>MeshModemSA: spectrum</title>
<style>
:root{--bg:#0d1322;--card:#151e33;--ink:#e7ecf5;--mut:#a3aec4;--line:#283450;--acc:#3cc3c4;--me:#a893ff;--sys:#76819a;--warn:#f2b53c;
--bad:#ff6b7a;--log:#10182a;--mono:ui-monospace,"SF Mono","Cascadia Mono",Consolas,"DejaVu Sans Mono",monospace;color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
padding:env(safe-area-inset-top,0) env(safe-area-inset-right,0) env(safe-area-inset-bottom,0) env(safe-area-inset-left,0)}
.app{max-width:1400px;margin:0 auto}
header{display:flex;flex-wrap:wrap;align-items:center;gap:8px 10px;padding:12px 14px;border-bottom:1px solid var(--line)}
header h1{font-size:1.05rem;margin:0;margin-right:auto}
.pill{font-size:.8rem;padding:3px 10px;border-radius:999px;background:var(--card);border:1px solid var(--line);color:var(--mut);white-space:nowrap}
.pill b{color:var(--ink);font-weight:600}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--acc);margin-right:6px;vertical-align:1px}
.dot.sa{background:var(--warn)}
button{font:inherit;font-weight:600;border:0;border-radius:8px;padding:8px 13px;background:var(--acc);color:#0d1322;cursor:pointer}
button.sec{background:transparent;color:var(--ink);border:1px solid var(--line)}
button.stop{background:var(--warn)}
button:disabled{opacity:.45;cursor:default}
button:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid var(--me);outline-offset:1px}
a.lnk{font-weight:600;padding:7px 12px;border:1px solid var(--line);border-radius:8px;color:var(--ink);text-decoration:none}
.note{margin:0;padding:8px 14px;font-size:.84rem;color:var(--mut);border-bottom:1px solid var(--line)}
.note.on{color:var(--warn)}
.body{display:grid;grid-template-columns:1fr 290px;gap:12px;padding:12px 14px}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:10px}
.st{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:8px 12px;min-width:0}
.st h3{margin:0;font-size:.72rem;font-weight:600;color:var(--mut);text-transform:uppercase;letter-spacing:.04em}
.st .v{font:600 1.25rem var(--mono);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.st .s{font-size:.76rem;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.plot{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:8px 0;min-width:0}
#sp{display:block;width:100%;height:340px;touch-action:none}
#wf{display:block;height:230px;margin:6px 10px 0 46px;width:calc(100% - 56px);background:#000008;image-rendering:pixelated}
aside{display:grid;gap:12px;align-content:start}
.box{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px;display:grid;gap:8px}
.box h2{font-size:.78rem;margin:0;color:var(--mut);text-transform:uppercase;letter-spacing:.04em}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
label{display:grid;gap:3px;font-size:.76rem;font-weight:600;color:var(--mut)}
input,select{font:14px var(--mono);padding:6px 8px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--ink);width:100%}
.chk{display:flex;flex-wrap:wrap;gap:6px 14px;font-size:.84rem}
.chk label{display:flex;align-items:center;gap:5px;font-weight:500}
.chk input{width:auto}
.btns{display:flex;flex-wrap:wrap;gap:6px}
.hint{font-size:.76rem;color:var(--mut);margin:0}
#msg{font:12px var(--mono);color:var(--mut);padding:0 14px 12px}
@media(max-width:860px){.body{grid-template-columns:1fr}.stats{grid-template-columns:1fr 1fr}.st .s{white-space:normal}#sp{height:280px}#wf{height:180px}}
</style></head><body><div class="app">
<header><h1>LoRa spectrum</h1>
<span class="pill"><span class="dot" id="dot"></span><span id="st">mesh active</span></span>
<span class="pill" id="p-left" hidden>—</span>
<button id="b-run">Start analyzer</button><a class="lnk" href="/">Chat</a></header>
<p class="note" id="note">The analyzer uses the same radio as the mesh: while it is active the node does not receive, send or relay packets. It returns to the mesh by itself when stopped, when this page is closed, or after 20 s without data requests from the page.</p>
<div class="body"><main>
<div class="stats">
<div class="st"><h3>Peak</h3><div class="v" id="s-pk">—</div><div class="s" id="s-pk2"></div></div>
<div class="st"><h3>Noise floor</h3><div class="v" id="s-fl">—</div><div class="s" id="s-fl2"></div></div>
<div class="st"><h3>Occupancy</h3><div class="v" id="s-oc">—</div><div class="s" id="s-oc2"></div></div>
<div class="st"><h3>Sweep</h3><div class="v" id="s-sw">—</div><div class="s" id="s-sw2"></div></div>
</div>
<div class="plot"><canvas id="sp"></canvas><canvas id="wf"></canvas></div>
</main>
<aside>
<div class="box"><h2>Sweep</h2>
<div class="row2"><label>Start [MHz]<input id="c-f0" type="number" step="0.001"></label><label>Stop [MHz]<input id="c-f1" type="number" step="0.001"></label></div>
<div class="row2"><label>Step [kHz]<input id="c-st" type="number" step="0.1" min="1"></label><label>RBW [kHz]<select id="c-rbw"></select></label></div>
<div class="row2"><label>Samples/point<input id="c-ns" type="number" min="1" max="64"></label><label>&nbsp;<button type="button" id="b-apply">Apply</button></label></div>
<p class="hint" id="pts"></p>
<div class="btns" id="presets"></div></div>
<div class="box"><h2>Display</h2>
<div class="row2"><label>Reference [dBm]<input id="v-ref" type="number" value="-20"></label><label>Range [dB]<input id="v-rng" type="number" value="100" min="10"></label></div>
<div class="row2"><label>Offset [dB]<input id="v-off" type="number" value="0" step="0.1"></label><label>Average [sweeps]<input id="v-avn" type="number" value="8" min="1"></label></div>
<div class="row2"><label>Occupancy threshold [dB]<input id="v-thr" type="number" value="10" min="1"></label><label>Grid<select id="v-grid">
<option value="slots">Meshtastic slots</option><option value="spi">SPI 8 MHz spurs</option><option value="none">No grid</option></select></label></div>
<div class="chk"><label><input type="checkbox" id="t-live" checked><span style="color:#fade2a">Live</span></label>
<label><input type="checkbox" id="t-max" checked><span style="color:#f2495c">Max hold</span></label>
<label><input type="checkbox" id="t-avg" checked><span style="color:#5794f2">Average</span></label></div>
<div class="btns"><button type="button" class="sec" id="b-reset">Reset traces</button><button type="button" class="sec" id="b-csv">CSV</button></div>
<p class="hint">Occupancy: fraction of sweeps in which the peak exceeds the noise floor plus the threshold (over the band and per slot, in the strip below the spectrum). It depends on the RBW and the threshold.</p></div>
</aside></div>
<div id="msg"></div></div>
<script>
const $=id=>document.getElementById(id);
const RBW=[4.8,5.8,7.3,9.7,11.7,14.6,19.5,23.4,29.3,39.0,46.9,58.6,78.2,93.8,117.3,156.2,187.2,234.3,312.0,373.6,467.0];
const ML=46,MR=10,MT=20,MB=44,STRIP=12;
const C={grid:'#23304a',grid2:'#3a4766',txt:'#a3aec4',live:'#fade2a',max:'#f2495c',avg:'#5794f2',pk:'#73bf69',own:'rgba(60,195,196,.16)',spi:'#ff9830'};
let on=false,mine=false,polling=false,info=null;
let n=0,f0=0,st=0,freqs=[],live=null,mx=null,avL=null,av=null,meta={};
let frames=0,occHit=0,occTot=0,slots=[],lastSeq=-1,tPrev=0,rate=0,curX=null,floorDb=null;
// Waterfall palette (same as lora_sa_dashboard.py)
const LUT=(()=>{const s=[[0,[0,0,8]],[.2,[10,20,90]],[.4,[20,90,200]],[.55,[30,190,200]],[.7,[110,220,80]],[.85,[250,220,40]],[.95,[250,80,40]],[1,[255,240,240]]];
const o=new Uint8Array(768);for(let i=0;i<256;i++){const t=i/255;for(let k=0;k<s.length-1;k++){const[t0,c0]=s[k],[t1,c1]=s[k+1];
if(t<=t1){const u=(t-t0)/(t1-t0);for(let j=0;j<3;j++)o[i*3+j]=Math.round(c0[j]+(c1[j]-c0[j])*u);break}}}return o})();
const lutCss=i=>{i=Math.max(0,Math.min(255,i|0));return'rgb('+LUT[i*3]+','+LUT[i*3+1]+','+LUT[i*3+2]+')'};
RBW.forEach(r=>$('c-rbw').add(new Option(r.toFixed(1),r)));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const msg=t=>{$('msg').textContent='['+new Date().toTimeString().slice(0,8)+'] '+t};
const num=(id,d)=>{const v=parseFloat($(id).value);return isFinite(v)?v:d};
const fmt=(v,d)=>v.toFixed(d);
async function at(cmd){const r=await fetch('/api/at',{method:'POST',body:new URLSearchParams({cmd})});return r.text()}
function parseSpec(t){const l=t.split(/\r?\n/).find(x=>/^\+SPEC:(ON|OFF),[0-9]/.test(x));
if(!l)return null;const f=l.slice(6).split(',');if(f.length<7||isNaN(+f[1]))return null;
return{on:f[0]==='ON',f0:+f[1],f1:+f[2],st:+f[3],rbw:+f[4],ns:+f[5],pts:+f[6]}}
function fill(s){$('c-f0').value=s.f0;$('c-f1').value=s.f1;$('c-st').value=s.st;$('c-rbw').value=s.rbw.toFixed(1);$('c-ns').value=s.ns;estPts()}
function estPts(){const a=num('c-f0',0),b=num('c-f1',0),s=num('c-st',0);const p=s>0?Math.floor((b-a)*1000/s+1e-6)+1:0;
const ok=p>=2&&p<=2048;$('pts').textContent=ok?p+' points (max 2048)'+(s>num('c-rbw',0)?' · step > RBW: gaps between points':''):'Invalid values (150 to 960 MHz, up to 2048 points)';
$('pts').style.color=ok?'':'var(--bad)'}
['c-f0','c-f1','c-st','c-rbw'].forEach(id=>$(id).addEventListener('input',estPts));
function setOn(v){on=v;$('dot').className='dot'+(v?' sa':'');$('st').textContent=v?'analyzer active · mesh suspended':'mesh active';
$('b-run').textContent=v?'Stop and return to the mesh':'Start analyzer';$('b-run').className=v?'stop':'';
$('note').className='note'+(v?' on':'');$('p-left').hidden=!v;if(!v)mine=false}
// ---------------------------------------------------------------- mesh slots
function buildSlots(){slots=[];if(!info||!info.bw||!info.nch||!n)return;const bw=info.bw/1000,a=freqs[0],b=freqs[n-1],sm=st/1000;
for(let j=0;j<info.nch;j++){const c=info.f0+bw/2+j*bw,lo=c-bw/2,hi=c+bw/2;if(hi<a||lo>b)continue;
let i0=Math.max(0,Math.ceil((lo-a)/sm-1e-9)),i1=Math.min(n-1,Math.floor((hi-a)/sm+1e-9));if(i1<i0){i0=i1=Math.min(n-1,Math.max(0,Math.round((c-a)/sm)))}
slots.push({j,c,lo,hi,i0,i1,hit:0,own:Math.abs(c-info.freq)<bw/4})}
if(info.fov>0&&!slots.some(s=>s.own))msg('Manual frequency '+fmt(info.freq,3)+' MHz: outside the slot grid')}
function resetTraces(){mx=null;avL=null;av=null;live=null;frames=0;occHit=0;occTot=0;slots.forEach(s=>s.hit=0);wfClear();draw()}
// ---------------------------------------------------------------- data
function frame(buf){if(buf.byteLength<32)return;const d=new DataView(buf);if(d.getUint8(0)!==83||d.getUint8(1)!==65)return;
meta={ns:d.getUint8(3),seq:d.getUint32(4,true),tus:d.getUint32(8,true),rbw:d.getFloat32(20,true),left:d.getUint16(28,true),settle:d.getUint16(30,true)};
const F0=d.getFloat32(12,true),ST=d.getFloat32(16,true),N=d.getUint16(24,true),na=d.getUint16(26,true);
$('p-left').innerHTML='<b>'+Math.floor(meta.left/60)+':'+String(meta.left%60).padStart(2,'0')+'</b> left';
if(!na||buf.byteLength<32+2*N)return;
if(N!==n||Math.abs(F0-f0)>1e-4||Math.abs(ST-st)>1e-4){n=N;f0=F0;st=ST;freqs=Array.from({length:n},(_,i)=>F0+i*ST/1000);buildSlots();resetTraces()}
const pk=new Uint8Array(buf,32,N),ar=new Uint8Array(buf,32+N,N),off=num('v-off',0);
live=Array.from(pk,v=>-v/2+off);const a=Array.from(ar,v=>-v/2+off);
if(!mx)mx=live.slice();else for(let i=0;i<n;i++)if(live[i]>mx[i])mx[i]=live[i];
const k=1/Math.max(1,num('v-avn',8));                                // exponential average in linear power
if(!avL)avL=a.map(v=>10**(v/10));else for(let i=0;i<n;i++)avL[i]+=k*(10**(a[i]/10)-avL[i]);
av=avL.map(p=>10*Math.log10(p));
const srt=av.slice().sort((x,y)=>x-y);floorDb=srt[srt.length>>1];         // floor = median of the average
const thr=floorDb+num('v-thr',10);let h=0;for(let i=0;i<n;i++)if(live[i]>thr)h++;
occHit+=h;occTot+=n;frames++;
for(const s of slots){let m=-999;for(let i=s.i0;i<=s.i1;i++)if(live[i]>m)m=live[i];if(m>thr)s.hit++}
const now=performance.now();if(lastSeq>=0&&meta.seq>lastSeq&&tPrev){const r=(meta.seq-lastSeq)/((now-tPrev)/1000);rate=rate?.8*rate+.2*r:r}
lastSeq=meta.seq;tPrev=now;
wfRow(live);draw();stats()}
function stats(){if(!live)return;let ip=0;for(let i=1;i<n;i++)if(live[i]>live[ip])ip=i;
$('s-pk').textContent=fmt(live[ip],1)+' dBm';$('s-pk2').textContent=fmt(freqs[ip],4)+' MHz';
const ktb=-174+10*Math.log10(meta.rbw*1e3);
$('s-fl').textContent=fmt(floorDb,1)+' dBm';$('s-fl2').textContent='kTB '+fmt(ktb,1)+' dBm · apparent NF ~'+fmt(floorDb-ktb,1)+' dB';
$('s-oc').textContent=occTot?fmt(100*occHit/occTot,1)+' %':'—';
const ow=slots.find(s=>s.own);$('s-oc2').textContent=frames+' sweeps'+(ow&&frames?' · own slot '+(ow.j+1)+': '+fmt(100*ow.hit/frames,0)+' %':'');
$('s-sw').textContent=Math.round(meta.tus/1000)+' ms';
$('s-sw2').textContent=fmt(rate,1)+' sweeps/s · '+n+' pts · RBW '+fmt(meta.rbw,1)+' kHz · N='+meta.ns}
// ---------------------------------------------------------------- spectrum
function ticks(a,b,t){const sp=b-a;if(sp<=0)return[];let s=sp/t;const m=10**Math.floor(Math.log10(s));
for(const k of[1,2,5,10])if(s<=k*m){s=k*m;break}const o=[];for(let v=Math.ceil(a/s)*s;v<=b+1e-9;v+=s)o.push(+v.toFixed(6));return o}
function draw(){const c=$('sp'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
if(c.width!==Math.round(W*dpr)||c.height!==Math.round(H*dpr)){c.width=Math.round(W*dpr);c.height=Math.round(H*dpr)}
const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
const x0=ML,y0=MT,x1=W-MR,y1=H-MB,ref=num('v-ref',-20),rng=Math.max(10,num('v-rng',100)),fl=ref-rng;
const A=n?freqs[0]:num('c-f0',902),B=n?freqs[n-1]:num('c-f1',928);if(B<=A)return;
const fx=f=>x0+(f-A)/(B-A)*(x1-x0),dy=v=>y0+(ref-Math.min(Math.max(v,fl),ref))/rng*(y1-y0);
g.font='11px ui-monospace,Consolas,monospace';g.lineWidth=1;
for(const v of ticks(fl,ref,8)){const y=Math.round(dy(v))+.5;g.strokeStyle=C.grid;g.beginPath();g.moveTo(x0,y);g.lineTo(x1,y);g.stroke();
g.fillStyle=C.txt;g.textAlign='right';g.textBaseline='middle';g.fillText(v.toFixed(0),x0-5,y)}
g.textAlign='center';g.textBaseline='top';
for(const f of ticks(A,B,Math.max(3,Math.floor((x1-x0)/80)))){const x=Math.round(fx(f))+.5;g.strokeStyle=C.grid;g.beginPath();g.moveTo(x,y0);g.lineTo(x,y1);g.stroke();
g.fillStyle=C.txt;g.fillText(String(f),x,y1+STRIP+8)}
g.textAlign='left';g.fillText('dBm',4,2);g.fillText('MHz',4,y1+STRIP+8);
const grid=$('v-grid').value;
if(grid==='slots'&&slots.length){for(const s of slots){const xa=fx(s.lo),xb=fx(s.hi);
if(s.own){g.fillStyle=C.own;g.fillRect(xa,y0,xb-xa,y1-y0)}
if((x1-x0)/slots.length>4){g.strokeStyle=C.grid;g.setLineDash([2,3]);g.beginPath();g.moveTo(Math.round(xa)+.5,y0);g.lineTo(Math.round(xa)+.5,y1);g.stroke();g.setLineDash([])}}}
if(grid==='spi'){g.strokeStyle=C.spi;g.setLineDash([2,3]);for(let f=Math.ceil(A/8)*8;f<=B;f+=8){const x=Math.round(fx(f))+.5;g.beginPath();g.moveTo(x,y0);g.lineTo(x,y1);g.stroke()}g.setLineDash([])}
g.strokeStyle=C.grid;g.strokeRect(x0+.5,y0+.5,x1-x0,y1-y0);
// per-slot occupancy strip
const ys=y1+3;g.fillStyle='#0a0f1c';g.fillRect(x0,ys,x1-x0,STRIP);
for(const s of slots){const xa=Math.max(x0,fx(s.lo)),xb=Math.min(x1,fx(s.hi)),o=frames?s.hit/frames:0;
g.fillStyle=lutCss(o*255);g.fillRect(xa+.5,ys,Math.max(1,xb-xa-1),STRIP);if(s.own){g.strokeStyle='#3cc3c4';g.strokeRect(xa+.5,ys+.5,Math.max(1,xb-xa-1),STRIP-1)}}
if(!n){g.fillStyle=C.txt;g.textAlign='center';g.textBaseline='middle';g.fillText(on?'waiting for the first sweep':'analyzer stopped',(x0+x1)/2,(y0+y1)/2);return}
const tr=(vals,col)=>{if(!vals)return;g.strokeStyle=col;g.beginPath();const w=x1-x0;
if(n<=w){for(let i=0;i<n;i++){const x=fx(freqs[i]),y=dy(vals[i]);i?g.lineTo(x,y):g.moveTo(x,y)}}
else{let px=-1,m=-999,first=true;for(let i=0;i<=n;i++){const p=i<n?Math.floor(fx(freqs[i])):-2;  // decimation: maximum per pixel column
if(p!==px&&px>=0){const y=dy(m);first?g.moveTo(px,y):g.lineTo(px,y);first=false;m=-999}if(i<n){px=p;if(vals[i]>m)m=vals[i]}}}
g.stroke()};
if($('t-avg').checked)tr(av,C.avg);if($('t-max').checked)tr(mx,C.max);if($('t-live').checked)tr(live,C.live);
if(live){let ip=0;for(let i=1;i<n;i++)if(live[i]>live[ip])ip=i;const x=fx(freqs[ip]),y=dy(live[ip]);
g.fillStyle=C.pk;g.beginPath();g.moveTo(x,y-7);g.lineTo(x-5,y-14);g.lineTo(x+5,y-14);g.fill();
g.textAlign=x>(x0+x1)/2?'right':'left';g.textBaseline='bottom';g.fillText(fmt(freqs[ip],3)+' MHz  '+fmt(live[ip],1)+' dBm',x+(x>(x0+x1)/2?-8:8),y-8)}
if(curX!==null&&curX>=x0&&curX<=x1){const f=A+(curX-x0)/(x1-x0)*(B-A),i=Math.min(n-1,Math.max(0,Math.round((f-A)/(st/1000)))),xc=Math.round(fx(freqs[i]))+.5;
g.strokeStyle=C.txt;g.setLineDash([1,2]);g.beginPath();g.moveTo(xc,y0);g.lineTo(xc,y1+STRIP+3);g.stroke();g.setLineDash([]);
let t=fmt(freqs[i],4)+' MHz';if(live)t+='  live '+fmt(live[i],1);if(mx)t+='  max '+fmt(mx[i],1);if(av)t+='  avg '+fmt(av[i],1);t+=' dBm';
const s=slots.find(s=>freqs[i]>=s.lo&&freqs[i]<=s.hi);if(s)t+='  · slot '+(s.j+1)+(frames?' '+fmt(100*s.hit/frames,0)+' %':'');
g.textAlign='right';g.textBaseline='top';const tw=g.measureText(t).width;g.fillStyle='rgba(13,19,34,.85)';g.fillRect(x1-tw-10,y0+3,tw+8,16);
g.fillStyle='#e7ecf5';g.fillText(t,x1-6,y0+5)}}
const pos=e=>{const r=$('sp').getBoundingClientRect(),p=e.touches?e.touches[0]:e;return p.clientX-r.left};
$('sp').addEventListener('pointermove',e=>{curX=pos(e);draw()});
$('sp').addEventListener('pointerdown',e=>{curX=pos(e);draw()});
$('sp').addEventListener('pointerleave',e=>{if(e.pointerType==='mouse'){curX=null;draw()}});
// ---------------------------------------------------------------- waterfall
const wf=$('wf');let wg=null,wW=0,wH=0;
function wfClear(){const dpr=window.devicePixelRatio||1;wW=Math.max(10,Math.round(wf.clientWidth*dpr));wH=Math.max(10,Math.round(wf.clientHeight*dpr));
wf.width=wW;wf.height=wH;wg=wf.getContext('2d');wg.fillStyle='#000008';wg.fillRect(0,0,wW,wH)}
function wfRow(vals){if(!wg||!n)return;const dpr=window.devicePixelRatio||1,RH=Math.max(1,Math.round(2*dpr));
wg.drawImage(wf,0,0,wW,wH-RH,0,RH,wW,wH-RH);
const ref=num('v-ref',-20),rng=Math.max(10,num('v-rng',100)),fl=ref-rng,k=255/rng,img=wg.createImageData(wW,RH),D=img.data;
for(let px=0;px<wW;px++){const lo=Math.floor(px*(n-1)/wW+.5),hi=Math.floor((px+1)*(n-1)/wW+.5);let v=vals[lo];
for(let i=lo+1;i<hi;i++)if(vals[i]>v)v=vals[i];const c=Math.max(0,Math.min(255,((v-fl)*k)|0))*3;
for(let r=0;r<RH;r++){const o=(r*wW+px)*4;D[o]=LUT[c];D[o+1]=LUT[c+1];D[o+2]=LUT[c+2];D[o+3]=255}}
wg.putImageData(img,0,0)}
// ---------------------------------------------------------------- control
async function poll(){if(polling)return;polling=true;
while(on){if(document.visibilityState!=='visible'){await sleep(400);continue}
try{const r=await fetch('/api/spec',{cache:'no-store'});
if(r.status===409){setOn(false);msg('The modem returned to the mesh');draw();break}
frame(await r.arrayBuffer())}catch(e){msg('No connection to the modem')}
await sleep(250)}
polling=false}
$('b-run').onclick=async()=>{const b=$('b-run');b.disabled=true;
try{if(!on){const r=await at('AT+SPEC=1');if(/OK\s*$/.test(r)){mine=true;lastSeq=-1;rate=0;setOn(true);resetTraces();msg('Analyzer active: mesh suspended');poll()}
else msg('Could not start: '+r.trim().split(/\r?\n/)[0])}
else{await at('AT+SPEC=0');setOn(false);msg('Back on the mesh');draw()}}catch(e){msg('No connection to the modem')}
b.disabled=false};
$('b-apply').onclick=async()=>{const c='AT+SPECCFG='+num('c-f0',0)+','+num('c-f1',0)+','+num('c-st',0)+','+$('c-rbw').value+','+Math.round(num('c-ns',4));
try{const r=await at(c);if(/OK\s*$/.test(r)){const s=parseSpec(r);if(s)fill(s);n=0;resetTraces();msg('Sweep applied')}
else msg('Configuration rejected (150 to 960 MHz, step ≥ 1 kHz, up to 2048 points, 1 to 64 samples)')}catch(e){msg('No connection to the modem')}};
$('b-reset').onclick=resetTraces;
$('b-csv').onclick=()=>{if(!n||!live){msg('No sweep to save');return}
let t='# MeshModemSA RBW_kHz='+meta.rbw+' N='+meta.ns+' settle_us='+meta.settle+' offset_dB='+num('v-off',0)+' threshold_dB='+num('v-thr',10)+' sweeps='+frames+'\nfreq_MHz,live_dBm,max_dBm,avg_dBm\n';
for(let i=0;i<n;i++)t+=freqs[i].toFixed(6)+','+live[i]+','+(mx?mx[i]:'')+','+(av?av[i].toFixed(2):'')+'\n';
if(slots.length){t+='\n# per-slot occupancy (sweeps above the threshold / total)\nslot,center_MHz,occupancy\n';for(const s of slots)t+=(s.j+1)+','+s.c.toFixed(4)+','+(frames?(s.hit/frames).toFixed(4):'')+'\n'}
const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([t],{type:'text/csv'}));
a.download='meshmodem_sa_'+new Date().toISOString().replace(/[:T]/g,'-').slice(0,19)+'.csv';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),2000)};
function presets(){const p=[];if(info)p.push(['Node band',info.f0,info.f1]);p.push(['902–928',902,928],['915–928',915,928],['863–870',863,870]);
const box=$('presets');box.innerHTML='';for(const[nm,a,b]of p){const e=document.createElement('button');e.type='button';e.className='sec';e.textContent=nm;
e.onclick=()=>{$('c-f0').value=a;$('c-f1').value=b;if(b-a>50){$('c-st').value=200;$('c-rbw').value='234.3'}estPts();$('b-apply').click()};box.appendChild(e)}}
['v-ref','v-rng','v-grid','t-live','t-max','t-avg'].forEach(id=>$(id).addEventListener('change',()=>draw()));
$('v-off').addEventListener('change',resetTraces);
function wfResize(){if(!wg){wfClear();return}const dpr=window.devicePixelRatio||1,nw=Math.max(10,Math.round(wf.clientWidth*dpr)),nh=Math.max(10,Math.round(wf.clientHeight*dpr));
if(nw===wW&&nh===wH)return;const o=document.createElement('canvas');o.width=wW;o.height=wH;o.getContext('2d').drawImage(wf,0,0);
const ow=wW,oh=wH;wfClear();wg.drawImage(o,0,0,ow,Math.min(oh,wH),0,0,wW,Math.min(oh,wH))}   // width scaled, rows untouched
window.addEventListener('resize',()=>{wfResize();draw()});
window.addEventListener('pagehide',()=>{if(on&&mine&&navigator.sendBeacon)navigator.sendBeacon('/api/at',new URLSearchParams({cmd:'AT+SPEC=0'}))});
(async()=>{wfClear();try{info=await(await fetch('/api/info',{cache:'no-store'})).json()}catch(e){}
presets();
try{const s=parseSpec(await at('AT+SPEC?'));if(s){fill(s);if(s.on){setOn(true);msg('The analyzer was already active (started from another interface)');poll()}}}catch(e){msg('No connection to the modem')}
draw()})();
</script></body></html>)HTML";

// ======================= Web server =========================================
void hRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", portal ? PAGE_PORTAL : PAGE_MAIN);
}

void hAt() {                                                         // POST /api/at  cmd=...
  String cmd = server.arg("cmd"); cmd.replace("\r", " "); cmd.replace("\n", " ");
  String out;
  io.printf("[web] %s\r\n", cmd.c_str());
  io.cap = &out; at(cmd); io.cap = nullptr;
  server.send(200, "text/plain; charset=utf-8", out);
}

void hInfo() {                                                       // GET /api/info
  const Preset& p = PRESETS[cfg.preset];
  String j = "{\"node\":\"" + hexId(cfg.node) + "\",\"ln\":" + jStr(cfg.ln) + ",\"sn\":" + jStr(cfg.sn) +
             ",\"preset\":" + String(cfg.preset) + ",\"pname\":\"" + p.name + "\",\"f0\":" + String(cfg.f0, 3) +
             ",\"f1\":" + String(cfg.f1, 3) + ",\"fov\":" + String(cfg.fov, 4) + ",\"freq\":" + String(freq, 4) +
             ",\"slot\":" + String(slot + 1) + ",\"nch\":" + String(nCh) + ",\"hash\":" + String(chHash) +
             ",\"hop\":" + String(cfg.hop) + ",\"pwr\":" + String(cfg.pwr) + ",\"relay\":" + String(cfg.relay) +
             ",\"rly\":" + String(stRelayed) + ",\"rlc\":" + String(stCanceled) + ",\"slotms\":" + String(slotMs, 1) +
             ",\"ssid\":" + jStr(apMode ? ("own network " + WiFi.softAPSSID()).c_str() : wSsid.c_str()) +
             ",\"ip\":\"" + (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() +
             "\",\"wrssi\":" + String(wifiUp ? WiFi.RSSI() : 0) + ",\"up\":" + String(millis() / 1000) +
             ",\"time\":" + String(nowTs()) +
             ",\"batt\":" + String(battOk ? 1 : 0) + ",\"soc\":" + String(min(battSoc, 100.0f), 1) +
             ",\"vbat\":" + String(battV, 3) + ",\"crate\":" + String(battRate, 1) +
             ",\"usb\":" + String(usbPower() ? 1 : 0) + ",\"unread\":" + String(unread) +
             ",\"ble\":" + String(bleOn ? (bleConn ? 2 : 1) : 0) +
             ",\"bw\":" + String(p.bw, 1) + ",\"spec\":" + String(specOn ? 1 : 0) + ",\"presets\":[";
  for (int i = 0; i < NPRE; i++) j += String(i ? "," : "") + "\"" + PRESETS[i].name + "\"";
  j += "],\"nodes\":[";
  bool first = true;
  for (auto& n : nodes) if (n.id) {
    j += String(first ? "" : ",") + "{\"id\":\"" + hexId(n.id) + "\",\"sn\":" + jStr(n.sn) + ",\"ln\":" + jStr(n.ln) +
         ",\"rssi\":" + String(n.rssi, 0) + ",\"snr\":" + String(n.snr, 1) + ",\"ago\":" + String((millis() - n.seen) / 1000) + "}";
    first = false;
  }
  server.send(200, "application/json", j + "]}");
}

void hEv() {                                                         // GET /api/ev?since=N
  uint32_t since = server.arg("since").toInt();
  if (server.arg("seen") == "1") unread = 0;                         // the page is visible: messages read
  uint32_t from = evSeq > NEV ? evSeq - NEV : 0;
  if (since > evSeq) since = 0;                                      // the device rebooted
  if (since < from) since = from;
  String j = "{\"next\":" + String(evSeq) + ",\"ev\":[";
  for (uint32_t s = since; s < evSeq; s++) j += (s > since ? "," : "") + evBuf[s % NEV];
  server.send(200, "application/json", j + "]}");
}

void hLog() {                                                        // GET /api/log?n=N
  int n = server.hasArg("n") ? server.arg("n").toInt() : 300;
  if (n < 1 || n > 500) n = 300;
  std::vector<String> v; logTail(n, v);
  String j; j.reserve(256 + v.size() * 160);
  j = "{\"next\":" + String(evSeq) + ",\"log\":[";
  for (size_t i = 0; i < v.size(); i++) { if (i) j += ","; j += v[i]; }
  server.send(200, "application/json", j + "]}");
}

void hLogDl() {                                                      // GET /log.jsonl (full download)
  server.sendHeader("Content-Disposition", "attachment; filename=meshmodem_log.jsonl");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/x-ndjson", "");
  char buf[1024];
  for (const char* path : {LOG_OLD, LOG_FILE}) {
    File f = LittleFS.open(path, "r");
    if (!f) continue;
    while (f.available()) { size_t n = f.read((uint8_t*)buf, sizeof(buf)); server.sendContent(buf, n); }
    f.close();
  }
  server.sendContent("");
}

void hScan() {                                                       // GET /scan (portal)
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) j += String(i ? "," : "") + "{\"s\":" + jStr(WiFi.SSID(i).c_str()) + ",\"r\":" + String(WiFi.RSSI(i)) + "}";
  WiFi.scanDelete();
  server.send(200, "application/json", j + "]");
}

void hWifi() {                                                       // POST /wifi (portal)
  wSsid = server.arg("ssid"); wPass = server.arg("pass");
  String shown = wSsid; shown.replace("&", "&amp;"); shown.replace("<", "&lt;");
  nvs.putString("ssid", wSsid); nvs.putString("pass", wPass);
  io.printf("[web] WiFi saved: %s\r\n", wSsid.c_str());
  server.send(200, "text/html; charset=utf-8",
    "<meta name=viewport content='width=device-width'><body style='font-family:system-ui;padding:24px'>"
    "<h2>Saved</h2><p>The modem is connecting to <b>" + shown + "</b>. Go back to your WiFi network and open "
    "<a href='http://" HOSTNAME ".local/'>http://" HOSTNAME ".local/</a> or the IP shown by ATI on the terminal.</p>");
  tWifiPending = millis() | 1;
}

void hSetup() {                                                      // GET /setup: enter the home network
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", PAGE_PORTAL);
}

void hSpecPage() {                                                   // GET /spec: analyzer page
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", PAGE_SPEC);
}

// GET /api/spec: renews the lease and returns the last sweep in binary (little-endian)
//   0 'S' 'A' ver samples | 4 seq u32 | 8 t_us u32 | 12 f0 MHz f32 | 16 step kHz f32 | 20 RBW kHz f32
//  24 n u16 | 26 accumulated sweeps u16 (0 = no new data) | 28 s left u16 | 30 settle us u16
//  32 peak[n] (maximum since the previous read) | 32+n average[n] (last sweep); 1 B = -2*P[dBm]
void hSpec() {
  server.sendHeader("Cache-Control", "no-store");
  if (!specOn) { server.send(409, "text/plain", "OFF"); return; }
  saLease = millis();
  uint8_t* b = saBuf;
  float f0 = sa.f0, st = sa.step, rbw = sa.rbw;
  uint16_t n = sa.n, na = saNacc, settle = sa.settle > 65535 ? 65535 : sa.settle;
  uint32_t el = millis() - saStart;
  uint16_t left = el < SA_MAX_MS ? (uint16_t)((SA_MAX_MS - el) / 1000) : 0;
  b[0] = 'S'; b[1] = 'A'; b[2] = 1; b[3] = sa.ns;
  memcpy(b + 4, &saSeq, 4); memcpy(b + 8, &saTus, 4); memcpy(b + 12, &f0, 4); memcpy(b + 16, &st, 4);
  memcpy(b + 20, &rbw, 4); memcpy(b + 24, &n, 2); memcpy(b + 26, &na, 2); memcpy(b + 28, &left, 2);
  memcpy(b + 30, &settle, 2);
  size_t L = SA_HDR;
  if (na) { memcpy(b + L, saAcc, n); memcpy(b + L + n, saOut, n); L += 2 * (size_t)n; saNacc = 0; }
  server.send_P(200, "application/octet-stream", (PGM_P)b, L);
}

void hNotFound() {
  if (portal) { server.sendHeader("Location", "http://192.168.4.1/"); server.send(302, "text/plain", ""); }
  else server.send(404, "text/plain", "not found");
}

// ======================= Setup / loop =======================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_DIO1, INPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  nvs.begin("mmweb", false);
  Cfg c;
  if (nvs.getBytes("cfg", &c, sizeof(c)) == sizeof(c) && c.magic == DEF.magic) cfg = c;
  wSsid = nvs.getString("ssid", ""); wPass = nvs.getString("pass", "");
  wifiMode = nvs.getUChar("wmode", 0); if (wifiMode > 2) wifiMode = 0;
  retryMax = nvs.getUChar("retry", RETRY_DEF); if (retryMax > 3) retryMax = RETRY_DEF;
  diagReset();
  apPass = nvs.getString("appass", AP_PASS);
  setenv("TZ", TZ_STR, 1); tzset();                                  // local time even without NTP
  // Message log: uses the "spiffs" partition (partitions.csv or 4 MB schemes) or, if it does not
  // exist, the "ffat" one of the IDE's "16M Flash (... FATFS)" schemes. Formats only the first time.
  const char* fsLabel = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs")
                        ? "spiffs" : "ffat";
  fsOk = LittleFS.begin(true, "/littlefs", 10, fsLabel);
  if (HAS_BATT) battInit();                                          // no fuel gauge: battOk stays false
  if (HAS_LEDS) { pinMode(PIN_LED_RED, OUTPUT); pinMode(PIN_LED_GREEN, OUTPUT); }
  if (PIN_PGOOD >= 0) pinMode(PIN_PGOOD, INPUT_PULLUP);              // /PGOOD is open-drain

  aesKeyExpand(PSK);
  derive();
  spiLora.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  const Preset& p = PRESETS[cfg.preset];
  int e = radioBegin(freq, p.bw, p.sf, p.cr, cfg.pwr);              // sync word 0x2B, preamble 16
  radio.setDio2AsRfSwitch(DIO2_RFSW);
  radio.setCurrentLimit(140);
  radio.setRxBoostedGainMode(true);
  radio.startReceive();
  radioErr = e;
  saInit();                                                          // analyzer off: configuration only

  server.on("/", HTTP_GET, hRoot);
  server.on("/spec", HTTP_GET, hSpecPage);
  server.on("/api/spec", HTTP_GET, hSpec);
  server.on("/api/at", HTTP_POST, hAt);
  server.on("/api/info", HTTP_GET, hInfo);
  server.on("/api/ev", HTTP_GET, hEv);
  server.on("/scan", HTTP_GET, hScan);
  server.on("/api/log", HTTP_GET, hLog);
  server.on("/log.jsonl", HTTP_GET, hLogDl);
  server.on("/wifi", HTTP_POST, hWifi);
  server.on("/setup", HTTP_GET, hSetup);
  server.onNotFound(hNotFound);

  io.printf("\r\n+READY:MeshModemSA v3.9 (Mesh_Modem_Web_v3_TCXO_BLE v3.7 + SA + diagnostics),RADIO=%d,BOOT=%s  (AT+HELP = commands)\r\n", e, resetReason());
  bleOn = nvs.getUChar("ble", BLE_ENABLE); blePin = nvs.getULong("blepin", BLE_PIN);
  if (bleOn) bleInit();
  wifiStart();
  info();
  logEv("\"t\":\"sys\",\"txt\":" + jStr((String("Modem started (") + resetReason() + ")").c_str()));
  if (!e) sendNodeInfo();
}

void loop() {
  static String line; static uint32_t tNI = millis();
  receive();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') { if (line.length()) unread = 0; at(line); line = ""; }   // terminal in use: messages read
    else if (line.length() < 240) line += c;
  }
  server.handleClient();
  wifiLoop();
  bootButton();
  if (bleSrv) bleLoop();
  if (HAS_LEDS) ledsLoop();
  ownLoop();                                                          // retries and "no relay heard" (3 interfaces)
  nfSample();                                                         // noise floor for AT+DIAG
  static uint32_t tBatt = millis();
  if (millis() - tBatt >= BATT_PERIOD_MS) { tBatt = millis(); battRead(); }
  serviceRelay();
  if (millis() - tNI >= NI_PERIOD_MS) { tNI = millis(); sendNodeInfo(); }   // with the analyzer on: stays pending
  if (specOn) specLoop();                                            // one slice of the sweep
}
