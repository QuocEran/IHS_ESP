// HTTP VS WIFI
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
// MAX30102
#include <Wire.h>
#include "spo2_algorithm.h"
#include "heartRate.h"
#include "MAX30105.h"
// DHT11 VS DS18B20
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <DHT.h>            // Khai báo sử dụng thư viện DHT
//OLED
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
//defines the I2C pins to which the display is connected
#define OLED_SDA 21
#define OLED_SCL 22
Adafruit_SH1106 display(21, 22);


#define ONE_WIRE_BUS 26 
#define DHTPIN 14            // Chân dữ liệu của DHT11 kết nối với GPIO13
#define DHTTYPE DHT11       // Loại DHT được sử dụng
#define MAX_BRIGHTNESS 255
#define REPORTING_PERIOD_MS 3000 // frequency of updates sent to blynk app in ms

// SENSORS
MAX30105 particleSensor;
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(ONE_WIRE_BUS); 
DallasTemperature DS18B20(&oneWire);

uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data
uint32_t tsLastReport = 0;  //stores the time the last update was sent to the blynk app

int32_t bufferLength; //data length
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid
long lastBeat = 0; //Time at which the last beat occurred

byte pulseLED = 2; //onboard led on esp32 nodemcu
byte readLED = 19; //Blinks with each data read 

float beatsPerMinute; //stores the BPM as per custom algorithm
float beatAvg, sp02Avg; //stores the average BPM and SPO2 
float ledBlinkFreq; //stores the frequency to blink the pulseLED

const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;

// WIFI SETUP
#ifndef STASSID
#define STASSID "Eran"
#define STAPSK "123456789"
#endif
String SERVER_IP = "http://ihs-api-v1.herokuapp.com";
String patientId;
String macAdrress; 
int isNRegistered = 0;

void setup() {
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
  //set the text size, color, cursor position and displayed text
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("IHS V1");
  display.println("Connecting");
  display.display();
  WiFi.begin(STASSID, STAPSK);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      display.print(".");
      display.display();
    }
    display.clearDisplay();
    display.display();
    display.setCursor(0, 0);
    display.print("IP: ");
    display.println(WiFi.localIP());
    macAdrress = WiFi.macAddress();
    String serverAddress = SERVER_IP + "/api/esp?espId=" + macAdrress;
    display.println("MAC address: ");
    display.println(macAdrress);
    display.display();
    WiFiClient client;
    HTTPClient http;
    // get patientId
    if (http.begin(client, serverAddress)) {  // HTTP
      // start connection and send HTTP header
      int httpCode = http.GET();
      // httpCode will be negative on error
      display.println("");
      display.println("[HTTP] GET... ");
      display.display();
      if (httpCode == 200) {
        // HTTP header has been send and Server response header has been handled
          // file found at server
          String payload = http.getString();
          DynamicJsonDocument doc(1024);
          deserializeJson(doc, payload);
          auto id = doc["patientId"].as<String>();
          patientId = id;
          if(patientId == "null" || patientId == "")
          {
            display.println("patientId is null, Pls register patientId!");
            display.display();
            while(1);
          }
          display.println("PatientId: ");
          display.println(patientId);
          display.display();
          delay(5000);
          // Sensors setup
          DS18B20.begin(); 
          dht.begin(); 
            // Initialize sensor
          if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
          {
            display.clearDisplay();
            display.display();
            display.setCursor(0, 0);
            display.print("MAX30102 was not found. Please check wiring/power.");
            display.display();
            while (1);
          }
            display.clearDisplay();
            display.display();
            display.setCursor(0, 0);
            display.println("Wait 5s to start");
            display.display();
            delay(5000);

          /*The following parameters should be tuned to get the best readings for IR and RED LED. 
          *The perfect values varies depending on your power consumption required, accuracy, ambient light, sensor mounting, etc. 
          *Refer Maxim App Notes to understand how to change these values
          *I got the best readings with these values for my setup. Change after going through the app notes.
          */
          byte ledBrightness = 50; //Options: 0=Off to 255=50mA
          byte sampleAverage = 1; //Options: 1, 2, 4, 8, 16, 32
          byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
          byte sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
          int pulseWidth = 69; //Options: 69, 118, 215, 411
          int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
          
          particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
      } else {
        if(httpCode == 404)
        {
          isNRegistered = 1;
          display.clearDisplay();
          display.display();
          display.setCursor(0, 0);
          display.println("Device wasn't registered!");
          display.println("Registing...");
          display.display();
          delay(1000);
        }
        else
        {
          display.println("HTTP request Error: ");
          display.println(http.errorToString(httpCode).c_str());
          display.display();
          while(1);
        }
      }
      http.end();
    } else {
      display.print("Unable to connect");
      display.display();
    }

    if(isNRegistered){
        // start connection and send HTTP header and body
        http.begin(client, serverAddress);
        http.addHeader("Content-Type", "application/json");
        String httpRequestData = "{\"patientId\":\"\",\"espId\":\""+macAdrress+"\",\"location\":\"\",\"createdDate\":\"\",\"sensors\":[\"DHT11\",\"MAX30102\",\"DS18B20\"]}";
        int httpPostCode = http.POST(httpRequestData);
        // httpCode will be negative on error
        if (httpPostCode > 0) {
          // HTTP header has been send and Server response header has been handled
            display.println("[HTTP]code: 201");
            display.println("Reset device!");
            display.display();
        } else {
          display.println("HTTP request Error: ");
          display.println(http.errorToString(httpPostCode).c_str());
          display.println("Reset, try again");
          display.display();
        }
        while(1);
    }
    else {
      delay(100);
    }
}

void loop() {

  // wait for WiFi connection
  if ((WiFi.status() == WL_CONNECTED)) {

    WiFiClient client;
    HTTPClient http;

    bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps
  
    //read the first 100 samples, and determine the signal range
    for (byte i = 0 ; i < bufferLength ; i++)
    {
      while (particleSensor.available() == false) //do we have new data?
        particleSensor.check(); //Check the sensor for new data
    
      redBuffer[i] = particleSensor.getIR();
      irBuffer[i] = particleSensor.getRed();
      particleSensor.nextSample(); //We're finished with this sample so move to next sample
    }
    //calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
  
    //Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 1 second
    while (1)
    {
      //dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
      for (byte i = 25; i < 100; i++)
      {
        redBuffer[i - 25] = redBuffer[i];
        irBuffer[i - 25] = irBuffer[i];
      }
    
      //take 25 sets of samples before calculating the heart rate.
      for (byte i = 75; i < 100; i++)
      {
        while (particleSensor.available() == false) //do we have new data?
          particleSensor.check(); //Check the sensor for new data
      
        digitalWrite(readLED, !digitalRead(readLED)); //Blink onboard LED with every data read
      
        redBuffer[i] = particleSensor.getRed();
        irBuffer[i] = particleSensor.getIR();
        particleSensor.nextSample(); //We're finished with this sample so move to next sample
      }
    
      //After gathering 25 new samples recalculate HR and SP02
      maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
      
        //Calculates average HR 
        if(validHeartRate == 1 && heartRate < 100 && heartRate > 60 && sp02Avg == 0)
        {
          beatAvg = heartRate;
        }
        else if (validHeartRate == 1 && heartRate < 100 && heartRate > 60)
        {
          beatAvg = (beatAvg+heartRate)/2;
        } 
        else
        {
          heartRate = 0;
          beatAvg = (beatAvg+heartRate);
        }

        //Calculates average SPO2 
        if(validSPO2 == 1 && spo2 < 100 && spo2 > 90 && sp02Avg == 0)
        {
          sp02Avg = spo2;
        }
        else if (validSPO2 == 1 && spo2 < 100 && spo2 > 85)
        {
          sp02Avg = (sp02Avg+spo2)/2;
        } 
        else
        {
          spo2 = 0;
          sp02Avg = (sp02Avg+spo2);
        }

      //Send Data 
      if (millis() - tsLastReport > REPORTING_PERIOD_MS)
      { 
        // Serial.print(" Requesting temperatures..."); 
        DS18B20.requestTemperatures();
        // Serial.println("DONE"); 
        // Serial.print("Temperature is: ");
        float temp = DS18B20.getTempCByIndex(0);
        // Serial.println(temp);

        float roomTemp = dht.readTemperature();
        float humi = dht.readHumidity();
        if (isnan(roomTemp) || isnan(humi)) {
           roomTemp = 0;
           humi = 0;
        }
        display.clearDisplay();
        display.display();
        display.setCursor(0, 0);
        display.println("Body Temp:"+String(temp, 1) + "*C");
        display.println("");
        display.print("BPM:"+String(beatAvg, 1));
        display.print("   ");
        display.println("SPO2:"+String(sp02Avg, 1) + "%");
        display.println("");
        display.println("RoomTemp:"+String(roomTemp, 1)+ "*C");
        display.println("Humid:"+String(humi, 1)+ "%");
        display.println("");
        display.display();
        http.begin(client, SERVER_IP + "/api/espData?espId=" + macAdrress + "&patientId=" + patientId); //HTTP
        http.addHeader("Content-Type", "application/json");
       
        // start connection and send HTTP header and body
        String httpRequestData = "{\"SPO2\":\""+String(sp02Avg, 1)+"\",\"RoomTemp\":\""+String(roomTemp, 1)+"\",\"Humid\":\""+String(humi, 1)+"\",\"Temp\":\""+String(temp, 1)+"\",\"HeartBeat\":\""+String(beatAvg, 1)+"\"}";
        int httpCode = http.POST(httpRequestData);
        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          display.println("[HTTP]POST...201");
          display.display();
        } else {
          display.print("[HTTP]POST...error");
          display.display();
        }
        http.end();
      }
    }
  }
}

