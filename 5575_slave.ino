#include <AsyncElegantOTA.h>
#include <elegantWebpage.h>
#include <Hash.h>

// https://github.com/hideakitai/ESP32SPISlave
#include <ESP32SPISlave.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
AsyncWebServer server(80);

const char *ssid = "";
const char *password = "";

ESP32SPISlave slave;

static constexpr uint32_t BUFFER_SIZE {32};
uint8_t spi_slave_tx_buf[BUFFER_SIZE];
uint8_t spi_slave_rx_buf[BUFFER_SIZE];

// the core number the task should run on (0 or 1)
constexpr uint8_t CORE_TASK_SPI_SLAVE {1};
constexpr uint8_t CORE_TASK_PROCESS_BUFFER {1};

static TaskHandle_t task_handle_wait_spi = 0;
static TaskHandle_t task_handle_process_buffer = 0;

uint32_t transactionNo = 0;
volatile boolean cs_last = false;
volatile boolean cs = false;

#define pin_inv_SS 25
#define pin_inv_SS_OUT 26

uint8_t rk1Stell [38];
int rkIdx = 0;


// Slave answers this on MISO so master sees that slave wants to send commands?
uint8_t slaveRadyCommand[] = {
    0xF9 
};

boolean requestDataFromMaster = false;

// int (in transaction cycles)
// 0 - set MISO high (master sees slave)
// 1 - send "slave-ready-to-send-command-to-master" msg (0xF9)
// 2 - ... send register data requests
// last - send coil data request
uint8_t requestDataFromMaster_stage = 0; 

uint32_t pollDataDelay_ms = 10000;
int64_t lastDataRequestTime = 0;
uint8_t readARegister_cmd [] = {
    0x06, 0x0C, 0x00, 0xFA, 0x08, 0xFF, 
    0x03, // FunctionID 3 = read 
    0xFF, 0xFF, // start register No (to be replaced by revolveCommand())
    0x00, 0x01, // number of registers to read (= 1)
    0xFF, 0xFF, // CRC checksum (to be replaced by revolveCommand())
    0x55
};
int NUM_READAREGISTER_CMD = sizeof(readARegister_cmd);

uint16_t registersToRead[] = {
    0x0000, // Gerätekennung (5575)
    0x0009, // AF1
    0x000C, // Vorlauftemp VF1
    0x0010, // RueckltempRueF1
    0x0016, // SpeichertempSF1
    0x001C, // Meßwert [Imp/h] am Eingang WMZ
    // 0x0062, // MaxVorlSollw, Maximaler Vorlaufsollwert des Reglers
    0x006A, // StellsignalRk1, Stellsignal Rk1 [0...100%] (CL90)
    0x03E7 // VorlSollwRk1, Vorlaufsollwert Rk1 (CL116)
};
int NUM_REGISTERS_TO_READ = (sizeof(registersToRead) / sizeof(uint16_t));

char* registerNames  [] = {
    "Geraetekennung",
    "Aussenfuehler 1 (AF1)",
    "Vorlauftemperatur (VF1)",
    "Rueckltemperatur (RueF1)",
    "Speichertemperatur (SF1)",
    "Eingang WMZ [Imp/h]",
    // "MaxVorlSollwert",
    "Stellsignal Rk1",
    "Vorlaufsollwert Rk1",
    "Coils"
};

uint16_t registerValues  [] = {
    0x1FFF,
    0x1FFF,
    0x1FFF,
    0x1FFF,
    0x1FFF,
    0x1FFF,
    0x1FFF,
    0x1FFF,
    0x1FFF // coils
};



// Coild 56 - 71, 56 = Umwaelzpumpe Rk1 (UP1 Netzseite, CL96), 59 = Speicherladepumpe TW (SLP, CL99)
uint8_t readCoils [] = {
    0x06, 0x0B, 0xFA, 0x08, 0xFF, 0x01, 0x00, 0x38, 0x00, 0x0F, 0xE8, 0x1D, 0x55
};


// read 1 register, stating from 0x00, Gerätetyp "5575"
uint8_t readCommand_inv[] = 
    {0xF9, 0xF3, 0xFF, 0x05, 0xF7, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFE, 0x6E, 0x2B, 0xAA} 
;

uint8_t readCommand_crc_inv[] = 
    {0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFE} 
;

uint8_t readCommand_crc[] = 
    {0xFF, 0x03, 0x00, 0x00, 0x00, 0x01} // 0x91 0xD4
;


// See setupSPISlave()
ICACHE_RAM_ATTR void slave_signal_from_master () {
    cs = !cs;
    if (!cs) {
        digitalWrite(pin_inv_SS_OUT, HIGH); // Idle
    } else {
        digitalWrite(pin_inv_SS_OUT, LOW); // select
    }
}

void set_tx_buffer(uint8_t value) {
    // memset(spi_slave_tx_buf, value, BUFFER_SIZE);
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
        spi_slave_tx_buf[i] = value;
    }
}

void set_tx_buffer(uint8_t* command, uint32_t size) {
    // have MOSI "high" while reading the reponse on MISO in this transaction
    memset(spi_slave_tx_buf, 255, BUFFER_SIZE);
    for (uint32_t i = 0; i < size; i++) {
        spi_slave_tx_buf[i] = command[i];
    }
}

void revolveReadRegisterCommand () {

    uint16_t registerToRead = registersToRead[requestDataFromMaster_stage - 2];

    uint8_t p1 = (uint8_t) ((registerToRead >> 8) & 0xFF);
    uint8_t p2 = (uint8_t) (registerToRead & 0xFF);

    readARegister_cmd[7] = p1;
    readARegister_cmd[8] = p2;

    readCommand_crc[2] = p1;
    readCommand_crc[3] = p2;
    
    uint16_t crc = MODBUS_CRC16_v3(readCommand_crc, sizeof(readCommand_crc));

    uint8_t c1 = (uint8_t) ((crc >> 8) & 0xFF);
    uint8_t c2 = (uint8_t) (crc & 0xFF);
    
    readARegister_cmd[11] = c1;
    readARegister_cmd[12] = c2;
    
    uint8_t read_cmd_inv[NUM_READAREGISTER_CMD];
    // invert everything
    for (uint8_t k = 0; k < NUM_READAREGISTER_CMD; k++) {
        read_cmd_inv[k] = (0xFF - readARegister_cmd[k]);
    }
    set_tx_buffer(read_cmd_inv, sizeof(read_cmd_inv));
}

void revolveReadCoilCommand () {
    uint8_t read_cmd_inv[sizeof(readCoils)];
    // invert everything
    for (uint8_t k = 0; k < sizeof(readCoils); k++) {
        read_cmd_inv[k] = (0xFF - readCoils[k]);
    }
    set_tx_buffer(read_cmd_inv, sizeof(read_cmd_inv));
}

void processSPISlaveResult () {
    
    /*
    Serial.printf("%d (stage: %d) Processe buffer (size: %d)\n", micros(), requestDataFromMaster_stage, slave.available());
    for (uint8_t k = 0; k < BUFFER_SIZE; k++) {
        Serial.printf(" %d TX - MISO: spi_slave_tx_buf    [0x%02x]\n", k, (spi_slave_tx_buf[k]));
    }
    Serial.printf("---\n");
    for (uint8_t k = 0; k < BUFFER_SIZE; k++) {
        Serial.printf(" %d RX - MOSI: spi_slave_rx_buf    [0x%02x]\n", k, (spi_slave_rx_buf[k]));
    }*/

    
    if (requestDataFromMaster_stage <= 1) {
        // answer should be empty, Master ready so serve data
    } else if (requestDataFromMaster_stage < (NUM_REGISTERS_TO_READ + 2)) {
        // this is a register-read result...
        registerValues[requestDataFromMaster_stage - 2] = 
            (spi_slave_rx_buf[5 + NUM_READAREGISTER_CMD] << 8) | spi_slave_rx_buf[6 + NUM_READAREGISTER_CMD];        
        /*
        Serial.printf("   Register [0x%02x%02x]: [0x%02x%02x] (%s)\n", 
            (255 - spi_slave_tx_buf[7]), (255 - spi_slave_tx_buf[8]), 
            spi_slave_rx_buf[5 + numberOfArrayElements(readARegister_cmd)], spi_slave_rx_buf[6 + numberOfArrayElements(readARegister_cmd)],
            registerNames[requestDataFromMaster_stage - 4]
        );*/
    } else if (requestDataFromMaster_stage == (NUM_REGISTERS_TO_READ + 2)) {
        // this is a coils-read result...
        registerValues[requestDataFromMaster_stage - 2] = 
            (spi_slave_rx_buf[5 + NUM_READAREGISTER_CMD] << 8) | spi_slave_rx_buf[6 + NUM_READAREGISTER_CMD];
        
        /*Serial.printf("   Coils [0x%02x%02x + 0x%02x%02x]: [0x%02x%02x]\n", 
            (255 - spi_slave_tx_buf[6]), (255 - spi_slave_tx_buf[7]),
            (255 - spi_slave_tx_buf[8]), (255 - spi_slave_tx_buf[9]), 
            spi_slave_rx_buf[5 + numberOfArrayElements(readARegister_cmd)], spi_slave_rx_buf[6 + numberOfArrayElements(readARegister_cmd)]
        );*/
    } else {
        Serial.printf("THIS SHOULD NOT HAPPEN!\n\n");
    }
    

}

void task_wait_spi(void* pvParameters) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // block until the transaction comes from master
        slave.wait(spi_slave_rx_buf, spi_slave_tx_buf, BUFFER_SIZE); 

/*
        spi_slave_transaction_t* r_trans;
        r_trans->length = 8 * BUFFER_SIZE;  // in bit size
        r_trans->rx_buffer = spi_slave_rx_buf;
        r_trans->tx_buffer = spi_slave_tx_buf;
        r_trans->user = (void*)"ABC";
*/
        /**
        * Queues a SPI transaction to be executed by this slave device. (The transaction queue size was specified when the slave
        * device was initialised via spi_slave_initialize.) This function may block if the queue is full (depending on the
        * ticks_to_wait parameter). No SPI operation is directly initiated by this function, the next queued transaction
        * will happen when the master initiates a SPI transaction by pulling down CS and sending out clock signals.
        */
/*
        esp_err_t e = spi_slave_queue_trans(VSPI_HOST, r_trans, portMAX_DELAY);
        if (e != ESP_OK) {
            printf("[ERROR] SPI queue trans failed : %d\n", e);
        } else {
*/
            /**
            * This routine will wait until a transaction to the given device (queued earlier with
            * spi_slave_queue_trans) has succesfully completed. It will then return the description of the
            * completed transaction so software can inspect the result and e.g. free the memory or
            * re-use the buffers.
            *
            * It is mandatory to eventually use this function for any transaction queued by ``spi_slave_queue_trans``.
            */
/*
            esp_err_t e = spi_slave_get_trans_result(VSPI_HOST, &r_trans, portMAX_DELAY);
            if (e != ESP_OK) {
                printf("[ERROR] SPI slave get trans result failed : %d\n", e);
            }
        }
*/
        // had-over to process-buffer-task
        xTaskNotifyGive(task_handle_process_buffer);
    }
}

void task_process_buffer(void* pvParameters) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);        

        if (requestDataFromMaster) {

            // processSPISlaveResult();
            if (requestDataFromMaster_stage <= 1) {
                // do nothing with answer
            } else if (requestDataFromMaster_stage < (NUM_REGISTERS_TO_READ + 2)) {
                // this is a register-read result...
                registerValues[requestDataFromMaster_stage - 2] = 
                    (spi_slave_rx_buf[5 + NUM_READAREGISTER_CMD] << 8) | spi_slave_rx_buf[6 + NUM_READAREGISTER_CMD];      
                if (requestDataFromMaster_stage - 2 == 6) { // Stellsignal Rk1
                    rk1Stell[rkIdx] = (uint8_t) spi_slave_rx_buf[6 + NUM_READAREGISTER_CMD];
                    rkIdx ++;
                    if (rkIdx > 37) {
                        rkIdx = 0;
                    }
                }  
            } else if (requestDataFromMaster_stage == (NUM_REGISTERS_TO_READ + 2)) {
                // this is a coils-read result...
                registerValues[requestDataFromMaster_stage - 2] = 
                    (spi_slave_rx_buf[5 + NUM_READAREGISTER_CMD] << 8) | spi_slave_rx_buf[6 + NUM_READAREGISTER_CMD];
            } else {
                Serial.printf("THIS SHOULD NOT HAPPEN!\n\n");
            }

            requestDataFromMaster_stage++;

            if (requestDataFromMaster_stage == 0) {
                set_tx_buffer(0xFF); // high = tell master slave is there
            } else if (requestDataFromMaster_stage == 1) {
                set_tx_buffer(slaveRadyCommand, sizeof(slaveRadyCommand)); // tell master to prepare for commands from slave
            } else if (requestDataFromMaster_stage < (NUM_REGISTERS_TO_READ + 2)) {
                revolveReadRegisterCommand();
            } else if (requestDataFromMaster_stage == (NUM_REGISTERS_TO_READ + 2)) {
                revolveReadCoilCommand();
            } else {
                requestDataFromMaster_stage = 0;
                lastDataRequestTime = millis(); // wait for pollDataDelay_ms until fetch the next time the same data
                requestDataFromMaster = false; 
                set_tx_buffer(0x00); // low = master can relax clock
            }
        } else {
            set_tx_buffer(0x00); // low = master can relax clock
        }

        slave.pop();
        transactionNo++;
        
        // hand-over to wait-task
        xTaskNotifyGive(task_handle_wait_spi);
    }
}


void setupWiFiServer() {

    int numberOfNetworks = WiFi.scanNetworks();
    Serial.printf("Found %d Wifi networks: \n", numberOfNetworks);

    for(int i =0; i<numberOfNetworks; i++){
        Serial.print("Network name: ");
        Serial.println(WiFi.SSID(i));
        Serial.print("Signal strength: ");
        Serial.println(WiFi.RSSI(i));
        Serial.println("-----------------------");
    }
    Serial.println("Connecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    //connect to your local wi-fi network
    WiFi.begin(ssid, password);

    //check wi-fi is connected to wi-fi network
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected..!");
    Serial.print("Got IP: ");  Serial.println(WiFi.localIP());

    if (MDNS.begin("esp32")) { // access this ESP32 via http://esp32.local
        Serial.println("mDNS responder started");
    }
    MDNS.setInstanceName("5575_slave");

    // register a http-service in DNS-SD
    if (mdns_service_add("5575_slave", "_http", "_tcp", 80, NULL, 0)) {
        Serial.println("DNS-SD responder started");
    }
    mdns_service_txt_item_set("5575_slave", "_http._tcp", "version", "1.0");


    // server.on("/", handleRoot);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        handleRoot(request);
        
    });
    
    // server.on("/test.svg", drawGraph);
    server.on("/test.svg", HTTP_GET, [](AsyncWebServerRequest *request){
        drawGraph(request);
        
    });

    // server.onNotFound(handle_NotFound);
    server.onNotFound(handle_NotFound);
    
    // OTA-Updates via http://esp32.local/update
    AsyncElegantOTA.begin(&server); 
    
    server.begin();

    // set buffer for diagram to 0
    memset(rk1Stell, 0, sizeof(rk1Stell));
}

void setupSPISlave () {
    // HSPI = CS: 15, CLK: 14, MOSI: 13, MISO: 12
    // VSPI = CS: 5, CLK: 18, MOSI: 23, MISO: 19

    // Trovis 5575 sends an invertes "Slave Select" signal (idle = low, slave select = high)
    // So by using this interrupt service routine with an "input" pin connected to this
    // inverted "slave select" line from master, we trigger (inverted to master's signal) 
    // another "output" pin which is connected to the actual "slave select" pin of our ESP32.  
    pinMode(pin_inv_SS, INPUT);
    attachInterrupt(digitalPinToInterrupt(pin_inv_SS), slave_signal_from_master, CHANGE);

    pinMode(pin_inv_SS_OUT, OUTPUT);
    digitalWrite(pin_inv_SS_OUT, HIGH); // Idle = HIGH

    // digitalWrite(MISO, HIGH); // this signals Trovis 5575 that a slave is present
    
    // slave.setDataMode(SPI_MODE0); // Clock polarity = 0, Clock phase = 0
    // slave.setDataMode(SPI_MODE1); // Clock polarity = 0, Clock phase = 1
    // slave.setDataMode(SPI_MODE2); // Clock polarity = 1, Clock phase = 0
    slave.setDataMode(SPI_MODE3); // Clock polarity = 1, Clock phase = 1
    slave.setQueueSize(1);  // transaction queue size
    
    set_tx_buffer(0x00);
    
    slave.begin(VSPI, SCK, MISO, MOSI, SS);

    xTaskCreatePinnedToCore(
        task_wait_spi,      //Function to implement the task 
        "task_wait_spi",    //Name of the task
        2048,               //Stack size in words 
        NULL,               //Task input parameter 
        2,                  //Priority of the task 
        &task_handle_wait_spi,  //Task handle.
        CORE_TASK_SPI_SLAVE //Core no where the task should run 
    );
    xTaskNotifyGive(task_handle_wait_spi);

    xTaskCreatePinnedToCore(task_process_buffer, "task_process_buffer", 2048, NULL, 2, &task_handle_process_buffer, CORE_TASK_PROCESS_BUFFER);    

    lastDataRequestTime = millis();
}

void setup() {
    Serial.begin(115200);
    while (!Serial)
    ; // wait for serial attach

    delay(10000);

    setupWiFiServer();

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
        Serial.printf("Begin new request at transaction-counter %d\n", transactionNo);
    }

    AsyncElegantOTA.loop();
}


// https://github.com/LacobusVentura/MODBUS-CRC16
static uint16_t MODBUS_CRC16_v3( const unsigned char *buf, unsigned int len ) {
	static const uint16_t table[256] = {
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
	0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
	0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
	0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
	0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
	0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
	0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
	0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
	0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
	0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
	0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
	0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
	0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
	0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
	0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
	0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
	0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
	0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
	0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
	0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
	0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040 };

	uint8_t xor_ = 0;
	uint16_t crc = 0xFFFF;

	while( len-- )
	{
		xor_ = (*buf++) ^ crc;
		crc >>= 8;
		crc ^= table[xor_];
	}

    uint16_t swapped = (crc>>8) | (crc<<8);
    // Serial.printf(" CRC3   [0x%04x]\n", swapped);
	return swapped;
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
  
    char temp[2400];
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;
    // <meta http-equiv='refresh' content='5'/>
    snprintf(temp, 2400,

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
            <img src=\"/test.svg\" />\
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

