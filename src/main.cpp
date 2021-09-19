#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Hash.h>
#include <EEPROM.h>
#include <WiFiManager.h>

// include MDNS
#ifdef ESP8266
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <ESPmDNS.h>
#endif

// select which pin will trigger the configuration portal when set to LOW
#define TRIGGER_PIN 0

WiFiManager wm;

unsigned int timeout = 120; // seconds to run for
unsigned int startTime = millis();
bool portalRunning = false;
bool startAP = false; // start AP and webserver if true, else start only webserver

const String BLANK_DEVICE_PASSPHRASE = "YK$gkE%YdFzTbt%NyK%fBN&-z83AP@hV*ey?RfJ8G?Z5WX3@rs!b+*@KUBjGx36tQDMqr5q89NS#w&Ye3F$tr6Yp?Gaj-d79StJD8D-2suhQVwX@jzQ?22P%G#QyfvP&V@q*HG_2QnJ#AA3m+VVGvk_w?#GKE58cF-ZHW$YRrW4Q9uHcsk2AfP5FeUcg$*!_grbV?KV9%Y?Un8MLLSb@mX*=?!dLJ$tHZF*tXMxtVyuPQ@gs2qZk@ZBDQtd&epv+";

SocketIOclient socketIO;

#define USE_SERIAL Serial
uint8_t counter = 0;
#define OUTPUT_PIN 2
#define TRIGGER_PIN 0
unsigned long messageTimestamp = 0;

String createSampleStatus()
{
    // creat JSON message for Socket.IO (event)
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();

    // add evnet name
    // Hint: socket.on('event_name', ....
    array.add("payload");

    // add payload (parameters) for the event
    JsonObject param1 = array.createNestedObject();
    param1["current_temp"] = String(counter++);
    param1["current_humidity"] = "82.36";
    param1["cycle_profile"] = "chickens";
    param1["cycle_start_date"] = "22/04/21 18:45:56";
    param1["cycle_status"] = "running";
    param1["water_level"] = "good";
    param1["battery_status"] = "good";
    String output;
    serializeJson(doc, output);
    return output;
}

void sendMessage(String msg)
{
    // creat JSON message for Socket.IO (event)
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();

    // add evnet name
    // Hint: socket.on('event_name', ....
    array.add("payload");

    // add payload (parameters) for the event
    JsonObject param1 = array.createNestedObject();
    param1["token"] = "eyJhbGciOiJIUzI1NiJ9.cmFiZWVAaXF0LnJ1bg.CVtgjp9KvaPY4yPw_9iMYDNcbn2fyTUXy2Rr8Jy1LTE";
    param1["client"] = String(ESP.getChipId());
    param1["request"]["destination"] = "";
    param1["request"]["msg"] = msg;
    // JSON to String (serializion)
    String output;
    serializeJson(doc, output);

    // Send event
    socketIO.sendEVENT(output);
}

void eventHandler(String event = "", String data = "")
{
    StaticJsonDocument<1024> doc;
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, data);

    // Test if parsing succeeds.
    if (error)
    {
        return;
    }
    if (event == "set")
    {
        if (data == "1")
        {
            digitalWrite(OUTPUT_PIN, LOW);
            sendMessage("led is on");
        }
        else
        {
            digitalWrite(OUTPUT_PIN, HIGH);
            sendMessage("led is off");
        }
    }
    else if (event == "register")
    {
        String name = doc["name"];
        USE_SERIAL.println(name);
    }
}

void eventParser(uint8_t *payload, size_t length)
{
    char *str = (char *)payload;
    USE_SERIAL.println(str);
    String event;
    String data;
    bool split = true;
    //{"set","0"}
    for (size_t i = 2; i < length - 2; i++)
    {
        if (payload[i] != ',' && split)
        {
            event += (char)payload[i];
        }
        else if (payload[i] == ',' && split)
        {
            split = false;
        }
        else if (!split)
        {
            data += (char)payload[i];
        }
    }
    event.remove(event.length() - 1);
    data.remove(0, 1);
    eventHandler(event, data);
    // USE_SERIAL.println("Event: " + event + "\n" + "Data: " + data + "\n");
}

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case sIOtype_DISCONNECT:
        USE_SERIAL.printf("[IOc] Disconnected!\n");
        break;
    case sIOtype_CONNECT:
        USE_SERIAL.printf("[IOc] Connected to url: %s\n", payload);
        // join default namespace (no auto join in Socket.IO V3)
        socketIO.send(sIOtype_CONNECT, "/");
        sendMessage("register");
        break;
    case sIOtype_EVENT:
        // USE_SERIAL.printf("[IOc] get event: %s\n", payload);
        eventParser(payload, length);
        break;
    case sIOtype_ACK:
        USE_SERIAL.printf("[IOc] get ack: %u\n", length);
        hexdump(payload, length);
        break;
    case sIOtype_ERROR:
        USE_SERIAL.printf("[IOc] get error: %u\n", length);
        hexdump(payload, length);
        break;
    case sIOtype_BINARY_EVENT:
        USE_SERIAL.printf("[IOc] get binary: %u\n", length);
        hexdump(payload, length);
        break;
    case sIOtype_BINARY_ACK:
        USE_SERIAL.printf("[IOc] get binary ack: %u\n", length);
        hexdump(payload, length);
        break;
    default:
        USE_SERIAL.printf("[IOc] Unhandled event: %s\n", payload);
        break;
    }
}

void doWiFiManager()
{
    // is auto timeout portal running
    if (portalRunning)
    {
        wm.process(); // do processing

        // check for timeout
        if ((millis() - startTime) > (timeout * 1000))
        {
            Serial.println("portaltimeout");
            portalRunning = false;
            if (startAP)
            {
                wm.stopConfigPortal();
            }
            else
            {
                wm.stopWebPortal();
            }
        }
    }

    // is configuration portal requested?
    if (digitalRead(TRIGGER_PIN) == LOW && (!portalRunning))
    {
        if (startAP)
        {
            Serial.println("Button Pressed, Starting Config Portal");
            wm.setConfigPortalBlocking(false);
            wm.startConfigPortal();
        }
        else
        {
            Serial.println("Button Pressed, Starting Web Portal");
            wm.startWebPortal();
        }
        portalRunning = true;
        startTime = millis();
    }
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void setup()
{
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
                         // put your setup code here, to run once
    USE_SERIAL.begin(9600);
    USE_SERIAL.setDebugOutput(true);
    delay(1000);
    Serial.println("\n Starting");

    pinMode(TRIGGER_PIN, INPUT_PULLUP);

    // wm.resetSettings();
    wm.setHostname("Incubator");
    // wm.setEnableConfigPortal(false);
    // wm.setConfigPortalBlocking(false);
    wm.setAPCallback(configModeCallback);
    wm.autoConnect();
    pinMode(OUTPUT_PIN, OUTPUT);
    digitalWrite(OUTPUT_PIN, LOW);

    // server address, port and URL
    socketIO.begin("192.168.149.219", 5050);

    // event handler
    socketIO.onEvent(socketIOEvent);
}

void loop()
{
    socketIO.loop();
    uint64_t now = millis();
    if (now - messageTimestamp > 1000)
    {
        messageTimestamp = now;
        sendMessage(createSampleStatus());
    }

#ifdef ESP8266
    MDNS.update();
#endif
    doWiFiManager();
    // put your main code here, to run repeatedly:
}
