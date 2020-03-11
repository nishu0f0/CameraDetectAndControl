#include "esp_camera.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// Includes for BLE
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>

//
// WARNING!!! Make sure that you have either selected ESP32 Wrover Module,
//            or another board which has PSRAM enabled
//

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

#define CONFIGURATION_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONFIG_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define DEVICE_NAME_SIZE 19

#define NAME_WIFI_CREDENTIALS "WIFI_CREDENTIALS"
#define KEY_SSID_PRIMARY "SSID_PRIMARY"
#define KEY_PASSWORD_PRIMARY "PASSWORD_PRIMARY"
#define EMPTY_STRING ""

//Preferences preferences;

const char* ssid = "*********";
const char* password = "*********";

char deviceName[DEVICE_NAME_SIZE];

/**
 * Create unique device name from MAC address
 **/
void createName() {
  uint8_t baseMac[6];
  // Get MAC address for WiFi station
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  // Write unique name into apName
  sprintf(deviceName, "ESP32-%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
}

void startCameraServer();

void loadPreferences() {
  Serial.println(F("loadPreferences()"));
  Preferences preferences;
  preferences.begin(NAME_WIFI_CREDENTIALS, true);
  ssid = preferences.getString(KEY_SSID_PRIMARY, EMPTY_STRING).c_str();
  password = preferences.getString(KEY_PASSWORD_PRIMARY, EMPTY_STRING).c_str();
  Serial.println(ssid);
  Serial.println(password);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  createName();
  loadPreferences();
  initble();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    Serial.println(F("psram found"));
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    Serial.println(F("psram not found"));
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);//flip it back
    s->set_brightness(s, 1);//up the blightness just a bit
    s->set_saturation(s, -2);//lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println(F("WiFi connected"));

  startCameraServer();

  Serial.print(F("Camera Ready! Use 'http://"));
  Serial.print(WiFi.localIP());
  Serial.println(F("' to connect"));
}

class ConfigCallbackHandler: public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {
    Serial.println(F("ConfigCallbackHandler:onWrite"));
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) {
      return;
    }
    Serial.println("Received over BLE: " + String((char *)&value[0]));
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, value);
    // Test if parsing succeeds.
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
    if(doc.containsKey(F("wifi_ssid")) && doc.containsKey(F("wifi_password"))) {
      ssid = doc[F("wifi_ssid")];
      password = doc[F("wifi_password")];
      Serial.print("SSID: ");
      Serial.print(ssid);
      Serial.print(" PASSWORD: ");
      Serial.print(password);
      Serial.println("");
      Preferences preferences;
      preferences.begin(NAME_WIFI_CREDENTIALS, false);
      preferences.putString(KEY_SSID_PRIMARY, ssid);
      //preferences.putString("ssidSecondary", ssid);
      preferences.putString(KEY_PASSWORD_PRIMARY, password);
      //preferences.putString("pwSecondary", password);
      preferences.putBool("valid", true);
      preferences.end();      
    }
    if(doc.containsKey(F("restart"))) {
      WiFi.disconnect();
      esp_restart();
    }
    
  }

  void onRead(BLECharacteristic *pCharacteristics) {
    Serial.println(F("ConfigCallbackHandler:onRead"));
    StaticJsonDocument<200> doc;
    doc["wifi_ssid"] = ssid;
    doc["wifi_password"] = password;
    String camConfig;
    serializeJson(doc, camConfig);
    pCharacteristics->setValue((uint8_t*)&camConfig[0], camConfig.length());
  }
};

void initble() {
  Serial.println("Starting BLE work!");

  BLEDevice::init(deviceName);
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(CONFIGURATION_SERVICE_UUID);
  BLECharacteristic *pConfigCharacteristic = pService->createCharacteristic(
                                         CONFIG_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  
  pConfigCharacteristic->setCallbacks(new ConfigCallbackHandler());
  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(CONFIGURATION_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristic defined! Now you can read it in your phone!");
}

String getProperty(String key) {
  
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(10000);
}
