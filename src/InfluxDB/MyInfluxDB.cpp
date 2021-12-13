#include "MyInfluxDB.h"

#include <HTTPClient.h>           // https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient

#include "../Config/MyConfig.h"
#include "../SPI/MySPI.h"

bool saveSPIDataToInflux = false;


void saveToInflux () {

    if (String(influx_bucket).equals("")) {
        Serial.println("Please provide influx credentials");
        return;
    }

    if (!String(registerValues[0]).equals("5575")) {
        Serial.printf("Can't write data to influx: Invalid data: %s\n", String(registerValues[0]));
        return;
    }

    char* urlAndQuery = (char*) malloc(100);
    snprintf(urlAndQuery, 100,
        "%s\
?org=%s\
&bucket=%s\
&precision=ms", 
        influx_url, 
        influx_org, 
        influx_bucket
    );
    Serial.println(String(urlAndQuery));
    Serial.flush();

    HTTPClient http;
    http.begin(String(urlAndQuery));

    http.addHeader("Content-Type", "text/plain"); //Specify content-type header
    http.addHeader("Authorization", "Token " + String(influx_token));

    char* body = (char*) malloc(5000);
    snprintf(body, 5000,
        "measures,esp32id=%s \
%s=%d,\
%s=%f,\
%s=%f,\
%s=%f,\
%s=%f,\
%s=%d,\
%s=%d,\
%s=%f,\
C56_Umwaelzpumpe_Rk1=%d,\
C57_Umwaelzpumpe_Rk2=%d,\
C58=%d,\
C59_Speicherladepumpe_TW=%d,\
C60_Zirkulationspumpe=%d,\
C61_Rk1_3-Pkt_Zu-Signal=%d,\
C62_Rk1_3-Pkt_Auf-Signal=%d,\
C63_Rk2_3-Pkt_Zu-Signal=%d,\
C64_Rk2_3-Pkt_Auf-Signal=%d,\
C65=%d,\
C66=%d,\
C67_Pumpenmanagement_UP1_Ein_Aus=%d,\
C68_Pumpenmanagement_Drehzahl_UP1=%d,\
C69=%d,\
C70=%d,\
C71=%d \
%lld",
        chipUUID,        

        registerNames[0], (unsigned int) registerValues[0], 
        registerNames[1], ((signed int) registerValues[1] / (float) 10.0), 
        registerNames[2], ((unsigned int) registerValues[2] / (float) 10.0), 
        registerNames[3], ((unsigned int) registerValues[3] / (float) 10.0), 
        registerNames[4], ((unsigned int) registerValues[4] / (float) 10.0), 
        registerNames[5], registerValues[5],
        registerNames[6], (unsigned int) registerValues[6], 
        registerNames[7], ((unsigned int) registerValues[7] / (float) 10.0),

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
        (registerValues[8] & 0b0000000000000001) >> 0,
        
        getCurrentTime()
    );

    Serial.printf("Body: %s\n", body);


    int httpResponseCode = http.POST(String(body));
    if (httpResponseCode > 0) {
        if(httpResponseCode == HTTP_CODE_NO_CONTENT) { // 204
            Serial.printf("Save successfully\n");
        } else {
            Serial.printf("Error received from influxdb-server: %d\n", httpResponseCode);
        }
    } else {
        Serial.printf("Error on sending POST: %d\n", httpResponseCode);
    }

    http.end();  //Free resources
    free(body);
    free(urlAndQuery);
}