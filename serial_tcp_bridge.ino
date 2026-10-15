// ESP32 transparent Serial <-> TCP bridge for a DLMS meter with a serial port.
// Client connects over TCP and speaks HDLC (interface type HDLC, not Wrapper).
// The bridge does not parse anything; it only copies bytes.

#include <WiFi.h>

// ---- Settings: edit these ----
const char* WIFI_SSID = "Sahasra";
const char* WIFI_PASS = "wintek@143";
const uint16_t TCP_PORT = 4059;

const uint32_t METER_BAUD = 9600;          // match the meter
const uint32_t METER_CONFIG = SERIAL_8N1;  // or SERIAL_8E1, SERIAL_7E1
// Meter UART pins (fixed hardware)
#define RXD1_DEFAULT 5
#define TXD1_DEFAULT 4
#define DTR_PIN_DEFAULT 6

const int PIN_RX = RXD1_DEFAULT;     // ESP32 RX <- meter TX
const int PIN_TX = TXD1_DEFAULT;     // ESP32 TX -> meter RX

// MAX3232 inverts the signal: GPIO LOW -> RS-232 +V = DTR asserted (ON).
// Same as the client project: idle HIGH, driven LOW while a PC is connected.
#define DTR_ACTIVE_LEVEL LOW
#define DTR_WAKE_MS      100

const uint32_t IDLE_TIMEOUT_MS = 60000;  // drop silent client after 60 s
// -----------------------------

WiFiServer server(TCP_PORT);
WiFiClient client;
uint32_t lastActivity = 0;

const bool HEX_DEBUG = true;  // print every byte that passes through

void hexDump(const char* dir, const uint8_t* b, size_t n) {
  if (!HEX_DEBUG) return;
  Serial.print(dir);
  Serial.print(" (");
  Serial.print(n);
  Serial.print("): ");
  for (size_t i = 0; i < n; i++) {
    if (b[i] < 0x10) Serial.print('0');
    Serial.print(b[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

void setDtr(bool on) {
  digitalWrite(DTR_PIN_DEFAULT, on ? DTR_ACTIVE_LEVEL : !DTR_ACTIVE_LEVEL);
}

void setup() {
  Serial.begin(115200);
  // DTR: output, de-asserted until a PC connects.
  pinMode(DTR_PIN_DEFAULT, OUTPUT);
  setDtr(false);

  Serial1.setRxBufferSize(2048);
  Serial1.begin(METER_BAUD, METER_CONFIG, PIN_RX, PIN_TX);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  Serial.print("Bridge IP: ");
  Serial.println(WiFi.localIP());

  server.begin();
  server.setNoDelay(true);
}

void loop() {
  // Reconnect Wi-Fi if it drops.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(1000);
    return;
  }

  // Accept a new client only when none is active (one at a time).
  if (!client || !client.connected()) {
    if (client) {
      client.stop();
      setDtr(false);
    }
    WiFiClient incoming = server.accept();
    if (incoming) {
      client = incoming;
      client.setNoDelay(true);
      // Assert DTR and give the meter port time to wake up.
      setDtr(true);
      delay(DTR_WAKE_MS);
      while (Serial1.available()) Serial1.read();  // flush stale meter data
      lastActivity = millis();
      Serial.println("Client connected");
    }
    return;
  }

  // TCP -> serial
  int n = client.available();
  if (n > 0) {
    uint8_t buf[256];
    n = client.read(buf, min(n, (int)sizeof(buf)));
    if (n > 0) {
      hexDump("PC->METER", buf, n);
      Serial1.write(buf, n);
      Serial1.flush();  // wait until the last byte is sent
      lastActivity = millis();
    }
  }

  // serial -> TCP
  int m = Serial1.available();
  if (m > 0) {
    uint8_t buf[256];
    m = Serial1.readBytes(buf, min(m, (int)sizeof(buf)));
    if (m > 0) {
      hexDump("METER->PC", buf, m);
      client.write(buf, m);
      lastActivity = millis();
    }
  }

  // Drop idle clients so the port is freed.
  if (millis() - lastActivity > IDLE_TIMEOUT_MS) {
    Serial.println("Idle timeout, closing client");
    client.stop();
    setDtr(false);
  }
}
