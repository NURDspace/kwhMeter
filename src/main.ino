#define PULSE_PIN 13 //D7
#define MS_PER_HOUR    3.6e6
#define PULSE_FACTOR 100 
#define MAX_WATT 3400

#define MQTT_SERVER "10.38.18.11"
#define MQTT_PORT 1883

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <ESP8266mDNS.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <PubSubClient.h>

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient client(espClient);

// Power meter stuff
unsigned long SEND_FREQUENCY = 10000;
unsigned long lastSend;
unsigned long watt = 0;
unsigned long oldWatt = 0;
unsigned long pulseCount = 0;
unsigned long oldPulseCount = 0;
unsigned long previous = 0;
unsigned long oldKwh = 0;
bool reconnected = false;
char msg[50]; //mqtt message buffer

void setup() {
    Serial.begin(115200);
    Serial.println("Booting");

    wifiManager.autoConnect("AutoConnectAP");

    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    //Setup pins
    pinMode(PULSE_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PULSE_PIN), kwhChange, FALLING);

	client.setServer(MQTT_SERVER, MQTT_PORT);
	client.setCallback(mqttCallback);
}

void loop() {
    unsigned long now = millis();
    // Only send values at a the configured frequency or when woken up from sleep
    bool sendTime = now - lastSend > SEND_FREQUENCY;
    if (sendTime) {
        if (watt != oldWatt) {
            // Check that we dont get unreasonable large watt value.
            // could happen when long wraps or false interrupt triggered
            if (watt<((unsigned long)MAX_WATT)) {
                snprintf (msg, 50, "%ld", watt);
                client.publish("powermeter/watt", msg);
            }
            oldWatt = watt;
            Serial.print("W:");
            Serial.println(watt);
        }

        // Pulse count has changed
        if (pulseCount != oldPulseCount) {
            Serial.print("P:");
            Serial.println(pulseCount);
            snprintf (msg, 50, "%ld", pulseCount);
            client.publish("powermeter/pulsecount", msg, true);
            double kwh = ((double)pulseCount/((double)PULSE_FACTOR));
            oldPulseCount = pulseCount;
            if (kwh != oldKwh) {
                snprintf (msg, 50, "%s", String(kwh,3).c_str());
                client.publish("powermeter/kwh", msg);
                oldKwh = kwh;
                Serial.print("K:");
                Serial.println(msg);
            }
        }
        lastSend = now;
    }

    //Handle OTA
    ArduinoOTA.handle();

    //MQTT
	if (!client.connected()) {
		mqttReconnect();
	}
	client.loop();
	yield();
}

void kwhChange() {
    unsigned long now = millis();

    if(now == previous) return;
    if((now - previous) < 1000) return;

    unsigned long time = now - previous;
    previous = now;

    watt = 1000 * ((double) MS_PER_HOUR / time) / (unsigned long) PULSE_FACTOR;
    pulseCount++;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char data[length];
    for (int i=0;i<length;i++) {
        data[i] = (char)payload[i];
    }
    if ( strcmp(topic,"powermeter/pulsecount")==0 )
    {
        unsigned long receivedPulseCount = atoi(data);
        if (pulseCount == 0)
            pulseCount = receivedPulseCount;
        else if (receivedPulseCount > pulseCount)
        {
            Serial.println("Resetting pulse count");
            pulseCount = receivedPulseCount;
        }
        else
        {
            if (reconnected)
            { 
                snprintf (msg, 50, "%ld", pulseCount);
                client.publish("powermeter/pulsecount", msg, true);
                reconnected = false;
            }
        }
    }
}

void mqttReconnect() {
    // reconnect code from PubSubClient example
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe("powermeter/pulsecount");
      reconnected = true;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
}
