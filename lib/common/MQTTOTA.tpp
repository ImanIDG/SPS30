#ifndef SENSENET_MQTTOTA_TPP
#define SENSENET_MQTTOTA_TPP

#include <Ticker.h>

#define FW_CHECKSUM_ATTR "fw_checksum"
#define FW_CHECKSUM_ALG_ATTR "fw_checksum_algorithm"
#define FW_SIZE_ATTR "fw_size"
#define FW_TITLE_ATTR "fw_title"
#define FW_VERSION_ATTR "fw_version"

#define FW_STATE_ATTR "fw_state"
#define FW_ERROR_ATTR "fw_error"

#define FW_REQUEST_TOPIC "v2/fw/request/"
#define FW_RESPONSE_TOPIC "v2/fw/response/"

void OTAResetESP() {
    ESP.restart();
}

class MQTTOTA {
public:
    bool begin(String current_fw_title, String current_fw_version);

    bool checkForUpdate();

    MQTTOTA(MQTTController *mqttController, uint16_t chunkSize);

    void startHandleOTAMessages() {
        enabled = true;
    }

    void stopHandleOTAMessages() {
        enabled = false;
    }

private:
    String current_fw_title, current_fw_version;
    MQTTController *mqttController;
    Ticker restartTicker;
    uint16_t totalChunks, chunkSize, currentChunk, requestId;
    bool enabled;
    uint8_t lastSentProgressPercent;
    MQTTController::MqttCallbackJsonPayload callbackJson = [this](String topic, DynamicJsonDocument json) -> bool {
        return handleMessage(topic, json);
    };

    bool handleMessage(String topic, DynamicJsonDocument json);

    bool handleMessageRaw(String topic, uint8_t *payload, unsigned int length);

    void requestChunkPart(int chunkPart);
};

bool MQTTOTA::handleMessage(String topic, DynamicJsonDocument json) {
    if (json.containsKey("shared"))
        json = json["shared"].as<JsonObject>();

    if (!json.containsKey(FW_CHECKSUM_ATTR) || !json.containsKey(FW_CHECKSUM_ALG_ATTR) ||
        !json.containsKey(FW_SIZE_ATTR) || !json.containsKey(FW_TITLE_ATTR) || !json.containsKey(FW_VERSION_ATTR)) {
        return false;
    }

    if (!enabled)
        return true;

    String targetTitle = json[FW_TITLE_ATTR].as<String>();
    String targetVersion = json[FW_VERSION_ATTR].as<String>();

    if (json[FW_CHECKSUM_ATTR].as<String>().equals(ESP.getSketchMD5())) {
        printDBGln("Firmware is Up-to-date");
        DynamicJsonDocument status(200);
        status[FW_STATE_ATTR] = "UPDATED";
        status.shrinkToFit();
        mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, status.as<String>(), true);
        return true;
    }

    // starting OTA Update
    printDBGln(
            String("New Firmware Available .... Start Update from [" + current_fw_title + ":" + current_fw_version +
                   "] To [" + targetTitle + ":" + targetVersion + "]"));

    if (!json[FW_CHECKSUM_ALG_ATTR].as<String>().equals("MD5")) {
        printDBGln("Unsupported checksum Algorithm");
        DynamicJsonDocument status(100);
        status[FW_STATE_ATTR] = "FAILED";
        status[FW_ERROR_ATTR] = "Unsupported checksum Algorithm";
        status.shrinkToFit();
        mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, status.as<String>(), true);
        return true;
    }

    OTAUpdate.onStart([&]() {
        printDBGln("OTA started");
        lastSentProgressPercent = 0;
        currentChunk = 0;
        DynamicJsonDocument status(300);
        status[FW_STATE_ATTR] = "UPDATING";
        mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, status.as<String>(), true);

        mqttController->setTimeout(30000);
        if (!mqttController->setBufferSize(chunkSize + 50)) {
            mqttController->resetTimeout();
            mqttController->resetBufferSize();
            printDBGln("NOT ENOUGH RAM!");
            status[FW_STATE_ATTR] = "FAILED";
            status[FW_ERROR_ATTR] = "NOT ENOUGH RAM!";
            status.shrinkToFit();
            mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, status.as<String>(), true);
            return;
        }

        requestId = random(1, 1000);
        totalChunks = (json[FW_SIZE_ATTR].as<String>().toInt() / chunkSize);
        if (json[FW_SIZE_ATTR].as<String>().toInt() % chunkSize == 0) totalChunks--;
        requestChunkPart(0);
    });

    OTAUpdate.onEnd([&](bool result) {
        mqttController->resetTimeout();
        mqttController->resetBufferSize();
        if (result) {
            printDBGln("OTA Ended Successfully! ");
            restartTicker.once(5, OTAResetESP);
        }
    });

    OTAUpdate.onProgress([&](int current, int total) {
        uint8_t progressPercent = current * 10 / total;
        if (progressPercent != 0 && progressPercent > lastSentProgressPercent) {
            DynamicJsonDocument status(200);
            status[FW_STATE_ATTR] = "UPDATING_" + String(progressPercent * 10);
            status.shrinkToFit();
            mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, status.as<String>(), true);
            lastSentProgressPercent = progressPercent;
        }
    });

    OTAUpdate.onError([&](int err) {
        mqttController->resetTimeout();
        mqttController->resetBufferSize();
        printDBGln(String("OTA ERROR [" + String(err) + "]: " + OTAUpdate.getLastErrorString()));
        DynamicJsonDocument status(100);
        status[FW_STATE_ATTR] = "FAILED";
        status[FW_ERROR_ATTR] = String("OTA ERROR [" + String(err) + "]: " + OTAUpdate.getLastErrorString());
        status.shrinkToFit();
        mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, status.as<String>(), true);
    });

    OTAUpdate.rebootOnUpdate(false);
//    todo force to start new one after the last process failed. And try to continue in connection loss
    if (!OTAUpdate.startUpdate(json[FW_SIZE_ATTR], json[FW_CHECKSUM_ATTR])) {
        printDBGln("Can Not start OTA");
        printDBGln(OTAUpdate.getLastErrorString());
        DynamicJsonDocument status(100);
        status[FW_STATE_ATTR] = "FAILED";
        status.shrinkToFit();
        mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, status.as<String>(), true);
    }

    return true;
}

bool MQTTOTA::handleMessageRaw(String topic, uint8_t *payload, unsigned int length) {
    if (topic.indexOf(FW_RESPONSE_TOPIC) != 0) return false;

    if (!enabled) return true;

    int id = topic.substring(15, topic.indexOf("/", 15)).toInt();
    if (requestId != id)
        return true;

    int chunkPart = topic.substring(topic.lastIndexOf("/") + 1).toInt();
    if (OTAUpdate.isUpdating() && chunkPart == currentChunk) {
        printDBGln(String("Writing Chuck part: " + String(currentChunk)));
        printDBG("OTA progress: ");
        printDBGln(String(String((((float) currentChunk) / ((float) totalChunks)) * 100) + "%"));

        if (!OTAUpdate.writeUpdateChunk(payload, length))
            return true;

        if (currentChunk == totalChunks) {
            OTAUpdate.endUpdate();
        } else {
            currentChunk++;
            requestChunkPart(currentChunk);
        }
    }
    return true;
}

bool MQTTOTA::begin(String currentFirmwareTitle, String currentFirmwareVersion) {
    requestId = 0;
    enabled = true;

    this->current_fw_title = currentFirmwareTitle;
    this->current_fw_version = currentFirmwareVersion;

    DynamicJsonDocument data(200);
    data["current_fw_title"] = current_fw_title;
    data["current_fw_version"] = current_fw_version;
    data.shrinkToFit();
    mqttController->addToPublishQueue(V1_TELEMETRY_TOPIC, data.as<String>(), true);

    mqttController->registerCallbackJsonPayload(callbackJson);
    mqttController->registerCallbackRawPayload([this](String topic, uint8_t *payload, unsigned int length) -> bool {
        return handleMessageRaw(topic, payload, length);
    });

    checkForUpdate();
    return true;
}

bool MQTTOTA::checkForUpdate() {
    DynamicJsonDocument requestData(300);
    requestData["sharedKeys"] = String(
            String(FW_CHECKSUM_ATTR) + "," + FW_CHECKSUM_ALG_ATTR + "," + FW_SIZE_ATTR + "," + FW_TITLE_ATTR + "," +
            FW_VERSION_ATTR);
    requestData.shrinkToFit();
    return mqttController->requestAttributesJson(requestData.as<String>(), callbackJson);
}

void MQTTOTA::requestChunkPart(int chunkPart) {
    mqttController->addToPublishQueue(
            String(FW_REQUEST_TOPIC + String(requestId) + "/chunk/" + String(chunkPart)),
            String(chunkSize), true);
}

MQTTOTA::MQTTOTA(MQTTController *mqttController, uint16_t chunkSize) : mqttController(mqttController),
                                                                       chunkSize(chunkSize) {}

#endif
