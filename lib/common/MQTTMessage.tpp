#ifndef SENSENET_MQTT_Message_TPP
#define SENSENET_MQTT_Message_TPP

#define V1_Attributes_TOPIC "v1/devices/me/attributes"
#define V1_Attributes_GATEWAY_TOPIC "v1/gateway/attributes"

#define V1_TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define V1_TELEMETRY_GATEWAY_TOPIC "v1/gateway/telemetry"

class MQTTMessage {
public:
    MQTTMessage(const String &topic, const String &payload);

    MQTTMessage();

    const String &getTopic() const;

    const String &getPayload() const {
        return payload;
    }


private:
    String topic, payload;
};

MQTTMessage::MQTTMessage() {
    topic = "";
    payload = "";
}

MQTTMessage::MQTTMessage(const String &messageTopic, const String &messagePayload) {
    this->topic = messageTopic;
    this->payload = messagePayload;
}

const String &MQTTMessage::getTopic() const {
    return topic;
}


#endif