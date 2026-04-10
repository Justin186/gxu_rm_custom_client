#ifndef ROBOTSTATE_H
#define ROBOTSTATE_H

#include <QObject>
#include <QMutex>

// 存放机器人所有的遥测状态数据，供 UI 和外部读取，由于多线程修改使用读写锁控制
class RobotState : public QObject {
    Q_OBJECT
    Q_PROPERTY(int hp READ hp NOTIFY hpChanged)
    Q_PROPERTY(int maxHp READ maxHp NOTIFY maxHpChanged)
    Q_PROPERTY(int heat READ heat NOTIFY heatChanged)

public:
    static RobotState& instance();

    int hp() const;
    int maxHp() const;
    int heat() const;

    void updateFromJson(const QByteArray& jsonData);
    void updateFromProtobuf(const QString& topic, const QByteArray& data);

signals:
    void hpChanged(int newHp);
    void maxHpChanged(int newMaxHp);
    void heatChanged(int newHeat);

private:
    explicit RobotState(QObject *parent = nullptr);
    ~RobotState() = default;

    RobotState(const RobotState&) = delete;
    RobotState& operator=(const RobotState&) = delete;

    mutable QMutex m_mutex;
    int m_hp = 200;
    int m_maxHp = 200;
    int m_heat = 0;
};

#endif // ROBOTSTATE_H
