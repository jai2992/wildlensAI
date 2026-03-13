/**
 * WildLens - ESP32 Sender
 *
 * Triggers a GPIO pin HIGH on serial input, then
 * forwards a response message to a peer ESP32 via ESP-NOW.
 *
 * Flow:
 *   Serial input → GPIO_TRIGGER_PIN HIGH → wait for Pi → read response → ESP-NOW relay
 *
 * Author : WildLens Project
 * Board  : ESP32 (Arduino framework)
 */

#include <WiFi.h>
#include <esp_now.h>

// ─────────────────────────────────────────────
//  CONFIGURATION  (edit these before flashing)
// ─────────────────────────────────────────────

// GPIO pin that goes HIGH to signal the Raspberry Pi
#define GPIO_TRIGGER_PIN  4

// Duration (ms) the trigger pin stays HIGH
#define TRIGGER_PULSE_MS  200

// How long to wait for the Pi's serial/UART response (ms)
#define PI_RESPONSE_TIMEOUT_MS  8000

// MAC address of the receiver ESP32
uint8_t PEER_MAC[] = { 0x48, 0x55, 0x19, 0xE0, 0xA9, 0x9C };

// ─────────────────────────────────────────────
//  DATA STRUCTURES
// ─────────────────────────────────────────────

// ESP-NOW payload (max 250 bytes)
struct DataPacket {
  char text[250];
};

// ─────────────────────────────────────────────
//  GLOBALS
// ─────────────────────────────────────────────

static DataPacket outPacket;

// ─────────────────────────────────────────────
//  ESP-NOW CALLBACK
// ─────────────────────────────────────────────

void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS
    ? "[ESP-NOW] ✓ Delivery confirmed"
    : "[ESP-NOW] ✗ Delivery failed");
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("   WildLens ESP32 — GPIO + ESP-NOW");
  Serial.println("========================================");

  // GPIO
  pinMode(GPIO_TRIGGER_PIN, OUTPUT);
  digitalWrite(GPIO_TRIGGER_PIN, LOW);
  Serial.printf("[GPIO] Trigger pin %d ready\n", GPIO_TRIGGER_PIN);

  // ESP-NOW (WiFi must be in STA mode even without a network)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] ✗ Init failed — halting");
    while (true) delay(1000);
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, PEER_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] ✗ Failed to add peer — halting");
    while (true) delay(1000);
  }

  Serial.print("[ESP-NOW] Peer: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X%s", PEER_MAC[i], i < 5 ? ":" : "\n");
  }

  Serial.println("\n✓ Ready — type anything + Send to capture\n");
}

// ─────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────

void loop() {
  if (Serial.available()) {
    flushSerial();          // discard the typed command
    triggerCapture();       // pulse GPIO
    String msg = waitForPiResponse();  // read Pi's serial reply
    sendViaCOWNow(msg);    // relay to peer ESP32
    Serial.println("→ Ready for next capture\n");
  }

  delay(100);
}

// ─────────────────────────────────────────────
//  HELPERS
// ─────────────────────────────────────────────

/** Discard all bytes currently in the serial RX buffer. */
void flushSerial() {
  while (Serial.available()) Serial.read();
}

/**
 * Pulse GPIO_TRIGGER_PIN HIGH for TRIGGER_PULSE_MS milliseconds.
 * The Raspberry Pi should detect this rising edge and start capture.
 */
void triggerCapture() {
  Serial.println("[GPIO] Trigger → HIGH");
  digitalWrite(GPIO_TRIGGER_PIN, HIGH);
  delay(TRIGGER_PULSE_MS);
  digitalWrite(GPIO_TRIGGER_PIN, LOW);
  Serial.println("[GPIO] Trigger → LOW");
}

/**
 * Block until the Pi sends a newline-terminated response over Serial,
 * or until PI_RESPONSE_TIMEOUT_MS elapses.
 *
 * The Pi should write a single JSON line, e.g.:
 *   {"animal":"leopard","confidence":0.91,"note":"moving north"}
 *
 * @return The received string, or an error JSON on timeout.
 */
String waitForPiResponse() {
  Serial.println("[PI] Waiting for response...");

  String response = "";
  unsigned long start = millis();

  while (millis() - start < PI_RESPONSE_TIMEOUT_MS) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') break;   // end of Pi's message
      response += c;
    }
    delay(10);
  }

  if (response.isEmpty()) {
    Serial.println("[PI] ✗ Timeout — no response received");
    return "{\"error\":\"timeout\"}";
  }

  Serial.println("[PI] ✓ Response: " + response);
  return response;
}

/**
 * Send a string to the peer ESP32 via ESP-NOW.
 * Messages longer than 249 characters are truncated.
 *
 * @param message  The string to transmit.
 */
void sendViaCOWNow(const String& message) {
  memset(outPacket.text, 0, sizeof(outPacket.text));

  String payload = message;
  if (payload.length() >= sizeof(outPacket.text)) {
    payload = payload.substring(0, sizeof(outPacket.text) - 1);
    Serial.println("[ESP-NOW] ⚠ Message truncated to 249 chars");
  }

  payload.toCharArray(outPacket.text, sizeof(outPacket.text));

  Serial.println("[ESP-NOW] Sending: " + payload);

  esp_err_t err = esp_now_send(PEER_MAC, (uint8_t*)&outPacket, sizeof(outPacket));
  if (err != ESP_OK) {
    Serial.printf("[ESP-NOW] ✗ Send error code: %d\n", err);
  }
}
