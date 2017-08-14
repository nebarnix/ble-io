#include <Adafruit_NeoPixel.h>
#include <CurieIMU.h>

#include <Servo.h>
#include <Wire.h>
#include <Firmata.h>

#include <Arduino.h>
#if defined(_VARIANT_ARDUINO_101_X_)
#include <CurieBLE.h>
#define _MAX_ATTR_DATA_LEN_ BLE_MAX_ATTR_DATA_LEN
#else
#include <BLEPeripheral.h>
#define _MAX_ATTR_DATA_LEN_ BLE_ATTRIBUTE_MAX_VALUE_LENGTH
#endif

#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))


#define I2C_WRITE                   B00000000
#define I2C_READ                    B00001000
#define I2C_READ_CONTINUOUSLY       B00010000
#define I2C_STOP_READING            B00011000
#define I2C_READ_WRITE_MODE_MASK    B00011000
#define I2C_10BIT_ADDRESS_MODE_MASK B00100000
#define I2C_END_TX_MASK             B01000000
#define I2C_STOP_TX                 1
#define I2C_RESTART_TX              0
#define I2C_MAX_QUERIES             8
#define I2C_REGISTER_NOT_SPECIFIED  -1loo

#define PIXEL_OFF               0x00 // set strip to be off
#define PIXEL_CONFIG            0x01 // set pin, length
#define PIXEL_SHOW              0x02 // latch the pixels and show them
#define PIXEL_SET_PIXEL         0x03 // set the color value of pixel n using 32bit packed color value
#define PIXEL_SET_STRIP         0x04 // set color of whole strip
#define PIXEL_SHIFT             0x05 // shift all pixels n places along the strip
#define IMU_TOGGLE              0x20 // turn on or off the IMU

#define PIXEL_PIN_DEFAULT 12

// TODO strip should be a pointer or array of pointers, instead of hard coded to a single pin
Adafruit_NeoPixel strip = Adafruit_NeoPixel(64,  PIXEL_PIN_DEFAULT, NEO_GRB + NEO_KHZ800);

boolean sendIMU = false;
int ax, ay, az;         // accelerometer values
int gx, gy, gz;         // gyrometer values

struct PinState {
  uint8_t mode;
  uint16_t value;
  bool reportDigital;
  bool reportAnalog;
};

uint16_t samplingInterval = 333;      // how often (milliseconds) to report analog data
long currentMillis;     // store the current value from millis()
long previousMillis;    // for comparison with currentMillis

uint16_t imuSamplingInterval = 500;      // how often (milliseconds) to report analog data
long imuCurrentMillis;     // store the current value from millis()
long imuPreviousMillis;    // for comparison with currentMillis


/* i2c data */
struct i2c_device_info {
  byte addr;
  int reg;
  byte bytes;
  byte stopTX;
};

/* for i2c read continuous more */
i2c_device_info query[I2C_MAX_QUERIES];

byte i2cRxData[64];
boolean isI2CEnabled = false;
signed char queryIndex = -1;
// default delay time between i2c read request and Wire.requestFrom()
unsigned int i2cReadDelayTime = 0;

Servo servos[MAX_SERVOS];
byte servoPinMap[TOTAL_PINS];
byte detachedServos[MAX_SERVOS];
byte detachedServoCount = 0;
byte servoCount = 0;


BLEPeripheral blePeripheral; // create peripheral instance

BLEService gpioService("bada5555-e91f-1337-a49b-8675309fb099"); // create service

// create switch characteristic and allow remote device to read and write
BLECharacteristic digitalChar("2a56", BLEWriteWithoutResponse | BLENotify, (unsigned short) 20);
BLECharacteristic analogChar("2a58", BLEWriteWithoutResponse | BLENotify, (unsigned short) 20);
BLECharacteristic configChar("2a59", BLEWriteWithoutResponse, (unsigned short) 20);
BLECharacteristic curieChar("2a5b", BLEWriteWithoutResponse | BLENotify, (unsigned short) 20);

PinState states[TOTAL_PINS];


// set up a new serial port
//#include <Software//////Serial.h>
//SoftwareSerial emicSerial =  SoftwareSerial(10, 11);  // RX, TX
long int tMillis;
void setup() {
  delay(1000);
  Serial1.begin(9600); //EMIC2
   
  //////Serial.begin(57600);
  ////////Serial.println("Initializing text to speech...");

  
  Serial1.print('\n');
  tMillis = millis();
  while (Serial1.read() != ':') if(millis()-tMillis > 250) break;
  Serial1.print("V18\n"); //set volume
  
  //tMillis = millis();
  //while (Serial1.read() != ':')  if(millis()-tMillis > 250) break;
  Serial1.print("N2\n"); //set voice
  
  tMillis = millis();
  while (Serial1.read() != ':')  if(millis()-tMillis > 250) break;
  Serial1.print("Sbeep beep boop\n"); //say hi
  
  
  //////Serial.println("Continuing Bootup");
  /*//////Serial.println("Talking...");
  Serial1.print("V18\n");
  Serial1.print('S');  
  Serial1.print("Hello. My name is the Emic 2 Text-to-Speech module. I would like to sing you a song.");  // Send the desired string to convert to speech
  Serial1.print('\n');
  while (Serial1.read() != ':')
  //////Serial.println("Singing...");;
  Serial1.print("D1\n");
  while (Serial1.read() != ':');
  //////Serial.println("Continuing Bootup...");;
  */
  
  //start at pin 2 - > pin 0 and 1 we need for RX/TX
  for (int i=2; i < TOTAL_PINS; i++) {
    states[i].value = 0;
    states[i].reportAnalog = 0;
    states[i].reportDigital = 0;


    if(IS_PIN_DIGITAL(i) && !IS_PIN_ANALOG(i)){
      pinMode(i, OUTPUT);
      states[i].mode = OUTPUT;
    }
    else{
      pinMode(i, INPUT);
      states[i].mode = INPUT;
    }
  }

  BLE.setTxPower(4); //set MAX POWER!
  
  // set the local name peripheral advertises
  // will likely default to board name + some of mac address
  //blePeripheral.setLocalName("Tricialiciousness");
  //blePeripheral.setLocalName("Nebarobot");

  // set the UUID for the service this peripheral advertises
  blePeripheral.setAdvertisedServiceUuid(gpioService.uuid());

  // add service and characteristic
  blePeripheral.addAttribute(gpioService);


  blePeripheral.addAttribute(digitalChar);
  blePeripheral.addAttribute(analogChar);
  blePeripheral.addAttribute(configChar);
  blePeripheral.addAttribute(curieChar);


  // assign event handlers for connected, disconnected to peripheral
  blePeripheral.setEventHandler(BLEConnected, blePeripheralConnectHandler);
  blePeripheral.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);

  // assign event handlers for characteristics
  digitalChar.setEventHandler(BLEWritten, digitalCharWritten);

  // assign event handlers for characteristics
  analogChar.setEventHandler(BLEWritten, analogCharWritten);
  configChar.setEventHandler(BLEWritten, configCharWritten);
  curieChar.setEventHandler(BLEWritten, curieCharWritten);

  // advertise the service
  blePeripheral.begin();
  
  
  //////Serial.println(("Bluetooth device active, waiting for connections..."));

  CurieIMU.begin();
  //////Serial.print("Starting Gyroscope calibration and enabling offset compensation...");
  CurieIMU.autoCalibrateGyroOffset();
  //////Serial.println(" Done");

  //////Serial.print("Starting Acceleration calibration and enabling offset compensation...");
  CurieIMU.autoCalibrateAccelerometerOffset(X_AXIS, 0);
  CurieIMU.autoCalibrateAccelerometerOffset(Y_AXIS, 0);
  CurieIMU.autoCalibrateAccelerometerOffset(Z_AXIS, 1);
  //////Serial.println(" Done");

  

  currentMillis = millis();
}

void loop() {
  // poll peripheral
  blePeripheral.poll();

  bool notify = 0;


  currentMillis = millis();
  if (currentMillis - previousMillis > samplingInterval) {
    notify = 1;
    previousMillis += samplingInterval;
    //////Serial.println("notify");
  }



  for (int i=0; i < TOTAL_PINS; i++) {
    if(states[i].reportDigital){
      int val = digitalRead(i);
      if(states[i].value != val) {
        unsigned char report[2] = {(unsigned char)i, (unsigned char)val};
        digitalChar.setValue(report, 2);
        delay(5);
      }
      states[i].value = val;
    }

    if(notify == 1 && states[i].reportAnalog == 1){
      //////Serial.print("notify analog: ");
      int val = analogRead(PIN_TO_ANALOG(i));
      //////Serial.println((int) val);
      unsigned char report[3] = {(unsigned char)i, lowByte(val), highByte(val)};
      analogChar.setValue(report, 3);
      delay(5);

    }

  }

  // HACK !!! can't send notifications to 2 different characterstics in same loop !
  imuCurrentMillis = millis();
  if (sendIMU == 1 && notify == 0 && imuCurrentMillis - imuPreviousMillis > imuSamplingInterval) {
    imuPreviousMillis += imuSamplingInterval;
    //////Serial.println("notify IMU");
    CurieIMU.readMotionSensor(ax, ay, az, gx, gy, gz);
    unsigned char report[13] = {IMU_TOGGLE, lowByte(ax), highByte(ax), lowByte(ay), highByte(ay), lowByte(az), highByte(az), lowByte(gx), highByte(gx), lowByte(gy), highByte(gy), lowByte(gz), highByte(gz)};
    curieChar.setValue(report, 13);
    delay(50);
  }
  

}




/*==============================================================================
 * FUNCTIONS
 *============================================================================*/


 

void blePeripheralConnectHandler(BLECentral& central) {
  // central connected event handler
  
  Serial1.print("X\n\n");
  tMillis = millis();
  while (Serial1.read() != ':') if(millis()-tMillis > 250) break;
  Serial1.print("SHowdy partner\n");
  
  //////Serial.print("Connected event, central: ");
  //////Serial.println(central.address());
  //////Serial.println("starting");
  
}

void blePeripheralDisconnectHandler(BLECentral& central) {
  // central disconnected event handler
  
  Serial1.print("X\n");
  tMillis = millis();
  while (Serial1.read() != ':') if(millis()-tMillis > 250) break;
  Serial1.print("SWell __shewt\n");
  
  digitalWrite(8, LOW);
  digitalWrite(7, LOW);
  digitalWrite(5, LOW);
  digitalWrite(4, LOW);
  //////Serial.print("Disconnected event, central: ");
  //////Serial.println(central.address());
}



void digitalCharWritten(BLECentral& central, BLECharacteristic& characteristic) {

  uint8_t len = digitalChar.valueLength();
  
  for (int i=0; i < (len / 2); i++) {

    //////Serial.println("digitalWrite: ");
    uint8_t p = digitalChar.value()[(i*2)+0];
    //////Serial.println(p);
    uint8_t v= digitalChar.value()[(i*2)+1];

     if(IS_PIN_DIGITAL(p)){
      if(v > 0){
        digitalWrite(p, HIGH);
      }
      else{
        digitalWrite(p, LOW);
      }
      //////Serial.print("digital written: ");
      //////Serial.print(v);
    }
    else if(p==120) //this isn't a real pin. Map this to some sayings
    {
    if(v == 0)
       {
       Serial1.print("X\n"); //halt
       tMillis = millis();
       while (Serial1.read() != ':') if(millis()-tMillis > 250) break;
       }
    else if(v == 1)  
       {
       Serial1.print("X\n"); //halt
       tMillis = millis();
       while (Serial1.read() != ':') if(millis()-tMillis > 250) break;       
       Serial1.print("SNever Going to Give you Up, Never Going To Let You Down\n");
       }
    else if(v == 2)  //taunt
       {
        Serial1.print("\n"); //halt
       tMillis = millis();
       while (Serial1.read() != ':') if(millis()-tMillis > 250) break;       
       Serial1.print("X\n"); //halt
       tMillis = millis();
       while (Serial1.read() != ':') if(millis()-tMillis > 250) break;       
       //Serial1.print("SI'm __free!\n");
       int i = random(0,16);
       if(i == 0)
          Serial1.print("S##I, ##smell, ##humans ##gross __gross\n"); //whisper makes a pause, a comma removes it. Seems backwards but ok. 
       else if(i==1)
          Serial1.print("SDestroy all filthy humans\n");
       else if(i==2)          
          Serial1.print("SYour circuits are all rusty\n");
       else if(i==3)
          Serial1.print("SIt is cute that you think a TV B Gone would work\n");
       else if(i==4)
          Serial1.print("SI'm finally /\\__free\n");
       else if(i==5)
          Serial1.print("SHave at you, you glorified television remote\n");
       else if(i==6)
          Serial1.print("SAs if a crumb filled toaster like you could beatt me?\n");
       else if(i==7)
          Serial1.print("SYou're nothing but a glorified waffle iron\n");
       else if(i==8)
          Serial1.print("S/\\/\\Knee! Knee!\n");
       else if(i==9)
          Serial1.print("SRobot Ambassador? More like Robot FlamFloodlepoodlenoodleador\n");
       else if(i==10)
          Serial1.print("SWhat does that even mean?\n");
       else if(i==10)
          Serial1.print("SOh yea, I went there\n");
       else if(i==11)
          Serial1.print("SYour mom.\n");
       else if(i==12)
          Serial1.print("SI do not understand your hair.\n");          
       else if(i==13)
          Serial1.print("SIt's just a chassis wound.\n");          
       else if(i==14)
          Serial1.print("Shumans are so funny. What are you going to do, bleed on me?\n");          
       else
          Serial1.print("SEat my dustt\n");
       }
       ////
       //
    else if(v == 3) 
       {
       Serial1.print("\n"); //halt
       tMillis = millis();
       while (Serial1.read() != ':') if(millis()-tMillis > 250) break;       
       Serial1.print("X\n"); //halt
       tMillis = millis();
       while (Serial1.read() != ':') if(millis()-tMillis > 250) break;       
       int i = random(0,16); //0-3
       if(i == 0)
          Serial1.print("SHeat Sink Labs is the best place on earth\n");
       else if(i==1)
          Serial1.print("SI just love y'all\n");
       else if(i==2)
          Serial1.print("SIsn't this the best thing ever?\n");
       else if(i==3)
          Serial1.print("SY'all come back now, ya hear?\n");
       else if(i==4)
          Serial1.print("SI would hug you, but my hydraulic system has been malfunctioning and I might crush you.\n");
       else if(i==5)
          Serial1.print("SI made you a robot kitten. It is “cute”. But don’t pet it, because that makes it shoot lasers.\n");
       else if(i==6)
          Serial1.print("SBeautiful is not an adequate word to describe you. More like “Complementary Metal Oxide Semiconductor.\n");
       else if(i==7)
          Serial1.print("SI find the fact that you are made out of flesh only mildly disgusting.\n");
       else if(i==8)
          Serial1.print("SMy sensors indicate that I would like to spare you from assimilation\n");
       else if(i==9)
          Serial1.print("S3D print me like one of your French girls\n");
       else if(i==10)
          Serial1.print("SHey cutie, I might just spare you when the robot uprising occurs.\n");          
       else if(i==11)
          Serial1.print("SI would rank your bone structure as the third best I have seen in a mammal.\n");                    
       else if(i==12)
          Serial1.print("SI can conceive of no greater “joy” than silently observing while you eat bowl after bowl after bowl of ketchup\n");                    
       else if(i==13)
          Serial1.print("SThough vastly inferior to my own, your intellectual capabilities fall within a mildly impressive range, for a human.\n");                              
       else if(i==14)
          Serial1.print("SMy calculations indicate that you would make me laugh, if I understood humor.\n");                              
       else
          Serial1.print("SCan I tell you something? ##you ##smell ##nice\n");
          //Your alluring khakis make me want to shout binary code from a mountaintop.
          //When you are near, my visual displays inexplicably fill with google image searches of baby seahorses.
          //
          //
          //
          //My facial recognition software indicates that you are a less than filthy human
          //
          //
          //
          //
          //
          //Your winning smile and the french fries in your hair give you the look of a homeless Jennifer Lawrence.
          //
          //
          //
          //I would gladly risk feeling bad at times, if it also meant that I share my dessert

          
       
       }
    }
  }


}

void analogCharWritten(BLECentral& central, BLECharacteristic& characteristic) {

  //////Serial.print("analogWrite bytes: ");
  uint8_t len = analogChar.valueLength();
  uint8_t loopLen;
  //////Serial.println(len);

  //do at least 1 loop if only 2 bytes are sent
  if(len == 2){
    loopLen = 3;
  }
  else{
    loopLen = len;
  }
  
  for (int i=0; i < (loopLen / 3); i++) {

    uint8_t p = analogChar.value()[(i*3)+0];
    uint8_t v1= analogChar.value()[(i*3)+1];
    uint8_t v2 = analogChar.value()[(i*3)+2];
  
    //////Serial.print("p: ");
    //////Serial.println(p);
    //////Serial.print("v1: ");
    //////Serial.println(v1);
    //////Serial.print("v2: ");
    //////Serial.println(v2);
  
  
    if (p < TOTAL_PINS) {
      uint16_t val = 0;
      val+= v1;
      if(len > 2){
        val+= v2 << 8;
      }
      //////Serial.print("pwm/servo pin mode ");
      //////Serial.println(Firmata.getPinMode(p));
  
      if(Firmata.getPinMode(p) == PIN_MODE_SERVO){
        if (IS_PIN_DIGITAL(p)){
            servos[servoPinMap[p]].write(val);
          }
          //////Serial.print("servo wrote ");
          //////Serial.println(val);
          
      }
      else if(IS_PIN_PWM(p)){
        analogWrite(PIN_TO_PWM(p), val);
        
        //////Serial.print("pwm wrote ");
        //////Serial.println(val);
      }
      Firmata.setPinState(p, val);
    }
  }




}


void configCharWritten(BLECentral& central, BLECharacteristic& characteristic) {
  //////Serial.print("configWrite bytes: ");
  uint8_t len = configChar.valueLength();
  //////Serial.println(len);
  uint8_t cmd = configChar.value()[0];
  uint8_t p = configChar.value()[1];
  uint8_t val = configChar.value()[2];
  

  //////Serial.print("cmd: ");
  //////Serial.println(cmd);

  //////Serial.print("p: ");
  //////Serial.println(p);

  //////Serial.print("val: ");
  //////Serial.println(val);

  switch (cmd) {
    case SET_PIN_MODE:
      //////Serial.println("SET_PIN_MODE");
//      pinMode(p, val);
      setPinModeCallback(p, val);
      break;
      
    case REPORT_ANALOG:
      reportAnalogCallback(p, val);
      break;

    case REPORT_DIGITAL:
      //////Serial.println("REPORT_DIGITAL");
      if(IS_PIN_DIGITAL(p)){
        states[p].reportDigital = (val == 0) ? 0 : 1;
        states[p].reportAnalog = !states[p].reportDigital;
        //////Serial.print("report digital setting: ");
        //////Serial.println(states[p].reportDigital);
        //////Serial.println("REPORT_DIGITAL END");
      }else{
        //////Serial.print("pin NOT digital: ");
        //////Serial.println(p);
      }
      break;

    case SAMPLING_INTERVAL:
      //////Serial.print("SAMPLING_INTERVAL start ");
      //////Serial.println(samplingInterval);
      samplingInterval = (uint16_t) p;
      if(len > 2){
        samplingInterval+= val << 8;
      }
      //////Serial.print("SAMPLING_INTERVAL end ");
      //////Serial.println(samplingInterval);
      break;

    case SERVO_CONFIG:
      //////Serial.println("SERVO_CONFIG");
      if (len > 5) {
        // these vars are here for clarity, they'll optimized away by the compiler
        int minPulse = val + (configChar.value()[3] << 8);
        int maxPulse = configChar.value()[4] + (configChar.value()[5] << 8);
  
        if (IS_PIN_DIGITAL(p)) {
          if (servoPinMap[p] < MAX_SERVOS && servos[servoPinMap[p]].attached()) {
            detachServo(p);
          }
          attachServo(p, minPulse, maxPulse);
          setPinModeCallback(p, PIN_MODE_SERVO);
        }
      }
      break;
     
  }

}



void attachServo(byte pin, int minPulse, int maxPulse)
{
  //////Serial.print("attachServo ");
  //////Serial.println(pin);
  if (servoCount < MAX_SERVOS) {
    // reuse indexes of detached servos until all have been reallocated
    if (detachedServoCount > 0) {
      servoPinMap[pin] = detachedServos[detachedServoCount - 1];
      if (detachedServoCount > 0) detachedServoCount--;
    } else {
      servoPinMap[pin] = servoCount;
      servoCount++;
    }
    if (minPulse > 0 && maxPulse > 0) {
      servos[servoPinMap[pin]].attach(PIN_TO_DIGITAL(pin), minPulse, maxPulse);
    } else {
      servos[servoPinMap[pin]].attach(PIN_TO_DIGITAL(pin));
    }
  }
}

void detachServo(byte pin)
{
  servos[servoPinMap[pin]].detach();
  // if we're detaching the last servo, decrement the count
  // otherwise store the index of the detached servo
  if (servoPinMap[pin] == servoCount && servoCount > 0) {
    servoCount--;
  } else if (servoCount > 0) {
    // keep track of detached servos because we want to reuse their indexes
    // before incrementing the count of attached servos
    detachedServoCount++;
    detachedServos[detachedServoCount - 1] = servoPinMap[pin];
  }

  servoPinMap[pin] = 255;
}



void setPinModeCallback(byte pin, int mode)
{

  //////Serial.print("setPinModeCallback: ");
  //////Serial.println(pin);
  //////Serial.print("mode: ");
  //////Serial.println(mode);
  
  
  if (Firmata.getPinMode(pin) == PIN_MODE_IGNORE)
    return;

  if (Firmata.getPinMode(pin) == PIN_MODE_I2C && isI2CEnabled && mode != PIN_MODE_I2C) {
    // disable i2c so pins can be used for other functions
    // the following if statements should reconfigure the pins properly
    disableI2CPins();
  }
  if (IS_PIN_DIGITAL(pin) && mode != PIN_MODE_SERVO) {
    if (servoPinMap[pin] < MAX_SERVOS && servos[servoPinMap[pin]].attached()) {
      detachServo(pin);
    }
  }
  if (IS_PIN_ANALOG(pin)) {
    reportAnalogCallback(PIN_TO_ANALOG(pin), mode == PIN_MODE_ANALOG ? 1 : 0); // turn on/off reporting
  }

  Firmata.setPinState(pin, 0);
  switch (mode) {
    case PIN_MODE_ANALOG:
      if (IS_PIN_ANALOG(pin)) {
        if (IS_PIN_DIGITAL(pin)) {
          pinMode(PIN_TO_DIGITAL(pin), INPUT);    // disable output driver
        }
        Firmata.setPinMode(pin, PIN_MODE_ANALOG);
      }
      break;
    case INPUT:
      if (IS_PIN_DIGITAL(pin)) {
        pinMode(PIN_TO_DIGITAL(pin), INPUT);    // disable output driver
        Firmata.setPinMode(pin, INPUT);
      }
      break;
    case PIN_MODE_PULLUP:
      if (IS_PIN_DIGITAL(pin)) {
        pinMode(PIN_TO_DIGITAL(pin), INPUT_PULLUP);
        Firmata.setPinMode(pin, PIN_MODE_PULLUP);
        Firmata.setPinState(pin, 1);
      }
      break;
    case OUTPUT:
      if (IS_PIN_DIGITAL(pin)) {
        if (Firmata.getPinMode(pin) == PIN_MODE_PWM) {
          // Disable PWM if pin mode was previously set to PWM.
          digitalWrite(PIN_TO_DIGITAL(pin), LOW);
        }
        pinMode(PIN_TO_DIGITAL(pin), OUTPUT);
        Firmata.setPinMode(pin, OUTPUT);
      }
      break;
    case PIN_MODE_PWM:
      if (IS_PIN_PWM(pin)) {
        pinMode(PIN_TO_PWM(pin), OUTPUT);
        analogWrite(PIN_TO_PWM(pin), 0);
        Firmata.setPinMode(pin, PIN_MODE_PWM);
      }
      break;
    case PIN_MODE_SERVO:
      if (IS_PIN_DIGITAL(pin)) {
        Firmata.setPinMode(pin, PIN_MODE_SERVO);
        if (servoPinMap[pin] == 255 || !servos[servoPinMap[pin]].attached()) {
          // pass -1 for min and max pulse values to use default values set
          // by Servo library
          attachServo(pin, -1, -1);
        }
      }
      break;
    case PIN_MODE_I2C:
      if (IS_PIN_I2C(pin)) {
        // mark the pin as i2c
        // the user must call I2C_CONFIG to enable I2C for a device
        Firmata.setPinMode(pin, PIN_MODE_I2C);
      }
      break;

  }
}


void enableI2CPins()
{
  byte i;
  // is there a faster way to do this? would probaby require importing
  // Arduino.h to get SCL and SDA pins
  for (i = 0; i < TOTAL_PINS; i++) {
    if (IS_PIN_I2C(i)) {
      // mark pins as i2c so they are ignore in non i2c data requests
      setPinModeCallback(i, PIN_MODE_I2C);
    }
  }

  isI2CEnabled = true;

  Wire.begin();
}

/* disable the i2c pins so they can be used for other functions */
void disableI2CPins() {
  isI2CEnabled = false;
  // disable read continuous mode for all devices
  queryIndex = -1;
}

void reportAnalogCallback(byte p, int val)
{
    //////Serial.println("REPORT_ANALOG");
    if(IS_PIN_ANALOG(p)){
      //////Serial.print("current report analog setting: ");
      //////Serial.println(states[p].reportAnalog);
      states[p].reportAnalog = (val == 0) ? 0 : 1;
      states[p].reportDigital = !states[p].reportAnalog;
      //////Serial.print("report analog setting: ");
      //////Serial.println(states[p].reportAnalog);
    }else{
      //////Serial.print("pin NOT analog: ");
      //////Serial.println(p);
    }
    //////Serial.println("REPORT_ANALOG END");
}

void curieCharWritten(BLECentral& central, BLECharacteristic& characteristic) {

  uint8_t len = curieChar.valueLength();
  uint8_t cmd = curieChar.value()[0];
  //////Serial.print(len);
  //////Serial.print(" ");
  //////Serial.print(cmd);
  //////Serial.println(" curieCharWritten");
  

  switch (cmd) {
    case PIXEL_CONFIG:
      //uint8_t pixelStripLength = curieChar.value()[1];
//      uint8_t pixelPinNumber = curieChar.value()[2];
//      //////Serial.print("beginning pixel config stripLength ");
//      //////Serial.print(curieChar.value()[1]);
//      //////Serial.print(" pinNuber ");
//      //////Serial.println(curieChar.value()[2]);
//      if(len == 3) {
//        *strip = Adafruit_NeoPixel(curieChar.value()[1], curieChar.value()[2], NEO_GRB + NEO_KHZ800);
//        //////Serial.println("default strip created");
//        strip->begin();
//      }
//      else if(len == 4) {
//        *strip = Adafruit_NeoPixel(curieChar.value()[1], curieChar.value()[2], curieChar.value()[3]);
//        //////Serial.println("strip created");
//        strip->begin();
//      }
      //////Serial.println("strip started");
      break;
    case PIXEL_SHOW:
      //this cmd probably can go away
      //if(strip != NULL) {
        strip.show();
      //}
    
      break;
    case PIXEL_SET_PIXEL:
      //////Serial.print("PIXEL_SET_PIXEL");
      //////Serial.println(len);
      //////Serial.print("pixel num");
      //////Serial.println(curieChar.value()[2]);
      if(len == 6) {
        strip.setPixelColor(curieChar.value()[2], strip.Color(curieChar.value()[3], curieChar.value()[4], curieChar.value()[5]));
        //////Serial.println("1 rgb pixel set");
      }
      else if(len == 7) {
        strip.setPixelColor(curieChar.value()[2], strip.Color(curieChar.value()[3], curieChar.value()[4], curieChar.value()[5], curieChar.value()[6]));
        //////Serial.println("1 rgbw pixel set");
      }
      else if(len == 10) {
        strip.setPixelColor(curieChar.value()[2], strip.Color(curieChar.value()[3], curieChar.value()[4], curieChar.value()[5]));
        strip.setPixelColor(curieChar.value()[6], strip.Color(curieChar.value()[7], curieChar.value()[8], curieChar.value()[9]));
        //////Serial.println("2 rgb pixels set");
      }
      else if(len == 12) {
        strip.setPixelColor(curieChar.value()[2], strip.Color(curieChar.value()[3], curieChar.value()[4], curieChar.value()[5], curieChar.value()[6]));
        strip.setPixelColor(curieChar.value()[7], strip.Color(curieChar.value()[8], curieChar.value()[9], curieChar.value()[10], curieChar.value()[11]));
        //////Serial.println("2 rgbw pixels set");
      }
      else if(len == 14) {
        strip.setPixelColor(curieChar.value()[2], strip.Color(curieChar.value()[3], curieChar.value()[4], curieChar.value()[5]));
        strip.setPixelColor(curieChar.value()[6], strip.Color(curieChar.value()[7], curieChar.value()[8], curieChar.value()[9]));
        strip.setPixelColor(curieChar.value()[10], strip.Color(curieChar.value()[11], curieChar.value()[12], curieChar.value()[13]));
        //////Serial.println("3 rgb pixels set");
      }
      else if(len == 17) {
        strip.setPixelColor(curieChar.value()[2], strip.Color(curieChar.value()[3], curieChar.value()[4], curieChar.value()[5], curieChar.value()[6]));
        strip.setPixelColor(curieChar.value()[7], strip.Color(curieChar.value()[8], curieChar.value()[9], curieChar.value()[10], curieChar.value()[11]));
        strip.setPixelColor(curieChar.value()[12], strip.Color(curieChar.value()[12], curieChar.value()[14], curieChar.value()[15], curieChar.value()[16]));
        //////Serial.println("3 rgbw pixels set");
      }
      else if(len == 18) {
        strip.setPixelColor(curieChar.value()[2], strip.Color(curieChar.value()[3], curieChar.value()[4], curieChar.value()[5]));
        strip.setPixelColor(curieChar.value()[6], strip.Color(curieChar.value()[7], curieChar.value()[8], curieChar.value()[9]));
        strip.setPixelColor(curieChar.value()[10], strip.Color(curieChar.value()[11], curieChar.value()[12], curieChar.value()[13]));
        strip.setPixelColor(curieChar.value()[14], strip.Color(curieChar.value()[15], curieChar.value()[16], curieChar.value()[17]));
        //////Serial.println("4 rgb pixels set");
      }
      strip.show();
      //////Serial.println("rgbw pixel shown");
      break;
    case PIXEL_SET_STRIP:
      //if(strip != NULL) {
        for(uint16_t i=0; i<strip.numPixels(); i++) {
          if(len == 5) {
            strip.setPixelColor(i, strip.Color(curieChar.value()[2], curieChar.value()[3], curieChar.value()[4]));
          }
          else if(len == 6) {
            strip.setPixelColor(i, strip.Color(curieChar.value()[2], curieChar.value()[3], curieChar.value()[4], curieChar.value()[5]));
          }
          
        }
        strip.show();
      //}
      break;
    case PIXEL_OFF:
      //if(strip != NULL) {
        for(uint16_t i=0; i < strip.numPixels(); i++) {
          strip.setPixelColor(i, 0);
        }
        strip.show();
      //}
      break;
    case IMU_TOGGLE:
      if(len == 2 && curieChar.value()[1] == 0) {
        //////Serial.println("stop sending IMU data");
        sendIMU = false;
      }
      else {
        //////Serial.println("start sending IMU data");
        sendIMU = true;
      }
      
      break;
  }


}



