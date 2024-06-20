#include <Arduino.h>
#include <Ticker.h>
#include <WiFi.h>

#include "DHTesp.h" // Click here to get the library: http://librarymanager/All#DHTesp
#define LED_PIN 23
#define HW512_PIN 22

/** Initialize DHT sensor 1 */
DHTesp dhtSensor1;

/** Task handle for the light value read task */
TaskHandle_t tempTaskHandle = NULL;
/** Pin number for DHT11 1 data pin */
int dhtPin1 = 25;

/** Ticker for temperature reading */
Ticker tempTicker;
/** Flags for temperature readings finished */
bool gotNewTemperature = false;
/** Data from sensor 1 */
TempAndHumidity sensor1Data;

/* Flag if main loop is running */
bool tasksEnabled = false;

bool isLedRed = false;
unsigned long previousMillis = 0;
const long interval = 60000; // 30 secondes

// web server
const char *ssid = "CoursAssembleur";
const char *password = "CoursAssembleur$*";

WiFiServer serveur(80);

String generateHTMLPage() {
    String pageHtml = "<html><body>";
    pageHtml += "<h1>Données du capteur</h1>";
    pageHtml += "<p>Température: " + String(sensor1Data.temperature,2) + " °C</p>";
    pageHtml += "<p>Humidité: " + String(sensor1Data.humidity,1) + " %</p>";
    pageHtml += "</body></html>";
    return pageHtml;
}

/**
 * Task to reads temperature from DHT11 sensor
 * @param pvParameters
 *		pointer to task parameters
 */
void tempTask(void *pvParameters)
{
  Serial.println("tempTask loop started");
  while (1) // tempTask loop
  {
    if (tasksEnabled && !gotNewTemperature)
    { // Read temperature only if old data was processed already
      // Reading temperature for humidity takes about 250 milliseconds!
      // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
      sensor1Data = dhtSensor1.getTempAndHumidity(); // Read values from sensor 1

      gotNewTemperature = true;
    }
    vTaskSuspend(NULL);
  }
}

/**
 * triggerGetTemp
 * Sets flag dhtUpdated to true for handling in loop()
 * called by Ticker tempTicker
 */
void triggerGetTemp()
{
  if (tempTaskHandle != NULL)
  {
    xTaskResumeFromISR(tempTaskHandle);
  }
}

/**
 * Arduino setup function (called once after boot/reboot)
 */
void setup()
{
  Serial.begin(115200);
  Serial.println("Example for 3 DHT11/22 sensors");
  pinMode(LED_PIN, OUTPUT);
  pinMode(HW512_PIN, OUTPUT); // Configurez la broche HW-512 comme sortie

  // Initialize temperature sensor 1
  dhtSensor1.setup(dhtPin1, DHTesp::DHT22);

  // Start task to get temperature
  xTaskCreatePinnedToCore(
      tempTask,        /* Function to implement the task */
      "tempTask ",     /* Name of the task */
      4000,            /* Stack size in words */
      NULL,            /* Task input parameter */
      5,               /* Priority of the task */
      &tempTaskHandle, /* Task handle. */
      1);              /* Core where the task should run */

  if (tempTaskHandle == NULL)
  {
    Serial.println("[ERROR] Failed to start task for temperature update");
  }
  else
  {
    // Start update of environment data every 30 seconds
    tempTicker.attach(30, triggerGetTemp);
  }

  // Start the web server
  tasksEnabled = true;
  Serial.print("Connection au réseau ");
  Serial.println(ssid);
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  // Tant que l'état n'est pas connecté, attendre...
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

   // Une fois connecté, on affiche l'adresse IP du ESP32
  Serial.println("");
  Serial.println("Connecté au Wi-fi");
  Serial.println("Adresse IP = ");
  Serial.println(WiFi.localIP());

  serveur.begin();

} // End of setup.

/**
 * loop
 * Arduino loop function, called once 'setup' is complete (your own code
 * should go here)
 */
void loop()
{
  if (gotNewTemperature)
  {
    Serial.println("Données reçues:");
    Serial.println("Temp: " + String(sensor1Data.temperature, 2) + "'C Humidity: " + String(sensor1Data.humidity, 1) + "%");

    if (sensor1Data.humidity > 60)
    {
      digitalWrite(LED_PIN, HIGH); // Allumez la LED
                                   /*isLedRed = true;*/
    }
    else
    {
      digitalWrite(LED_PIN, LOW); // Éteignez la LED
    }

    gotNewTemperature = false;
  }

  unsigned long currentMillis = millis();
  if (isLedRed && currentMillis - previousMillis >= interval)
  {
    // Sauvegardez la dernière fois où vous avez basculé le HW-512
    previousMillis = currentMillis;
    digitalWrite(HW512_PIN, HIGH); // Activez le HW-512
    delay(1000);                   // Laissez le HW-512 activé pendant 1 seconde
    digitalWrite(HW512_PIN, LOW);  // Désactivez le HW-512
  }

  // Regarde s'il y a un client sur le serveur
  WiFiClient client = serveur.available();

  if(client)
  {
    Serial.println("Nouveau Client.");
    // rester dans la boucle tant que le client est connecté
    while (client.connected()) 
    { 
     if (client.available()) // Vérifie si le client a envoyé des données
     {
        char rxData = client.read();  
        Serial.write(rxData);         
        if (rxData == '\n') // si c'.est la fin de la commande reçue
        {
          Serial.println("");
          Serial.println("Charge page HTML");
          client.println("HTTP/1.1 200 OK"); //send new page
          client.println("Content-Type: text/html");

          client.println();
          client.print(generateHTMLPage());     
          client.stop();
        }
     }
    }
  }
} // End of loop