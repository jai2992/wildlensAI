/**
 * WildLens — Universal Mesh Node
 *
 * Every ESP32 in the network runs this SAME firmware.
 * Each node can:
 *   1. Originate captures  (serial input → GPIO pulse → Pi response → broadcast)
 *   2. Relay packets       (receive from any peer → forward to all other peers)
 *
 * Loop prevention  : packets carrying this node's own MAC as origin are dropped.
 * Hop limiting     : MAX_HOPS hard-caps the relay depth.
 * Non-blocking     : millis()-based FSM — receive/relay while waiting for Pi.
 *
 * Topology         : Mesh — add as many PEER_MACs as needed in setup().
 *
 * Author : WildLens Project
 * Board  : ESP32 (Arduino framework)
 */

#include <WiFi.h>
#include <esp_now.h>
#include <string.h>

// ─────────────────────────────────────────────
//  CONFIGURATION
// ─────────────────────────────────────────────

#define GPIO_TRIGGER_PIN       4
#define TRIGGER_PULSE_MS       200
#define PI_RESPONSE_TIMEOUT_MS 8000
#define MAX_HOPS               5        // drop packet if hop count exceeds this
#define MAX_PEERS              10       // maximum mesh peers this node tracks

// ── Peer MACs ────────────────────────────────
// List every neighbour this node should forward to.
// Add / remove entries freely; MAX_PEERS is the ceiling.
uint8_t PEER_MACS[][6] = {
  { 0x48, 0x55, 0x19, 0xE0, 0xA9, 0x9C },   // peer 0  (downstream receiver / next hop)
  // { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }, // peer 1  — uncomment to add more
};
const int PEER_COUNT = sizeof(PEER_MACS) / 6;

// ─────────────────────────────────────────────
//  DATA STRUCTURES
// ─────────────────────────────────────────────

struct MeshPacket {
  uint8_t  origin_mac[6];   // MAC of the node that generated this data
  uint8_t  hop_count;       // incremented at every relay hop
  char     text[240];       // JSON payload from Pi (null-terminated)
};                          // total ≤ 250 bytes (ESP-NOW limit)

// ─────────────────────────────────────────────
//  STATE MACHINE FOR LOCAL CAPTURE
// ─────────────────────────────────────────────

enum CaptureState {
  IDLE,
  TRIGGER_HIGH,
  WAITING_FOR_PI,
  SENDING
};

static CaptureState captureState  = IDLE;
static unsigned long stateStart   = 0;
static String        piResponse   = "";

// ─────────────────────────────────────────────
//  GLOBALS
// ─────────────────────────────────────────────

static uint8_t myMAC[6];

// ─────────────────────────────────────────────
//  HELPERS — PEER MANAGEMENT
// ─────────────────────────────────────────────

/**
 * Register a MAC as an ESP-NOW peer if not already known.
 * Called on startup for configured peers, and dynamically
 * when we receive a packet from an unknown sender.
 */
bool registerPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) == ESP_OK) {
    Serial.printf("[MESH] Peer registered: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
  }
  Serial.println("[MESH] ✗ Failed to register peer");
  return false;
}

// ─────────────────────────────────────────────
//  HELPERS — PACKET FORWARDING
// ─────────────────────────────────────────────

/**
 * Forward a MeshPacket to every registered peer except the one
 * it just arrived from (srcMac). Increments hop_count in place.
 *
 * @param pkt     Packet to forward (modified: hop_count++)
 * @param srcMac  MAC to skip (the sender we received from); NULL = skip none
 */
void forwardPacket(MeshPacket& pkt, const uint8_t* srcMac) {
  pkt.hop_count++;

  for (int i = 0; i < PEER_COUNT; i++) {
    if (srcMac && memcmp(PEER_MACS[i], srcMac, 6) == 0) continue; // don't echo back

    esp_err_t err = esp_now_send(PEER_MACS[i], (uint8_t*)&pkt, sizeof(pkt));
    if (err == ESP_OK) {
      Serial.printf("[MESH] ↷ Forwarded to %02X:%02X:%02X (hop %d)\n",
        PEER_MACS[i][0], PEER_MACS[i][1], PEER_MACS[i][2], pkt.hop_count);
    } else {
      Serial.printf("[MESH] ✗ Forward error %d\n", err);
    }
  }
}

// ─────────────────────────────────────────────
//  HELPERS — LOCAL ORIGINATION
// ─────────────────────────────────────────────

/**
 * Build a MeshPacket from a Pi response string and broadcast
 * it to all peers as an originated (hop 0) message.
 */
void originatePacket(const String& payload) {
  MeshPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  memcpy(pkt.origin_mac, myMAC, 6);
  pkt.hop_count = 0;

  String safe = payload;
  if (safe.length() >= sizeof(pkt.text)) {
    safe = safe.substring(0, sizeof(pkt.text) - 1);
    Serial.println("[MESH] ⚠ Payload truncated to 239 chars");
  }
  safe.toCharArray(pkt.text, sizeof(pkt.text));

  Serial.println("[MESH] ◉ Originating: " + safe);
  forwardPacket(pkt, nullptr);  // send to ALL peers; srcMac = nullptr (skip none)
}

// ─────────────────────────────────────────────
//  ESP-NOW CALLBACKS
// ─────────────────────────────────────────────

void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS
    ? "[ESP-NOW] ✓ Sent OK"
    : "[ESP-NOW] ✗ Send failed");
}

/**
 * Receive callback — runs in WiFi task context, keep it short.
 *
 * Decision tree:
 *   1. Auto-register unknown sender as peer (bidirectional ACK support).
 *   2. Drop if origin is our own MAC           (loop prevention).
 *   3. Drop if hop_count >= MAX_HOPS           (depth limit).
 *   4. Log the payload for local use.
 *   5. Forward to all other peers.
 */
void onDataReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(MeshPacket)) {
    Serial.println("[MESH] ✗ Packet too short — discarded");
    return;
  }

  MeshPacket pkt;
  memcpy(&pkt, data, sizeof(MeshPacket));

  const uint8_t* senderMac = info->src_addr;

  // ── 1. Auto-register unknown sender ──────────
  registerPeer(senderMac);

  // ── 2. Loop prevention ────────────────────────
  if (memcmp(pkt.origin_mac, myMAC, 6) == 0) {
    Serial.println("[MESH] ↩ Own packet returned — dropped");
    return;
  }

  // ── 3. Hop limit ──────────────────────────────
  if (pkt.hop_count >= MAX_HOPS) {
    Serial.printf("[MESH] ✗ Max hops (%d) reached — dropped\n", MAX_HOPS);
    return;
  }

  // ── 4. Local logging ──────────────────────────
  Serial.printf("\n[MESH] ▼ Received (origin %02X:%02X:%02X, hop %d):\n  %s\n",
    pkt.origin_mac[0], pkt.origin_mac[1], pkt.origin_mac[2],
    pkt.hop_count, pkt.text);

  // ── 5. Relay onward ───────────────────────────
  forwardPacket(pkt, senderMac);
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("   WildLens ESP32 — Universal Mesh Node");
  Serial.println("========================================");

  // GPIO
  pinMode(GPIO_TRIGGER_PIN, OUTPUT);
  digitalWrite(GPIO_TRIGGER_PIN, LOW);

  // Read own MAC
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_read_mac(myMAC, ESP_MAC_WIFI_STA);
  Serial.printf("[NODE] My MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    myMAC[0], myMAC[1], myMAC[2], myMAC[3], myMAC[4], myMAC[5]);

  // ESP-NOW init
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] ✗ Init failed — halting");
    while (true) delay(1000);
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  // Register configured peers
  for (int i = 0; i < PEER_COUNT; i++) {
    registerPeer(PEER_MACS[i]);
  }

  Serial.println("\n✓ Mesh node ready — type anything to capture\n");
}

// ─────────────────────────────────────────────
//  MAIN LOOP  (non-blocking FSM)
// ─────────────────────────────────────────────

void loop() {

  switch (captureState) {

    // ── IDLE: watch for serial input ────────────
    case IDLE:
      if (Serial.available()) {
        while (Serial.available()) Serial.read();  // flush command bytes
        piResponse = "";

        Serial.println("[GPIO] Trigger → HIGH");
        digitalWrite(GPIO_TRIGGER_PIN, HIGH);
        stateStart   = millis();
        captureState = TRIGGER_HIGH;
      }
      break;

    // ── TRIGGER_HIGH: hold pin for pulse duration ─
    case TRIGGER_HIGH:
      if (millis() - stateStart >= TRIGGER_PULSE_MS) {
        digitalWrite(GPIO_TRIGGER_PIN, LOW);
        Serial.println("[GPIO] Trigger → LOW");
        Serial.println("[PI]  Waiting for response...");
        stateStart   = millis();
        captureState = WAITING_FOR_PI;
      }
      break;

    // ── WAITING_FOR_PI: accumulate serial chars ───
    case WAITING_FOR_PI:
      while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
          captureState = SENDING;  // complete line received
          break;
        }
        piResponse += c;
      }

      if (captureState != SENDING &&
          millis() - stateStart >= PI_RESPONSE_TIMEOUT_MS) {
        Serial.println("[PI] ✗ Timeout");
        piResponse   = "{\"error\":\"timeout\"}";
        captureState = SENDING;
      }
      break;

    // ── SENDING: originate packet into mesh ──────
    case SENDING:
      Serial.println("[PI] ✓ Response: " + piResponse);
      originatePacket(piResponse);
      Serial.println("→ Ready for next capture\n");
      captureState = IDLE;
      break;
  }

  delay(10);
}