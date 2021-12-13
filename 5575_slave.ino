
#include "src/Config/MyConfig.h"
#include "src/SPI/MySPI.h"
#include "src/InfluxDB/MyInfluxDB.h"
#include "src/WiFi/MyWiFi.h"
#include "src/WebServer/MyWebServer.h"

void setup() {
    Serial.begin(115200);
    while (!Serial)
    ; // wait for serial attach

    
    setupConfig();

    delay(10000);

    // WiFi Manager, additonal params, WiFi connect 
    setupWiFiServer();

    setupWebServer();

    setupSPISlave();
}


void loop() {

    /*
    if (cs != cs_last) {
        cs_last = cs;
        if (!cs) {
            Serial.printf("%d transactionNo: %d SS HIGH\n", micros(), transactionNo);
        } else {            
            Serial.printf("%d transactionNo: %d SS LOW\n", micros(), transactionNo);
        }        
    }
    */

    if (!requestDataFromMaster && (millis() - lastDataRequestTime) > pollDataDelay_ms) {
        requestDataFromMaster_stage = 0;
        requestDataFromMaster = true;
        // Serial.printf("Begin new request to 5575 via SPI at transaction-counter %d\n", transactionNo);
        // saveToInflux();
    }


    if (saveSPIDataToInflux) {
        saveToInflux();
        saveSPIDataToInflux = false;
    }
    
}





