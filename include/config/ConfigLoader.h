#pragma once
#include <string>
#include "config/SystemConfig.h"

class ConfigLoader {
    public:
        static SystemConfig load(const std::string& filename);

    private:
        static std::string getString(const std::string& json, const std::string& key);
        static int getInt(const std::string& json, const std::string& key);
        static float getFloat(const std::string& json, const std::string& key);
        static bool getBool(const std::string& json, const std::string& key);
};