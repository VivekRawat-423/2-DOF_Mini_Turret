/*
  Pan/Tilt Laser — WebSocket controller
  ──────────────────────────────────────
  Library deps (install via Arduino Library Manager):
    • ESP32Servo
    • ArduinoWebsockets  (by Gil Maimon)

  Message format (browser → ESP32), plain text:
    p=<0-180>&t=<0-180>&l=<0|1>
    Examples:
      p=90&t=90&l=0    — move to centre, laser off
      p=45&t=120&l=1   — move + laser on

  The ESP sends back a one-line JSON status after each move:
    {"p":90,"t":90,"l":0}
*/

#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ESP32Servo.h>

using namespace websockets;

// ── User config ───────────────────────────────────────────────────────────────
const char* SSID     = "SSID";
const char* PASSWORD = "PASSWORD";
const int   WS_PORT  = 81;

// ── Pin map ───────────────────────────────────────────────────────────────────
const int PAN_PIN   = 13;
const int TILT_PIN  = 12;
const int LASER_PIN = 14;

// ── State ─────────────────────────────────────────────────────────────────────
int panAngle  = 90;
int tiltAngle = 90;
int laserOn   = 0;

Servo panServo;
Servo tiltServo;

WebsocketsServer wsServer;
WebsocketsClient client;      // single client; extend to array for multi-client
bool clientConnected = false;

// ── Apply state from parsed values ────────────────────────────────────────────
void applyState(int p, int t, int l) {
  p = constrain(p, 0, 180);
  t = constrain(t, 0, 180);
  l = constrain(l, 0, 1);

  if (p != panAngle)  { panAngle  = p; panServo.write(p);  }
  if (t != tiltAngle) { tiltAngle = t; tiltServo.write(t); }
  if (l != laserOn)   { laserOn   = l; digitalWrite(LASER_PIN, l ? HIGH : LOW); }
}

// ── Parse "p=90&t=90&l=0" in-place (no String heap allocs) ───────────────────
void parseAndApply(const char* msg) {
  int p = panAngle, t = tiltAngle, l = laserOn;

  char buf[64];
  strncpy(buf, msg, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* tok = strtok(buf, "&");
  while (tok) {
    if      (strncmp(tok, "p=", 2) == 0) p = atoi(tok + 2);
    else if (strncmp(tok, "t=", 2) == 0) t = atoi(tok + 2);
    else if (strncmp(tok, "l=", 2) == 0) l = atoi(tok + 2);
    tok = strtok(NULL, "&");
  }
  applyState(p, t, l);
}

// ── Send current state back to browser ───────────────────────────────────────
void sendState() {
  if (!clientConnected) return;
  char buf[48];
  snprintf(buf, sizeof(buf), "{\"p\":%d,\"t\":%d,\"l\":%d}", panAngle, tiltAngle, laserOn);
  client.send(buf);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  // Hardware init
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);

  panServo.attach(PAN_PIN,   500, 2400);
  tiltServo.attach(TILT_PIN, 500, 2400);
  panServo.write(panAngle);
  tiltServo.write(tiltAngle);
  Serial.println("Servos centred");

  // WiFi
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // WebSocket server
  wsServer.listen(WS_PORT);
  Serial.print("WebSocket server ready on ws://");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(WS_PORT);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  // Accept new client if none connected
  if (!clientConnected && wsServer.poll()) {
    client = wsServer.accept();
    if (client.available()) {
      clientConnected = true;
      Serial.println("Client connected");
      sendState();   // send current state so browser can sync display

      client.onMessage([](WebsocketsMessage msg) {
        parseAndApply(msg.data().c_str());
        // Uncomment the line below if you want ACK messages back:
        // sendState();
      });

      client.onEvent([](WebsocketsEvent event, String data) {
        if (event == WebsocketsEvent::ConnectionClosed) {
          clientConnected = false;
          // Safety: turn laser off when browser disconnects
          digitalWrite(LASER_PIN, LOW);
          laserOn = 0;
          Serial.println("Client disconnected — laser off");
        }
      });
    }
  }

  // Poll active client
  if (clientConnected) {
    client.poll();
  }
}
