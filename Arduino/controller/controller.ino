/**
 * @file amb_controller.ino
 * @copyright Delano Flipse 2020
 * Control the AMB's behaviour using bluetooth and light sensors
 */

/* LIBRARIES */
#include <CmdMessenger.h>
#include <ArduinoBLE.h>
#include <ChainableLED.h>

/* CONSTANTS */

#define NUM_LEDS 2
#define RSSI_LIMIT -90
#define SENSOR_LIMIT 100

// device details
String deviceBaseName = "Ambichair-iot-";
String deviceUniqueName = "1";
String deviceName = deviceBaseName + deviceUniqueName;

// math constants
const float N_CONST = 2;
const float MP_CONST = -65;

/* VARIABLES */

// sensor values
int sensorA0 = 0;

// state variables
// states: 0 -> dorment, 1 -> picked up, 3-> sit on
int state = 0;
int foundNearbyDevices = 0;
int activity = 0;
int volume = 0;
int targetVolume = 0;

// idle fade variables
const int fadeUpperLimit = 600;
const int fadeLowerLimit = 100;
int fadeValue = fadeLowerLimit;
int fadeDelta = 2;

// music samples
int musicSample1 = 512;
int musicSample2 = 512;

// debugging/testing
int forceActivity = 0;

ChainableLED leds(7, 8, NUM_LEDS);
CmdMessenger cmdMessenger = CmdMessenger(Serial, ' ', '\n', '/');

// timing variables
unsigned long lastSensorValuesWrittenMark = 0;
unsigned long sensorSampleInterval = 40;

unsigned long lastBLEscan = 0;
unsigned long BLEscanInterval = 4000;

/* SETUP */
void setup() {
  // listen on serial connection for messages from the PC
  Serial.begin(115200);

  // add newline to every command
  cmdMessenger.printLfCr();
  cmdMessenger.attach(0, OnSetMusicSample);
  
  // begin Bluetooth LE initialization
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");

    while (1);
  }

  // start BLE advertising
  BLE.setDeviceName(deviceName.c_str());
  BLE.setLocalName(deviceName.c_str());
  BLE.advertise();

  // setup pins
  pinMode(A0, INPUT);
  pinMode(A2, INPUT);
 
  // log setup data
  Serial.println("Log: Board setup");
}

/* get distance based on RSSI value*/
float getDistance(int rssi) {
  return pow(10, (MP_CONST - rssi) / (10 * N_CONST));
}

/* determine if the other device is like us */
boolean isOtherDevice(String peripheralName) {
  // for debugging purposes, detect my phone as well
  if (peripheralName == "Delano 1+6") {
    return true;
  }

  // AMB's share a common name, so detect them on this.
  return peripheralName.startsWith(deviceBaseName);
}

/* check peripheral if they are another AMB */
void checkPeripheral(BLEDevice peripheral) {
  // determine a name
  String p_name = peripheral.hasLocalName()
    ? peripheral.localName()
    : peripheral.deviceName();

  // ignore other BLE devices
  if (!isOtherDevice(p_name)) {
    return;
  }

  // get more data
  int p_rssi = peripheral.rssi();
  String p_adress = peripheral.address();

  // limit to a certain signal strength
  if (p_rssi > RSSI_LIMIT) {
    foundNearbyDevices++;
    Serial.print("Log: Device ");
    Serial.print(p_adress + "/" + p_name);
    Serial.print(" signal strength: ");
    Serial.println(p_rssi);
  }
}

/* get the number of nearby devices and their distance */
void getNearbyDevices() {
  // timed interval for checking nearby devices
  unsigned long timeNow = millis();
  if (timeNow > (lastBLEscan + BLEscanInterval)) {
    // reset counter
    foundNearbyDevices = 0;

    // scan for devices
    BLE.scan();
    BLEDevice nextPeripheral = BLE.available();

    // check if there are devices available
    while(nextPeripheral) {
      checkPeripheral(nextPeripheral);
      nextPeripheral = BLE.available();
    }

    // set timing
    lastBLEscan = timeNow;
  }
}

/* MAX/MSP music sample listener */
void OnSetMusicSample() {
  int cmd = cmdMessenger.commandID();
  int value1 = cmdMessenger.readInt16Arg();
  int value2 = cmdMessenger.readInt16Arg();

  switch (cmd) {
    case 0:
      musicSample1 = value1;
      musicSample2 = value2;
      break;
  }
}

/* read sensor values */
void readSensorValues() {
  sensorA0 = analogRead(A0);
  forceActivity = digitalRead(A2);
}

void determineStateAndVolume() {
  // determine activity is high or low
  activity = forceActivity == HIGH
    ? 1
    : foundNearbyDevices > 0 ? 1 : 0;

  // determine next state
  switch (state) {
    // dorment
    case 0:
      // wake up if user is sitting
      if (sensorA0 < SENSOR_LIMIT) {
        state = 2;
        targetVolume = 100;
      }
    break;
    
    // picked up
    case 1:
    break;
    
    // sit on
    case 2:
      if (sensorA0 > SENSOR_LIMIT) {
        state = 0;
        targetVolume = 0;
      }
    break;
    
    default:
    break;
  }

  // move volume to the target volume
  if (targetVolume != volume) {
    volume += targetVolume > volume ? 1 : -1;
  }

  // create a fade loop when not in state 2 (active)
  if (state != 2) {
    fadeValue += fadeDelta;
    fadeValue = constrain(fadeValue, fadeLowerLimit, fadeUpperLimit);

    if (fadeValue == fadeLowerLimit || fadeValue == fadeUpperLimit) {
      fadeDelta = -fadeDelta;
    }
  }
  
}

/* write sensor values to the serial port, for use in MAX/MSP */
void writeSensorValues() {
  // limit send rate
  unsigned long timeNow = millis();
  if (timeNow > (lastSensorValuesWrittenMark + sensorSampleInterval)) {
    Serial.print("s ");
    Serial.print(activity);
    Serial.print(" ");
    Serial.print(state);
    Serial.print(" ");
    Serial.print(volume);
    Serial.print("\n");
  
    lastSensorValuesWrittenMark = timeNow;
  }
}

/* calculate a new color for the LED's */
void determineAndSetColors() {
  // active = red/yellow
  // dorment = greenish
  float hue1 = activity > 0 ? 0 : 100.0 / 360.0;
  float hue2 = activity > 0 ? 30.0 / 360.0 : 120.0 / 360.0;

  float saturation = 1.0;

  // brightness is based on state
  // active - based on music samples
  // passive - based on fade loop
  float light1 = state == 2
    ? musicSample1 / 1024.0
    : fadeValue / 1024.0;
  float light2 = state == 2
    ? musicSample2 / 1024.0
    : fadeValue / 1024.0;
  
  leds.setColorHSB(0, hue1, saturation, light1);
  leds.setColorHSB(1, hue2, saturation, light2);
}

/* main control loop */
void loop() {
  // get nearby AMB's
  getNearbyDevices();

  // determine colors, state and volume based on sensor values
  readSensorValues();
  determineStateAndVolume();
  determineAndSetColors();

  // read serial messages
  cmdMessenger.feedinSerialData();

  // write serial message
  writeSensorValues();
}
