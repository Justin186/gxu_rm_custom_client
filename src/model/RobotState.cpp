#include "RobotState.h"
#include "messages.pb.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

RobotState& RobotState::instance() {
    static RobotState state;
    return state;
}

RobotState::RobotState(QObject *parent) : QObject(parent) {}

int RobotState::hp() const {
    QMutexLocker locker(&m_mutex);
    return m_hp;
}

int RobotState::maxHp() const {
    QMutexLocker locker(&m_mutex);
    return m_maxHp;
}

int RobotState::heat() const {
    QMutexLocker locker(&m_mutex);
    return m_heat;
}

void RobotState::updateFromJson(const QByteArray& jsonData) {
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject()) {
        qWarning() << "Invalid JSON state";
        return;
    }
    QJsonObject obj = doc.object();
    
    QMutexLocker locker(&m_mutex);
    bool hpChangedFlag = false;
    bool maxHpChangedFlag = false;
    bool heatChangedFlag = false;

    if (obj.contains("hp")) {
        int v = obj["hp"].toInt();
        if (m_hp != v) {
            m_hp = v;
            hpChangedFlag = true;
        }
    }
    if (obj.contains("max_hp")) {
        int v = obj["max_hp"].toInt();
        if (m_maxHp != v) {
            m_maxHp = v;
            maxHpChangedFlag = true;
        }
    }
    if (obj.contains("heat")) {
        int v = obj["heat"].toInt();
        if (m_heat != v) {
            m_heat = v;
            heatChangedFlag = true;
        }
    }
    
    locker.unlock(); // Emit 之前释放锁，防止 UI 在响应时更新其他引发死锁

    if (hpChangedFlag) emit hpChanged(m_hp);
    if (maxHpChangedFlag) emit maxHpChanged(m_maxHp);
    if (heatChangedFlag) emit heatChanged(m_heat);
}

void RobotState::updateFromProtobuf(const QString& topic, const QByteArray& data) {
    if (topic == "GameStatus") {
        rm_client_up::GameStatus status;
        if (status.ParseFromArray(data.constData(), data.size())) {
            qDebug() << "GameStatus updated. Red Score :" << status.red_score() << " Blue Score :" << status.blue_score();
            // TODO: Extract scores to ui
        }
    } else if (topic == "RobotDynamicStatus") {
        rm_client_up::RobotDynamicStatus dynamic_status;
        if (dynamic_status.ParseFromArray(data.constData(), data.size())) {
            QMutexLocker locker(&m_mutex);
            bool hpChangedFlag = false;
            bool heatChangedFlag = false;

            if (m_hp != dynamic_status.current_health()) {
                m_hp = dynamic_status.current_health();
                hpChangedFlag = true;
            }

            if (m_heat != (int)dynamic_status.current_heat()) {
                m_heat = dynamic_status.current_heat();
                heatChangedFlag = true;
            }

            locker.unlock();

            if (hpChangedFlag) emit hpChanged(m_hp);
            if (heatChangedFlag) emit heatChanged(m_heat);
        }
    } else if (topic == "RobotStaticStatus") {
        rm_client_up::RobotStaticStatus static_status;
        if (static_status.ParseFromArray(data.constData(), data.size())) {
            QMutexLocker locker(&m_mutex);
            bool maxHpChangedFlag = false;

            if (m_maxHp != static_status.max_health()) {
                m_maxHp = static_status.max_health();
                maxHpChangedFlag = true;
            }

            locker.unlock();

            if (maxHpChangedFlag) emit maxHpChanged(m_maxHp);
        }
    }
}
