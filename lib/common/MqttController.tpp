#ifndef SENSENET_MQTT_CONTROLLER_TPP
#define SENSENET_MQTT_CONTROLLER_TPP

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "MQTTMessage.tpp"

#include "map"

#include <utility>
#include "sensenet.h"

class MQTTController {
public:

    typedef std::function<bool(const String, DynamicJsonDocument)> DefaultMqttCallbackJsonPayload;

    typedef std::function<bool(const String &, uint8_t *, unsigned int)> DefaultMqttCallbackRawPayload;

    typedef std::function<bool(String topic, DynamicJsonDocument json)> MqttCallbackJsonPayload;

    typedef std::function<bool(const String &topic, uint8_t *payload, unsigned int length)> MqttCallbackRawPayload;

    typedef std::function<void(MQTTMessage mqttMessage)> SentMQTTMessageCallback;

    typedef std::function<void(void)> ConnectionEvent;

    MQTTController();

    void updateSendSystemAttributesInterval(float seconds);

    bool isConnected();

    void connect(Client &client, String id, String username, String pass, String url, uint16_t port,
                 DefaultMqttCallbackJsonPayload callback, DefaultMqttCallbackRawPayload callbackRaw = nullptr,
                 ConnectionEvent connectionEvent = nullptr);

    void init();

    void loop();

    bool setBufferSize(uint16_t chunkSize);

    bool addToPublishQueue(const String &topic, const String &payload, bool memory_fs);

    bool requestAttributesJson(const String &keysJson, const MqttCallbackJsonPayload &callback = nullptr);

    bool requestRPC(const String &payload, const MqttCallbackJsonPayload &callback = nullptr);

    bool sendAttributes(DynamicJsonDocument json, bool queueOnMemoryOrFs, const String &deviceName = "");

    bool sendAttributes(const String &json, bool queueOnMemoryOrFs);

    bool sendTelemetry(DynamicJsonDocument json, bool queueOnMemoryOrFs, const String &deviceName = "");

    bool sendTelemetry(const String &json, bool queueOnMemoryOrFs);

    bool sendTelemetry(const DynamicJsonDocument &data, bool queueOnMemoryOrFs, uint64_t ts);

    bool sendClaimRequest(const String &key, uint32_t duration_ms, const String &deviceName = "");

    bool sendGatewayConnectEvent(const String &deviceName);

    bool sendGatewayDisConnectEvent(const String &deviceName);

    bool subscribeToGatewayEvent();

    bool unsubscribeToGatewayEvent();

    void registerCallbackRawPayload(const MqttCallbackRawPayload &callback);

    void registerCallbackJsonPayload(const MqttCallbackJsonPayload &callback);

    void onSentMQTTMessageCallback(const SentMQTTMessageCallback &callback);

    void setTimeout(int timeout_ms);

    void resetTimeout();

    void resetBufferSize();

    void sendSystemAttributes(bool value);

    void disconnect();

private:
    Queue *memoryQueue;
    Queue *fsQueue;
#ifdef INC_FREERTOS_H
    SemaphoreHandle_t semaQueue;
#endif
    PubSubClient mqttClient;
    float updateInterval = 10;
    uint64_t lastSendAttributes;
    uint64_t connectionRecheckTimeout;
    bool isSendAttributes = false;
    String id, username, pass, url;
    uint16_t port;
    ConnectionEvent connectedEvent;
    DefaultMqttCallbackJsonPayload defaultCallback;
    DefaultMqttCallbackRawPayload defaultCallbackRaw;
    std::vector<MqttCallbackRawPayload> registeredCallbacksRaw;
    std::vector<MqttCallbackJsonPayload> registeredCallbacksJson;
    std::map<unsigned int, MqttCallbackJsonPayload> requestsCallbacksJson;
    SentMQTTMessageCallback sentMqttMessageCallback;
    uint16_t defaultTimeout, defaultBufferSize, jsonSerializeBuffer;
    uint32_t timeout, requestId;

    String getChipInfo();

    void sendAttributesFunc();

    void on_message(const char *topic, uint8_t *payload, unsigned int length);

};

void MQTTController::registerCallbackRawPayload(const MqttCallbackRawPayload &callback) {
    registeredCallbacksRaw.push_back(callback);
}

void MQTTController::registerCallbackJsonPayload(const MQTTController::MqttCallbackJsonPayload &callback) {
    registeredCallbacksJson.push_back(callback);
}

void MQTTController::on_message(const char *tp, uint8_t *payload, unsigned int length) {
    String topic = String(tp);

    printDBG("On message: ");
    printDBGln(topic);
    printDBG("Length: ");
    printDBGln(String(length));


    for (MqttCallbackRawPayload callback: registeredCallbacksRaw)
        if (callback(tp, payload, length))
            return;

    char json[length + 1];
    strncpy(json, (char *) payload, length);
    json[length] = '\0';

    printDBG("Topic: ");
    printDBGln(tp);
    printDBG("Message: ");
    printDBGln(json);

    // Decode JSON request
    DynamicJsonDocument data(jsonSerializeBuffer);
    auto error = deserializeJson(data, (char *) json);
    if (error) {
        printDBG("deserializeJson() failed with code ");
        printDBGln(error.c_str());
        if (defaultCallbackRaw != nullptr) defaultCallbackRaw(topic, payload, length);
        return;
    }

    if (topic.indexOf("v1/devices/me/attributes/response/") == 0 || topic.indexOf("v1/devices/me/rpc/response/") == 0) {
        unsigned int topicId = topic.substring(topic.lastIndexOf("/") + 1).toInt();
        printDBGln("TopicId response: " + String(topicId));
        auto it = requestsCallbacksJson.begin();
        for (int i = 0; i < requestsCallbacksJson.size(); i++) {
            if (it->first == topicId) {
                if (it->second(topic, data)) {
                    requestsCallbacksJson.erase(topicId);
                    return;
                } else break;
            }
            it++;
        }
    }

    for (MqttCallbackJsonPayload callback: registeredCallbacksJson)
        if (callback(topic, data))
            return;

    if (defaultCallback != nullptr) defaultCallback(topic, data);
    if (defaultCallbackRaw != nullptr) defaultCallbackRaw(tp, payload, length);
}

bool MQTTController::isConnected() {
    return mqttClient.connected();
}

void
MQTTController::connect(Client &client, String Id, String username_in, String password, String url_in, uint16_t port_in,
                        MQTTController::DefaultMqttCallbackJsonPayload callback,
                        MQTTController::DefaultMqttCallbackRawPayload callbackRaw,
                        MQTTController::ConnectionEvent connectionEvent) {

    this->id = Id;
    this->username = username_in;
    this->pass = password;
    this->url = url_in;
    this->port = port_in;

    connectedEvent = connectionEvent;
    this->defaultCallback = callback;
    this->defaultCallbackRaw = callbackRaw;

    this->mqttClient.setClient(client);
    mqttClient.setBufferSize(2048);
    mqttClient.setServer(url.c_str(), port);
    mqttClient.setCallback([&](const char *tp, uint8_t *payload, unsigned int length) {
        on_message(tp, payload, length);
    });

}


void MQTTController::init() {
#ifdef INC_FREERTOS_H
    semaQueue = xSemaphoreCreateBinary();
    if (semaQueue == NULL) {
        printDBGln("Error: Could Not create Semaphore Queue");
        delay(5000);
        ESP.restart();
    }
    xSemaphoreGive(semaQueue);
#endif
    delete memoryQueue;
    memoryQueue = new Queue(50, true);
    delete fsQueue;
    fsQueue = new Queue(512, true);
}

String lastTopic = "";
String lastPayload = "";
unsigned short lastRetry = 0;

void MQTTController::loop() {
    if (url.isEmpty() || username.isEmpty()) return;

    uint64_t millis = Uptime.getMilliseconds();
    mqttClient.loop();

#ifdef INC_FREERTOS_H
    if (xSemaphoreTake(semaQueue, portMAX_DELAY)) {
#endif
        uint16_t memoryQueueSize = memoryQueue == nullptr ? 0 : memoryQueue->getSize();
        uint16_t fsQueueSize = fsQueue == nullptr ? 0 : fsQueue->getSize();
        bool mqttIsConnected = isConnected();

        if ((memoryQueueSize > 0 || fsQueueSize > 0) && mqttIsConnected) {

            if (true) {
                bool memory_fs = memoryQueueSize > 0;
                MQTTMessage message = memory_fs ? memoryQueue->peek() : fsQueue->peek();
                if (message.getPayload().equalsIgnoreCase(lastPayload) &&
                    message.getTopic().equalsIgnoreCase(lastTopic)) {
                    lastRetry++;
                } else {
                    lastRetry = 0;
                    lastTopic = message.getTopic();
                    lastPayload = message.getPayload();
                }

                if (lastRetry >= 10) {
                    printDBGln(String("Memory Type [" + String(memory_fs) + "] " +
                                      "MQTT failing 10 times to send a message, remove message - queue size is: " +
                                      String(memory_fs ? memoryQueueSize : fsQueueSize)));
                    memory_fs ? memoryQueue->removeLastPeek() : fsQueue->removeLastPeek();
                } else if (lastRetry >= 5) {
                    printDBGln(String("Memory Type [" + String(memory_fs) + "] " +
                                      "MQTT failing 5 times to send a message, disconnect - queue size is: " +
                                      String(memory_fs ? memoryQueueSize : fsQueueSize)));
                    disconnect();
                }

                if (mqttClient.publish(message.getTopic().c_str(), message.getPayload().c_str())) {
                    memory_fs == 1 ? memoryQueue->removeLastPeek() : fsQueue->removeLastPeek();;
                }
            }
        }
#ifdef INC_FREERTOS_H
        xSemaphoreGive(semaQueue);
    }
#endif

    if (isSendAttributes && ((millis - lastSendAttributes) > ((uint64_t) (updateInterval * 1000)))) {
        lastSendAttributes = millis;
        sendAttributesFunc();
    }

    if ((millis - connectionRecheckTimeout) <= timeout) return;
    connectionRecheckTimeout = millis;

    if (!isConnected()) {
        //todo: fixbug: not reConnect to cloud after invalid token
        disconnect();
        printDBG(String("Connecting to MQTT server... "));
        if (mqttClient.connect(id.c_str(), username.c_str(), pass.c_str())) {
            printDBGln("[Connected]");
            if (isSendAttributes)
                addToPublishQueue(V1_Attributes_TOPIC, getChipInfo(), true);

            mqttClient.subscribe("v1/devices/me/rpc/request/+");
            mqttClient.subscribe("v1/devices/me/attributes/response/+");
            mqttClient.subscribe(V1_Attributes_TOPIC);
            mqttClient.subscribe("v2/fw/response/+/chunk/+");

            if (connectedEvent != nullptr) {
                connectedEvent();
            }

        } else {
            printDBG("[FAILED] [ rc = ");
            printDBG(String(mqttClient.state()));
            printDBGln(String(" : retrying in " + String(timeout / 1000) + " seconds]"));
        }
    }
}

#ifdef ESP32
#ifdef __cplusplus
extern "C" {
#endif

uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();

#endif

String MQTTController::getChipInfo() {
    DynamicJsonDocument data(400);
    data[String("Cpu FreqMHZ")] = ESP.getCpuFreqMHz();
    data[String("Sensenet SDK Version")] = SDK_VERSION;
#ifdef ESP32
    data[String("Chip Type")] = "ESP32";
#endif

#ifdef ESP8266
    data[String("Chip Type")] = "ESP8266";
    data[String("Chip ID")] = ESP.getChipId();
#endif

    return data.as<String>();;
}

void MQTTController::sendAttributesFunc() {
    if (!isConnected())
        return;

    DynamicJsonDocument data(400);
    if (memoryQueue != nullptr) {
        DynamicJsonDocument queueStats = memoryQueue->showStoreOnDiskStatus();
        for (const auto &kv: queueStats.as<JsonObject>())
            data["memory_" + String(kv.key().c_str())] = queueStats[kv.key()];
    }
    if (fsQueue != nullptr) {
        DynamicJsonDocument queueStats = fsQueue->showStoreOnDiskStatus();
        for (const auto &kv: queueStats.as<JsonObject>())
            data["fs_" + String(kv.key().c_str())] = queueStats[kv.key()];
    }
    data[String("upTime")] = Uptime.getSeconds();
    data[String("ESP Free Heap")] = ESP.getFreeHeap();
    data[String("ESP Min Heap")] = ESP.getMinFreeHeap();
    printDBGln("Esp free heap: " + String(ESP.getFreeHeap()));

#ifdef ESP32
    data[String("ESP32 temperature")] = (temprature_sens_read() - 32) / 1.8;
#endif

    addToPublishQueue(V1_Attributes_TOPIC, data.as<String>(), true);
}

void MQTTController::updateSendSystemAttributesInterval(float seconds) {
    updateInterval = seconds;
}

bool MQTTController::setBufferSize(uint16_t chunkSize) {
    return mqttClient.setBufferSize(chunkSize);
}

bool MQTTController::addToPublishQueue(const String &topic, const String &payload, bool memory_fs) {

    bool result = false;
#ifdef INC_FREERTOS_H
    if (xSemaphoreTake(semaQueue, portMAX_DELAY)) {
#endif
        MQTTMessage message(topic, payload);
        if (memory_fs ? memoryQueue == nullptr : fsQueue == nullptr) {
            printDBGln(String("Memory Type [" + String(memory_fs) + "] " + "Queue is null"));
            result = false;
        } else if (memory_fs ? !memoryQueue->push(message) : !fsQueue->push(message)) {
            printDBGln(String("Memory Type [" + String(memory_fs) + "] " + "Could not pushed message: " +
                              String(memory_fs ? memoryQueue->getSize() : fsQueue->getSize())));
            result = false;
        } else {
//            printDBGln(String("Memory Type [" + String(memory_fs) + "] " + "MQTT Queue Size after push the message: " +
//                              String(memory_fs ? memoryQueue->getSize() : fsQueue->getSize())));
            result = true;
        }

#ifdef INC_FREERTOS_H
        xSemaphoreGive(semaQueue);
    }
#endif
    return result;
}

MQTTController::MQTTController() {
    defaultTimeout = 3000;
    defaultBufferSize = 5120;
    timeout = defaultTimeout;
    requestId = 0;
    jsonSerializeBuffer = 1024;
}

bool
MQTTController::requestAttributesJson(const String &keysJson, const MQTTController::MqttCallbackJsonPayload &callback) {
    requestId++;
    if (callback != nullptr)
        requestsCallbacksJson[requestId] = callback;

    return addToPublishQueue(String("v1/devices/me/attributes/request/" + String(requestId)), keysJson, true);
}

bool
MQTTController::requestRPC(const String &payload, const MQTTController::MqttCallbackJsonPayload &callback) {
    requestId++;
    if (callback != nullptr)
        requestsCallbacksJson[requestId] = callback;

    return addToPublishQueue(String("v1/devices/me/rpc/request/" + String(requestId)), payload, true);
}

bool MQTTController::sendAttributes(DynamicJsonDocument json, bool queueOnMemoryOrFs, const String &deviceName) {
    if (!deviceName.isEmpty()) {
        DynamicJsonDocument newData(json.capacity() + 200);
        newData[deviceName] = json;
        newData.shrinkToFit();
        return addToPublishQueue(V1_Attributes_GATEWAY_TOPIC, newData.as<String>(), queueOnMemoryOrFs);
    }
    json.shrinkToFit();
    return addToPublishQueue(V1_Attributes_TOPIC, json.as<String>(), queueOnMemoryOrFs);
}

bool MQTTController::sendTelemetry(DynamicJsonDocument json, bool queueOnMemoryOrFs, const String &deviceName) {
    if (!deviceName.isEmpty()) {
        DynamicJsonDocument newData(json.capacity() + 200);
        newData[deviceName].add(json);
        newData.shrinkToFit();
        return addToPublishQueue(V1_TELEMETRY_GATEWAY_TOPIC, newData.as<String>(), queueOnMemoryOrFs);
    }
    json.shrinkToFit();
    return addToPublishQueue(V1_TELEMETRY_TOPIC, json.as<String>(), queueOnMemoryOrFs);
}

bool MQTTController::sendAttributes(const String &json, bool queueOnMemoryOrFs) {
    return addToPublishQueue(V1_Attributes_TOPIC, json, queueOnMemoryOrFs);
}

bool MQTTController::sendTelemetry(const String &json, bool queueOnMemoryOrFs) {
    return addToPublishQueue(V1_TELEMETRY_TOPIC, json, queueOnMemoryOrFs);
}

bool MQTTController::sendTelemetry(const DynamicJsonDocument &data, bool queueOnMemoryOrFs, uint64_t ts) {
    if (ts > 946713600000) {  // If ts greater than 2000
        DynamicJsonDocument dataWithTs(data.capacity() + 300);
        dataWithTs["ts"] = String(ts);
        dataWithTs["values"] = data;
        dataWithTs.shrinkToFit();
        return addToPublishQueue(V1_TELEMETRY_TOPIC, dataWithTs.as<String>(), queueOnMemoryOrFs);
    }
    return addToPublishQueue(V1_TELEMETRY_TOPIC, data.as<String>(), queueOnMemoryOrFs);
}

bool MQTTController::sendClaimRequest(const String &key, uint32_t duration_ms, const String &deviceName) {
    DynamicJsonDocument data(200);
    data["secretKey"] = key;
    data["durationMs"] = duration_ms;
    data.shrinkToFit();

    if (!deviceName.isEmpty()) {
        DynamicJsonDocument deviceData(1024);
        deviceData[deviceName] = data.as<String>();
        deviceData.shrinkToFit();
        return addToPublishQueue("v1/gateway/claim", deviceData.as<String>(), false);
    }

    return addToPublishQueue("v1/devices/me/claim", data.as<String>(), false);
}

void MQTTController::setTimeout(int timeout_ms) {
    this->timeout = timeout_ms;
}

void MQTTController::resetTimeout() {
    timeout = defaultTimeout;
}

void MQTTController::resetBufferSize() {
    mqttClient.setBufferSize(defaultBufferSize);
}

void MQTTController::sendSystemAttributes(bool value) {
    MQTTController::isSendAttributes = value;
}

bool MQTTController::sendGatewayConnectEvent(const String &deviceName) {
    DynamicJsonDocument data(200);
    data["device"] = deviceName;
    data.shrinkToFit();
    return addToPublishQueue("v1/gateway/connect", deviceName, true);
}

bool MQTTController::sendGatewayDisConnectEvent(const String &deviceName) {
    DynamicJsonDocument data(200);
    data["device"] = deviceName;
    data.shrinkToFit();
    return addToPublishQueue("v1/gateway/disconnect", deviceName, true);
}

bool MQTTController::subscribeToGatewayEvent() {
    if (mqttClient.subscribe(V1_Attributes_GATEWAY_TOPIC))
        return mqttClient.subscribe("v1/gateway/rpc");
    return false;
}

bool MQTTController::unsubscribeToGatewayEvent() {
    if (mqttClient.unsubscribe(V1_Attributes_GATEWAY_TOPIC))
        return mqttClient.unsubscribe("v1/gateway/rpc");
    return false;
}

void MQTTController::onSentMQTTMessageCallback(const MQTTController::SentMQTTMessageCallback &callback) {
    sentMqttMessageCallback = callback;
}

void MQTTController::disconnect() {
    mqttClient.disconnect();
}

#endif