#include "MyConfig.h"

// https://github.com/tzapu/WiFiManager/blob/master/examples/Parameters/SPIFFS/AutoConnectWithFSParameters/AutoConnectWithFSParameters.ino
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <HTTPClient.h>           // https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient


char influx_url[512]    = "http://YOUR_IP:9999/api/v2/write";
char influx_org[128]    = "";
char influx_bucket[128] = "";
char influx_token[512]  = "v3ryw8rd7ing0a57rng3nd1g8y==";
bool shouldSaveConfig   = false;
uint64_t currentTimebase; // in ms

char chipUUID[23];


void setupConfig () {

    uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
    uint16_t chip = (uint16_t)(chipid >> 32);
    snprintf(chipUUID, 23, "DEVICE-%04X-%08X", chip, (uint32_t)chipid);
    Serial.println(chipUUID);

    readConfigFromFileSystem();

    // call initClock() only after the WiFi connection was established....

}

void saveConfigFileToFileSystem () {
     // Serial.println("saving config");

    #ifdef ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
    #else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
    #endif
        json["influx_url"] = influx_url;
        json["influx_bucket"] = influx_bucket;
        json["influx_org"] = influx_org;
        json["influx_token"] = influx_token;

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println("failed to open config file for writing");
        }

    #ifdef ARDUINOJSON_VERSION_MAJOR >= 6
        serializeJson(json, Serial);
        serializeJson(json, configFile);
    #else
        json.printTo(Serial);
        json.printTo(configFile);
    #endif
        configFile.close();

}

void readConfigFromFileSystem () {
    //clean FS, for testing
    //SPIFFS.format();

    //read configuration from FS json
    // Serial.println("mounting FS...");

    if (SPIFFS.begin()) {
        // Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            //file exists, reading and loading
            // Serial.println("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                // Serial.println("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                // std::unique_ptr<char[]> buf(new char[size]);
                char *buf = new char[size];

                configFile.readBytes(buf, size);

                #ifdef ARDUINOJSON_VERSION_MAJOR >= 6
                DynamicJsonDocument json(1024);
                auto deserializeError = deserializeJson(json, buf);
                serializeJson(json, Serial);
                if ( ! deserializeError ) {
                #else
                DynamicJsonBuffer jsonBuffer;
                JsonObject& json = jsonBuffer.parseObject(buf.get());
                json.printTo(Serial);
                if (json.success()) {
                    #endif
                    // Serial.println("\nparsed json");
                    strcpy(influx_url, json["influx_url"]);
                    strcpy(influx_org, json["influx_org"]);
                    strcpy(influx_bucket, json["influx_bucket"]);
                    strcpy(influx_token, json["influx_token"]);
                } else {
                    Serial.println("failed to load json config");
                }
                configFile.close();
                delete buf;
            } else {
                Serial.println("cannot open /config.json in ESP32 FS");
            }
        } else {
            Serial.println("/config.json does not exists in ESP32 FS");
        }
    } else {
        Serial.println("failed to mount FS");
    }
}



long initClock () {
    HTTPClient http;
    http.begin("http://worldtimeapi.org/api/timezone/Europe/London");
    int httpResponseCode = http.GET();    
    if (httpResponseCode > 0) {
        if(httpResponseCode == HTTP_CODE_OK) {

            String response = http.getString();
            http.end();  //Free resources

            StaticJsonDocument<1000> doc;
            DeserializationError error =deserializeJson(doc, response);
            if (error) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.f_str());
                http.end();  //Free resources
                return 0;
            }

            long unixtime = doc["unixtime"];
            currentTimebase = ((uint64_t)unixtime) * 1000;
            Serial.printf("Current time is %lldms since the epoch\n", ((uint64_t)unixtime) * 1000);
            return unixtime;
        } else {
            Serial.printf("Error received from time-server: %d\n", httpResponseCode);
            http.end();  //Free resources
            return 0;
        }

    } else {
        Serial.printf("Error on GET: %d\n", httpResponseCode);
        http.end();  //Free resources
        return 0;
    }

}

uint64_t getCurrentTime () {
    return currentTimebase + millis();
} 