#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

// ======================================================================================
// 基石智慧农业控制系统 - 系统状态管理
// ======================================================================================

// ===================================================================
// 传感器数据结构
// ===================================================================
typedef struct {
    float temperature;
    float humidity;
    bool online;
    uint32_t last_update;
} SensorData_t;

// ===================================================================
// 继电器状态结构
// ===================================================================
typedef struct {
    bool state;
    uint32_t last_update;
    bool is_pending;
} RelayData_t;

// ===================================================================
// 系统状态结构
// ===================================================================
typedef struct {
    // 传感器状态
    SensorData_t canopy_sensor;    // 冠层传感器
    SensorData_t root_sensor;      // 根区传感器
    
    // 继电器状态
    RelayData_t relays[RELAY_COUNT];
    
    // 系统状态
    bool wifi_connected;
    bool modbus_initialized;
    bool system_ready;
    uint32_t system_uptime;
    uint32_t last_sensor_poll;
    
    // 通信状态
    bool modbus_busy;
    uint8_t current_device_index;
} SystemState_t;

// ===================================================================
// 继电器命令结构
// ===================================================================
typedef struct {
    uint8_t relay_id;
    bool state;
    uint32_t timestamp;
} RelayCommand_t;

// ===================================================================
// 传感器数据更新结构
// ===================================================================
typedef struct {
    uint8_t device_type;
    uint8_t device_id;
    float temperature;
    float humidity;
    bool online;
    uint32_t timestamp;
} SensorUpdate_t;

// ===================================================================
// 全局状态变量声明
// ===================================================================
extern SystemState_t g_system_state;
extern SemaphoreHandle_t g_system_state_mutex;
extern QueueHandle_t g_relay_command_queue;
extern QueueHandle_t g_sensor_data_queue;

// ===================================================================
// 系统状态管理函数声明
// ===================================================================

/**
 * 初始化系统状态管理
 * @return true 成功, false 失败
 */
bool initSystemState();

/**
 * 更新传感器数据
 * @param sensor_id 传感器ID (3=冠层, 6=根区)
 * @param temperature 温度值
 * @param humidity 湿度值
 * @param online 在线状态
 */
void updateSensorData(uint8_t sensor_id, float temperature, float humidity, bool online);

/**
 * 更新继电器状态
 * @param relay_id 继电器ID (1-16)
 * @param state 继电器状态
 */
void updateRelayState(uint8_t relay_id, bool state);

/**
 * 获取系统状态副本（线程安全）
 * @return 系统状态副本
 */
SystemState_t getSystemState();

/**
 * 获取继电器状态数组副本（线程安全）
 * @param states 输出数组，至少16个元素
 */
void getRelayStates(bool* states);

/**
 * 设置Modbus忙碌状态
 * @param busy 忙碌状态
 */
void setModbusBusy(bool busy);

/**
 * 获取Modbus忙碌状态
 * @return 忙碌状态
 */
bool isModbusBusy();

/**
 * 发送继电器控制命令到队列
 * @param relay_id 继电器ID
 * @param state 目标状态
 * @return true 成功, false 失败
 */
bool sendRelayCommand(uint8_t relay_id, bool state);

/**
 * 更新系统连接状态
 * @param wifi_connected WiFi连接状态
 */
void updateSystemConnectivity(bool wifi_connected);

#endif // SYSTEM_STATE_H
