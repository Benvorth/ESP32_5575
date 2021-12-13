#include "MyWiFi.h"
#include "../Config/MyConfig.h"



//WiFiManager callback notifying us of the need to save config
void wifiSettingsSavedCallback () {
    Serial.println("Should save config ");
    shouldSaveConfig = true;
    // ESP.restart();
}

void setupWiFiServer() {
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

    int numberOfNetworks = WiFi.scanNetworks();
    Serial.printf("Found %d Wifi networks: \n", numberOfNetworks);

    for(int i =0; i<numberOfNetworks; i++){
        Serial.print("Network name: ");
        Serial.println(WiFi.SSID(i));
        Serial.print("Signal strength: ");
        Serial.println(WiFi.RSSI(i));
        Serial.println("-----------------------");
    }

    // together with WiFi SSID + Password we ask the user to give the credentials for an influxdb target in the cloud to store the data there.
    
    WiFiManagerParameter custom_influxDB_url("influxURL", "Influx URL", influx_url, 512);
    WiFiManagerParameter custom_influxDB_org("influxOrg", "Influx Org", influx_org, 128);
    WiFiManagerParameter custom_influxDB_bucket("influxBucket", "Influx Bucket", influx_bucket, 128);
    WiFiManagerParameter custom_influxDB_token("influxToken", "Influx Token", influx_token, 512);

    WiFiManager wifiManager;
    wifiManager.setSaveConfigCallback(wifiSettingsSavedCallback);
    // reset settings - wipe stored credentials for testing
    // these are stored by the esp library
    // wifiManager.resetSettings(); 

    wifiManager.addParameter(&custom_influxDB_url);
    wifiManager.addParameter(&custom_influxDB_org);
    wifiManager.addParameter(&custom_influxDB_bucket);
    wifiManager.addParameter(&custom_influxDB_token);

    boolean wifiConnectRes;
    // go into a blocking loop, awaiting configuration, http://192.168.4.1
    wifiConnectRes = wifiManager.autoConnect("ESP32_5575");
    if (wifiConnectRes) {
        Serial.println("WiFi connection ok");
        initClock();
    } else {
        Serial.println("Error connecting to WiFi.");
    }

    //read updated parameters
    strcpy(influx_url, custom_influxDB_url.getValue());
    strcpy(influx_org, custom_influxDB_org.getValue());
    strcpy(influx_bucket, custom_influxDB_bucket.getValue());
    strcpy(influx_token, custom_influxDB_token.getValue());
    // Serial.println("The values in the file are: ");
    // Serial.println("\tinflux_url : " + String(influx_url));
    // Serial.println("\tinflux_org : " + String(influx_org));
    // Serial.println("\tinflux_bucket : " + String(influx_bucket));
    // Serial.println("\tinflux_token : " + String(influx_token));
    
    if (shouldSaveConfig) {
        saveConfigFileToFileSystem();
        ESP.restart();
    }

    //check wi-fi is connected to wi-fi network
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected..!");
    Serial.print("Got IP: ");  Serial.println(WiFi.localIP());
}

void resetWiFiSettings () {
    WiFiManager wifiManager;
    wifiManager.resetSettings();
}