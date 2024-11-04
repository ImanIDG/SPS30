#ifndef SENSENET_PRINTDBG_TPP
#define SENSENET_PRINTDBG_TPP

#ifdef SENSENET_DEBUG_WRITE_TO_SD

#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "Uptime.h"

String logFilename = "";
SPIClass hspi = SPIClass(HSPI);
#ifdef INC_FREERTOS_H
SemaphoreHandle_t semaWriteLog;
#endif

bool sdCardInit() {
#ifdef INC_FREERTOS_H
    semaWriteLog = xSemaphoreCreateBinary();
    if (semaWriteLog == NULL) {
        Serial.println("Error: Could Not create sema for writing log in sd card");
        delay(5000);
        ESP.restart();
    }
    xSemaphoreGive(semaWriteLog);
#endif

    Serial.println("Init sd card for logs!");
    pinMode(2, OUTPUT);
    hspi.begin(14, 12, 13, 2);
    if (!SD.begin(2, hspi)) {
        Serial.println("Card Mount Failed");
        return false;
    }

    Serial.println("SD Card mounted");
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return false;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t totalSize = SD.totalBytes();
    uint64_t usedSize = SD.usedBytes();
    float percent = (usedSize * 100) / totalSize;
    Serial.printf("SD Card Size: %llu and used percent: %f\n", totalSize, percent);

    String dirPath = "/SecurityGatewayLogs";
    if (SD.exists(dirPath)) {
        File path = SD.open(dirPath, FILE_READ);
        if (!path) {
            if (!path.isDirectory()) {
                SD.remove(dirPath);
                SD.mkdir(dirPath);
            }
            path.close();
        }
    } else SD.mkdir(dirPath);

    File path = SD.open(dirPath, FILE_READ);
    String file = path.getNextFileName();
    uint32_t maxIndex = 1;
    while (file != nullptr && !file.isEmpty()) {
        uint32_t i = strtol(pathToFileName(file.c_str()), NULL, 10);
        if (i == 0) {
            SD.rmdir(file);
            SD.remove(file);
        } else if (i > maxIndex) maxIndex = i;

        file = path.getNextFileName();
    }

    String logFilenameTest = dirPath + "/" + String(maxIndex + 1);
    File logFile = SD.open(logFilenameTest, FILE_WRITE, true);
    if (logFile) {
        logFile.println("Inited SD card successfully!");
        logFile.close();
        Serial.println("Log file is: " + String(logFilenameTest));
        logFilename = logFilenameTest;
    }
    return true;
}

void writeToSDCard(const char *text, bool newLine) {

#ifdef INC_FREERTOS_H
    if (xSemaphoreTake(semaWriteLog, portMAX_DELAY)) {
#endif
        if (logFilename == nullptr || logFilename.isEmpty()) return;
        File file = SD.open(logFilename, FILE_APPEND, true);
        if (!file) {
            Serial.println("Could not open file to write logs to sd card");
            return;
        }

        if (newLine) file.println(text);
        else file.print(text);
        file.close();
#ifdef INC_FREERTOS_H
        xSemaphoreGive(semaWriteLog);
    }
#endif
}

#endif

void printDBGln(const char *text) {
    String textWithUptime = String(Uptime.getMilliseconds()) + "ms --> " + text;
#ifdef SENSENET_DEBUG
    SerialMon.println(textWithUptime);
#endif
#ifdef SENSENET_DEBUG_WRITE_TO_SD
    if (logFilename != nullptr && !logFilename.isEmpty())
        writeToSDCard(textWithUptime.c_str(), true);
#endif
}

void printDBG(const char *text) {
    String textWithUptime = String(Uptime.getMilliseconds()) + "ms --> " + text;
#ifdef SENSENET_DEBUG
    SerialMon.print(textWithUptime);
#endif
#ifdef SENSENET_DEBUG_WRITE_TO_SD
    if (logFilename != nullptr && !logFilename.isEmpty())
        writeToSDCard(textWithUptime.c_str(), false);
#endif
}

void printDBGln(const String &text) {
    printDBGln(text.c_str());
}

void printDBG(const String &text) {
    printDBG(text.c_str());
}


#endif //SENSENET_PRINTDBG_TPP
