// src/ConfigManager.cpp

#include "ConfigManager.h"
#include <Preferences.h> // 引入ESP32的Preferences库

// 创建一个Preferences实例
Preferences preferences;

// 定义一个命名空间，防止与其他库的键名冲突
const char* PREF_NAMESPACE = "jishi_config";

// 定义配置项的键名
const char* KEY_TIME_CONFIGURED = "time_conf";
const char* KEY_WIFI_SSID = "wifi_ssid";
const char* KEY_WIFI_PASSWORD = "wifi_pass";

void ConfigManager::begin() {
    // 初始化，打开我们定义的命名空间
    // 第二个参数 false 表示以读写模式打开
    preferences.begin(PREF_NAMESPACE, false);
}

bool ConfigManager::isTimeConfigured() {
    // 从NVS读取布尔值。如果键不存在，默认返回false。
    return preferences.getBool(KEY_TIME_CONFIGURED, false);
}

void ConfigManager::setTimeConfigured(bool configured) {
    // 将布尔值写入NVS
    preferences.putBool(KEY_TIME_CONFIGURED, configured);
    Serial.printf("配置状态 'time_configured' 已保存为: %s\n", configured ? "true" : "false");
}

void ConfigManager::factoryReset() {
    // 清除命名空间下的所有键值对
    preferences.clear();
    // 提交清除操作到闪存
    preferences.end();
    Serial.println("所有配置已被清除（恢复出厂设置）。");
}

void ConfigManager::saveWifiCredentials(const String& ssid, const String& password) {
    // 将WiFi凭据保存到NVS
    preferences.putString(KEY_WIFI_SSID, ssid);
    preferences.putString(KEY_WIFI_PASSWORD, password);
    Serial.printf("WiFi凭据已保存: SSID=%s\n", ssid.c_str());
}

bool ConfigManager::loadWifiCredentials(String& ssid, String& password) {
    // 从NVS加载WiFi凭据
    ssid = preferences.getString(KEY_WIFI_SSID, "");
    password = preferences.getString(KEY_WIFI_PASSWORD, "");

    // 如果SSID为空，说明没有保存过WiFi凭据
    if (ssid.length() == 0) {
        Serial.println("未找到保存的WiFi凭据。");
        return false;
    }

    Serial.printf("WiFi credentials loaded: SSID=%s\n", ssid.c_str());
    return true;
}

void ConfigManager::commit() {
    // 调用end()会确保所有挂起的更改都被写入物理闪存
    preferences.end();
    Serial.println("配置更改已提交到闪存。");
}
