#include <Wire.h>
#include <Arduino.h>
#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <NOxGasIndexAlgorithm.h>
#include <SensirionI2CSgp41.h>
#include <SensirionI2CSht4x.h>
#include <VOCGasIndexAlgorithm.h>
#include <LiquidCrystal_I2C.h>
#include "esp32-hal-cpu.h"


/* INFLUXDB */
// Time Zone
#define TZ_INFO "UTC-8"
// InfluxDB v2 server url, e.g. https://eu-central-1-1.aws.cloud2.influxdata.com (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "http://10.0.0.10:8086"
// InfluxDB v2 server or cloud API token (Use: InfluxDB UI -> Data -> API Tokens -> Generate API Token)
#define INFLUXDB_TOKEN "YOUR_TOKEN"
// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG "YOUR_ORG_NAME"
// InfluxDB v2 bucket name (Use: InfluxDB UI ->  Data -> Buckets)
#define INFLUXDB_BUCKET "YOUR_BUCKET_NAME"
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
// data point for RH and T
Point sensor("climate");
auto psensor = &sensor;
/* database link status */
int databaseLinkStatus = 0;

/* TIME SYNC */
time_t nowTime;

/* LCD */
LiquidCrystal_I2C lcd(0x27, 16, 2);
/* LCD CHARACTERS */
// Make custom characters:
byte Check[] = {
  B00000,
  B00001,
  B00011,
  B10110,
  B11100,
  B01000,
  B00000,
  B00000
};
byte Signal[] = {
  B00000,
  B00001,
  B00001,
  B00001,
  B00101,
  B00101,
  B10101,
  B10101
};


/* WIFI */
WiFiMulti wifiMulti;
int wifiConnectStatus = 0;

/* TIME */
struct tm timeinfo;

/* HARDWARE */
/* SENSORS */
SensirionI2CSht4x sht4x;
SensirionI2CSgp41 sgp41;
VOCGasIndexAlgorithm voc_algorithm;
NOxGasIndexAlgorithm nox_algorithm;
uint16_t error;
float humidity = 0;                       // %RH
float temperature = 0;                    // degreeC
int32_t voc_index = 0;
int32_t nox_index = 0;
uint16_t srawVoc = 0;
uint16_t srawNox = 0;
uint16_t defaultCompenstaionRh = 0x8000;  // in ticks as defined by SGP41
uint16_t defaultCompenstaionT = 0x6666;   // in ticks as defined by SGP41
uint16_t compensationRh = 0;              // in ticks as defined by SGP41
uint16_t compensationT = 0;               // in ticks as defined by SGP41
uint16_t conditioning_s = 10;             // time in seconds needed for NOx conditioning
char errorMessage[64];

/* THREADING */
xQueueHandle xQueue;
TaskHandle_t xTask1;
TaskHandle_t xTask2;


void localHardware(void* parameters) {
    // setting up I2C
    Wire.begin();
    // lcd.init();  //initialize the lcd
    // lcd.backlight();  //open the backlight
    // lcd.createChar(0, Check);
    // lcd.createChar(1, Signal);
    // sensors' I2C
    sht4x.begin(Wire);
    sgp41.begin(Wire);
    int32_t index_offset;
    int32_t learning_time_offset_hours;
    int32_t learning_time_gain_hours;
    int32_t gating_max_duration_minutes;
    int32_t std_initial;
    int32_t gain_factor;
    voc_algorithm.get_tuning_parameters(
        index_offset, learning_time_offset_hours, learning_time_gain_hours,
        gating_max_duration_minutes, std_initial, gain_factor);
    Serial.println("\nVOC Gas Index Algorithm parameters");
    Serial.print("Index offset:\t");
    Serial.println(index_offset);
    Serial.print("Learing time offset hours:\t");
    Serial.println(learning_time_offset_hours);
    Serial.print("Learing time gain hours:\t");
    Serial.println(learning_time_gain_hours);
    Serial.print("Gating max duration minutes:\t");
    Serial.println(gating_max_duration_minutes);
    Serial.print("Std inital:\t");
    Serial.println(std_initial);
    Serial.print("Gain factor:\t");
    Serial.println(gain_factor);
    nox_algorithm.get_tuning_parameters(
        index_offset, learning_time_offset_hours, learning_time_gain_hours,
        gating_max_duration_minutes, std_initial, gain_factor);
    Serial.println("\nNOx Gas Index Algorithm parameters");
    Serial.print("Index offset:\t");
    Serial.println(index_offset);
    Serial.print("Learing time offset hours:\t");
    Serial.println(learning_time_offset_hours);
    Serial.print("Gating max duration minutes:\t");
    Serial.println(gating_max_duration_minutes);
    Serial.print("Gain factor:\t");
    Serial.println(gain_factor);
    Serial.println("");
    sensor.addTag("device", "ESP32");

    for(;;) {
        // 0xFD 
        error = sht4x.measureHighPrecision(temperature, humidity);
        if (error) {
            Serial.print("Core0-I: SHT4x - Error trying to execute measureHighPrecision(): ");
            errorToString(error, errorMessage, 256);
            Serial.println(errorMessage);
            Serial.println("Core0-I: Fallback to use default values for humidity and temperature compensation for SGP41");
            compensationRh = defaultCompenstaionRh;
            compensationT = defaultCompenstaionT;
          } 
        else {
            Serial.print("Core0-I: T= ");
            Serial.print(temperature);
            Serial.print("\t");
            Serial.print("RH= ");
            Serial.println(humidity);

            // convert temperature and humidity to ticks as defined by SGP41
            // interface
            // NOTE: in case you read RH and T raw signals check out the
            // ticks specification in the datasheet, as they can be different for
            // different sensors
            compensationT = static_cast<uint16_t>((temperature + 45) * 65535 / 175);
            compensationRh = static_cast<uint16_t>(humidity * 65535 / 100);
          }
        // 3. Measure SGP4x signals
        if (conditioning_s > 0) {
            // During NOx conditioning (10s) SRAW NOx will remain 0
            error = sgp41.executeConditioning(compensationRh, compensationT, srawVoc);
            conditioning_s--;
          } 
        else {
            error = sgp41.measureRawSignals(compensationRh, compensationT, srawVoc, srawNox);
          }

        // 4. Process raw signals by Gas Index Algorithm to get the VOC and NOx
        // index
        //    values
        if (error) {
            Serial.print("Core0-I: SGP41 - Error trying to execute measureRawSignals(): ");
            errorToString(error, errorMessage, 256);
            Serial.println(errorMessage);
        } else {
            voc_index = voc_algorithm.process(srawVoc);
            nox_index = nox_algorithm.process(srawNox);
            Serial.print("Core0-I: VOC Index= ");
            Serial.print(voc_index);
            Serial.print("\t");
            Serial.print("NOx Index= ");
            Serial.println(nox_index);
        }
        
        // lcd.setCursor(0,0);
        // lcd.send_string("Hello,World!");
        // lcd.print("Hello, world!");
        // lcd.clear();
        
        // time(&nowTime);
        // lcd.clear();
        // lcd.setCursor(0, 0); 
        // getLocalTime(&timeinfo);
        // lcd.print(&timeinfo, "%H:%M:%S");
        // lcd.setCursor(9, 0);
        // if(wifiConnectStatus == 1) {
        //     lcd.write(1);
        // }
        // else {
        //     lcd.print("X");
        // }
        // lcd.setCursor(10, 0);
        // if(databaseLinkStatus == 1) {
        //     lcd.write(0);
        // }
        // else {
        //     lcd.print("X");
        // }
        // lcd.setCursor(12, 0); 
        // lcd.print(temperature); 
        // lcd.setCursor(0, 1); 
        // lcd.print(humidity);  
        // lcd.setCursor(6, 1); 
        // lcd.print(voc_index);  
        // lcd.setCursor(10, 1); 
        // lcd.print(nox_index);  



        delay(1000);
        
        // lcd.printstr("hello");
    }
      
}

void uploadRemote(void* parameters) {
    wifiMulti.addAP("AP_NAME", "PASSWORD");
    wifiMulti.addAP("ustcnet", 0x00);
    wifiMulti.addAP("AP_NAME", "PASSWORD");
    wifiMulti.addAP("AP_NAME", "PASSWORD");
    wifiMulti.addAP("AP_NAME", "PASSWORD");
    for(;;) {
        if(wifiMulti.run()==WL_CONNECTED) {
            Serial.print("Core1-I: Already connected to ");
            Serial.println(WiFi.SSID());
            wifiConnectStatus = 1;
        }
        else {
            wifiConnectStatus = 0;
            databaseLinkStatus = 0;
            //Wait till ESP8266 connects with an AP
            Serial.print("Core1-E: WiFi Disconnected");
            Serial.print("Core1-I: Establishing a connection with a nearby Wi-Fi...");
            while(wifiMulti.run()!=WL_CONNECTED) {
                Serial.print(".");
            }
            wifiConnectStatus = 1;
            Serial.println();
            Serial.print("Core1-I: Connected to ");
            Serial.println(WiFi.SSID());
            Serial.print("Core1-I: IP Address: ");
            Serial.println(WiFi.localIP());
            // after reconnecting, sync the time
            timeSync(TZ_INFO, "edu.ntp.org.cn", "pool.ntp.org", "time.nis.gov");
        }
        //Clear fields for reusing the point. Tags will remain untouched
        sensor.clearFields();                             
        // Store measured value into point                 
        sensor.addField("Temperature", temperature);                              
        sensor.addField("Humidity", humidity);   
        sensor.addField("VOC", voc_index);
        sensor.addField("NOx", nox_index);
        //Write data point
        if (!client.writePoint(sensor)) {
            Serial.print("Core1-E: InfluxDB write failed: ");
            Serial.println(client.getLastErrorMessage());
            databaseLinkStatus = 0;
        } 
        else {
            Serial.println("Core1-I: Successfully upload data to influxDB");
            databaseLinkStatus = 1;
        }
        Serial.print("Core1-I: ");
        // printLocalTime();
        time(&nowTime);
        if(nowTime % 80000 == 0) {
            timeSync(TZ_INFO, "edu.ntp.org.cn", "pool.ntp.org", "time.nis.gov");
        }
        delay(800);
    }
}


void printLocalTime(){
    // struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        Serial.println("Failed to obtain time");
        return;
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    // Serial.print("Day of week: ");
    // Serial.println(&timeinfo, "%A");
    // Serial.print("Month: ");
    // Serial.println(&timeinfo, "%B");
    // Serial.print("Day of Month: ");
    // Serial.println(&timeinfo, "%d");
    // Serial.print("Year: ");
    // Serial.println(&timeinfo, "%Y");
    // Serial.print("Hour: ");
    // Serial.println(&timeinfo, "%H");
    // Serial.print("Hour (12 hour format): ");
    // Serial.println(&timeinfo, "%I");
    // Serial.print("Minute: ");
    // Serial.println(&timeinfo, "%M");
    // Serial.print("Second: ");
    // Serial.println(&timeinfo, "%S");

    // Serial.println("Time variables");
    // char timeHour[3];
    // strftime(timeHour,3, "%H", &timeinfo);
    // Serial.println(timeHour);
    // char timeWeekDay[10];
    // strftime(timeWeekDay,10, "%A", &timeinfo);
    // Serial.println(timeWeekDay);
    // Serial.println();
}


void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    wifiMulti.addAP("AP_NAME", "PASSW0RD");
    wifiMulti.addAP("ustcnet", 0x00);
    wifiMulti.addAP("AP_NAME", "PASSW0RD");
    wifiMulti.addAP("AP_NAME", "PASSW0RD");
    wifiMulti.addAP("AP_NAME", "PASSW0RD");
    // trying to connect to an AP at setup stage
    if(wifiMulti.run() != WL_CONNECTED) {
        Serial.println("WiFi not connected!");
    }
    // sync the time
    else {
        Serial.print("Core1-I: Already connected to ");
        Serial.println(WiFi.SSID());
        wifiConnectStatus = 1;
        timeSync(TZ_INFO, "edu.ntp.org.cn", "pool.ntp.org", "time.nis.gov");
    }


    xTaskCreatePinnedToCore(localHardware, "localHardware", 10000, NULL, 1, &xTask1, 1); 
    xTaskCreatePinnedToCore(uploadRemote, "uploadRemote", 10000, NULL, 1, &xTask2, 0);     
}

void loop() {
  // put your main code here, to run repeatedly:
}
