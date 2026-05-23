#include "pch.h"
#include <Engine/ConfigSystem.h>
#include <charconv>
#include <cctype>
#include <fstream>
#include <algorithm>

namespace Zero
{
    static std::string_view Trim(std::string_view sv)
    {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        {
            sv.remove_prefix(1);
        }
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        {
            sv.remove_suffix(1);
        }
        return sv;
    }

    ConfigSystem& ConfigSystem::Get()
    {
        static ConfigSystem instance;
        return instance;
    }

    bool ConfigSystem::Load(const std::string& filepath)
    {
        std::ifstream file(filepath, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        m_Config.clear();

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string_view view(content);
        std::string current_section = "";

        size_t pos = 0;
        while (pos < view.size())
        {
            size_t next_line = view.find('\n', pos);
            std::string_view line = view.substr(pos, next_line == std::string_view::npos ? std::string_view::npos : next_line - pos);
            pos = next_line == std::string_view::npos ? view.size() : next_line + 1;

            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
            {
                continue;
            }

            if (line.front() == '[' && line.back() == ']')
            {
                current_section = std::string(Trim(line.substr(1, line.size() - 2)));
                continue;
            }

            size_t eq = line.find('=');
            if (eq != std::string_view::npos)
            {
                std::string key = std::string(Trim(line.substr(0, eq)));
                std::string val = std::string(Trim(line.substr(eq + 1)));
                m_Config[current_section][key] = val;
            }
        }

        return true;
    }

    bool ConfigSystem::Save(const std::string& filepath) const
    {
        std::ofstream file(filepath, std::ios::out | std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        for (const auto& [section, keys] : m_Config)
        {
            if (!section.empty())
            {
                file << "[" << section << "]\n";
            }
            for (const auto& [key, val] : keys)
            {
                file << key << " = " << val << "\n";
            }
            file << "\n";
        }

        return true;
    }

    bool ConfigSystem::HasKey(const std::string& section, const std::string& key) const
    {
        auto s_it = m_Config.find(section);
        if (s_it == m_Config.end())
        {
            return false;
        }
        return s_it->second.find(key) != s_it->second.end();
    }

    std::string ConfigSystem::GetString(const std::string& section, const std::string& key, const std::string& default_val) const
    {
        auto s_it = m_Config.find(section);
        if (s_it == m_Config.end())
        {
            return default_val;
        }
        auto k_it = s_it->second.find(key);
        if (k_it == s_it->second.end())
        {
            return default_val;
        }
        return k_it->second;
    }

    int ConfigSystem::GetInt(const std::string& section, const std::string& key, int default_val) const
    {
        auto s_it = m_Config.find(section);
        if (s_it == m_Config.end())
        {
            return default_val;
        }
        auto k_it = s_it->second.find(key);
        if (k_it == s_it->second.end())
        {
            return default_val;
        }

        std::string_view sv = k_it->second;
        sv = Trim(sv);
        if (sv.empty())
        {
            return default_val;
        }

        int val = default_val;
        auto first = sv.data();
        auto last = sv.data() + sv.size();
        if (*first == '+')
        {
            first++;
        }
        auto [ptr, ec] = std::from_chars(first, last, val);
        if (ec == std::errc{})
        {
            return val;
        }
        return default_val;
    }

    unsigned int ConfigSystem::GetUInt(const std::string& section, const std::string& key, unsigned int default_val) const
    {
        auto s_it = m_Config.find(section);
        if (s_it == m_Config.end())
        {
            return default_val;
        }
        auto k_it = s_it->second.find(key);
        if (k_it == s_it->second.end())
        {
            return default_val;
        }

        std::string_view sv = k_it->second;
        sv = Trim(sv);
        if (sv.empty())
        {
            return default_val;
        }

        unsigned int val = default_val;
        auto first = sv.data();
        auto last = sv.data() + sv.size();
        if (*first == '+')
        {
            first++;
        }
        auto [ptr, ec] = std::from_chars(first, last, val);
        if (ec == std::errc{})
        {
            return val;
        }
        return default_val;
    }

    float ConfigSystem::GetFloat(const std::string& section, const std::string& key, float default_val) const
    {
        auto s_it = m_Config.find(section);
        if (s_it == m_Config.end())
        {
            return default_val;
        }
        auto k_it = s_it->second.find(key);
        if (k_it == s_it->second.end())
        {
            return default_val;
        }

        std::string_view sv = k_it->second;
        sv = Trim(sv);
        if (sv.empty())
        {
            return default_val;
        }

        float val = default_val;
        auto first = sv.data();
        auto last = sv.data() + sv.size();
        if (*first == '+')
        {
            first++;
        }
        auto [ptr, ec] = std::from_chars(first, last, val);
        if (ec == std::errc{})
        {
            return val;
        }
        return default_val;
    }

    bool ConfigSystem::GetBool(const std::string& section, const std::string& key, bool default_val) const
    {
        auto s_it = m_Config.find(section);
        if (s_it == m_Config.end())
        {
            return default_val;
        }
        auto k_it = s_it->second.find(key);
        if (k_it == s_it->second.end())
        {
            return default_val;
        }

        std::string val = k_it->second;
        std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });

        if (val == "true" || val == "1" || val == "yes" || val == "on")
        {
            return true;
        }
        if (val == "false" || val == "0" || val == "no" || val == "off")
        {
            return false;
        }
        return default_val;
    }

    void ConfigSystem::SetString(const std::string& section, const std::string& key, const std::string& val)
    {
        m_Config[section][key] = val;
    }

    void ConfigSystem::SetInt(const std::string& section, const std::string& key, int val)
    {
        char buf[64];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec == std::errc{})
        {
            m_Config[section][key] = std::string(buf, ptr - buf);
        }
    }

    void ConfigSystem::SetUInt(const std::string& section, const std::string& key, unsigned int val)
    {
        char buf[64];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec == std::errc{})
        {
            m_Config[section][key] = std::string(buf, ptr - buf);
        }
    }

    void ConfigSystem::SetFloat(const std::string& section, const std::string& key, float val)
    {
        char buf[64];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec == std::errc{})
        {
            m_Config[section][key] = std::string(buf, ptr - buf);
        }
    }

    void ConfigSystem::SetBool(const std::string& section, const std::string& key, bool val)
    {
        m_Config[section][key] = val ? "true" : "false";
    }
}
