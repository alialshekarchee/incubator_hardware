/*
 * WebSocketClientSocketIO.ino
 *
 *  Created on: 06.06.2016
 *
 */

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Hash.h>
#include <EEPROM.h>

const String BLANK_DEVICE_PASSPHRASE = "YK$gkE%YdFzTbt%NyK%fBN&-z83AP@hV*ey?RfJ8G?Z5WX3@rs!b+*@KUBjGx36tQDMqr5q89NS#w&Ye3F$tr6Yp?Gaj-d79StJD8D-2suhQVwX@jzQ?22P%G#QyfvP&V@q*HG_2QnJ#AA3m+VVGvk_w?#GKE58cF-ZHW$YRrW4Q9uHcsk2AfP5FeUcg$*!_grbV?KV9%Y?Un8MLLSb@mX*=?!dLJ$tHZF*tXMxtVyuPQ@gs2qZk@ZBDQtd&epv+";

ESP8266WiFiMulti WiFiMulti;
SocketIOclient socketIO;

#define USE_SERIAL Serial

#define led 2
#define btn 0

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
    param1["current_temp"] = "37.25";
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
    USE_SERIAL.println(data);
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
        if (data == "0")
        {
            digitalWrite(led, LOW);
            sendMessage("led is off");
        }
        else
        {
            digitalWrite(led, HIGH);
            sendMessage("led is on");
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

void setup()
{
    EEPROM.begin(512);
    pinMode(led, OUTPUT);
    digitalWrite(led, LOW);
    // USE_SERIAL.begin(921600);
    USE_SERIAL.begin(9600);

    //Serial.setDebugOutput(true);
    USE_SERIAL.setDebugOutput(true);

    USE_SERIAL.println();
    USE_SERIAL.println();
    USE_SERIAL.println();

    for (uint8_t t = 4; t > 0; t--)
    {
        USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
        USE_SERIAL.flush();
        delay(1000);
    }

    // disable AP
    if (WiFi.getMode() & WIFI_AP)
    {
        WiFi.softAPdisconnect(true);
    }

    WiFiMulti.addAP("HomeLink", "kuh4n3qf49");

    //WiFi.disconnect();
    while (WiFiMulti.run() != WL_CONNECTED)
    {
        delay(100);
        USE_SERIAL.print(".");
    }

    String ip = WiFi.localIP().toString();
    USE_SERIAL.printf("[SETUP] WiFi Connected %s\n", ip.c_str());

    // server address, port and URL
    socketIO.begin("192.168.149.218", 5050);

    // register connection within the server
    // registerConnection();

    // event handler
    socketIO.onEvent(socketIOEvent);
}

unsigned long messageTimestamp = 0;
void loop()
{
    socketIO.loop();

    uint64_t now = millis();

    if (now - messageTimestamp > 10000)
    {
        messageTimestamp = now;
        sendMessage(createSampleStatus());
        // Print JSON for debugging
        // USE_SERIAL.println(output);
    }
}
