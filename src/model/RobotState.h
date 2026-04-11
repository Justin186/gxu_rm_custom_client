#ifndef ROBOTSTATE_H
#define ROBOTSTATE_H

#include <QObject>
#include <QMutex>

// 存放机器人所有的遥测状态数据，供 UI 和外部读取，由于多线程修改使用读写锁控制
class RobotState : public QObject {
    Q_OBJECT

public:
    static RobotState& instance();

    int hp() const;
    int maxHp() const;
    int heat() const;
    int maxHeat() const;
    int remainingAmmo() const;
    float fireRate() const;
    
    int redScore() const;
    int blueScore() const;
    int stageCountdown() const;
    
    bool canRemoteHeal() const;
    bool canRemoteAmmo() const;
    QByteArray customData() const;

    void updateFromJson(const QByteArray& jsonData);
    void updateFromProtobuf(const QString& topic, const QByteArray& data);

signals:
    void stateUpdated(); // 统一用一个信号刷新UI即可，减少信号槽爆炸
    void customVideoReceived(const QByteArray& data); // 新增信号：抛出由 0x0310 带来的自定义图传流

private:
    explicit RobotState(QObject *parent = nullptr);
    ~RobotState() = default;

    RobotState(const RobotState&) = delete;
    RobotState& operator=(const RobotState&) = delete;

    mutable QMutex m_mutex;
    int m_hp = 200;
    int m_maxHp = 200;
    
    int m_heat = 0;
    int m_maxHeat = 100;
    
    int m_remainingAmmo = 0;
    float m_fireRate = 0.0f;
    
    int m_redScore = 0;
    int m_blueScore = 0;
    int m_stageCountdown = 0;
    
    QByteArray m_customData;

    bool m_canRemoteHeal = false;
    bool m_canRemoteAmmo = false;
};

#endif // ROBOTSTATE_H
