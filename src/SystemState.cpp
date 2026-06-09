#include "SystemState.h"

// ======================================================================================
// 基石智慧农业控制系统 - 系统状态管理实现
// ======================================================================================

// ===================================================================
// 全局状态变量定义
// ===================================================================
SystemState_t g_system_state;
SemaphoreHandle_t g_system_state_mutex = NULL;
QueueHandle_t g_relay_command_queue = NULL;
QueueHandle_t g_sensor_data_queue = NULL;

// ===================================================================
// 设备配置数组定义
// ===================================================================
DeviceConfig_t g_devices[] = {
    {8,  "Single Relay Tester",   DEVICE_RELAY,  0x0000, 1, CHANNEL_1},
    {20, "16-Channel Relay Controller",   DEVICE_RELAY,  0x0000, 3, CHANNEL_2},
    {3,  "Temp & Humidity Sensor 1",     DEVICE_SENSOR, 0x0000, 2, CHANNEL_3},
    {6,  "Temp & Humidity Sensor 2",     DEVICE_SENSOR, 0x0000, 2, CHANNEL_4}
};

const uint8_t g_device_count = sizeof(g_devices) / sizeof(g_devices[0]);

// ===================================================================
// 系统状态管理函数实现
// ===================================================================

bool initSystemState() {
    // 创建互斥锁
    g_system_state_mutex = xSemaphoreCreateMutex();
    if (g_system_state_mutex == NULL) {
        Serial.println("错误: 无法创建系统状态互斥锁");
        return false;
    }

    // 创建队列
    g_relay_command_queue = xQueueCreate(RELAY_COMMAND_QUEUE_SIZE, sizeof(RelayCommand_t));
    if (g_relay_command_queue == NULL) {
        Serial.println("错误: 无法创建继电器命令队列");
        return false;
    }

    g_sensor_data_queue = xQueueCreate(SENSOR_DATA_QUEUE_SIZE, sizeof(SensorUpdate_t));
    if (g_sensor_data_queue == NULL) {
        Serial.println("错误: 无法创建传感器数据队列");
        return false;
    }

    // 初始化系统状态
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 初始化传感器状态
        g_system_state.canopy_sensor.temperature = 0.0;
        g_system_state.canopy_sensor.humidity = 0.0;
        g_system_state.canopy_sensor.online = false;
        g_system_state.canopy_sensor.last_update = 0;

        g_system_state.root_sensor.temperature = 0.0;
        g_system_state.root_sensor.humidity = 0.0;
        g_system_state.root_sensor.online = false;
        g_system_state.root_sensor.last_update = 0;

        // 初始化继电器状态
        for (int i = 0; i < RELAY_COUNT; i++) {
            g_system_state.relays[i].state = false;
            g_system_state.relays[i].last_update = 0;
            g_system_state.relays[i].is_pending = false;
        }

        // 初始化系统状态
        g_system_state.wifi_connected = false;
        g_system_state.modbus_initialized = false;
        g_system_state.system_ready = false;
        g_system_state.system_uptime = 0;
        g_system_state.last_sensor_poll = 0;
        g_system_state.modbus_busy = false;
        g_system_state.current_device_index = 0;

        xSemaphoreGive(g_system_state_mutex);
        Serial.println("System state initialized OK");
        return true;
    } else {
        Serial.println("Error: failed to lock system state mutex for initialization");
        return false;
    }
}

void updateSensorData(uint8_t sensor_id, float temperature, float humidity, bool online) {
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t current_time = millis();
        
        if (sensor_id == 3) { // 冠层传感器
            g_system_state.canopy_sensor.temperature = temperature;
            g_system_state.canopy_sensor.humidity = humidity;
            g_system_state.canopy_sensor.online = online;
            g_system_state.canopy_sensor.last_update = current_time;
        } else if (sensor_id == 6) { // 根区传感器
            g_system_state.root_sensor.temperature = temperature;
            g_system_state.root_sensor.humidity = humidity;
            g_system_state.root_sensor.online = online;
            g_system_state.root_sensor.last_update = current_time;
        }
        
        xSemaphoreGive(g_system_state_mutex);
    }
}

void updateRelayState(uint8_t relay_id, bool state) {
    if (relay_id < 1 || relay_id > RELAY_COUNT) {
        return;
    }
    
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_system_state.relays[relay_id - 1].state = state;
        g_system_state.relays[relay_id - 1].last_update = millis();
        g_system_state.relays[relay_id - 1].is_pending = false;
        
        xSemaphoreGive(g_system_state_mutex);
    }
}

SystemState_t getSystemState() {
    SystemState_t state_copy;
    
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state_copy = g_system_state;
        xSemaphoreGive(g_system_state_mutex);
    } else {
        // 如果无法获取锁，返回默认状态
        memset(&state_copy, 0, sizeof(SystemState_t));
    }
    
    return state_copy;
}

void getRelayStates(bool* states) {
    if (states == NULL) return;
    
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < RELAY_COUNT; i++) {
            states[i] = g_system_state.relays[i].state;
        }
        xSemaphoreGive(g_system_state_mutex);
    } else {
        // 如果无法获取锁，返回全部关闭状态
        for (int i = 0; i < RELAY_COUNT; i++) {
            states[i] = false;
        }
    }
}

void setModbusBusy(bool busy) {
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_system_state.modbus_busy = busy;
        xSemaphoreGive(g_system_state_mutex);
    }
}

bool isModbusBusy() {
    bool busy = false;
    
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        busy = g_system_state.modbus_busy;
        xSemaphoreGive(g_system_state_mutex);
    }
    
    return busy;
}

bool sendRelayCommand(uint8_t relay_id, bool state) {
    if (relay_id < 1 || relay_id > RELAY_COUNT) {
        return false;
    }
    
    RelayCommand_t command;
    command.relay_id = relay_id;
    command.state = state;
    command.timestamp = millis();
    
    return xQueueSend(g_relay_command_queue, &command, pdMS_TO_TICKS(100)) == pdTRUE;
}

void updateSystemConnectivity(bool wifi_connected) {
    if (xSemaphoreTake(g_system_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_system_state.wifi_connected = wifi_connected;
        xSemaphoreGive(g_system_state_mutex);
    }
}
