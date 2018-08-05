#include "parse_ssh_config.h"
#include <fstream>
#include <sstream>

namespace // anonymous
{

std::filesystem::path get_home_dir()
{
#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4996)
	char* home_dir = getenv("USERPROFILE");
#pragma warning( pop )
#elif defined __unix__ || (defined __APPLE__ && defined __MACH__)// includes linux
    char const* home_dir = getenv("HOME");
#endif
    if (!home_dir)
    {
        throw std::runtime_error("Cannot find the home directory in the environment");
    }
    return home_dir;
}

// trim from left
inline std::string& ltrim(std::string& s, const char* t = " \t\n\r\f\v")
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from right
inline std::string& rtrim(std::string& s, const char* t = " \t\n\r\f\v")
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from left & right
inline std::string& trim(std::string& s, const char* t = " \t\n\r\f\v")
{
    return ltrim(rtrim(s, t), t);
}

void add_when_exists(std::filesystem::path const& priv, identities_t& identities)
{
    if (std::filesystem::is_regular_file(priv))
    {
        std::filesystem::path publ(priv);
        publ.concat(".pub");
        if (std::filesystem::is_regular_file(publ))
        {
            identities.push_back(key_pair_paths_t(priv, publ));
        }
    }
}

}; // anonymous

void find_identities(char const* host, identities_t& identities)
{
    std::filesystem::path home_dir = get_home_dir();
    std::filesystem::path local_config(home_dir);
    local_config.append(".ssh");
    std::filesystem::path default_id(local_config);
    default_id.append("id_rsa");
    add_when_exists(default_id, identities);
    local_config.append("config");
    std::ifstream ifs(local_config);
    if (!ifs.is_open())
    {
        return;
    }
    std::string line;
    std::string key;
    bool host_found = false;
    while (std::getline(ifs, line))
    {
        trim(line);
        if (line.empty())
        {
            continue;
        }
        if(line[0]=='#')
        {
            continue;
        }
        size_t pos = line.find('=');
        if (pos != std::string::npos)
        {
            line[pos] = ' ';
        }
        std::stringstream ss(line);
        ss >> key;
        if (key == "Host")
        {
            std::string value;
            std::getline(ss, value);
            value = trim(value);
            host_found = ((value == host) || (value == "*"));
        }
        else if (host_found && (key == "IdentityFile"))
        {
            std::string value;
            std::getline(ss, value);
            value = trim(value);
            if (value[0] == '~')
            {
                value = home_dir.string() + value.substr(1, value.size() - 1);
            }
            add_when_exists(std::filesystem::path(value), identities);
        }
    }
}
