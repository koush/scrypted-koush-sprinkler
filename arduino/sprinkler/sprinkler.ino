#include <WebSocketsClient.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <arduino-timer.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#include <EEPROM.h>
#include <esp32-hal.h>

const char *wsHost = "192.168.2.206";
const char *wsUrl = "/endpoint/@scrypted/koush-sprinkler/public/";
const int chipid = ESP.getEfuseMac();
const uint16_t chip = (uint16_t)(chipid >> 32);


const int ssidOffset = 0;
const int ssidLength = 32;
const int passOffset = ssidLength;
const int passLength = 32;

char ssid[ssidLength];
char pass[passLength];

#define WIFI_SETUP_UUID "d900a203-a982-4a49-85b8-1b5ff00cd6ea"
#define WIFI_SSID_UUID "24edce76-d542-4b9f-b201-e76e220396df"
#define WIFI_PASS_UUID "0b7beaf6-5a90-4209-8df0-ecc2b7bf0054"
int advertising = 0;

int open_pin = 32;
int close_pin = 33;
int button_pin = 0;
int led_pin = 2;
int WAIT_TIME = 1000;
int OPEN_TIME = 5000;
int state = 0;
int stateChanged = 0;
auto timer = timer_create_default();

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;

bool clearValvePins(void *argument) {
  digitalWrite(open_pin, HIGH);
  digitalWrite(close_pin, HIGH);
}

void hexdump(const void *mem, uint32_t len, uint8_t cols = 16) {
  const uint8_t* src = (const uint8_t*) mem;
  Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
  for (uint32_t i = 0; i < len; i++) {
    if (i % cols == 0) {
      Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
    }
    Serial.printf("%02X ", *src);
    src++;
  }
  Serial.printf("\n");
}

void sendState() {
  char json[64];
  sprintf(json, "{\"type\":\"state\",\"state\":\"%s\"}", state ? "open" : "close");
  webSocket.sendTXT(json);
}

void handleJson(uint8_t *payload) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char *type = doc["type"];
  if (!type) {
    return;
  }

  if (strcmp(type, "open") == 0) {
    digitalWrite(close_pin, LOW);
    digitalWrite(open_pin, HIGH);
    timer.in(10, clearValvePins);
    state = 1;
    sendState();
  }
  else if (strcmp(type, "close") == 0) {
    digitalWrite(open_pin, LOW);
    digitalWrite(close_pin, HIGH);
    timer.in(10, clearValvePins);
    state = 0;
    sendState();
  }
  else if (strcmp(type, "state") == 0) {
    sendState();
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WSc] Disconnected!\n");
      break;
    case WStype_CONNECTED:
      Serial.printf("[WSc] Connected to url: %s\n", payload);
      char json[64];

      char ssid[23];

      snprintf(ssid, 23, "MCUDEVICE-%04X%08X", chip, (uint32_t)chipid);

      sprintf(json, "{\"type\":\"id\",\"id\":\"%s\"}", ssid);
      webSocket.sendTXT(json);
      sendState();
      //      // send message to server when Connected
      //      webSocket.sendTXT("Connected");
      break;
    case WStype_TEXT:
      //      Serial.printf("[WSc] get text: %s\n", payload);
      handleJson(payload);

      // send message to server
      // webSocket.sendTXT("message here");
      break;
    case WStype_BIN:
      //      Serial.printf("[WSc] get binary length: %u\n", length);
      //      hexdump(payload, length);

      // send data to server
      // webSocket.sendBIN(payload, length);
      break;
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      break;
  }
}

void readEEPROMString(char *dest, int offset, int length) {
  Serial.printf("reading eeprom %d %d\n", offset, length);
  for (int i = 0; i < length - 1; i++) {
    dest[i] = EEPROM.read(offset + i);
  }
  dest[length - 1] = '\0';
}


void readEEPROM() {
  readEEPROMString(ssid, ssidOffset, sizeof(ssid));
  readEEPROMString(pass, passOffset, sizeof(pass));
  WiFiMulti.addAP(ssid, pass);

  Serial.println("ssid:");
  Serial.println(ssid);
  Serial.println(pass);
}

class EEPROMCallbacks : public BLECharacteristicCallbacks {
  public:
    int offset;
    int size;
    EEPROMCallbacks(int offset, int size) {
      this->offset = offset;
      this->size = size;
    }
    void onWrite(BLECharacteristic* pCharacteristic) override {
      auto value = pCharacteristic->getValue();
      Serial.println(value.c_str());
      for (int i = 0; i < value.length(); i++) {
        EEPROM.write(offset + i, value[i]);
      }
      EEPROM.write(offset + value.length(), '\0');
      EEPROM.commit();

      readEEPROM();
    }
};

void setup() {
  Serial.begin (115200);
  pinMode (open_pin, OUTPUT);
  pinMode (close_pin, OUTPUT);
  pinMode(led_pin, OUTPUT);

  digitalWrite(open_pin, HIGH);
  digitalWrite(close_pin, HIGH);
  digitalWrite(led_pin, LOW);

  EEPROM.begin(512);

  readEEPROM();

  //  Serial.println("connected to wifi");
  //  digitalWrite(led_pin, HIGH);
  //  delay(1000);
  //  digitalWrite(led_pin, LOW);

  webSocket.begin(wsHost, 10080, wsUrl);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);

  BLEDevice::init("Sprinkler Setup");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(WIFI_SETUP_UUID);
  BLECharacteristic *ssidChar = pService->createCharacteristic(
                                  WIFI_SSID_UUID,
                                  BLECharacteristic::PROPERTY_READ |
                                  BLECharacteristic::PROPERTY_WRITE
                                );
  BLEDescriptor *ssidLabel = new BLEDescriptor("2901");
  ssidLabel->setValue("SSID");
  ssidChar->addDescriptor(ssidLabel);
  ssidChar->setValue(ssid);
  ssidChar->setCallbacks(new EEPROMCallbacks(ssidOffset, ssidLength));

  BLECharacteristic *passChar = pService->createCharacteristic(
                                  WIFI_PASS_UUID,
                                  BLECharacteristic::PROPERTY_READ |
                                  BLECharacteristic::PROPERTY_WRITE
                                );
  BLEDescriptor *passLabel = new BLEDescriptor("2901");
  passLabel->setValue("Password");
  passChar->addDescriptor(passLabel);
  passChar->setValue(pass);
  passChar->setCallbacks(new EEPROMCallbacks(passOffset, passLength));

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(WIFI_SETUP_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
}

void loop() {
  if (WiFiMulti.run(5000) != WL_CONNECTED) {
    Serial.println("connecting to wifi");
    if (!advertising) {
      advertising = 1;
      BLEDevice::startAdvertising();
    }
  }
  else if (advertising) {
    advertising = 0;
    BLEDevice::stopAdvertising();
  }

  webSocket.loop();
  timer.tick();
}