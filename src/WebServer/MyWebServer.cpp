#include "MyWebServer.h"
#include "../SPI/MySPI.h"
#include "../WiFi/MyWiFi.h"

#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
// #include <AsyncElegantOTA.h>


AsyncWebServer server(80);

void setupWebServer () {

    if (MDNS.begin("esp32.local")) { // access this ESP32 via http://esp32.local
        Serial.println("mDNS responder started");
    }
    MDNS.addService("http", "tcp", 80);
    // MDNS.setInstanceName("5575_slave");

    // register a http-service in DNS-SD
    if (mdns_service_add("5575_slave", "_http", "_tcp", 80, NULL, 0)) {
        Serial.println("DNS-SD responder started");
    }
    mdns_service_txt_item_set("5575_slave", "_http._tcp", "version", "1.0");


    // server.on("/", handleRoot);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        handleRoot(request);
        
    });
    server.on("/resetWiFi", HTTP_GET, [](AsyncWebServerRequest *request){
        resetWiFiSettings();
        request->send(200, "text/html", "Wifi reset done");
        ESP.restart();
    });    
    server.on("/resetConfig", HTTP_GET, [](AsyncWebServerRequest *request){
        SPIFFS.format();
        request->send(200, "text/html", "Config reset done");
        ESP.restart();
    });
    
    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", "Rebooting...");
        ESP.restart();
    });
    
    // server.on("/test.svg", drawGraph);
    server.on("/test.svg", HTTP_GET, [](AsyncWebServerRequest *request){
        drawGraph(request);
        
    });

    // server.onNotFound(handle_NotFound);
    server.onNotFound(handle_NotFound);
    
    // OTA-Updates via http://esp32.local/update
    // AsyncElegantOTA.begin(&server); 

    server.begin();

    // set buffer for diagram to 0
    memset(rk1Stell, 0, sizeof(rk1Stell));

}


void handle_NotFound(AsyncWebServerRequest *request) {
    // server.send(404, "text/plain", "Not found");
    request->send(404, "text/plain", "Not found");
}

void drawGraph(AsyncWebServerRequest *request) {
    String out = "";
    char temp[100];
    out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"150\">\n";
    out += "<rect width=\"400\" height=\"150\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
    out += "<g stroke=\"black\">\n";
    int y = floor(1.3 * rk1Stell[0]);
    for (int x = 0; x < sizeof(rk1Stell); x++) {
        int y2 = floor(1.3 * rk1Stell[x]);
        sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", (x * 10), 140 - y, (x * 10) + 10, 140 - y2);
        out += temp;
        y = y2;
    }

    sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"2\" stroke=\"rgb(255, 0, 0)\"/>\n", (rkIdx * 10), 10, (rkIdx * 10), 140);
    out += temp;
    /*
    for (int x = 10; x < 390; x += 10) {
        int y2 = rand() % 130;
        sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", x, 140 - y, x + 10, 140 - y2);
        out += temp;
        y = y2;
    }*/
    out += "</g>\n</svg>\n";

    // server.send(200, "image/svg+xml", out);
    request->send(200, "image/svg+xml", out);
}

void handleRoot(AsyncWebServerRequest *request) {
  
    char temp[3000];
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;
    // <meta http-equiv='refresh' content='5'/>
    snprintf(temp, 3000,

        "<html>\
        <head>\
            <title>ESP32 Demo</title>\
            <style>\
            body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
            </style>\
        </head>\
        <body>\
            <h1>Hello from ESP32!</h1>\
            <p>Uptime: %02d:%02d:%02d</p>\
            <img src=\"/test.svg\"></img>\
            <table>\
                <tr><td>%s</td><td>%d</td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td>%f &deg;C</td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td>%f &deg;C</td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td>%f &deg;C</td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td>%f &deg;C</td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td></td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td>%d %</td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td>%f &deg;C</td><td>(%04X)</td></tr>\
                <tr><td>%s</td><td></td><td></td></tr>\
                <tr><td></td><td>56 - Umwaelzpumpe Rk1</td><td>%d</td></tr>\
                <tr><td></td><td>57 - Umwaelzpumpe Rk2</td><td>%d</td></tr>\
                <tr><td></td><td>58</td><td>%d</td></tr>\
                <tr><td></td><td>59 - Speicherladepumpe TW </td><td>%d</td></tr>\
                <tr><td></td><td>60 - Zirkulationspumpe</td><td>%d</td></tr>\
                <tr><td></td><td>61 - Rk1 3-Pkt Zu-Signal</td><td>%d</td></tr>\
                <tr><td></td><td>62 - Rk1 3-Pkt Auf-Signal</td><td>%d</td></tr>\
                <tr><td></td><td>63 - Rk2 3-Pkt Zu-Signal</td><td>%d</td></tr>\
                <tr><td></td><td>64 - Rk2 3-Pkt Auf-Signal</td><td>%d</td></tr>\
                <tr><td></td><td>65</td><td>%d</td></tr>\
                <tr><td></td><td>66</td><td>%d</td></tr>\
                <tr><td></td><td>67 - Pumpenmanagement UP1 Ein/Aus</td><td>%d</td></tr>\
                <tr><td></td><td>68 - Pumpenmanagement Drehzahl UP1</td><td>%d</td></tr>\
                <tr><td></td><td>69</td><td>%d</td></tr>\
                <tr><td></td><td>70</td><td>%d</td></tr>\
                <tr><td></td><td>71</td><td>%d</td></tr>\
            </table>\
            <br>\
            <a href=\"/update\">OTA</a><br>\
            <br>\
            <a href=\"/reboot\">Reboot ESP32</a><br>\
            <br>\
            <a href=\"/resetConfig\">Reset Config-Settings</a><br>\
            <br>\
            <a href=\"/resetWiFi\">Reset WiFi Settings</a><br>\
            <br>\
        </body>\
        </html>",

        hr, min % 60, sec % 60,
        registerNames[0], (unsigned int) registerValues[0], registerValues[0],
        registerNames[1], ((signed int) registerValues[1] / (float) 10.0), registerValues[1],
        registerNames[2], ((unsigned int) registerValues[2] / (float) 10.0), registerValues[2],
        registerNames[3], ((unsigned int) registerValues[3] / (float) 10.0), registerValues[3],
        registerNames[4], ((unsigned int) registerValues[4] / (float) 10.0), registerValues[4],
        registerNames[5], registerValues[5],
        registerNames[6], (unsigned int) registerValues[6], registerValues[6],
        registerNames[7], ((unsigned int) registerValues[7] / (float) 10.0), registerValues[7],
        registerNames[8], 
            (registerValues[8] & 0b1000000000000000) >> 15,
            (registerValues[8] & 0b0100000000000000) >> 14,
            (registerValues[8] & 0b0010000000000000) >> 13,
            (registerValues[8] & 0b0001000000000000) >> 12,
            (registerValues[8] & 0b0000100000000000) >> 11,
            (registerValues[8] & 0b0000010000000000) >> 10,
            (registerValues[8] & 0b0000001000000000) >> 9,
            (registerValues[8] & 0b0000000100000000) >> 8,
            (registerValues[8] & 0b0000000010000000) >> 7,
            (registerValues[8] & 0b0000000001000000) >> 6,
            (registerValues[8] & 0b0000000000100000) >> 5,
            (registerValues[8] & 0b0000000000010000) >> 4,
            (registerValues[8] & 0b0000000000001000) >> 3,
            (registerValues[8] & 0b0000000000000100) >> 2,
            (registerValues[8] & 0b0000000000000010) >> 1,
            (registerValues[8] & 0b0000000000000001) >> 0
    );
  // server.send(200, "text/html", temp);
  request->send(200, "text/html", temp);
}



