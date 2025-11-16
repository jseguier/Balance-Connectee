// Bibliothèques utilisées
#include "Arduino.h" // librairie Arduino
#include "PubSubClient.h" // librarie MQTT
#include "WiFi.h" // librarie Wifi
#include "esp_wpa2.h" // librairie protocole pour EDUROAM
#include "HX711.h" // librarie du HX711
#include "DHT.h" // librairie utile au DHT11
#include "Adafruit_Sensor.h" // autre librairie utile au DHT11

// constantes métrologies
const double u_r = 0.002886751; // incertitude sur l'erreur (constante)
const double u_j = 0.006708204; // incertitude de justesse (constante)

// Déclaration Capteur HX711
const int LOADCELL_DOUT_PIN = 21; // attribue broche Dout du HX711 au pin 21 (DATA)
const int LOADCELL_SCK_PIN = 22; // attribue broche SCK du HX711 au pin 21 (CLOCK)
const int Calibration_Weight = 100; // définit la masse étalon (g), utilisée lors de la calibration (tare)
HX711 scale;

bool mesure = false; // BOUTON MESURER (état booléen, par défaut false : on ne lance pas la mesure)

// Déclaration Capteur de température
#define DHTPIN 17 // attribue la broche DATA du DHT11
#define DHTTYPE DHT11 // Déclaration capteurs
DHT dht(DHTPIN, DHTTYPE); // on attribue le pin 17 et le type de capteur (DHT11) au DHT11

// Paramètres MQTT Broker
const char *mqtt_broker = ""; // Identifiant du broker (Adresse IP)

// Définitions des topics utilisés
const char *topic_status_1 = "Balance/0/status_1"; // TOPIC status 1 : état esp32
const char *topic_status_2 = "Balance/0/status_2"; // TOPIC status 1 : état DHT11
const char *topic_status_3 = "Balance/0/status_3"; // TOPIC status 1 : état Wifi
const char *topic_status_4 = "Balance/0/status_4"; // TOPIC status 1 : état MQTT

const char *topic_masse1 = "Balance/1/masse1"; // TOPIC masse 1 
const char *topic_masse2 = "Balance/1/masse2"; // TOPIC masse 2
const char *topic_temp = "Balance/1/temp"; // TOPIC température

const char *topic_incert_1= "Balance/2/incert_1"; // TOPIC incert_1
const char *topic_incert_2 = "Balance/2/incert_2"; // TOPIC incert_2

const char *topic_mesure = "Balance/3/mesure"; // TOPIC bouton

const char *mqtt_username = ""; // Identifiant dans le cas d'une liaison sécurisée (pas utilisé)
const char *mqtt_password = ""; // Mdp dans le cas d'une liaison sécurisée (pas utilisé)
const int mqtt_port = 1883; // Port : 1883 dans le cas d'une liaison non sécurisée et 8883 dans le cas d'une liaison cryptée

WiFiClient espClient; 
PubSubClient client(espClient); 

#define EAP_IDENTITY "" // identifiant EDUROAM
#define EAP_PASSWORD "" // mdp EDUROAM
#define EAP_USERNAME "" // username EDUROAM
const char* ssid = "eduroam"; // ssid EDUROAM

// Fonction réception du message MQTT 
void callback(char *topic, byte *payload, unsigned int length) { 
  Serial.print("Le message a été envoyé sur le topic : "); 
  Serial.println(topic); 
  Serial.print("Message:"); 
  for (int i = 0; i < length; i++) { 
    Serial.print((char) payload[i]); 
  }
  // reception signal boutton 
  if (String(topic) == topic_mesure) {
    mesure = true;
  }
}

void setup() { 
  Serial.begin(115200); // initialisation de la connexion à l'ESP32
  
  dht.begin();   // Initialisation du capteur DHT11

  // Connexion au réseau EDUROAM 
  WiFi.disconnect(true);
  WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD); 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  
// Connexion au broker MQTT  
  client.setServer(mqtt_broker, mqtt_port); 
  client.setCallback(callback); 
  while (!client.connected()) { 
    String client_id = "esp32-client-"; 
    client_id += String(WiFi.macAddress()); 
    Serial.printf("La chaîne de mesure %s se connecte au broker MQTT", client_id.c_str()); 
    if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) { 
      client.publish(topic_status_1, "✅"); // état ESP32
      client.publish(topic_status_2, "✅"); // état DHT11
      client.publish(topic_status_3, "✅"); // état Wifi
      client.publish(topic_status_4, "✅"); // état MQTT

      // Subscribe TOPICS
      client.subscribe(topic_mesure); // bouton mesurer

    } else { 
      Serial.print(client.state()); 
      client.publish(topic_status_4, "❌"); // échec état MQTT 
      delay(2000); 
    } 
  }

////////////////////// Calibration (tare) de la balance ///////////////////////////////////////////////////////////////////////////////////

  delay(5000);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN); // on attribue les pins à la librairie du HX711 
  scale.set_scale(); // appel de la fonction 'set_scale()' : on fixe une première fois la valeur de l'échelle par défaut à "1". Cette valeur "par défaut" sera ensuite modifiée
  scale.tare(); // appel de la fonction 'tare()' : mets à "0" la balance

  Serial.println("Calibration ⏳"); // début de la calibration (texte)
  Serial.println("Placer une masse de 100g sur la balance"); // consigne à l'utilisateur

  delay(5000);

  float x = scale.get_units(30); // on mesure 30 fois la masse étalon
  x = x / Calibration_Weight; // on divise la moyenne des 30 mesures par la valeur vraie de la masse étalon (facteur de correction)
  scale.set_scale(x); // on applique le facteur de correction précédement déterminé
  Serial.println("Calibration ✅"); // fin de la calibration (texte)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delay(3000);

}

void loop(){

client.loop(); // empêche la déconnexion node-red/esp32 (en théorie)

if (mesure) {
  
  if (scale.is_ready()) { // si HX711 prêt

    float reading = scale.get_units(10); // on réalise une moyenne de 10 mesures de la masse

    float masse1 = 0; // relatif à masse 1
    float masse2 = 0; // relatif à masse 2
    
    float incert_1 = 0; // relatif à l'inceritude 1 (classée)
    float incert_2 = 0; // relatif à l'inceritude 2 (tolérée)

    float EMT = 0; // relatif à l'EMT (classée)
    float u_e = 0; // relatif à l'inceritude de justesse (classée)
    float u_m1 = 0; // relatif à l'incertitude d'étalonnage  1 (classée)

    float u_m2 = 0; // relatif à l'incertitude d'étalonnage 2 (tolérée)
    float erreur = 0; // relatif à l'erreur (tolérée)
    float u_et = 0;  // relatif à l'incertitude d'étalonnage (tolérée)

/////////////////////////////             1g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 0) && (reading < 1.5)) { // 1g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.1;  // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1 = sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic
      

    // masse étalonnée 
      erreur = -0.00004; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.02160324; // incertitude étalonnage
      u_m2 = sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    }

/////////////////////////////             2g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 1.5) && (reading < 2.5)) { // 2g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.1;  // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1 = sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    // masse étalonnée 
      erreur = 0.01994; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.021605529; // incertitude étalonnage
      u_m2= sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    }

/////////////////////////////             5g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 2.5) && (reading < 7.5)) { // 5g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.1;  // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1 = sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    // masse étalonnée 
      erreur = 0.03992; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.0216215; // incertitude étalonnage
      u_m2 = sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    }

/////////////////////////////             10g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 7.5) && (reading < 15)) { // 10g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.1;  // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1 = sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic


    // masse étalonnée 
      erreur = 0.0399; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.021678425; // incertitude étalonnage
      u_m2 = sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    }

/////////////////////////////             20g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 15) && (reading < 35)) { // 20g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.1;  // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1= sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    // masse étalonnée 
      erreur = 0.0299; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.02190459; // incertitude étalonnage
      u_m2 = sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    }

/////////////////////////////             50g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 35) && (reading < 75)) { // 50g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.1;  // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1 = sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    // masse étalonnée 
      erreur = -0.0401; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.023426528; // incertitude étalonnage
      u_m2 = sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic
    }

/////////////////////////////             100g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 75) && (reading < 150)) { // 100g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.1;  // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1 = sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    // masse étalonnée 
      erreur = -0.0102; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.028199319; // incertitude étalonnage
      u_m2 = sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic
    }

/////////////////////////////             200g             //////////////////////////////////////////////////////////////////////////////////

    if ((reading >= 150) && (reading <= 200)) { // 200g

    // masse classée
      masse1 = reading;
      client.publish(topic_masse1, String(masse1, 3).c_str()); // Publication de la masse sur le topic

      EMT = 0.3; // (EXCEL)
      u_e = EMT / sqrt(3);
      u_m1 = sqrt(pow(u_e, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_1 = 2 * u_m1;
      client.publish(topic_incert_1, String(incert_1, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic

    // masse étalonnée 
      erreur = 0.2104; // erreur  // (EXCEL)
      masse2 = reading + erreur; // masse lue + erreur
      client.publish(topic_masse2, String(masse2, 3).c_str()); // Publication de la masse sur le topic

      u_et = 0.042199517; // incertitude étalonnage
      u_m2 = sqrt(pow(u_et, 2) + pow(u_r, 2) + pow(u_j, 2));
      incert_2 = 2 * u_m2;
      client.publish(topic_incert_2, String(incert_2, 3).c_str()); // Publication de l'incertitude "étalonnée" sur le topic
    }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    float temp = dht.readTemperature(); // lecture de la température
    client.publish(topic_temp, String(temp).c_str()); // Publication de la température sur le topic approprié
    
    mesure = false; // remet l'état du bouton à 0

    }

  else {
    Serial.println("Aucune masse détectée"); // si HX711 pas prêt
    }
}

}
