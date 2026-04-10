#ifndef MQTTMANAGER_H
#define MQTTMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <mosquitto.h>

class MqttManager : public QObject
{
    Q_OBJECT
public:
    explicit MqttManager(const QString& clientId, QObject *parent = nullptr);
    ~MqttManager();

    void connectToBroker(const QString &host, quint16 port);
    void disconnectFromBroker();
    
    // 下发命令到机器人
    void publishMsg(const QString &topic, const QByteArray &data);
    void publishJsonCmd(const QJsonObject& cmd); // 保留作历史兼容测试用

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString &topic, const QByteArray &message);

private:
    static void on_connect_cb(struct mosquitto *mosq, void *obj, int rc);
    static void on_disconnect_cb(struct mosquitto *mosq, void *obj, int rc);
    static void on_message_cb(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);

    struct mosquitto *m_mosq;
    QString m_cmdTopic = "rm_client_up"; 
};

#endif // MQTTMANAGER_H
