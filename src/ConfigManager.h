// src/ConfigManager.h

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>

/**
 * @class ConfigManager
 * @brief 负责管理系统的所有持久化配置。
 * 
 * 使用ESP32的Preferences库（NVS的封装）来存储键值对。
 * 所有方法均为静态，方便在项目各处调用。
 */
class ConfigManager {
public:
    /**
     * @brief 初始化配置管理器，必须在setup()的早期调用。
     */
    static void begin();

    /**
     * @brief 检查时间是否已经被配置过。
     * @return 如果时间已配置，返回true；否则返回false。
     */
    static bool isTimeConfigured();

    /**
     * @brief 设置时间配置状态。
     * @param configured 将状态设置为true或false。
     */
    static void setTimeConfigured(bool configured);

    /**
     * @brief 清除所有配置，恢复到出厂默认状态。
     */
    static void factoryReset();

    /**
     * @brief 保存WiFi凭据到NVS。
     * @param ssid WiFi网络名称
     * @param password WiFi密码
     */
    static void saveWifiCredentials(const String& ssid, const String& password);

    /**
     * @brief 从NVS加载WiFi凭据。
     * @param ssid 输出参数，WiFi网络名称
     * @param password 输出参数，WiFi密码
     * @return 如果成功加载凭据返回true，否则返回false
     */
    static bool loadWifiCredentials(String& ssid, String& password);

    /**
     * @brief 强制提交所有待处理的NVS更改到闪存。
     * 在重启前必须调用此方法以确保数据持久化。
     */
    static void commit();
};

#endif // CONFIG_MANAGER_H