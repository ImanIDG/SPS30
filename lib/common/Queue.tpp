#ifndef SENSENET_QUEUE_TPP
#define SENSENET_QUEUE_TPP

#include <Arduino.h>
#include <vector>
#include "PrintDBG.tpp"
#include "MQTTMessage.tpp"

#ifdef ESP32

#include "LittleFS.h"
#include "FS.h"
#include "ArduinoJson.h"

#endif
using namespace std;

class Queue {
private:
    vector<MQTTMessage> list;
    uint16_t size;
    bool storeOnMemory;

#ifdef ESP32
    String queueDirString = "NON_INIT";  //write a stupid without "/" in starting to create error
    uint32_t maxIndex = 1;
    uint32_t minIndex = -1;
    uint16_t currentSize = 0;
    String lastPeekFilePath = "";

    bool writeToFile(const char *message, String filename) {
        filename = queueDirString + String("/") + filename;
        printDBGln(String("Writing [" + String(message) + "] to [" + filename + "]"));
        File file = LittleFS.open(filename, FILE_WRITE, true);
        if (!file) {
            printDBGln("failed to open file for writing");
            return false;
        }
        bool result = file.print(message);
        file.close();
        printDBGln("file written result: " + String(result));
        return result;
    }

#endif

public:
    Queue(uint16_t size_t, bool storeOnMemory = true, bool format = false) {
        this->size = size_t;
        this->storeOnMemory = true;

#ifdef ESP32
        if (!storeOnMemory) {
            if (!LittleFS.begin(true)) {
                printDBGln("LittleFS Mount Failed");
                return;
            } else {
                if (format) {
                    printDBGln("LittleFS formatting...");
                    printDBGln("LittleFS formatting finished with result: " + String(LittleFS.format()));
                }
                double totalSize = LittleFS.totalBytes();
                double usedSize = LittleFS.usedBytes();
                double percent = usedSize / totalSize * 100;
                printDBGln(String("Inited LittleFS with total size [" + String(totalSize) +
                                  "] and used size [" + String(usedSize) + "] and used percent is: [" +
                                  String(percent) + "]"));

                String queueDirPath = "/queue";
                printDBGln("Testing IO for file " + queueDirPath);
                if (!LittleFS.exists(queueDirPath))
                    printDBGln("create queue dir with result: " + String(LittleFS.mkdir(queueDirPath)));

                File queueDir = LittleFS.open(queueDirPath, FILE_READ);
                if (!queueDir.isDirectory()) {
                    printDBGln("remove queue dir with result: " + String(LittleFS.remove(queueDirPath)));
                    printDBGln("create queue dir with result: " + String(LittleFS.mkdir(queueDirPath)));
                }
                queueDir.close();

                queueDir = LittleFS.open(queueDirPath, FILE_READ);
                String file = queueDir.getNextFileName();
                while (file != nullptr && !file.isEmpty()) {
                    uint32_t i = strtol(pathToFileName(file.c_str()), NULL, 10);
                    if (i == 0) {
                        LittleFS.rmdir(file);
                        LittleFS.remove(file);
                    } else {
                        currentSize++;
                        if (i > maxIndex) maxIndex = i;
                        if (i < minIndex) minIndex = i;
                    }
                    file = queueDir.getNextFileName();
                }
                if (minIndex > maxIndex) minIndex = maxIndex;
                queueDir.close();

//                printDBGln("Queue Size is [" + String(currentSize) +
//                           "] and maxIndex is [" + String(maxIndex) + "] and minIndex is [" + String(minIndex) + "]");
                queueDirString = queueDirPath;
                this->storeOnMemory = false;
            }
        }
#endif
    }

    ~Queue() {
        clear();
    }

    uint16_t getSize();

    bool push(const MQTTMessage &item);

    MQTTMessage peek();

    void clear();

    bool removeLastPeek();

#ifdef ESP32

    DynamicJsonDocument showStoreOnDiskStatus() {
        DynamicJsonDocument data(1024);
#ifdef ESP32
        if (!storeOnMemory) {
            data["FS TotalSize"] = String(LittleFS.totalBytes());
            data["FS UsedSize"] = String(LittleFS.usedBytes());
            data["QueueSize"] = String(getSize());
            data.shrinkToFit();
            return data;
        }
#endif
        data["QueueSize"] = String(getSize());
        data.shrinkToFit();
        return data;
    }

    void listDir() const {
#ifdef ESP32
        if (!storeOnMemory) {
            printDBGln("List Dir: " + queueDirString);
            File queueDir = LittleFS.open(queueDirString, FILE_READ);
            String file = queueDir.getNextFileName();
            while (file != nullptr && !file.isEmpty()) {
                printDBGln("File: " + file);
                file = queueDir.getNextFileName();
            }
            queueDir.close();
        }
#endif
    }

#endif
};

bool Queue::removeLastPeek() {
    if (getSize() == 0) {
        printDBGln(String("Error: Queue is empty"));
        return false;
    }
#ifdef ESP32
    if (!storeOnMemory) {
        if (!lastPeekFilePath.isEmpty()) {
            bool result = LittleFS.remove(lastPeekFilePath);
            if (result) {
                currentSize--;
                int i = strtol(pathToFileName(lastPeekFilePath.c_str()), NULL, 10);
                if (i >= minIndex) minIndex = i + 1;
            }
            return result;
        }
        return false;
    }
#endif

    list.erase(list.begin());
    return true;
}

uint16_t Queue::getSize() {
#ifdef ESP32
    if (!storeOnMemory) {
        if (currentSize == 0) {
            maxIndex = 1;
            minIndex = 1;
        }
        return currentSize;
    }
#endif
    return list.size();
}

bool Queue::push(const MQTTMessage &item) {
    int _size = getSize();
    if (_size == size) {
        printDBGln(String("Queue is full and it's size is: " + String(currentSize)));
        if (peek().getPayload().isEmpty() || !removeLastPeek()) {
            printDBGln(String("Error: Can not peek one and continue round robin: "));
            listDir();
            return false;
        }
    }

#ifdef ESP32
    if (!storeOnMemory) {
        DynamicJsonDocument data(1024);
        data["payload"] = item.getPayload();
        data["topic"] = item.getTopic();
        data.shrinkToFit();
        bool result = writeToFile(data.as<String>().c_str(), String(maxIndex));
        if (result) {
            maxIndex++;
            currentSize++;
            if (maxIndex == 0 || minIndex == 0) {
                printDBGln("Error: End of world in queue!");
                LittleFS.format();
                ESP.restart();
            }
        }
        return result;
    }
#endif

    list.push_back(item);
    return true;
}

MQTTMessage Queue::peek() {
    int _size = getSize();
    if (_size == 0) return {};

#ifdef ESP32
    if (!storeOnMemory) {
        if (minIndex > maxIndex) {
            printDBGln("Error: minIndex > maxIndex");
            listDir();
            delay(1000);
            ESP.restart();
        }

        bool found = false;
        while (minIndex <= maxIndex) {
            if (LittleFS.exists(queueDirString + "/" + String(minIndex))) {
                found = true;
                break;
            }
            minIndex++;
        }

        if (!found) {
            printDBGln("Error: could not find any file but current size is: " + String(currentSize));
            listDir();
            delay(1000);
            ESP.restart();
        }

        File file = LittleFS.open(queueDirString + "/" + String(minIndex), FILE_READ);
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, file.readString());
        lastPeekFilePath = file.path();
        MQTTMessage message = MQTTMessage(doc["topic"].as<String>(), doc["payload"].as<String>());
        printDBGln("Peek file: " + String(file.path()));
        file.close();
        return message;
    }
#endif

    return list.front();
}

void Queue::clear() {
#ifdef ESP32
    if (!storeOnMemory) {
        LittleFS.rmdir(queueDirString);
        getSize();
    }
#endif
    list.clear();
}

#endif //SENSENET_QUEUE_TPP
