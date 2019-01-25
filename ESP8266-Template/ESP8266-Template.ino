/*
 * ESP8266 Sketch template
 * ========================
 * 
 * Includes useful functions like
 * - Watchdog
 * - DeepSleep
 * - Read VCC
 * - MQTT
 * - OTA Sketch Updates (ATTN: requires MQTT!)
 * 
 * If you use DeepSleep, make sure to connect pin 16 (D0) to RESET, or your ESP will never wake up!
 * Also keep in mind that you can DeepSleep for ~ 1 hour max (hardware limitation)!
 * ATTENTION: Keep in mind that it takes quite a while after the sketch has booted until we receive messages from all subscribed topics!
 * This is especially important if you want to modify the ESP DeepSleep time. Not recommended to maximize battery lifetime, use a suitable hardcoded value instead!
 * You will have to keep things going until you've received the DeepSleepDuration Topic, or the default value will be used!
 * To get OTA update working on windows, you need to install python and python.exe needs to be in %PATH%
 * First flash needs to be wired of course. Afterwards Arduino IDE needs to be restarted if you cannot find
 * the ESP OTA-port in the IDE (also MQTT ota_topic needs to be set to "on" to be able to flash OTA).
 * Keep in mind that you'll need a reliable power source for OTA updates, 2x AA batteries might not work.
 * If you brick your ESP during OTA update, you can probably revive it by flashing it wired.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

// Set ADC mode to read VCC (Attn: Pin A0 must be floating!)
// ==========================================================
ADC_MODE(ADC_VCC);
// Corrected Divider (might need tuning for your ESP), default is 1024
#define VCCCORRDIV 919

// Enable (define) debug output on serial port
// ============================================
#define DEBUG
#ifdef DEBUG
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
#endif

// Enable Watchdog (sends ESP to DeepSleep if sketch hangs)
// ===========================================================
#define ENABLEWATCHDOG
// OS Timer for Software Watchdog
os_timer_t WDTimer;
// This flag will be set FALSE at every WD timer trigger
// so be sure to regularily set in TRUE in your sketch
// if it's still FALSE at the next WD timer run, WD will trigger
bool ProgramResponding = true;
// WD may be overriden (i.e. OTA Update)
bool OverrideWD = false;
// WDT will trigger every 10 seconds
#define WDTIMEOUT 10000

// Status LED on D4 (LED inverted!)
// =================================
#define USELED //do not define this to disable LED signalling
#define LED D4
#define LEDON LOW
#define LEDOFF HIGH

// Enable ESP DeepSleep
// =====================
// Enable (define) ESP deepSleep
#define DEEPSLEEP

// Set WiFi Sleep Mode
// ====================
// CAUTION: Light sleep might disconnect you from broker during sketch execution!
// Handle with care!
#define WIFISLEEP WIFI_NONE_SLEEP
//#define WIFISLEEP WIFI_LIGHT_SLEEP


// WLAN Network SSID and PSK
// ============================
#define WIFINAME TemplWifi
WiFiClient WIFINAME;
const char* ssid = "xxxx";
const char* password = "xxxx";


// OTA Update settings
// =====================
// OTANAME will show up as Arduino IDE "Port" Name
#define OTANAME "ESPtemplate"
#define OTAPASS "xxxx"


// MQTT Broker Settings
// ==========================================
#define mqtt_server "192.168.0.1"
#define mqtt_Client_Name "temp-esp"
// Maximum connection attempts to MQTT broker before going to sleep
const int MaxConnAttempts = 3;
// Message buffer for incoming Data from MQTT subscriptions
char message_buff[20];


// MQTT Topics and corresponding local vars
// ===========================================
//OTA Update specific vars
//to start an OTA update on the ESP, you will need to set ota_topic to "on"
//(don't forget to add the "retain" flag, especially if you want a sleeping ESP to enter flash mode at next boot)
#define ota_topic "HB7/Test/OTAupdate" //local BOOL, MQTT either "on" or "off"
bool OTAupdate = false;
#define otaStatus_topic "HB7/Test/OTAstatus"
// OTAstatus strings sent by sketch
#define UPDATEREQ "update_requested"
#define UPDATECANC "update_cancelled"
#define UPDATEOK "update_success"
bool SentUpdateRequested = false;
//An additional "external flag" is required to "remind" a freshly running sketch that it was just OTA-flashed..
//during an OTA update, PubSubClient functions do not ru (or cannot access the network)
//so this flag will be set to ON when actually waiting for the OTA update to start
//it will be reset if OtaInProgress and OTAupdate are true (in that case, ESP has most probably just been successfully flashed)
#define otaInProgress_topic "HB7/Test/OTAinProgress" //local BOOL, MQTT either "on" or "off"
bool OtaInProgress = false;
bool OtaIPsetBySketch = false;
bool SentOtaIPtrue = false;

// DeepSleep Time in Minutes
#define dsmin_topic "HB7/Test/DeepSleepMinutes"
//  ATTENTION: it can take up to 30 seconds after boot until your ESP has received values for all topics!
// so for a long battery lifetime, you might not want to fetch new DeepSleepDuration values by MQTT!
// set your desired default value here instead and re-flash if required to put the ESP to sleep asap.
int DeepSleepDuration = 2;

// Topic where VCC will be published
#define vcc_topic "HB7/Test/Vcc"
float VCC = 3.333;


/*
 * Callback Functions
 * ========================================================================
 */

// Watchdog Timer Callback function
#ifdef ENABLEWATCHDOG
void WDTCallback(void *pArg)
{
  if (OverrideWD) {
    return;
  }
  if (ProgramResponding)
  {
    // If ProgramResponding is not reset to true before next WDT trigger..
    ProgramResponding = false;
    return;
  }
  // ..program is probably dead, go to DeepSleep
  #ifdef USELED
  // signal SOS
  digitalWrite(LED, LEDOFF);
  delay(250);
  ToggleLed(LED,200,6);
  ToggleLed(LED,600,6);
  ToggleLed(LED,200,6);
  #endif
  DEBUG_PRINTLN("Watchdog Timer detected program not responding, going to sleep!");
  #ifdef DEEPSLEEP
  ESP.deepSleep(DeepSleepDuration * 60000000);
  delay(3000);
  #endif
  delay(100);
}
#endif

//MQTT Subscription callback function
void MqttCallback(char* topic, byte* payload, unsigned int length)
{
  int i = 0;
  // create character buffer with ending null terminator (string)
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  String msgString = String(message_buff);

  DEBUG_PRINTLN("MQTT: Message arrived [" + String(topic) + "]: " + String(msgString));

  // run through topics
  if ( String(topic) == ota_topic ) {
    if (msgString == "on") { OTAupdate = true; }
    else if (msgString == "off") { OTAupdate = false; }
    else
    {
      DEBUG_PRINTLN("MQTT: ERROR: Fetched invalid OTA-Update: " + String(msgString));
      delay(200);
    }
  }
  else if ( String(topic) == dsmin_topic ) {
    int IntPayLd = msgString.toInt();
    if ((IntPayLd > 0) && (IntPayLd <= 60))
    {
      // Valid value, do something
      DeepSleepDuration = IntPayLd;
      DEBUG_PRINTLN("MQTT: Fetched new DeepSleep duration: " + String(DeepSleepDuration));
    }
    else
    {
      DEBUG_PRINTLN("MQTT: ERROR: Fetched invalid DeepSleep duration: " + String(msgString));
      delay(200);
    }
  }
  else if ( String(topic) == otaInProgress_topic ) {
    if (msgString == "on") { OtaInProgress = true; }
    else if (msgString == "off") { OtaInProgress = false; }
    else
    {
      DEBUG_PRINTLN("MQTT: ERROR: Fetched invalid OtaInProgress: " + String(msgString));
      delay(200);
    }
  }
  else {
    DEBUG_PRINTLN("ERROR: Unknown topic: " + String(topic));
    DEBUG_PRINTLN("ERROR: Unknown topic value: " + String(msgString));
    delay(200);
  }     
}


/*
 * Setup PubSub Client instance
 * ===================================
 * must be done before setting up ConnectToBroker function and after MqttCallback Function
 * to avoid compilation errors
 */
PubSubClient mqttClt(mqtt_server,1883,MqttCallback,WIFINAME);


/*
 * Common Functions
 * =================================================
 */

bool ConnectToBroker()
{
  bool RetVal = false;
  int ConnAttempt = 0;
  // Try to connect x times, then return error
  while (ConnAttempt < MaxConnAttempts)
  {
    DEBUG_PRINT("Connecting to MQTT broker..");
    // Attempt to connect
    if (mqttClt.connect(mqtt_Client_Name))
    {
      DEBUG_PRINTLN("connected");
      RetVal = true;
      
      // Subscribe to Topics
      if (mqttClt.subscribe(ota_topic))
      {
        DEBUG_PRINTLN("Subscribed to " + String(ota_topic));
        delay(1);
      }
      else
      {
        DEBUG_PRINTLN("Failed to subscribe to " + String(ota_topic));
        delay(100);
      }
      if (mqttClt.subscribe(otaInProgress_topic))
      {
        DEBUG_PRINTLN("Subscribed to " + String(otaInProgress_topic));
        delay(1);
      }
      else
      {
        DEBUG_PRINTLN("Failed to subscribe to " + String(otaInProgress_topic));
        delay(100);
      }
      if (mqttClt.subscribe(dsmin_topic))
      {
        DEBUG_PRINTLN("Subscribed to " + String(dsmin_topic));
        delay(1);
      }
      else
      {
        DEBUG_PRINTLN("Failed to subscribe to " + String(dsmin_topic));
        delay(100);
      }
      delay(200);
      break;
    } else {
      DEBUG_PRINTLN("failed, rc=" + String(mqttClt.state()));
      DEBUG_PRINTLN("Sleeping 2 seconds..");
      delay(2000);
      ConnAttempt++;
    }
  }
  return RetVal;
}


/*
 * Common Functions
 */

void ToggleLed (int PIN,int WaitTime,int Count)
{
  // Toggle digital output
  for (int i=0; i < Count; i++)
  {
   digitalWrite(PIN, !digitalRead(PIN));
   delay(WaitTime); 
  }
}


/*
 * Setup
 * ========================================================================
 */
void setup() {
  // start serial port and digital Outputs
  #ifdef DEBUG
  Serial.begin(115200);
  #endif
  DEBUG_PRINTLN();
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("ESP8266 Template");
  #ifdef USELED
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LEDOFF);
  ToggleLed(LED,200,6);
  #endif

  // Set WiFi Sleep Mode
  WiFi.setSleepMode(WIFISLEEP);
  
  // Connect to WiFi network  
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("Connecting to " + String(ssid));
  WiFi.begin(ssid, password);
  // Next command avoids ESP broadcasting an unwanted ESSID..
  WiFi.mode(WIFI_STA);
   
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG_PRINT(".");
  }
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WiFi connected");
  DEBUG_PRINTLN("Device IP Address: " + String(WiFi.localIP()));
  #ifdef USELED
  // WiFi connected - blink once
  ToggleLed(LED,200,2);
  #endif
  
  // Setup MQTT Connection to broker and subscribe to topic
  if (ConnectToBroker())
  {    
    DEBUG_PRINTLN("Connected to MQTT broker, fetching topics..");
    mqttClt.loop();
    #ifdef USELED
    // broker connected - blink twice
    ToggleLed(LED,200,4);
    #else
    delay(300);
    #endif
  }
  else
  {
    DEBUG_PRINTLN("3 connection attempts to broker failed, using default values..");
    delay(100);
  }

  // Setup OTA Updates
  //ATTENTION: calling MQTT Publish function inside ArduinoOTA functions MIGHT NOT WORK!
  ArduinoOTA.setHostname(OTANAME);
  ArduinoOTA.setPassword(OTAPASS);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
  });
  ArduinoOTA.onEnd([]() {
    #ifdef USELED
    ToggleLed(LED,200,4);
    #else
    //ATTENTION: calling MQTT Publish function here does NOT WORK!
    delay(200);
    #endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percentComplete = (progress / (total / 100));
    if (percentComplete = 100) {
      mqttClt.publish(otaStatus_topic, String("upload_complete").c_str(), true);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    mqttClt.publish(ota_topic, String("off").c_str(), true);
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      mqttClt.publish(otaStatus_topic, String("Auth_Error").c_str(), true);
    } else if (error == OTA_BEGIN_ERROR) {
      mqttClt.publish(otaStatus_topic, String("Begin_Error").c_str(), true);
    } else if (error == OTA_CONNECT_ERROR) {
      mqttClt.publish(otaStatus_topic, String("Connect_Error").c_str(), true);
    } else if (error == OTA_RECEIVE_ERROR) {
      mqttClt.publish(otaStatus_topic, String("Receive_Error").c_str(), true);
    } else if (error == OTA_END_ERROR) {
      mqttClt.publish(otaStatus_topic, String("End_Error").c_str(), true);
    }
    delay(300);
  });
  ArduinoOTA.begin();

  #ifdef ENABLEWATCHDOG
  // Assign function and arm Watchdog Timer
  os_timer_setfn(&WDTimer, WDTCallback, NULL);
  os_timer_arm(&WDTimer, WDTIMEOUT, true);
  ProgramResponding = true;
  OverrideWD = false;
  #endif

  #ifdef USELED
  // Signal setup finished
  delay(300);
  ToggleLed(LED,200,6);
  #endif
}



/*
 * Main Loop
 * ========================================================================
 */
void loop() {
  // Dont forget to reset WD flag regularily
  ProgramResponding = true;
  delay(200);
  // Check connection to MQTT broker and update topics
  if (!mqttClt.connected()) {
    if (ConnectToBroker()) {
      mqttClt.loop();
    } else {
      DEBUG_PRINTLN("Unable to connect to MQTT broker.");
      delay(100);
    }
  } else {
    mqttClt.loop();
  }

  // If OTA Firmware Update is requested,
  // only loop through OTA function until finished (or reset by MQTT)
  if (OTAupdate) {
    DEBUG_PRINTLN("Millis: " + String(millis()));
    if (millis() < 27000) {
      // this delay is required to make sure that we know our correct status before doing anything..
      // shorter delay will not work reliably (fetching all MQTT topics takes a long time)
      DEBUG_PRINTLN("Sketch just booted, delaying OTA operation until all MQTT topics arrived..");
      #ifdef USELED
      ToggleLed(LED,1000,2);
      #else
      delay(2000);
      #endif
      return;
    }
    if (OtaInProgress && !OtaIPsetBySketch) {
      DEBUG_PRINTLN("OTA firmware update successful, resuming normal operation..");
      mqttClt.publish(otaStatus_topic, String(UPDATEOK).c_str(), true);
      mqttClt.publish(ota_topic, String("off").c_str(), true);
      mqttClt.publish(otaInProgress_topic, String("off").c_str(), true);
      OTAupdate = false;
      OtaInProgress = false;
      OtaIPsetBySketch = true;
      SentOtaIPtrue = false;
      SentUpdateRequested = false;
      ProgramResponding = true;
      OverrideWD = false;    
      return;
    }
    if (!SentUpdateRequested) {
      mqttClt.publish(otaStatus_topic, String(UPDATEREQ).c_str(), true);
      SentUpdateRequested = true;
    }
    // Override watchdog during OTA update
    OverrideWD = true;
    DEBUG_PRINTLN("OTA firmware update requested, waiting for upload..");
    #ifdef USELED
    // Signal OTA update requested
    ToggleLed(LED,100,10);
    #endif
    //set MQTT reminder that OTA update was executed
    if (!SentOtaIPtrue) {
      DEBUG_PRINTLN("Setting MQTT OTA-update reminder flag on broker..");
      mqttClt.publish(otaInProgress_topic, String("on").c_str(), true);
      OtaInProgress = true;      
      SentOtaIPtrue = true;
      OtaIPsetBySketch = true;
      delay(100);
    }
    //call OTA function to receive upload
    ArduinoOTA.handle();
    return;
  } else {
    if (SentUpdateRequested) {
      DEBUG_PRINTLN("OTA firmware update cancelled by MQTT, resuming normal operation..");
      mqttClt.publish(otaStatus_topic, String(UPDATECANC).c_str(), true);
      mqttClt.publish(otaInProgress_topic, String("off").c_str(), true);
      OtaInProgress = false;
      OtaIPsetBySketch = true;
      SentOtaIPtrue = false;
      SentUpdateRequested = false;
      ProgramResponding = true;
      OverrideWD = false;
    }
  }

  // Dont forget to reset WD flag regularily
  ProgramResponding = true;

  // START STUFF YOU WANT TO RUN HERE!
  // ============================================
  #ifdef USELED
  // Toggle LED at each loop
  ToggleLed(LED,500,4);
  #endif
  
  // Read VCC and publish to MQTT
  delay(300);
  VCC = ((float)ESP.getVcc())/VCCCORRDIV;
  mqttClt.publish(vcc_topic, String(VCC).c_str(), true);
  delay(100);

  #ifdef DEEPSLEEP
  // disconnect WiFi and go to sleep
  DEBUG_PRINTLN("Good night for " + String(DeepSleepDuration) + " minutes.");
  WiFi.disconnect();
  ESP.deepSleep(DeepSleepDuration * 60000000);
  #else
  DEBUG_PRINTLN("Loop finished, DeepSleep disabled. Restarting in 5 seconds.");
  ProgramResponding = true;
  #endif
  //ATTN: Sketch continues to run for a short time after initiating DeepSleep, so pause here
  delay(5000);
}
