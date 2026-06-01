#pragma once
#include <string>
#include "config/SystemConfig.h"

class ConfigLoader {
    public:
        // static SystemConfig load(const std::string& filename);
        static SystemConfig loadFromString(const char* json);

    private:
        static void getString(const char* json, const char* key, char* out, size_t max_len);
        static int getInt(const char* json, const char* key);
        static float getFloat(const char* json, const char* key);
        static bool getBool(const char* json, const char* key);
};