#ifndef MYCONFIG_H
#define MYCONFIG_H
#include <stdint.h>


extern char chipUUID[23];
extern char influx_url[512];
extern char influx_org[128];
extern char influx_bucket[128];
extern char influx_token[512];
extern bool shouldSaveConfig;
extern  uint64_t currentTimebase; 

void setupConfig();
void saveConfigFileToFileSystem();
void readConfigFromFileSystem();
long initClock();
uint64_t getCurrentTime();

#endif