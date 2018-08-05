#ifndef PARSE_SSH_CONFIG
#define PARSE_SSH_CONFIG

#include <filesystem>
#include <vector>

struct key_pair_paths_t
{
    key_pair_paths_t(std::filesystem::path priv, std::filesystem::path publ)
        : m_priv(priv)
        , m_publ(publ)
    {}
    std::filesystem::path m_priv;
    std::filesystem::path m_publ;
};

using identities_t = std::vector<key_pair_paths_t>;

void find_identities(char const* host, identities_t& identities);

#endif // PARSE_SSH_CONFIG