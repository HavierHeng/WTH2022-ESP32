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


//\car-tell\LotStatus
//
//Mall: String
//Floor: Int
//Lot: Int
//Status: Int (2 amber, 1 taken, 0 available)
//
//\car-tell\FloorStatus
//Mall: String
//Floor: Int
//TotalLots: NUMLOTSPERFLOOR (FIXED)
//AvailableLots: Int




// Map: Static map of all lots to priorities (NOT STORED IN FIREBASE)
// Lot no.: Integer
// Priority (Higher the better, based on distance to lift): Integer

// Pin defintions
// Pins 16-19(4 pins), 21-23(3 pins), 25-27(3 pins), 32-33(2 pins) --> Input from sensors
const int CarSensorsPins[8] = {16, 17, 18, 19, 21, 22, 23, 25};
const int InSensorsPins[2] = {26, 27};
const int OutSensorsPins[2] = {32, 33};
unsigned long readCarsPrevMillis = 0;
const unsigned long carsTimerDelay = IRPOLLINTERVAL;

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
  int currentParked;  // No. of ppl parked, i.e state is green or 0
} Floor;

typedef struct mall_struct {
  Floor floors[FLOORSPERMALL];
  int totalOccupancy;
} Mall;

// Lvl 1 Lots: 11, 12, 13, 14
// Lvl 2 Lots: 21, 22, 23, 24

// Mall floor is arranged such that highest priority is first, so then linear search can work right away 
// e.g lvl 1: 12, 13, 11, 14
// e.g lvl 2: 22, 23, 21, 24
Mall coconutMall = {{{{{12, 0, CarSensorsPins[1], 1}, {13, 0, CarSensorsPins[2], 2}, {14, 0, CarSensorsPins[3], 3}, {11, 0, CarSensorsPins[0], 0}}, InSensorsPins[0], OutSensorsPins[1], 0, 0},
                    {{{22, 0, CarSensorsPins[5], 5}, {23, 0, CarSensorsPins[6], 6}, {24, 0, CarSensorsPins[7], 7}, {21, 0, CarSensorsPins[4], 4}}, InSensorsPins[0], OutSensorsPins[1], 0, 0}}, 
                    0};
String mallName = String(MALLNAME);

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
bool signupOK = false;
const unsigned long dbtimerDelay = DBPOLLINTERVAL;
String databaseParentPath = "/car-tell";
String parentPath;  // Parent Node to update the JSON by


void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

void setColours() { 
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);
  for (int floor_idx = 0; floor_idx < FLOORSPERMALL; floor_idx++) {  // 0 - 1 floors
    for (int lot_idx = 0; lot_idx < NUMLOTSPERFLOOR; lot_idx++) {  // 0 - 3 lots
      int pinNo = coconutMall.floors[floor_idx].lots[lot_idx].lightIdx;
      int state = coconutMall.floors[floor_idx].lots[lot_idx].state;
      switch(state) {
        case 0:  // Available
          parkingLights[pinNo].setHue(GREENHUE);
          break;
        case 1:  // Taken
          parkingLights[pinNo].setHue(REDHUE);
          break;
        case 2:  // Potentially Reserved
          parkingLights[pinNo].setHue(AMBERHUE);
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
  bool carPresent = true;
  Serial.print(parkingPin);
  for (int timestep = 0; timestep < 5; timestep++) {  // make sure it always goes through the 5 time steps
    bool inp = digitalRead(parkingPin);
//    Serial.print(inp);
//    carPresent = carPresent && digitalRead(inp);  // Make sure no error lol
    delayMicroseconds(10000); 
  }
  Serial.println();
  return carPresent;
}

void updateLotStatusDatabase() {
  //\car-tell\LotStatus\<Mall>\<Floor>
  //Lot: Int
  //Status: Int (2 amber, 1 taken, 0 available)

  String lotStatusParentPath = databaseParentPath + "/" + String("LotStatus") + "/" + mallName;

  for (int lvl = 1; lvl < FLOORSPERMALL + 1; lvl++) {
    FirebaseJson lotStatus; 
    String lvlName = String("lvl") + String(lvl);
    parentPath = lotStatusParentPath + "/" + lvlName;
    int floor_idx = lvl - 1;
    for (int lot_idx = 0; lot_idx < NUMLOTSPERFLOOR; lot_idx++) {  // 0 - 3 lots
      // For each lot no, 
      int lotNo = coconutMall.floors[floor_idx].lots[lot_idx].lotNo;
      int state = coconutMall.floors[floor_idx].lots[lot_idx].state;
      lotStatus.set(String(lotNo).c_str(), state);
    }
    Serial.println(parentPath);
    lotStatus.toString(Serial, true);
    Serial.printf("Set LotStatus for %s ... %s\n", lvlName, Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &lotStatus) ? "ok" : fbdo.errorReason().c_str());
  }
}

void updateFloorStatusDatabase() {
  //\car-tell\FloorStatus\Mall\Floor
  //TotalLots: NUMLOTSPERFLOOR (FIXED)
  //AvailableLots: Int
  String floorStatusParentPath = databaseParentPath + "/" + "FloorStatus";
  FirebaseJson floorStatus;  
  
  for (int lvl = 1; lvl < FLOORSPERMALL + 1; lvl++) {
    int floor_idx = lvl - 1;
    String lvlName = String("lvl") + String(lvl);
    String totalLotsName = lvlName + "/" + "TotalLots";
    String availableLotsName = lvlName + "/" + "AvailableLots";
    int totalLots = NUMLOTSPERFLOOR;
    int availableLots = NUMLOTSPERFLOOR - coconutMall.floors[floor_idx].floorOccupancy;
    floorStatus.set(totalLotsName, totalLots);
    floorStatus.set(availableLotsName, availableLots);
  }
  floorStatus.toString(Serial, true);
  Serial.printf("Set FloorStatus for Mall ... %s\n", Firebase.RTDB.setJSON(&fbdo, floorStatusParentPath.c_str(), &floorStatus) ? "ok" : fbdo.errorReason().c_str());
}


void updateParked(){
  for (int floor_idx = 0; floor_idx < FLOORSPERMALL; floor_idx++) {  // 0 - 1 floors
    int currentParkedNo = 0;
    for (int lot_idx = 0; lot_idx < NUMLOTSPERFLOOR; lot_idx++) {  // 0 - 3 lots
      if (coconutMall.floors[floor_idx].lots[lot_idx].state == 0) {
        // Then add to counter
        currentParkedNo += 1;
      }
    }
    coconutMall.floors[floor_idx].currentParked = currentParkedNo;
  }
}

void setup() {
  Serial.begin(115200);
  initWiFi();
  // Setup LED Pins FastLED
  
  FastLED.addLeds<WS2812B, ledsPin, RGB>(parkingLights, NUMLEDS);
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);
  for (int i = 0; i < 8; i++) {
    parkingLights[i] = CRGB::Red;
  }
  FastLED.show();

  Serial.print("Firebase Client ");
  Serial.println(FIREBASE_CLIENT_VERSION);
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Give 3rd party perms to reconnectWiFi
  Firebase.reconnectWiFi(true);
  
  // Sign up
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.println(config.signer.signupError.message.c_str());
    // signupError.message shuld not show "ADMIN_ONLY_OPERATION" since anon provider is on
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
}

void loop() {
  // ENTERING/LEAVING CARS CODE HERE, NO DELAY COS SPEEEEEDDDDD, WE NEED TO MAKE SURE WE KEEP TRACK OF ALL CARS, UPDATES FLOOR OCCUPANY
  // PERFORM A CHECK BASED ON NO OF PARKED CARS, IF IT DOESN'T TALLY THEN CHECK AGAIN
  // TBC

  updateParked();
  
  if (millis() - readCarsPrevMillis > carsTimerDelay) {
    // Take 400 ms or 0.4s to check all 8 lots, during this period there is a chance that entering cars might not be detected
    for (int floor_idx = 0; floor_idx < FLOORSPERMALL; floor_idx++) {  // 0 - 1 floors
      for (int lot_idx = 0; lot_idx < NUMLOTSPERFLOOR; lot_idx++) {  // 0 - 3 lots
        coconutMall.floors[floor_idx].lots[lot_idx].state = carPresence(coconutMall.floors[floor_idx].lots[lot_idx].carSensorPin);  // see if can simplify else fuck it
      }
    
    // AMBER ALGO, should only kick in when a new car enters a floor, that is, if occupany > no. of taken lots 
    //  if no. of floor occupancy < NUMLOTSPERFLOOR,  then do a linear search for next open lot (i.e state is 1) and turn it amber (i.e state 2)
    if ((coconutMall.floors[floor_idx].floorOccupancy <= NUMLOTSPERFLOOR) && (coconutMall.floors[floor_idx].floorOccupancy > coconutMall.floors[floor_idx].currentParked)) {
      for (int lot_idx = 0; lot_idx < NUMLOTSPERFLOOR; lot_idx++) {  // 0 - 3 lots
        if (coconutMall.floors[floor_idx].lots[lot_idx].state == 0) {
          coconutMall.floors[floor_idx].lots[lot_idx].state = 2;  // amber
          break;
        }
      }
    }
  }
}
  
  // SetColours HERE, or in the above readCars, when the states are updated, it will update all LEDS at once
  setColours();

  // FIREBASE UPDATE AFTER EVERYTHING
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > dbtimerDelay || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    // HAHA WHAT IS EFFICIENCY, JUST SEND ALL DATA
    updateLotStatusDatabase();
    updateFloorStatusDatabase();
   
  }
}
