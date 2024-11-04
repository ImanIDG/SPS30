#define SENSENET_DEBUG // enable debug on SerialMon
#define SerialMon Serial // if you need DEBUG SerialMon should be defined

#define FIRMWARE_TITLE "Arduino_Data_Collector"
#define FIRMWARE_VERSION "0.3.14"

#include <Arduino.h>
#include "sensenet.h"
#include "WiFi.h"
#include "Wire.h"
#include "SPI.h"
//#include "Preferences.h"
#include "Ticker.h"
#include <esp_task_wdt.h>
#include <ESP32Time.h>

#include "DEV_Config.h"
#include "L76X.h"

#include "sps30.h"
#include <MG811.h>
#include <SoftwareSerial.h>
#include <MHZ.h>

#define WIFI_SSID "Sensenet_2.4G"
#define WIFI_PASS "Sensenet123"
#define TB_URL "tb.sensenet.ca"

// for COM7
//#define TOKEN "SGP4xESP32_3"

// for COM8
//#define TOKEN "SGP4xESP32_2"

// for COM10
#define TOKEN "GPSESP32"

ESP32Time internalRtc(0);  // offset in seconds GMT
Ticker restartTicker;
//Preferences preferences;
NetworkInterface wifiInterface("wifi", 2, 2);
NetworkInterfacesController networkController;
MQTTController mqttController;
MQTTOTA ota(&mqttController, 5120);

WiFiClient wiFiClient;

int i = 0;

void resetESP() {
    ESP.restart();
}

uint64_t getTimestamp() {
    if (internalRtc.getEpoch() < 946713600)
        return 0;
    uint64_t ts = internalRtc.getEpoch();
    ts = ts * 1000L;
    ts = ts + internalRtc.getMillis();
    return ts;
}

// Sampling interval in seconds
char errorMessage[32];

bool on_message(const String &topic, DynamicJsonDocument json) {
    Serial.print("Topic1: ");
    Serial.println(topic);
    Serial.print("Message1: ");
    Serial.println(json.as<String>());

    if (json.containsKey("shared")) {
        JsonObject sharedKeys = json["shared"].as<JsonObject>();
        for (JsonPair kv: sharedKeys)
            json[kv.key()] = sharedKeys[kv.key()];

    }

    if (json.containsKey("method")) {
        String method = json["method"].as<String>();

        bool handled = false;
        if (method.equalsIgnoreCase("restart_device")) {
            float seconds = 0;
            if (json["params"].containsKey("seconds"))
                seconds = json["params"]["seconds"];
            if (seconds == 0) seconds = 1;
            printDBGln("Device Will Restart in " + String(seconds) + " Seconds");
            restartTicker.once(seconds, resetESP);
            handled = true;
        }

        if (handled) {
            String responseTopic = String(topic);
            responseTopic.replace("request", "response");
            DynamicJsonDocument responsePayload(300);
            responsePayload["result"] = "true";
            mqttController.addToPublishQueue(responseTopic, responsePayload.as<String>(), true);
            return true;
        }
    }

    return false;
}

void connectToNetwork() {
    Serial.println("Added WiFi Interface");
    networkController.addNetworkInterface(&wifiInterface);

    networkController.setAutoReconnect(true, 10000);
    networkController.autoConnectToNetwork();
}

void connectToPlatform(Client &client, const bool enableOTA) {

    Serial.println("Trying to Connect Platform");
    mqttController.connect(client, "esp", TOKEN, "", TB_URL,
                           1883, on_message,
                           nullptr, [&]() {
                Serial.println("Connected To Platform");
                DynamicJsonDocument info(512);
                info["Token"] = TOKEN;
                info.shrinkToFit();
                mqttController.sendAttributes(info, true);
                if (enableOTA)
                    ota.begin(FIRMWARE_TITLE, FIRMWARE_VERSION);
                else
                    ota.stopHandleOTAMessages();

                DynamicJsonDocument requestKeys(512);
                requestKeys["sharedKeys"] = "desiredAllowSleep,desiredDisableIR,desiredSEN55TempOffset";
                requestKeys.shrinkToFit();
                mqttController.requestAttributesJson(requestKeys.as<String>());

                if (getTimestamp() == 0) {
                    DynamicJsonDocument requestTime(512);
                    requestTime["method"] = "requestTimestamp";
                    requestTime.shrinkToFit();
                    mqttController.requestRPC(requestTime.as<String>(),
                                              [](const String &rpcTopic, const DynamicJsonDocument &rpcJson) -> bool {
                                                  Serial.print("Updating Internal RTC to: ");
                                                  Serial.println(rpcJson.as<String>());
                                                  uint64_t tsFromCloud = rpcJson["timestamp"].as<uint64_t>();
                                                  tsFromCloud = tsFromCloud / 1000;
                                                  internalRtc.setTime(tsFromCloud);
                                                  Serial.print("Internal RTC updated to: ");
                                                  Serial.println(internalRtc.getDateTime(true));
                                                  getTimestamp();
                                                  return true;
                                              });
                } else {
                    Serial.print("Internal RTC updated to: ");
                    Serial.println(internalRtc.getDateTime(true));
                }
            });
}

int retry = 0;

void initInterfaces() {
    retry = 0;
    wifiInterface.setTimeoutMs(30000);
    wifiInterface.setConnectInterface([]() -> bool {
        Serial.println(String("Connecting To WiFi ") + WIFI_SSID);
        WiFi.mode(WIFI_MODE_NULL);
        delay(2000);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        return true;
    });
    wifiInterface.setConnectionCheckInterfaceInterface([]() -> bool {
        return WiFi.status() == WL_CONNECTED;
    });
    wifiInterface.OnConnectingEvent([]() {
        Serial.print(".");
    }, 500);
    wifiInterface.OnConnectedEvent([]() {
        retry = 0;
        Serial.println(String("Connected to WIFI with IP: ") + WiFi.localIP().toString());
        connectToPlatform(wiFiClient, true);
        DynamicJsonDocument data(200);
        data["Connection Type"] = "WIFI";
        data["IP"] = WiFi.localIP().toString();
        data.shrinkToFit();
    });
    wifiInterface.OnTimeoutEvent([]() {
        retry++;
        Serial.println("WiFi Connecting Timeout! retrying for " + String(retry) + " Times");
        WiFi.mode(WIFI_MODE_NULL);

//        if (retry >= 20)
//            ESP.restart();
    });
}

#define SP30_COMMS Serial1

/////////////////////////////////////////////////////////////
/* define RX and TX pin for softserial and Serial1 on ESP32
 * can be set to zero if not applicable / needed           */
/////////////////////////////////////////////////////////////
#define TX_PIN 0
#define RX_PIN 0

/////////////////////////////////////////////////////////////
/* define driver debug
 * 0 : no messages
 * 1 : request sending and receiving
 * 2 : request sending and receiving + show protocol errors */
 //////////////////////////////////////////////////////////////
#define DEBUG 0

///////////////////////////////////////////////////////////////
/////////// NO CHANGES BEYOND THIS POINT NEEDED ///////////////
///////////////////////////////////////////////////////////////

// function prototypes (sometimes the pre-processor does not create prototypes themself on ESPxx)
#define CO2_IN 10

// pin for uart reading
#define MH_Z19_RX 4  // D7
#define MH_Z19_TX 0  // D6

void ErrtoMess(char *mess, uint8_t r);
void Errorloop(char *mess, uint8_t r);
void GetDeviceInfo();
bool read_all(DynamicJsonDocument &data);
//enum SensorType { MHZ14A, MHZ14B, MHZ16, MHZ1911A, MHZ19B, MHZ19C, MHZ19D, MHZ19E };
MHZ co2(MH_Z19_RX, MH_Z19_TX, CO2_IN, MHZ::MHZ19C);

// create constructor
SPS30 sps30;
MG811 mySensor = MG811(A0); // Analog input A0

float v400 = 4.535;
float v40000 = 3.206;
void setupSPS30_MG811_MHZ19C();
void loopSPS30_MG811_MHZ19C(DynamicJsonDocument &data) {
  read_all(data);
  Serial.print("Raw voltage: ");
  float rawMG811 = mySensor.raw();
  Serial.print(rawMG811);
  data["rawMG811"] = String(rawMG811);
  Serial.print("V, C02 Concetration: ");
  float readMG811 = mySensor.read();
  data["readMG811"] = String(readMG811);
  Serial.print(readMG811);
  Serial.print(" ppm");

  
  Serial.print("\n----- Time from start: ");
  Serial.print(millis() / 1000);
  Serial.println(" s");

  int ppm_uart = co2.readCO2UART();
  Serial.print("PPMuart: ");

  if (ppm_uart > 0) {
    Serial.print(ppm_uart);
    data["ppm_uart"] = String(ppm_uart);
  } else {
    Serial.print("n/a");
  }

  int ppm_pwm = co2.readCO2PWM();
  Serial.print(", PPMpwm: ");
  Serial.print(ppm_pwm);
  data["ppm_pwm"] = String(ppm_pwm);

  int temperature = co2.getLastTemperature();
  Serial.print(", Temperature: ");

  if (temperature > 0) {
    Serial.println(temperature);
    data["temperature"] = String(temperature);
  } else {
    Serial.println("n/a");
  }

  Serial.println("\n------------------------------");
}

uint64_t lastSPS30_MG811_MHZ19C = 0;

void core0Loop(void *parameter) {
    //Dont do anything 1
    esp_task_wdt_init(600, true); //enable panic so ESP32 restarts
    esp_task_wdt_add(NULL); //add current thread to WDT watch
    //Dont do anything1

    lastSPS30_MG811_MHZ19C = Uptime.getMilliseconds();
    uint64_t now = Uptime.getMilliseconds();
    DynamicJsonDocument data(5120);
    if (now - lastSPS30_MG811_MHZ19C > 60000) {
        lastSPS30_MG811_MHZ19C = now;
        loopSPS30_MG811_MHZ19C(data);
    }

    if (data.size() > 0 && getTimestamp() > 0) {
        data.shrinkToFit();
        Serial.println("Data: " + data.as<String>());
        mqttController.sendTelemetry(data, true, getTimestamp());
    }
    delayMicroseconds(1);
    esp_task_wdt_reset();
}

void setup() {
    //Dont do anything in setup
    //Add setup SEN55
    internalRtc.setTime(1000);
    btStop();
    esp_task_wdt_init(60, true); //enable panic so ESP32 restarts
    esp_task_wdt_add(NULL); //add current thread to WDT watch
    esp_task_wdt_reset();

    Serial.begin(115200);
//    preferences.begin("Configs", false);
//    Serial.println("Hello from: " + preferences.getString("token", "not-set"));
    mqttController.init();
    mqttController.sendSystemAttributes(true);
    initInterfaces();
    Wire.begin();
    esp_task_wdt_reset();

    uint16_t error;
    char errorMessage[256];
    delay(1000);  // needed on some Arduino boards in order to have Serial ready
    setupSPS30_MG811_MHZ19C();
    esp_task_wdt_reset();
    connectToNetwork();
    esp_task_wdt_reset();

    delay(1000);
    xTaskCreatePinnedToCore(
            core0Loop, // Function to implement the task
            "Core0Loop", // Name of the task
            10000, // Stack size in words
            NULL,  // Task input parameter
            0, // Priority of the task
            NULL,  // Task handle.
            0); // Core where the task should run
    esp_task_wdt_reset();
}

uint64_t core1Heartbeat;

void loop() {
    //Dont do anything
    esp_task_wdt_reset();

    if (Serial.available()) {
        if (Serial.readString().indexOf("reboot") >= 0)
            resetESP();
    }

    if (networkController.getCurrentNetworkInterface() != nullptr &&
        networkController.getCurrentNetworkInterface()->lastConnectionStatus()) {
        mqttController.loop();
    }

    networkController.loop();

    if ((Uptime.getSeconds() - core1Heartbeat) > 10) {
        core1Heartbeat = Uptime.getSeconds();
        printDBGln("Core 1 Heartbeat");
    }
}

void setupSPS30_MG811_MHZ19C() {

  // set driver debug level
  sps30.EnableDebugging(DEBUG);

  // set pins to use for softserial and Serial1 on ESP32
  if (TX_PIN != 0 && RX_PIN != 0) sps30.SetSerialPin(RX_PIN,TX_PIN);

  // Begin communication channel;
  if (! sps30.begin(SP30_COMMS))
    Errorloop((char *) "could not initialize communication channel.", 0);

  // check for SPS30 connection
  if (! sps30.probe()) Errorloop((char *) "could not probe / connect with SPS30.", 0);
  else  Serial.println(F("Detected SPS30."));

  // reset SPS30 connection
  if (! sps30.reset()) Errorloop((char *) "could not reset.", 0);

  // read device info
  GetDeviceInfo();

  // start measurement
  if (sps30.start()) Serial.println(F("Measurement started"));
  else Errorloop((char *) "Could NOT start measurement", 0);

  if (SP30_COMMS == I2C_COMMS) {
    if (sps30.I2C_expect() == 4)
      Serial.println(F(" !!! Due to I2C buffersize only the SPS30 MASS concentration is available !!! \n"));
  }
  Serial.println("MG811 CO2 Sensor");
  
  // Calibration is not done in this examples - use default value
  // mySensor.calibrate()
  mySensor.begin(v400, v40000);
  pinMode(CO2_IN, INPUT);
  Serial.println("MHZ 19C");
  if (co2.isPreHeating()) {
    Serial.print("Preheating");
    while (co2.isPreHeating()) {
      Serial.print(".");
      delay(5000);
    }
    Serial.println();
  }
}

/**
 * @brief : read and display device info
 */
void GetDeviceInfo()
{
  char buf[32];
  uint8_t ret;
  SPS30_version v;

  //try to read serial number
  ret = sps30.GetSerialNumber(buf, 32);
  if (ret == SPS30_ERR_OK) {
    Serial.print(F("Serial number : "));
    if(strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess((char *) "could not get serial number", ret);

  // try to get product name
  ret = sps30.GetProductName(buf, 32);
  if (ret == SPS30_ERR_OK)  {
    Serial.print(F("Product name  : "));

    if(strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess((char *) "could not get product name.", ret);

  // try to get version info
  ret = sps30.GetVersion(&v);
  if (ret != SPS30_ERR_OK) {
    Serial.println(F("Can not read version info"));
    return;
  }

  Serial.print(F("Firmware level: "));  Serial.print(v.major);
  Serial.print("."); Serial.println(v.minor);

  if (SP30_COMMS != I2C_COMMS) {
    Serial.print(F("Hardware level: ")); Serial.println(v.HW_version);

    Serial.print(F("SHDLC protocol: ")); Serial.print(v.SHDLC_major);
    Serial.print("."); Serial.println(v.SHDLC_minor);
  }

  Serial.print(F("Library level : "));  Serial.print(v.DRV_major);
  Serial.print(".");  Serial.println(v.DRV_minor);
}

/**
 * @brief : read and display all values
 */
bool read_all(DynamicJsonDocument &data)
{
  static bool header = true;
  uint8_t ret, error_cnt = 0;
  struct sps_values val;

  // loop to get data
  do {

    ret = sps30.GetValues(&val);

    // data might not have been ready
    if (ret == SPS30_ERR_DATALENGTH){

        if (error_cnt++ > 3) {
          ErrtoMess((char *) "Error during reading values: ",ret);
          return(false);
        }
        delay(1000);
    }

    // if other error
    else if(ret != SPS30_ERR_OK) {
      ErrtoMess((char *) "Error during reading values: ",ret);
      return(false);
    }

  } while (ret != SPS30_ERR_OK);

  // only print header first time
  if (header) {
    Serial.println(F("-------------Mass -----------    ------------- Number --------------   -Average-"));
    Serial.println(F("     Concentration [μg/m3]             Concentration [#/cm3]             [μm]"));
    Serial.println(F("P1.0\tP2.5\tP4.0\tP10\tP0.5\tP1.0\tP2.5\tP4.0\tP10\tPartSize\n"));
    header = false;
  }

  Serial.print(val.MassPM1);
  data["val.MassPM1"] = String(val.MassPM1);
  Serial.print(F("\t"));
  Serial.print(val.MassPM2);
  data["val.MassPM2"] = String(val.MassPM2);
  Serial.print(F("\t"));
  Serial.print(val.MassPM4);
  data["val.MassPM4"] = String(val.MassPM4);
  Serial.print(F("\t"));
  Serial.print(val.MassPM10);
  data["val.MassPM10"] = String(val.MassPM10);
  Serial.print(F("\t"));
  Serial.print(val.NumPM0);
  data["val.NumPM0"] = String(val.NumPM0);
  Serial.print(F("\t"));
  Serial.print(val.NumPM1);
  data["val.NumPM1"] = String(val.NumPM1);
  Serial.print(F("\t"));
  Serial.print(val.NumPM2);
  data["val.NumPM2"] = String(val.NumPM2);
  Serial.print(F("\t"));
  Serial.print(val.NumPM4);
  data["val.NumPM4"] = String(val.NumPM4);
  Serial.print(F("\t"));
  Serial.print(val.NumPM10);
  data["val.NumPM10"] = String(val.NumPM10);
  Serial.print(F("\t"));
  Serial.print(val.PartSize);
  data["val.PartSize"] = String(val.PartSize);
  Serial.print(F("\n"));

  return(true);
}

/**
 *  @brief : continued loop after fatal error
 *  @param mess : message to display
 *  @param r : error code
 *
 *  if r is zero, it will only display the message
 */
void Errorloop(char *mess, uint8_t r)
{
  if (r) ErrtoMess(mess, r);
  else Serial.println(mess);
  Serial.println(F("Program on hold"));
  for(;;) delay(100000);
}

/**
 *  @brief : display error message
 *  @param mess : message to display
 *  @param r : error code
 *
 */
void ErrtoMess(char *mess, uint8_t r)
{
  char buf[80];

  Serial.print(mess);

  sps30.GetErrDescription(r, buf, 80);
  Serial.println(buf);
}