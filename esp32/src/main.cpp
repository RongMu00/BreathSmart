/*
  @author: Kenta Adachi, Rong Mu
  @date: 2023/6/12
  @project_detail: An ESP32 centered air quality sensor, which measures CO2 concentration, particulate matter counts,
                 toxic gas detection, and temperature and humidity
  @components: ESP32, MH-Z19B CO2 sensor, PMS5003 particulate matter sensor, MP503 toxic gas sensor, and DHT20 temperature and humidity sensor
  @protocol: Wi-Fi Communication, MQTT Protocol
  @reference: 
    - https://how2electronics.com/interfacing-pms5003-air-quality-sensor-arduino/ 
    - Azure-SDK-for-C-arduino library/Example at github repository: 
      https://github.com/Azure/azure-sdk-for-c-arduino/blob/main/examples/Azure_IoT_Hub_ESP32/readme.md
*/

#include <Arduino.h>
#include <HttpClient.h>
#include <WiFi.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <Adafruit_AHTX0.h>

#include "MHZ19.h"
#include <TinyGPSPLus.h>
#include <SoftwareSerial.h>

#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>

#include <FS.h>
#include "SPIFFS.h"
#include "TFT_eSPI.h"

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "iot_configs.h"

// MH-Z19 Config
#define CO2_RX 26
#define CO2_TX 27
// PMS5003 Config
#define PM_RX 13
#define PM_TX 12
#define BAUDRATE 9600 
// MP503 Config
#define SIGNAL_A 25
#define SIGNAL_B 33
#define LED_A 2
#define LED_B 15
// GPS GT-U7 Config
#define GPS_RX 39
#define GPS_TX 32
// Buzzer
#define BUZZER_PIN 17

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

// Translate iot_configs.h defines into variables used by the sample
static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[300];
static uint32_t telemetry_send_count = 0;

#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

static void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
{
  Logger.Info("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Logger.Info("Time initialized!");
}

void receivedCallback(char* topic, byte* payload, unsigned int length)
{
  Logger.Info("Received [");
  Logger.Info(topic);
  Logger.Info("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      Logger.Info("MQTT event MQTT_EVENT_ERROR");
      break;
    case MQTT_EVENT_CONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe for cloud-to-device messages.");
      }
      else
      {
        Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Logger.Info("MQTT event MQTT_EVENT_DATA");

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
      {
        incoming_data[i] = event->topic[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Topic: " + String(incoming_data));

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
      {
        incoming_data[i] = event->data[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Data: " + String(incoming_data));

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Logger.Error("MQTT event UNKNOWN");
      break;
  }

  return ESP_OK;
}

static void initializeIoTHubClient()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t*)host, strlen(host)),
          az_span_create((uint8_t*)device_id, strlen(device_id)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient()
{
#ifndef IOT_CONFIG_USE_X509_CERT
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }
#endif

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  Logger.Info("MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char*)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

/*
 * @brief Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getEpochTimeInSecs() { return (uint32_t)time(NULL); }

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  (void)initializeMqttClient();
}

unsigned long start_time = 0;

MHZ19 my_MHZ19; // CO2 sensor
Adafruit_AHTX0 aht; // temperature/humidity sensor
HardwareSerial *MHZ19_port = &Serial1;
HardwareSerial *PMS_port = &Serial2;
TFT_eSPI tft = TFT_eSPI();

struct PMS{
  uint16_t frame_len;
  uint16_t pm10_unit, pm25_unit, pm100_unit;
  uint16_t pm10_atm, pm25_atm, pm100_atm;
  uint16_t particle_count_3, particle_count_5, particle_count_10, particle_count_25, paricle_count_50, particle_count_100; 
  uint16_t reserved;
  uint16_t checksum;
};

struct PMS pms; // pm sensor
uint16_t prev_pm;
int CO2;
uint16_t pm25;

// The TinyGPSPlus object
TinyGPSPlus gps;
// The serial connection to the GPS device
SoftwareSerial GPS_port(GPS_RX, GPS_TX);

// handles uart communication with particulate matter sensor
int read_pms(){
  uint8_t start1, start2;
  uint8_t read_buffer[30];
  uint16_t checksum = 0;
  uint16_t struct_buffer[15];

  if (!PMS_port->available()) return 0;
  if ((start1 = PMS_port->read()) != 0x42) return 0;
  if ((start2 = PMS_port->read()) != 0x4d) return 0;

  // read byte 2 to byte 31
  for (int i = 0; i < 30; ++i){
    read_buffer[i] = PMS_port->read();
  }
  
  // sum byte 0 to byte 29 to get checksum
  checksum += start1+start2;
  for (int i = 0; i < 28; ++i){
    checksum += read_buffer[i];
  }

  if (checksum != ((read_buffer[28] << 8) + read_buffer[29])) return 0;

  for (int i = 0; i < 15; ++i){
    struct_buffer[i] = (read_buffer[i*2] << 8) + read_buffer[i*2+1];
  }

  memcpy(&pms, (void *)struct_buffer, 30);

  return 1;
}

std::string latitude;
std::string longitude;
float lat;
float lon;

int readLocation(){
  Serial.print(F("Location: ")); 
  while (GPS_port.available() > 0){
      gps.encode(GPS_port.read());
  }
  if (gps.location.isValid()){
    // double lat = gps.location.lat();
    // double lng = gps.location.lng();
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(","));
    Serial.print(gps.location.lng(), 6);
    latitude = "lat: " + std::to_string(gps.location.lat());
    longitude = "lon: " + std::to_string(gps.location.lng());
    lat = gps.location.lat();
    lon = gps.location.lng();
    return 1;
  }
  else {
    return 0;
  }
}
// Arduino setup and loop main functions.

void setup() 
{ 
  establishConnection(); 

  Serial.begin(9600);
  Serial.println("Adafruit AHT10/AHT20 demo!");

  if (! aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");
    while (1) delay(10);
  }
  Serial.println("AHT10 or AHT20 found");

  // configure UART communcation with CO2 sensor
  Serial.begin(9600);

 // setting up CO2 sensor
  MHZ19_port->begin(9600, SERIAL_8N1, CO2_RX, CO2_TX);
  my_MHZ19.begin(Serial1);
  my_MHZ19.autoCalibration();

  // setting up particulate matter sensor
  PMS_port->begin(BAUDRATE, SERIAL_8N1, PM_RX, PM_TX);

  // setting up temperature/humidity sensor
  if (!aht.begin()){
    while (1) delay(10);
  }

  // setting up the TTGO display
  tft.init();
  tft.fillScreen(TFT_BLACK);
  tft.setRotation(1);

  // setting up the ZP07-MP503 air pollution sensor
  Serial.begin(9600);
  pinMode(SIGNAL_A, INPUT);
  pinMode(SIGNAL_B, INPUT);
  pinMode(LED_A, OUTPUT);
  pinMode(LED_B, OUTPUT);

  // setting up the buzzer
  pinMode(BUZZER_PIN, OUTPUT);

  //set up the gps module
  GPS_port.begin(BAUDRATE);
  delay(1000);

  while (!readLocation());

  if (millis() > 5000 && gps.charsProcessed() < 10)
  {
    Serial.println(F("No GPS detected: check wiring."));
  }

  Serial.println("Setup completed!");
  Serial.println();

  tft.drawString("Start Monitoring...", 20, 60, 4);
  delay(5000);

  tft.fillScreen(TFT_BLACK);
  tft.setRotation(1);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi();
  }
#ifndef IOT_CONFIG_USE_X509_CERT
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }
#endif
  else if (millis() > next_telemetry_send_time_ms)
  {
    az_span telemetry = AZ_SPAN_FROM_BUFFER(telemetry_payload);

    Logger.Info("Sending telemetry ...");

    // The topic could be obtained just once during setup,
    // however if properties are used the topic need to be generated again to reflect the
    // current values of the properties.
    if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
            &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
    {
      Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
      return;
    }

    sensors_event_t humidity, temp;

    if (millis() >= start_time + 2000)
    {
      CO2 = my_MHZ19.getCO2();
      start_time = millis();

      Serial.print("CO2 concentration: ");
      Serial.print(CO2);
      Serial.println(" ppm");

      // tft.drawString("CO2: " + String(CO2) + " ppm", 20, 20, 4);

      pm25 = prev_pm;
      if (read_pms()){
        Serial.print("PM 1.0 (ug/m3): ");
        Serial.println(pms.pm10_atm);
        Serial.print("PM 2.5 (ug/m3): ");
        Serial.println(pms.pm25_atm);
        pm25 = pms.pm25_atm;
        prev_pm = pm25;
        Serial.print("PM 10.0 (ug/m3): ");
        Serial.println(pms.pm100_atm);
      }

      // tft.drawString("PM 2.5: " + String(pms.pm25_atm), 20, 40, 4);

      
      aht.getEvent(&humidity, &temp);
      Serial.print("Temperature: ");
      Serial.print(temp.temperature);
      Serial.println(" C");
      Serial.print("Humidity: ");
      Serial.print(humidity.relative_humidity);
      Serial.println(" % rh");

      // tft.drawString("Temp:" + String(temp.temperature) + " C", 20, 60, 4);
      // tft.drawString("Humidity:" + String(humidity.relative_humidity) + "% rh", 20, 80, 4);

      int output_A = digitalRead(SIGNAL_A);
      int output_B = digitalRead(SIGNAL_B);
      Serial.print("Air concentration: ");
      int air_concentration;

      if(output_A == 0 && output_B == 0)
      {
        air_concentration = 0;
        Serial.println("Clean air");
        tft.drawString("Clean air                               ", 20, 100, 4);
        digitalWrite(LED_A, 0);
        digitalWrite(LED_B, 0);
        digitalWrite(BUZZER_PIN, 0);
      }
      if(output_A == 0 && output_B == 1)
      {
        air_concentration = 1;
        Serial.println("Slight pollution air");
        tft.drawString("Slight pollution air", 20, 100, 4);
        digitalWrite(LED_A, 0);
        digitalWrite(LED_B, 1);
        digitalWrite(BUZZER_PIN, 0);
      }
      if(output_A == 1 && output_B == 0)
      {
        air_concentration = 2;
        Serial.println("Middle pollution air");
        tft.drawString("Middle pollution air", 20, 100, 4);
        digitalWrite(LED_A, 1);
        digitalWrite(LED_B, 0);
        digitalWrite(BUZZER_PIN, 1);
      }
      if(output_A == 1 && output_B == 1)
      {
        air_concentration = 3;
        Serial.println("Heavy pollution air");
        tft.drawString("Heavy pollution air", 20, 100, 4);
        digitalWrite(LED_A, 1);
        digitalWrite(LED_B, 1);
        digitalWrite(BUZZER_PIN, 1);
      }

      tft.drawString(latitude.c_str(), 20, 40, 4);
      tft.drawString(longitude.c_str(), 20, 60, 4);

      // tft.fillScreen(TFT_BLACK);
      // tft.setRotation(1);
    }

    // getTelemetryPayload(telemetry, &telemetry);
    az_span original_payload = telemetry;

    char temperatureString[10];
    char humidString[10];
    char co2String[10];
    char pm25String[10];
    char latString[20];
    char lonString[20];
    char locationString[50];
    snprintf(temperatureString, 10, "%.2f", temp.temperature);
    snprintf(humidString, 10, "%.2f", humidity.relative_humidity);
    snprintf(co2String, 10, "%d", CO2);
    snprintf(pm25String, 10, "%u", pm25);
    snprintf(latString, 20, "%lf", lat);
    snprintf(lonString, 20, "%lf", lon);
    Serial.print("latString: ");
    Serial.println(latString);
    Serial.print("lonString: ");
    Serial.println(lonString);
    sprintf(locationString, "{ \"lat\": %lf, \"lon\": %lf }", lat, lon);


    telemetry = az_span_copy(telemetry, AZ_SPAN_FROM_STR("{ \"temperature\": "));
    // az_span_atod(telemetry, temperature);
    telemetry = az_span_copy(telemetry, az_span_create((uint8_t *)temperatureString, strlen(temperatureString)));
    telemetry = az_span_copy(telemetry, AZ_SPAN_FROM_STR(", \"humidity\": "));
    //az_span_atod(telemetry, humid);
    telemetry = az_span_copy(telemetry, az_span_create((uint8_t *)humidString, strlen(humidString)));
    // add CO2
    telemetry = az_span_copy(telemetry, AZ_SPAN_FROM_STR(", \"CO2\": "));
    telemetry = az_span_copy(telemetry, az_span_create((uint8_t *)co2String, strlen(co2String)));
    // add pm25
    telemetry = az_span_copy(telemetry, AZ_SPAN_FROM_STR(", \"PM25\": "));
    telemetry = az_span_copy(telemetry, az_span_create((uint8_t *)pm25String, strlen(pm25String)));
    // add lat and lon
    telemetry = az_span_copy(telemetry, AZ_SPAN_FROM_STR(", \"lat\": "));
    telemetry = az_span_copy(telemetry, az_span_create((uint8_t *)latString, strlen(latString)));
    telemetry = az_span_copy(telemetry, AZ_SPAN_FROM_STR(", \"lon\": "));
    telemetry = az_span_copy(telemetry, az_span_create((uint8_t *)lonString, strlen(lonString)));
    telemetry = az_span_copy(telemetry, AZ_SPAN_FROM_STR(" }"));
    telemetry = az_span_copy_u8(telemetry, '\0');

    telemetry = az_span_slice(
        original_payload, 0, az_span_size(original_payload) - az_span_size(telemetry) - 1);

    if (esp_mqtt_client_publish(
            mqtt_client,
            telemetry_topic,
            (const char*)az_span_ptr(telemetry),
            az_span_size(telemetry),
            MQTT_QOS1,
            DO_NOT_RETAIN_MSG)
        == 0)
    {
      Logger.Error("Failed publishing");
    }
    else
    {
      Logger.Info("Message published successfully");
    }

    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }
}

