#include <Arduino.h>
#include <Ticker.h>
#include <WiFi.h>

#include "DHTesp.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>

#define LED_PIN 23
#define HW512_PIN 22

/** Initialize DHT sensor */
DHTesp dhtSensor1;

/** Task handle for the light value read task */
TaskHandle_t tempTaskHandle = NULL;
/** Pin number for DHT11 data pin */
int dhtPin1 = 25;

/** Ticker for temperature reading */
Ticker tempTicker;
/** Flags for temperature readings finished */
bool gotNewTemperature = false;
/** Data from sensor */
TempAndHumidity sensor1Data;

/* Flag if main loop is running */
bool tasksEnabled = false;

bool isLedRed = false;
unsigned long previousMillis = 0;
const long interval = 60000; // 30 seconds

// web server
const char *ssid = "CoursAssembleur";
const char *password = "CoursAssembleur$*";

// forceled
bool forceLedOn = false;

WiFiServer serveur(80);

String generateHTMLPage()
{
  String pageHtml = "<html><body>";
  pageHtml += "<h1>Donnees du capteur</h1>";
  pageHtml += "<p>Temperature: " + String(sensor1Data.temperature, 2) + " °C</p>";
  pageHtml += "<p>Humidite: " + String(sensor1Data.humidity, 1) + " %</p>";
  pageHtml += "<form action=\"/eteindre\" method=\"get\"><input type=\"submit\" value=\"Eteindre la LED\"></form>";
  pageHtml += "<form action=\"/allumer\" method=\"get\"><input type=\"submit\" value=\"Allumer la LED\"></form>";
  pageHtml += "<form action=\"/lirejson\" method=\"get\"><input type=\"submit\" value=\"Lire JSON\"></form>";
  pageHtml += "</body></html>";
  return pageHtml;
}

/**
 * Task to read temperature from DHT sensor
 * @param pvParameters
 *		pointer to task parameters
 */
void tempTask(void *pvParameters)
{
  Serial.println("tempTask loop started");
  while (1) // tempTask loop
  {
    if (tasksEnabled && !gotNewTemperature)
    {
      sensor1Data = dhtSensor1.getTempAndHumidity(); // Read values from sensor
      gotNewTemperature = true;
    }
    vTaskSuspend(NULL);
  }
}

void triggerGetTemp()
{
  if (tempTaskHandle != NULL)
  {
    xTaskResumeFromISR(tempTaskHandle);
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Setup for DHT sensor");
  pinMode(LED_PIN, OUTPUT);
  pinMode(HW512_PIN, OUTPUT);

  // Initialize temperature sensor
  dhtSensor1.setup(dhtPin1, DHTesp::DHT22);

  // Start task to get temperature
  xTaskCreatePinnedToCore(
      tempTask,
      "tempTask ",
      4000,
      NULL,
      5,
      &tempTaskHandle,
      1);

  if (tempTaskHandle == NULL)
  {
    Serial.println("[ERROR] Failed to start task for temperature update");
  }
  else
  {
    tempTicker.attach(30, triggerGetTemp);
  }

  // Start the Wi-Fi and server
  tasksEnabled = true;
  Serial.print("Connection au réseau ");
  Serial.println(ssid);
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("Connecté au Wi-fi");
  Serial.println("Adresse IP = ");
  Serial.println(WiFi.localIP());

  serveur.begin();
  if (!SPIFFS.begin(true))
  {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }
}

void loop()
{
  if (gotNewTemperature)
  {
    StaticJsonDocument<200> doc;
    doc["temperature"] = sensor1Data.temperature;
    doc["humidity"] = sensor1Data.humidity;

    File file = SPIFFS.open("/data.json", FILE_WRITE);
    if (!file)
    {
      Serial.println("There was an error opening the file for writing");
      return;
    }

    if (serializeJson(doc, file) == 0)
    {
      Serial.println("Failed to write to file");
    }

    file.close();

    gotNewTemperature = false;
  }

  unsigned long currentMillis = millis();

  if (isLedRed && currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    digitalWrite(HW512_PIN, HIGH);
    delay(1000);
    digitalWrite(HW512_PIN, LOW);
  }

  WiFiClient client = serveur.available();
  if (client)
  {
    Serial.println("Nouveau Client.");
    // stay in the loop while client is connected
    while (client.connected())
    {
      if (client.available()) // Check if the client has sent data
      {
        String request = client.readStringUntil('\r');
        Serial.println(request);
        client.flush();

        if (request.indexOf("/eteindre") != -1)
        {
          digitalWrite(LED_PIN, LOW); // Turn off the LED
          forceLedOn = false;
        }

        if (request.indexOf("/lirejson") != -1)
        {
          if (SPIFFS.exists("/data.json"))
          {
            File file = SPIFFS.open("/data.json", FILE_READ);
            if (!file)
            {
              Serial.println("There was an error opening the file for reading");
              return;
            }

            Serial.println("Reading JSON data");
            String jsonData = "";
            while (file.available())
            {
              jsonData += (char)file.read();
            }

            file.close();
            Serial.println(jsonData);
          }
          else
          {
            Serial.println("JSON file does not exist");
          }
        }

        if (request.indexOf("/allumer") != -1)
        {
          digitalWrite(LED_PIN, HIGH); // Turn on the LED
          forceLedOn = true;
        }

        if (sensor1Data.humidity > 60 || forceLedOn)
        {
          digitalWrite(LED_PIN, HIGH);
        }
        else
        {
          digitalWrite(LED_PIN, LOW);
        }

        Serial.println("Load HTML page");
        client.println("HTTP/1.1 200 OK"); // send new page
        client.println("Content-Type: text/html");
        client.println();
        client.print(generateHTMLPage());
        client.stop();
      }
    }
  }
}