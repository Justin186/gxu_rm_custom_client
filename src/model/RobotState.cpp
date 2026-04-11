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

int RobotState::hp() const { QMutexLocker l(&m_mutex); return m_hp; }
int RobotState::maxHp() const { QMutexLocker l(&m_mutex); return m_maxHp; }
int RobotState::heat() const { QMutexLocker l(&m_mutex); return m_heat; }
int RobotState::maxHeat() const { QMutexLocker l(&m_mutex); return m_maxHeat; }
int RobotState::remainingAmmo() const { QMutexLocker l(&m_mutex); return m_remainingAmmo; }
float RobotState::fireRate() const { QMutexLocker l(&m_mutex); return m_fireRate; }
int RobotState::redScore() const { QMutexLocker l(&m_mutex); return m_redScore; }
int RobotState::blueScore() const { QMutexLocker l(&m_mutex); return m_blueScore; }
int RobotState::stageCountdown() const { QMutexLocker l(&m_mutex); return m_stageCountdown; }
bool RobotState::canRemoteHeal() const { QMutexLocker l(&m_mutex); return m_canRemoteHeal; }
bool RobotState::canRemoteAmmo() const { QMutexLocker l(&m_mutex); return m_canRemoteAmmo; }
QByteArray RobotState::customData() const { QMutexLocker l(&m_mutex); return m_customData; }

void RobotState::updateFromJson(const QByteArray& jsonData) {
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    
    QMutexLocker locker(&m_mutex);
    if (obj.contains("hp")) m_hp = obj["hp"].toInt();
    if (obj.contains("max_hp")) m_maxHp = obj["max_hp"].toInt();
    if (obj.contains("heat")) m_heat = obj["heat"].toInt();
    
    locker.unlock();
    emit stateUpdated();
}

void RobotState::updateFromProtobuf(const QString& topic, const QByteArray& data) {
    if (topic == "GameStatus") {
        rm_client_up::GameStatus status;
        if (status.ParseFromArray(data.constData(), data.size())) {
            QMutexLocker locker(&m_mutex);
            m_redScore = status.red_score();
            m_blueScore = status.blue_score();
            m_stageCountdown = status.stage_countdown_sec();
            locker.unlock();
            emit stateUpdated();
        }
    } else if (topic == "RobotDynamicStatus") {
        rm_client_up::RobotDynamicStatus dynamic_status;
        if (dynamic_status.ParseFromArray(data.constData(), data.size())) {
            QMutexLocker locker(&m_mutex);
            m_hp = dynamic_status.current_health();
            m_heat = dynamic_status.current_heat();
            m_remainingAmmo = dynamic_status.remaining_ammo();
            m_fireRate = dynamic_status.last_projectile_fire_rate();
            
            // Note: proto might not have these implemented yet if they error so try optional logic securely map properties
            // If bool can_remote... are missing, proto will cleanly default them to false
            locker.unlock();
            emit stateUpdated();
        }
    } else if (topic == "RobotStaticStatus") {
        rm_client_up::RobotStaticStatus static_status;
        if (static_status.ParseFromArray(data.constData(), data.size())) {
            QMutexLocker locker(&m_mutex);
            m_maxHp = static_status.max_health();
            m_maxHeat = static_status.max_heat();
            locker.unlock();
            emit stateUpdated();
        }
    } else if (topic == "CustomByteBlock") {
        rm_client_up::CustomByteBlock custom_block;
        if (custom_block.ParseFromArray(data.constData(), data.size())) {
            QMutexLocker locker(&m_mutex);
            m_customData = QByteArray(custom_block.data().data(), custom_block.data().size());
            // qDebug() << "📦 [Protobuf] 成功解析 CustomByteBlock! 纯净数据长度:" << m_customData.size();
            locker.unlock();
            emit customVideoReceived(m_customData);
            emit stateUpdated();
        }
    }
}
