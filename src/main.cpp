#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Hash.h>
#include <EEPROM.h>
#include <WiFiManager.h>
#include <Arduino_JSON.h>

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
// #define OUTPUT_PIN 2
// #define TRIGGER_PIN 0
unsigned long messageTimestamp = 0;
void sendMessage(String);

struct data_Rx_from_Arduino_toesp
{ /////12 data 16 byte total
    float TEMP;
    uint16_t year;
    uint8_t HUMI;
    uint8_t COUNTER;
    uint8_t DAYS;
    uint8_t second;
    uint8_t minuts;
    uint8_t hours;
    uint8_t day_inweek;
    uint8_t day_inmonth;
    uint8_t month_inyear;
    uint8_t GROP;
    /// GROP= 0, 0, FAN, BATT, DOOR,LAMP,WATER,EGG
    ////////  D7,D6, D5,  D4,  D3,  D2,  D1,   D0
} sensors = {0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

struct parameter_from_Arduino_toesp
{
    float SETTMP;
    float TMPHI;
    float TMPLO;
    uint8_t PERIOD; //IN HOURS
    uint8_t HHI;
    uint8_t HLO;
    uint8_t HACHIN;
    bool TURN;
    ///RTC data
    uint8_t YEAR;
    uint8_t MONTH;
    uint8_t DAY;
    uint8_t HOUR;
    uint8_t MINUT;
} ini_values = {0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

struct tosaveinarduino
{
    uint8_t SIGNAL;
    float SETTMP;
    float TMPHI;
    float TMPLO;
    uint8_t PERIOD; //IN HOURS
    uint8_t HHI;
    uint8_t HLO;
    uint8_t HACHIN;
    bool TURN;
    ///RTC data
    uint8_t YEAR;
    uint8_t MONTH;
    uint8_t DAY;
    uint8_t HOUR;
    uint8_t MINUT;
} toarduino = {'s', 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

String createDataSet()
{
    JSONVar dataObject;
    dataObject["TEMP"] = sensors.TEMP;
    dataObject["YEAR"] = sensors.year;
    dataObject["HUMI"] = sensors.HUMI;
    dataObject["COUNTER"] = sensors.COUNTER;
    dataObject["DAYS"] = sensors.DAYS;
    dataObject["SEC"] = sensors.second;
    dataObject["MINUT"] = sensors.minuts;
    dataObject["HOURS"] = sensors.hours;
    dataObject["DINW"] = sensors.day_inweek;
    dataObject["DINM"] = sensors.day_inmonth;
    dataObject["MONTH"] = sensors.month_inyear;
    dataObject["GROP"] = sensors.GROP;
    return JSON.stringify(dataObject);
}

String createParameterSet()
{
    JSONVar param;
    param["SETTMP"] = ini_values.SETTMP;
    param["TMPHI"] = ini_values.TMPHI;
    param["TMPLO"] = ini_values.TMPLO;
    param["PERIOD"] = ini_values.PERIOD;
    param["HHI"] = ini_values.HHI;
    param["HLO"] = ini_values.HLO;
    param["HACHIN"] = ini_values.HACHIN;
    param["TURN"] = ini_values.TURN;
    //////////////
    param["YEAR"] = ini_values.YEAR;
    param["MONTH"] = ini_values.MONTH;
    param["DAY"] = ini_values.DAY;
    param["HOUR"] = ini_values.HOUR;
    param["MINUT"] = ini_values.MINUT;
    return JSON.stringify(param);
}

String getData()
{
    char da = 'd';
    Wire.beginTransmission(8);
    Wire.write(da);
    Wire.endTransmission();
    Wire.requestFrom(8, sizeof(sensors));
    Wire.readBytes((byte *)&sensors, sizeof(sensors));
    return createDataSet();
}

String getParameters()
{
    char da = 'p';
    Wire.beginTransmission(8);
    Wire.write(da);
    Wire.endTransmission();
    Wire.requestFrom(8, sizeof(ini_values));
    Wire.readBytes((byte *)&ini_values, sizeof(ini_values));
    return createParameterSet();
}

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
    StaticJsonDocument<1024> dataObj;
    DeserializationError error = deserializeJson(dataObj, data);

    if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
    
    if (event == "payload")
    {
        if (dataObj["CMD"] == "getParameters")
        {
            sendMessage(getParameters());
        }
        else if (dataObj["CMD"] == "setParameters")
        {
            USE_SERIAL.println("setting parameters");
            String dataset = dataObj["DATASET"];
            USE_SERIAL.println(dataset);
            sendMessage("setting parameters");
        }
        else if (dataObj["CMD"] == "getData")
        {
            USE_SERIAL.println("Sending Data");
            sendMessage(getData());
        }
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
    USE_SERIAL.println("Event: " + event + "\n" + "Data: " + data + "\n");
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
    // if (digitalRead(TRIGGER_PIN) == LOW && (!portalRunning))
    // {
    //     if (startAP)
    //     {
    //         Serial.println("Button Pressed, Starting Config Portal");
    //         wm.setConfigPortalBlocking(false);
    //         wm.startConfigPortal();
    //     }
    //     else
    //     {
    //         Serial.println("Button Pressed, Starting Web Portal");
    //         wm.startWebPortal();
    //     }
    //     portalRunning = true;
    //     startTime = millis();
    // }
}

void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());

    Serial.println(myWiFiManager->getConfigPortalSSID());
}

void setup()
{
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    Wire.begin(0, 2);
    USE_SERIAL.begin(9600);
    USE_SERIAL.setDebugOutput(true);
    delay(1000);
    Serial.println("\n Starting");

    // pinMode(TRIGGER_PIN, INPUT_PULLUP);

    // wm.resetSettings();
    wm.setHostname("Incubator");
    // wm.setEnableConfigPortal(false);
    // wm.setConfigPortalBlocking(false);
    wm.setAPCallback(configModeCallback);
    wm.setCaptivePortalEnable(true);
    wm.setDarkMode(true);
    wm.autoConnect();
    // pinMode(OUTPUT_PIN, OUTPUT);
    // digitalWrite(OUTPUT_PIN, LOW);

    // server address, port and URL
    socketIO.begin("192.168.149.219", 5050);

    // event handler
    socketIO.onEvent(socketIOEvent);
}

void loop()
{
    socketIO.loop();
    uint64_t now = millis();
    if (now - messageTimestamp > 10000)
    {
        messageTimestamp = now;
        // sendMessage(createSampleStatus());
        // getData();
    }

#ifdef ESP8266
    MDNS.update();
#endif
    doWiFiManager();
}
