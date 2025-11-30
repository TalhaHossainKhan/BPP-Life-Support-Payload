/*
  Combined script to read from all sensors, print to Serial,
  and log to SD Card.

  This version is optimized for low RAM usage by:
  1. Using F() macro for all string literals.
  2. Removing large char buffers from the loop().
  3. Printing data piece-by-piece to avoid a large buffer.
  4. Managing SPI hardware to resolve conflicts.

  Sensors:
  - DHT22 (Temp/Hum) -> Pin 2
  - UVM-30A (UV) -> Pin A0
  - GY-63/MS5611 (Pressure/Temp/Alt) -> I2C (SDA, SCL)
  - DS1307 (RTC) -> I2C (SDA, SCL)
  - DS18B20 (Temp Probe) -> Pin 8
  - LED -> Pin 9 (Always ON)
  - SCD41 (CO2/Temp/Hum) -> I2C (SDA, SCL)
  - SD Card Reader -> SPI (Pins 10, 11, 12, 13)
*/

// General & I2C
#include <Wire.h>
#include <Arduino.h>

// SD Card Libraries
#include <SPI.h>
#include <SD.h>

// SCD41 (CO2) Library
#include <SensirionI2cScd4x.h>

// DHT22 (Temp/Hum) Libraries
#include <Adafruit_Sensor.h>
#include <DHT.h>

// GY-63 (Pressure) Library
#include "MS5611.h"

// DS1307 (RTC) Library
#include "RTClib.h"

// DS18B20 (Temp Probe) Libraries
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Pin Definitions ---
#define LED_PIN 9
#define DHTPIN 2       // DHT22 Signal pin
#define UVPIN A0       // UV Sensor Signal pin
#define ONE_WIRE_BUS 8 // DS18B20 Signal pin
#define SD_CS_PIN 10   // SD Card Chip Select

// --- Filename ---
const char* filename = "datalog.csv";

// --- Sensor Definitions ---
#define DHTTYPE DHT22  // Sensor type
DHT dht(DHTPIN, DHTTYPE);

// Pressure Sensor (using 0x77, the default)
MS5611 MS5611(0x77);

// Real-Time Clock
RTC_DS1307 rtc;

// Temperature Probe
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// CO2 Sensor
SensirionI2cScd4x scd4x;
const uint8_t SCD41_I2C_ADDR = 0x62;

// --- Global variables for SCD41 (to hold last known value) ---
static uint16_t scd4x_co2 = 0;
static float scd4x_temp = 0.0f;
static float scd4x_hum = 0.0f;


void setup() {
  // --- Start Serial and LED ---
  Serial.begin(9600);
  while (!Serial) {
    delay(10); // Wait for serial
  }
  Serial.println(F("Combined Sensor Test Initializing..."));

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Turn LED on
  
  Wire.begin(); // Start I2C

  // --- Initialize DHT22 ---
  Serial.println(F("Initializing DHT22..."));
  dht.begin();

  // --- Initialize UV Sensor ---
  Serial.println(F("Initializing UV Sensor..."));
  pinMode(UVPIN, INPUT);

  // --- Initialize GY-63 / MS5611 (Pressure) ---
  Serial.println(F("Initializing MS5611 (Pressure)..."));
  if (MS5611.begin() == true) {
    Serial.println(F("MS5611 found."));
  } else {
    Serial.println(F("MS5611 not found. Check wiring."));
  }

  // --- Initialize DS1307 (RTC) ---
  Serial.println(F("Initializing DS1307 (RTC)..."));
  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC! Check wiring."));
  }
  if (!rtc.isrunning()) {
    Serial.println(F("RTC is NOT running, setting time!"));
  }
  
  // --- Initialize SCD41 (CO2) ---
  // Init this BEFORE SD and OneWire
  Serial.println(F("Initializing SCD41 (CO2)..."));
  
  uint16_t error;
  scd4x.begin(Wire, SCD41_I2C_ADDR);
  Serial.println(F("...SCD41 init done.")); 


  error = scd4x.stopPeriodicMeasurement();
  if (error) {
    Serial.print(F("Error stopping SCD41: "));
    Serial.println(error); // Just print the error code
  }
  
  error = scd4x.startPeriodicMeasurement();
  if (error) {
    Serial.print(F("Error starting SCD41: "));
    Serial.println(error); // Just print the error code
  }

  // --- Initialize SD Card ---
  Serial.print(F("Initializing SD card..."));
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("...initialization failed!"));
  } else {
    Serial.println(F("...initialization done."));
  }

  // --- CRITICAL: Release SPI bus for OneWire library ---
  SPI.end();
  Serial.println(F("SPI hardware released."));

  // --- Initialize DS18B20 (Temp Probe) (MOVED) ---
  Serial.println(F("Initializing DS18B20 (Temp Probe)..."));
  sensors.begin();
  Serial.println(F("...DS18B20 init done."));


  Serial.println(F("All sensors initialized. Waiting 5s for SCD41..."));
  delay(5000); // Wait for SCD41 to get first reading

  // --- WRITE HEADERS TO SD CARD ---
  // We must re-enable SPI to write headers
  SPI.begin();
  
  // Check if file exists. If not, create it and write headers.
  if (!SD.exists(filename)) {
    Serial.println(F("Creating datalog.csv and writing headers..."));
    File dataFile = SD.open(filename, FILE_WRITE);
    if (dataFile) {
      dataFile.println(F("Timestamp,DHT_Temp(C),DHT_Hum(%),UV_Raw,Press(mBar),Press_Temp(C),Press_Alt(Ft),Probe_Temp(C),CO2(ppm),SCD_Temp(C),SCD_Hum(%)"));
      dataFile.close();
    } else {
      Serial.println(F("Error creating datalog.csv"));
    }
  } else {
    Serial.println(F("datalog.csv already exists."));
  }
  
  SPI.end(); // Release SPI again
  Serial.println(F("Header check done, SPI released."));

  // --- PRINT HEADERS TO SERIAL ---
  Serial.println(F("Timestamp,DHT_Temp(C),DHT_Hum(%),UV_Raw,Press(mBar),Press_Temp(C),Press_Alt(Ft),Probe_Temp(C),CO2(ppm),SCD_Temp(C),SCD_Hum(%)"));
}


void loop() {
  // --- Define buffers for string building ---
  char tempBuffer[12]; // Small buffer just for float conversion

  // --- 1. Read DHT22 ---
  float dht_h = dht.readHumidity();
  float dht_t = dht.readTemperature();

  // --- 2. Read UV Sensor ---
  int uvValue = analogRead(UVPIN);

  // --- 3. Read MS5611 (Pressure) ---
  MS5611.read(); 
  double press_mBar = MS5611.getPressure();
  double press_Temp = MS5611.getTemperature();
  double press_Alt = MS5611.getAltitudeFeet();

  // --- 4. Read DS1307 (RTC) ---
  DateTime now = rtc.now();

  // --- 5. Read DS18B20 (Temp Probe) ---
  sensors.requestTemperatures(); 
  float probeTempC = sensors.getTempCByIndex(0);
  if (probeTempC == DEVICE_DISCONNECTED_C) {
    probeTempC = -127.0; // Error value
  }

  // --- 6. Read SCD41 (CO2 Sensor) ---
  uint16_t error;
  bool isDataReady = false;

  error = scd4x.getDataReadyStatus(isDataReady);
  if (error) {
    Serial.print(F("SCD4R_ERR:")); Serial.println(error);
  }

  if (isDataReady) {
    error = scd4x.readMeasurement(scd4x_co2, scd4x_temp, scd4x_hum);
    if (error) {
      Serial.print(F("SCD4M_ERR:")); Serial.println(error);
    } else if (scd4x_co2 == 0) {
    }
  }

  // --- 7. Re-enable SPI to access the SD Card ---
  SPI.begin();
  
  File dataFile = SD.open(filename, O_APPEND | O_WRITE);

  // --- 8. Print all data piece-by-piece to Serial and SD Card ---
  // Only proceed if the file opened successfully
  if (dataFile) {
    // Timestamp
    sprintf(tempBuffer, "%04d-%02d-%02dT%02d:%02d:%02d", 
            now.year(), now.month(), now.day(), 
            now.hour(), now.minute(), now.second());
    Serial.print(tempBuffer);
    dataFile.print(tempBuffer);

    // DHT Temp
    Serial.print(F(","));
    dataFile.print(F(","));
    if (isnan(dht_t)) {
      Serial.print(F("NaN"));
      dataFile.print(F("NaN"));
    } else {
      dtostrf(dht_t, 4, 2, tempBuffer);
      Serial.print(tempBuffer);
      dataFile.print(tempBuffer);
    }
    
    // DHT Humidity
    Serial.print(F(","));
    dataFile.print(F(","));
    if (isnan(dht_h)) {
      Serial.print(F("NaN"));
      dataFile.print(F("NaN"));
    } else {
      dtostrf(dht_h, 4, 2, tempBuffer);
      Serial.print(tempBuffer);
      dataFile.print(tempBuffer);
    }
    
    // UV Data
    Serial.print(F(","));
    dataFile.print(F(","));
    Serial.print(uvValue);
    dataFile.print(uvValue);
    
    // Pressure Data
    Serial.print(F(","));
    dataFile.print(F(","));
    dtostrf(press_mBar, 6, 2, tempBuffer);
    Serial.print(tempBuffer);
    dataFile.print(tempBuffer);
    
    Serial.print(F(","));
    dataFile.print(F(","));
    dtostrf(press_Temp, 4, 2, tempBuffer);
    Serial.print(tempBuffer);
    dataFile.print(tempBuffer);
    
    Serial.print(F(","));
    dataFile.print(F(","));
    dtostrf(press_Alt, 6, 2, tempBuffer);
    Serial.print(tempBuffer);
    dataFile.print(tempBuffer);
    
    // Temp Probe Data
    Serial.print(F(","));
    dataFile.print(F(","));
    dtostrf(probeTempC, 4, 2, tempBuffer);
    Serial.print(tempBuffer);
    dataFile.print(tempBuffer);

    // SCD41 Data
    Serial.print(F(","));
    dataFile.print(F(","));
    Serial.print(scd4x_co2);
    dataFile.print(scd4x_co2);
    
    Serial.print(F(","));
    dataFile.print(F(","));
    dtostrf(scd4x_temp, 4, 2, tempBuffer);
    Serial.print(tempBuffer);
    dataFile.print(tempBuffer);

    Serial.print(F(","));
    dataFile.print(F(","));
    dtostrf(scd4x_hum, 4, 2, tempBuffer);
    Serial.print(tempBuffer);
    dataFile.print(tempBuffer);

    // End of line
    Serial.println();
    dataFile.println();
    
    // Close the file
    dataFile.close();

    // --- Blink LED to show a successful WRITE ---
    digitalWrite(LED_PIN, LOW);
    delay(100); // Blink for 100ms
    digitalWrite(LED_PIN, HIGH);
    
    // Wait for the rest of the 5-second interval
    delay(4900); // (4900ms + 100ms = 5000ms total)
    
  } else {
    // This will print if the SD card fails to open in the loop
    Serial.println(F("SD Open Fail"));

    // --- Wait for 5 seconds before retrying ---
    delay(5000); 
  }

  // --- 9. CRITICAL: Release SPI bus again for OneWire ---
  SPI.end();
  
  // --- Delays are now handled inside the if/else block ---
}

