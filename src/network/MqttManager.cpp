#include "MqttManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include "RobotState.h"

MqttManager::MqttManager(const QString& clientId, QObject *parent)
    : QObject(parent)
{
    mosquitto_lib_init();
    
    // 创建一个 mosquitto 客户端实例，并把当前对象指针传递过去，供静态回调函数使用
    m_mosq = mosquitto_new(clientId.toUtf8().constData(), true, this);
    if (!m_mosq) {
        qWarning() << "Failed to allocate mosquitto client!";
        return;
    }

    mosquitto_connect_callback_set(m_mosq, on_connect_cb);
    mosquitto_disconnect_callback_set(m_mosq, on_disconnect_cb);
    mosquitto_message_callback_set(m_mosq, on_message_cb);

    // 连接收到消息的中转信号
    connect(this, &MqttManager::messageReceived, this, [=](const QString& topic, const QByteArray& data){
        RobotState::instance().updateFromProtobuf(topic, data);
    });
}

MqttManager::~MqttManager() {
    if (m_mosq) {
        mosquitto_disconnect(m_mosq);
        mosquitto_loop_stop(m_mosq, false);
        mosquitto_destroy(m_mosq);
    }
    mosquitto_lib_cleanup();
}

void MqttManager::connectToBroker(const QString &host, quint16 port) {
    if (m_mosq) {
        qDebug() << "Connecting to generic MQTT broker at" << host << ":" << port;
        int rc = mosquitto_connect_async(m_mosq, host.toUtf8().constData(), port, 60);
        if (rc == MOSQ_ERR_SUCCESS) {
            mosquitto_loop_start(m_mosq); // 启动后台网络线程
        } else {
            qWarning() << "Mosquitto connect failed, Error:" << mosquitto_strerror(rc);
        }
    }
}

void MqttManager::disconnectFromBroker() {
    if (m_mosq) {
        mosquitto_disconnect(m_mosq);
    }
}

void MqttManager::publishMsg(const QString &topic, const QByteArray &data) {
    if (m_mosq) {
        mosquitto_publish(m_mosq, nullptr, topic.toUtf8().constData(), data.size(), data.constData(), 0, false);
    }
}

void MqttManager::publishJsonCmd(const QJsonObject& cmd) {
    QJsonDocument doc(cmd);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    publishMsg(m_cmdTopic, payload);
}

// 静态回调函数：连接成功时触发订阅
void MqttManager::on_connect_cb(struct mosquitto *mosq, void *obj, int rc) {
    MqttManager *self = static_cast<MqttManager*>(obj);
    if (rc == 0) {
        qDebug() << "Mosquitto MQTT Connected! Subscribing to proto topics...";
        // 根据 SharkDataServer/protocol.md 我们需要订阅主题 (通常是指令名，如 GameStatus)
        mosquitto_subscribe(mosq, nullptr, "GameStatus", 0);
        mosquitto_subscribe(mosq, nullptr, "GlobalUnitStatus", 0);
        mosquitto_subscribe(mosq, nullptr, "RobotRespawnStatus", 0);
        mosquitto_subscribe(mosq, nullptr, "RobotDynamicStatus", 0);
        mosquitto_subscribe(mosq, nullptr, "RobotStaticStatus", 0);
        mosquitto_subscribe(mosq, nullptr, "CustomByteBlock", 0); // 订阅 0x0310 自定义图传数据
        emit self->connected();
    } else {
        qDebug() << "Mosquitto MQTT connect failed:" << mosquitto_connack_string(rc);
    }
}

void MqttManager::on_disconnect_cb(struct mosquitto *mosq, void *obj, int rc) {
    Q_UNUSED(mosq);
    Q_UNUSED(rc);
    MqttManager *self = static_cast<MqttManager*>(obj);
    qDebug() << "Mosquitto MQTT Disconnected!";
    emit self->disconnected();
}

void MqttManager::on_message_cb(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    Q_UNUSED(mosq);
    if (!msg || !msg->topic || msg->payloadlen <= 0 || !msg->payload) return;

    MqttManager *self = static_cast<MqttManager*>(obj);
    QString topic = QString::fromUtf8(msg->topic);
    QByteArray payload((const char*)msg->payload, msg->payloadlen);

    // 将底层线程收到的消息投递到 Qt 队列 (通过 invokeMethod 确保跨线程绝对安全调用信号)
    QMetaObject::invokeMethod(self, "messageReceived", Qt::QueuedConnection, 
                              Q_ARG(QString, topic), Q_ARG(QByteArray, payload));
}
