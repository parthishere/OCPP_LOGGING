#ifndef FILEMANAGE_H
#define FILEMANAGE_H

#include <WiFi.h>
#include <FS.h>

extern bool server_disconnected_file_management;
namespace parth
{
    extern bool server_disconnected_file_management;
    extern const long gmtOffset_sec;
    extern const int daylightOffset_sec;
    extern unsigned long time_now;
    extern int period;
    extern char timeSeconds[10];
    extern char timeMin[3];
    extern char timeHour[10];
    extern char timeDay[10];
    extern char timeMonth[10];
    extern char timeYear[5];
    extern int count;

};
extern struct tm timeinfo;
void listDir(fs::FS *fs, const char *dirname, uint8_t levels);
void readFile(fs::FS &fs, const char *path);
void deleteFile(fs::FS &fs, const char *path);
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);

#endif