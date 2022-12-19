#include <WiFi.h>
#include <FastLED.h>
#include <stdio.h>
#include <Firebase_ESP_Client.h>
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"
#include "settings.h"


// TO DO:
// Firebase
// Test FastLED Esp32
// Test Login to hotspot and connect to DB
// Test IR Sensors on every pin
// Database updating functions
// Put carPresence() into loop() so before db updating functions
// Set up variables for string names


// DB 1: /LotStatus
// Lot no.: Integer
// Floor no.: Integer
// Mall: String/Hash e.g Suntec
// Status: Integer, 2 for amber, 1 for taken, 0 for available

// DB 2: /FloorStatus
// Floor No.:
// Total Lots: NUMLOTSPERFLOOR
// Avaiable Lots: 

// Post fields from esp32 to firebase:
// Lot no.:
// Floor no.:
// Mall:


// Map: Static map of all lots to priorities (NOT STORED IN FIREBASE)
// Lot no.: Integer
// Priority (Higher the better, based on distance to lift): Integer

// Pin defintions
// Pins 16-19(4 pins), 21-23(3 pins), 25-27(3 pins), 32-33(2 pins) --> Input from sensors
const int CarSensorsPins[8] = {16, 17, 18, 19, 21, 22, 23, 25};
const int InSensorsPins[2] = {26, 27};
const int OutSensorsPins[2] = {32, 33};
unsigned long readCarsPrevMillis = 0;
const unsigned long carsTimerDelay = CARSPOLLINTERVAL;

// Sensor States
typedef struct lot {
  int lotNo;
  int state;
  int carSensorPin;
  int lightIdx;
} Lot;

typedef struct floor_struct {
  Lot lots[NUMLOTSPERFLOOR];
  int inPin;
  int outPin;
  int floorOccupancy;  // No. of ppl on floor
} Floor;

typedef struct mall_struct {
  Floor floors[FLOORSPERMALL];
  int totalOccupancy;
} Mall;

// Lvl 1 Lots: 11, 12, 13, 14
// Lvl 2 Lots: 21, 22, 23, 24
Mall coconutMall = {{{{11, 0, CarSensorsPin[0], 0}, {12, 0, CarSensorsPin[1], 1}, {13, 0, CarSensorsPin[2], 2}, {14, 0, CarSensorsPin[3], 3}, InSensorsPins[0], OutSensorsPins[1], 0},
                    {{21, 0, CarSensorsPin[4], 4}, {22, 0, CarSensorsPin[5], 5}, {23, 0, CarSensorsPin[6], 6}, {24, 0, CarSensorsPin[7], 7}, InSensorsPins[0], OutSensorsPins[1], 0}}, 
                    0};

// Later on these states will be used for a few things:
// 1) Predict next available lot, amber algorithm and mark taken lots
// 2) Check if there is an update to the lot struct, and floor occupancy. If there is, the two seperate functions will be used to update 2 different databases
// 3) 


// LED States
// Pin 13 - Chain of WS2812B
const int ledsPin = 13;
CRGB parkingLights[NUMLEDS];

//Firebase Data definitions
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long sendDataPrevMillis = 0;
int intValue;
float floatValue;
bool signupOK = false;
const unsigned long dbtimerDelay = DBPOLLINTERVAL;


void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(1000);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

void setColours() { 
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);
  for (floor_idx = 0; floor_idx < FLOORSPERMALL; floor_idx++) {  // 0 - 1 floors
    for (lot_idx = 0; lot_idx < NUMLOTSPERFLOOR; lot_idx++) {  // 0 - 3 lots
      int pinNo = coconutMall.floors[floor_idx].lots[lot_idx].lightIdx
      int state = coconutMall.floors[floor_idx].lots[lot_idx].state
      switch(state) {
        case 0:  // Available
          parkingLights[pinNo].setHue(greenHue);
          break;
        case 1:  // Taken
          parkingLights[pinNo].setHue(redHue);
          break;
        case 2:  // Potentially Reserved
          parkingLights[pinNo].setHue(amberHue);
          break;
        default:
          break;
      }
    }
  }
  FastLED.show();
}

bool carPresence(int parkingPin) {
  // Returns True if an object is above the lot for more than 50 ms (checking once every 10ms)
  bool carPresent = True;
  for (timestep = 0; timestep < 5; timestep++) {  // make sure it always goes through the 10 time steps
    carPresent = carPresent && digitalRead(parkingPin);  // Make sure no error lol
    delayMicroseconds(10000); 
  }
  return carPresent;
}

void updateLotStatusDatabase(*FirebaseJson lotStatusJson) {
  // aosdojf
  // set(key, value)
  lotStatusJson->set(
}

void updateFloorStatusDatabase(*FirebaseJson floorStatusJson) {
  // jsadoifjaspidf
  floorStatusJson->set
}

void setup() {
  Serial.begin(115200);
  initWiFi();
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  // Sign up
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.println(config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Pins 16-19(4 pins), 21-23(3 pins), 25-27(3 pins), 32-33(2 pins) --> Input from sensors
  for (int i = 0; i < NUMLOTSPERFLOOR * FLOORSPERMALL; i++) {
    pinMode(CarSensorsPins[i], INPUT);
    Serial.print("Pin ");
    Serial.print(CarSensorsPins[i]);
    Serial.println(" set as Input");
  }

  // Setup LED Pins FastLED
  FastLED.addLeds<WS2812B, ledsPin, RGB>(parkingLights, NUMLEDS);
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);
}

void loop() {
  // ENTERING/LEAVING CARS CODE HERE, NO DELAY COS SPEEEEEDDDDD, WE NEED TO MAKE SURE WE KEEP TRACK OF ALL CARS
  // PERFORM A CHECK BASED ON NO OF PARKED CARS, IF IT DOESN'T TALLY THEN CHECK AGAIN
  
  if (millis() - readCarsPrevMillis > carsTimerDelay) {
    // Take 400 ms or 0.4s to check all 8 lots, during this period there is a chance that entering cars might not be detected
    for (floor_idx = 0; floor_idx < FLOORSPERMALL; floor_idx++) {  // 0 - 1 floors
      for (lot_idx = 0; lot_idx < NUMLOTSPERFLOOR; lot_idx++) {  // 0 - 3 lots
        coconutMall.floors[floor_idx].lots[lot_idx].state = carPresence(coconutMall.floors[floor_idx].lots[lot_idx].carSensorPin);  // see if can simplify else fuck it
      }
    }
    // AMBER ALGO
    //  if no. of floor occupancy < NUMLOTSPERFLOOR,  then do a linear search for next open lot (i.e state is 1) and turn it amber (i.e state 2)
    if (coconutMall.floors[floor_idx].floorOccupancy < NUMLOTSPERFLOOR) {
      
    }
  }

  // AMBER ALGO HERE
  // 
  
  // SetColours HERE, or in the above readCars, when the states are updated, it will update all LEDS at once
  setColours();

  // FIREBASE UPDATE AFTER EVERYTHING
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > dbtimerDelay || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    // HAHA WHAT IS EFFICIENCY, JUST SEND ALL DATA
    FirebaseJson* lotStatus;  // setup JSON for sending data, esp32 should not need to get data
    FirebaseJson* floorStatus;  
    /*
    if (Firebase.RTDB.getInt(&fbdo, "/test/int")) {
      if (fbdo.dataType() == "int") {
        intValue = fbdo.intData();
        Serial.println(intValue);
      }
    }
    else {
      Serial.println(fbdo.errorReason());
    }
    
    if (Firebase.RTDB.getFloat(&fbdo, "/test/float")) {
      if (fbdo.dataType() == "float") {
        floatValue = fbdo.floatData();
        Serial.println(floatValue);
      }
    }
    else {
      Serial.println(fbdo.errorReason());
    }
   */
   
  }
}
