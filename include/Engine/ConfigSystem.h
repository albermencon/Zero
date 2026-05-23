#pragma once

#include <Engine/Core.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Zero
{
    class ENGINE_API ConfigSystem
    {
    public:
        static ConfigSystem& Get();

        ConfigSystem(const ConfigSystem&) = delete;
        ConfigSystem& operator=(const ConfigSystem&) = delete;
        ConfigSystem(ConfigSystem&&) = delete;
        ConfigSystem& operator=(ConfigSystem&&) = delete;

        bool Load(const std::string& filepath);
        bool Save(const std::string& filepath) const;

        bool HasKey(const std::string& section, const std::string& key) const;

        std::string GetString(const std::string& section, const std::string& key, const std::string& default_val = "") const;
        int GetInt(const std::string& section, const std::string& key, int default_val = 0) const;
        unsigned int GetUInt(const std::string& section, const std::string& key, unsigned int default_val = 0) const;
        float GetFloat(const std::string& section, const std::string& key, float default_val = 0.0f) const;
        bool GetBool(const std::string& section, const std::string& key, bool default_val = false) const;

        void SetString(const std::string& section, const std::string& key, const std::string& val);
        void SetInt(const std::string& section, const std::string& key, int val);
        void SetUInt(const std::string& section, const std::string& key, unsigned int val);
        void SetFloat(const std::string& section, const std::string& key, float val);
        void SetBool(const std::string& section, const std::string& key, bool val);

    private:
        ConfigSystem() = default;
        ~ConfigSystem() = default;

        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_Config;
    };
}
