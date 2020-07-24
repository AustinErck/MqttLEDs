#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <FastLED.h>

// MQTT Topics
#define TOPIC_BASE "bedroom/bed/led/"
#define TOPIC_SUB TOPIC_BASE "+/set"
#define TOPIC_ONLINE TOPIC_BASE "online"
#define TOPIC_POWER_SET TOPIC_BASE "power/set"
#define TOPIC_POWER_GET TOPIC_BASE "power/get"
#define TOPIC_MODE_TOGGLE TOPIC_BASE "mode/set"
#define TOPIC_RGB_SET TOPIC_BASE "rgb/set"
#define TOPIC_RGB_GET TOPIC_BASE "rgb/get"

// Wifi & MQTT
#define WIFI_SSID "<SSID>"
#define WIFI_PASS "<Password>"
#define MQTT_SERV "<ip addr>"
#define MQTT_PORT 1883
#define MQTT_USER "username"
#define MQTT_PASS "password"
WiFiClient espClient;
PubSubClient client(espClient);

// LEDs
#define DATA_PIN_LEFT    6
#define DATA_PIN_RIGHT    7
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB
#define NUM_LEDS    161
#define FRAMES_PER_SECOND  120
CRGB leds[NUM_LEDS];

// Function prototypes
void callback(char* topic, byte* payload, unsigned int length);
void startGlow();
void stepGlow();
void startComet();
void stepComet();

// Device state
bool power = true;
uint8_t displayMode = 0;
#define DISPLAY_MODES 2
CRGB color = CRGB(0,0,255);

// Display mode states
uint8_t glowStep = 0;
uint8_t cometStep = 0;

/**
 * Setup and connect to WiFi
 */
void setupWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting ...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.println('\n');
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
}

/**
 * Handle MQTT connection(and reconnection process)
 */
void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Attempt to connect to MQTT network
    if (client.connect("ESP8266Client", MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");

      // Publish current device state
      client.publish(TOPIC_ONLINE, "1");
      client.publish(TOPIC_POWER_GET, power ? "1" : "0");
      //TODO: Publish color

      // Subscribe to all set topics
      client.subscribe(TOPIC_SUB);
    } else {

      // Try to reconnect in 5 seconds
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

/**
 * Perform system setup
 */
void setup(void){

  // Setup serial monitor
  Serial.begin(115200);
  delay(3000);

  // Setup left and right LEDS
  FastLED.addLeds<LED_TYPE,DATA_PIN_LEFT,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<LED_TYPE,DATA_PIN_RIGHT,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // Connect to Wifi and MQTT server
  setupWifi();
  client.setServer(MQTT_SERV, MQTT_PORT);
  client.setCallback(callback);

  // Start LEDs for current mode
  startLEDs();
}

void loop(void){

  // Handle MQTT
  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();

  // Handle LEDs
  if(!power) {
    FastLED.clear(); 
    FastLED.show(); 
  } else {
    stepLEDs();
  }

  // Delay the next update based on FPS
  delay(1000/FRAMES_PER_SECOND);
}

/**
 * Handle MQTT topic subscriptions
 */
void callback(char* topic, byte* payload, unsigned int length) {

  // Print message to the serial monitor
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");

  // Handle specific topics
  if(strcmp(topic, TOPIC_POWER_SET) == 0) {

    // Exit if length is not correct (should only get 0 or 1)
    if(length < 1) {
      return;
    }

    // Turn the power on if a 1 was passed
    power = (char)payload[0] == '1';

    // Update MQTT with current power state
    client.publish(TOPIC_POWER_GET, power ? "1" : "0");
  } if(strcmp(topic, TOPIC_MODE_TOGGLE) == 0) {

    // Exit if length is not correct (should only get 0 or 1)
    if(length < 1) {
      return;
    }

    // Ignore non-"1" writes
    if((char)payload[0] != '1') {
      return;
    }

    // Update the display mode
    displayMode++;
    if(displayMode >= DISPLAY_MODES) {
      displayMode = 0;
    }

    // Start LEDs in new mode
    startLEDs();

    //TODO: Determine if homebridge-mqttthings requires a response for auto off switches
    //client.publish("bedroom/bed/led/mode/get", 0);
  } else if(strcmp(topic, TOPIC_RGB_SET) == 0) {

    // Exit if length is not correct (Should only be 6 char hex code)
    if(length < 6) {
      return;
    }

    // Create new line terminated version of payload
    char formattedPayload[7];
    for(uint8_t i = 0; i < 7; i++) {
      formattedPayload[i] = (char)payload[i];
    }
    formattedPayload[length] = '\n';

    // Convert payload to RGB
    int hexValue = strtol(formattedPayload, NULL, 16);
    uint8_t r = (hexValue >> 16) & 0xFF;
    uint8_t g = (hexValue >> 8) & 0xFF;
    uint8_t b = (hexValue) & 0xFF;

    // Generate hex string
    char hexStr[6];
    sprintf(hexStr, "%06X", hexValue);
  
    // Update color
    color = CRGB(r, g, b);
  
    // Restart display mode with new color
    startLEDs();

    // Publish current color to topic
    client.publish(TOPIC_RGB_GET, hexStr);
  }
}

/**
 * Starts a new LED display mode
 */
void startLEDs() {

  // Clear LEDS when starting new display mode
  FastLED.clear();
  
  if(displayMode == 0) {
    startGlow();
  } else if(displayMode == 1) {
    startComet();
  }

  // Update LEDs
  FastLED.show();
}

/**
 * Updates the LEDs to the next step of the current display mode
 */
void stepLEDs() {
  
  if(displayMode == 0) {
    stepGlow();
  } else if(displayMode == 1) {
    stepComet();
  }

  // Update LEDs
  FastLED.show();
}

/**
 * Setup the LEDs for the glow display mode
 */
void startGlow() {

  // Reset glowStep
  glowStep = 0;

  // Set LED strip to the selected color
  fill_solid(leds, NUM_LEDS, color);

  // Set the initial brightness to 0
  FastLED.setBrightness(0);
}

/**
 * Update the LEDs for the next step in the glow display mode
 * 
 * This mode cycles the brightness from 0 to 100 and back to 0
 */
void stepGlow() {

  // Display mode constants
  #define GLOW_BRIGHTNESS_STEP 5

  // Determine if the brightness is being increased or decreased
  if(glowStep <= 100) {
    FastLED.setBrightness(glowStep);
  } else {
    FastLED.setBrightness(100 - (glowStep - 100));     
  }

  // Update glowStep
  if(glowStep >= 200) {
    glowStep = 0;
  } else {
    glowStep++;
  }
}

/**
 * Setup the LEDs for the commet display mode
 */
void startComet() {

  // Reset cometStep
  cometStep = NUM_LEDS - 1;

  // Set maximum brightness
  FastLED.setBrightness(100);

  // Clear leds
  FastLED.clear();
}

/**
 * Update the LEDs for the next step in the comet display mode
 * 
 * This mode rotates a section of LEDs from the far end of the led strip towards the MC. It randomly fades the LEDs that have been passed
 * This was based off the great work found on Tweaking4All, but was reversed and slightly reworked hence the different name.
 * https://www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/
 */
void stepComet() {  
  #define COMET_SIZE 8
  #define COMET_TRAIL_DECAY 75 
  #define COMET_RAND_DECAY true
   
  // Fade all LEDs randomly
  for(int j = 0; j < NUM_LEDS; j++) {
    if( (!COMET_RAND_DECAY) || (random(10)>5) ) {
      leds[j].fadeToBlackBy(COMET_TRAIL_DECAY);        
    }
  }
   
  // Draw comet (overrides faded LEDs
  for(int j = 0; j < COMET_SIZE; j++) {
    if(cometStep+j < NUM_LEDS) {
      leds[cometStep+j] = color;
    }
  }

  // Update comet step
  if(cometStep > 0) {
    cometStep -= 1;
  } else {
    cometStep = NUM_LEDS - 1;
  }
}
