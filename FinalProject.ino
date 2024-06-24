#include <WiFi.h>
#include <PubSubClient.h>
#include <MQUnifiedsensor.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>

// Definitions
#define placa "ESP-32"
#define Voltage_Resolution 3.3
#define pin 34 // Analog input 34 from your ESP32
#define type "MQ-135" // MQ135
#define ADC_Bit_Resolution 12 // For ESP32
#define RatioMQ135CleanAir 3.6 // RS / R0 = 3.6 ppm
#define buzzerPin 14 // Pin for buzzer
#define servoPin 18 // Pin for servo
#define DHTPin 4 // Pin for DHT22 sensor
#define DHTTYPE DHT22 // DHT 22 (AM2302)

// WiFi credentials
const char* ssid = "Anwar-Space";
const char* password = "UseWiselyOrBlocked13";

// ThingsBoard credentials
const char* thingsboardServer = "demo.thingsboard.io";
const char* accessToken = "0vff6qz7i57d77vr9fnw"; 

// Sensor declarations
MQUnifiedsensor MQ135(placa, Voltage_Resolution, ADC_Bit_Resolution, pin, type);
DHT dht(DHTPin, DHTTYPE);

// Servo declaration
Servo myServo;

// MQTT client
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// MQTT callback function
void callback(char* topic, byte* payload, unsigned int length) {
  // Handle messages received from the MQTT broker
}

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");

  // Set buzzer pin as output
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW); // Ensure the buzzer is off initially

  // Attach the servo to the specified pin
  myServo.attach(servoPin);
  myServo.write(0); // Set initial position to 0 degrees

  // Initialize the DHT22 sensor
  dht.begin();

  // Set mathematical model to calculate PPM concentration and constant values
  MQ135.setRegressionMethod(1); // _PPM = a * ratio^b
  MQ135.setA(110.47); MQ135.setB(-2.862); // Configure equation to calculate CO2 concentration

  MQ135.init(); 
  Serial.print("Calibrating, please wait.");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ135.update(); // Update data, Arduino will read voltage from analog pin
    calcR0 += MQ135.calibrate(RatioMQ135CleanAir);
    Serial.print(".");
  }
  MQ135.setR0(calcR0 / 10);
  Serial.println("  done!");
  
  if (isinf(calcR0)) {
    Serial.println("Warning: Connection issue, R0 is infinite (open circuit detected) please check wiring and supply");
    while (1);
  }
  if (calcR0 == 0) {
    Serial.println("Warning: Connection issue detected, R0 is zero (analog pin shorted to ground) please check wiring and supply");
    while (1);
  }
  MQ135.serialDebug(true);

  // Initialize MQTT client
  client.setServer(thingsboardServer, 1883);
  client.setCallback(callback);

  // Connect to ThingsBoard
  reconnect();
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32Client", accessToken, NULL)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// Variables to manage the buzzer timing
unsigned long previousMillis = 0; // Store the last time the buzzer was updated
const long interval = 4500; // Interval of 4.5 seconds

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Read temperature and humidity from DHT22 sensor
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Print temperature and humidity to serial monitor
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.print(" %\t");
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" *C");

  // Read CO2 concentration
  MQ135.setA(110.47); MQ135.setB(-2.862); // Configure equation to calculate CO2 concentration
  MQ135.update(); // Update data for CO2
  float co2_ppm = MQ135.readSensor(); // Sensor will read CO2 concentration in PPM
  Serial.print("CO2: ");
  Serial.print(co2_ppm);
  Serial.println(" ppm");

  // Read NH3 concentration
  MQ135.setA(102.2); MQ135.setB(-2.473); // Configure equation to calculate NH3 concentration
  MQ135.update(); // Update data for NH3
  float nh3_ppm = MQ135.readSensor(); // Sensor will read NH3 concentration in PPM
  Serial.print("NH3: ");
  Serial.print(nh3_ppm);
  Serial.println(" ppm");

  // Check gas concentration, temperature, and humidity conditions
  bool gasAlert = (co2_ppm > 5) || (nh3_ppm > 5);
  bool tempHumidityAlert = (temperature > 28) && (humidity > 65);

  // Control buzzer and servo
  unsigned long currentMillis = millis();
  if (gasAlert || tempHumidityAlert) {
    myServo.write(180); // Rotate servo to 180 degrees

    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis; // Update the time

      digitalWrite(buzzerPin, HIGH); // Turn on buzzer
      delay(500); // Keep the buzzer on for 0.5 seconds
      digitalWrite(buzzerPin, LOW); // Turn off buzzer
    }
  } else {
    digitalWrite(buzzerPin, LOW); // Turn off buzzer
    myServo.write(90); // Return servo to 90 degrees
  }

  // Send data to ThingsBoard
  String payload = "{";
  payload += "\"temperature\":" + String(temperature) + ",";
  payload += "\"humidity\":" + String(humidity) + ",";
  payload += "\"co2_ppm\":" + String(co2_ppm) + ",";
  payload += "\"nh3_ppm\":" + String(nh3_ppm);
  payload += "}";

  client.publish("v1/devices/me/telemetry", (char*) payload.c_str());

  MQ135.serialDebug(); // Will print table to serial port
  delay(3000); // Sampling frequency
}