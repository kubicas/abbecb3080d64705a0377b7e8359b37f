#include "repo/repo.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <memory>
#include <filesystem>
#include "git2/git2.h"
#include "parse_ssh_config.h"

namespace // anonymous
{

struct repo_impl_t
    : repo::repo_t
{
public:
    repo_impl_t(std::ostream& os, std::istream& is, repo::ask_user_pwd_t ask_pwd_user)
        : m_os(os)
        , m_is(is)
        , m_ask_pwd_user(ask_pwd_user)
    {
        git_libgit2_init();
    }
    ~repo_impl_t() override
    {
        git_libgit2_shutdown();
    }
    void get(
        repo::repo_ref_t const& repo_ref,
        char const* path,
        char const* dirname) override
    {
        if (repo::gitfile_repo_ref_t const* pref = dynamic_cast<repo::gitfile_repo_ref_t const*>(&repo_ref))
        {
            get(*pref, path, dirname);
        }
        else if (repo::gitssh_repo_ref_t const* pref = dynamic_cast<repo::gitssh_repo_ref_t const*>(&repo_ref))
        {
            get(*pref, path, dirname);
        }
        else if (repo::githttps_repo_ref_t const* pref = dynamic_cast<repo::githttps_repo_ref_t const*>(&repo_ref))
        {
            get(*pref, path, dirname);
        }
        else
        {
            throw std::logic_error("Unsupported repository reference");
        }
    }
    bool has_commit_user() override
    {
        git_config *cfg = NULL;
        std::unique_ptr<git_config, decltype(&::git_config_free)>
            cfg_guard(cfg, &::git_config_free);
        check(git_config_open_default(&cfg));
        cfg_guard.reset(cfg);
        git_buf user_name = { 0 };
        int ret = git_config_get_string_buf(&user_name, cfg, "user.name");
        git_buf_free(&user_name);
        if (ret == GIT_ENOTFOUND)
        {
            return false;
        }
        check(ret);
        return true;
    }
protected:
    void get(
        repo::gitfile_repo_ref_t const& repo_ref,
        char const* path,
        char const* dirname)
    {
        if (!repo_ref.m_host)
        {
            throw std::logic_error("No host in repository reference defined");
        }
        if (!repo_ref.m_remote_name)
        {
            throw std::logic_error("No remote name in repository reference defined");
        }
        if (!repo_ref.m_local_name)
        {
            throw std::logic_error("No local name in repository reference defined");
        }
        find_identities(repo_ref.m_host, m_identities);
        m_pidentity = m_identities.begin();
        git_repository *repo = NULL;
        std::unique_ptr<git_repository, decltype(&::git_repository_free)>
            repo_guard(repo, &::git_repository_free);
        git_clone_options clone_options = GIT_CLONE_OPTIONS_INIT;
        clone_options.fetch_opts.callbacks.transfer_progress = fetch_progress;
        clone_options.fetch_opts.callbacks.credentials = credentials_cb;
        clone_options.fetch_opts.callbacks.payload = this;
        if (repo_ref.m_commit_sha)
        {
            clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;
        }
        clone_options.checkout_opts.progress_cb = checkout_progress;
        clone_options.checkout_opts.progress_payload = this;
        clone_options.checkout_branch = repo_ref.m_branch;
        clone_options.local = GIT_CLONE_LOCAL;
        m_fetch_state = fetch_state_t::start_count_objects;
        m_checkout_state = checkout_state_t::start;
        std::filesystem::path fullpath(path);
        fullpath /= (dirname ? dirname : repo_ref.m_local_name);
        if (!std::filesystem::exists(fullpath))
        {
            m_os << "Cloning into '" << fullpath << "'..." << std::endl;
            std::stringstream url;
            // e.g. file://../../../procts_repo/git/7594fed3a30c4c7b87eb614d30e71cf9
            url << "file:///" << repo_ref.m_host << '/';
            url << (repo_ref.m_subdir ? repo_ref.m_subdir : "");
            url << repo_ref.m_remote_name;
            check(git_clone(&repo, url.str().c_str(), fullpath.string().c_str(), &clone_options));
            repo_guard.reset(repo);
        }
        else
        {
            m_os << "Fetching '" << fullpath << "'..." << std::endl;
            check(git_repository_init(&repo, fullpath.string().c_str(), false));
            repo_guard.reset(repo);
            git_remote *remote = NULL;
            std::unique_ptr<git_remote, decltype(&::git_remote_free)>
                remote_guard(remote, &::git_remote_free);
            check(git_remote_lookup(&remote, repo, "origin"));
            remote_guard.reset(remote);
            check(git_remote_fetch(remote,
                NULL, /* refspecs, NULL to use the configured ones */
                &clone_options.fetch_opts, /* options, empty for defaults */
                NULL)); /* reflog mesage, usually "fetch" or "pull", you can leave it NULL for "fetch" */
            if (!repo_ref.m_commit_sha)
            {
                git_config *snap_cfg = NULL;
                std::unique_ptr<git_config, decltype(&::git_config_free)>
                    snap_cfg_guard(snap_cfg, &::git_config_free);
                check(git_repository_config_snapshot(&snap_cfg, repo));
                snap_cfg_guard.reset(snap_cfg);
                char const* branch_master_merge = NULL;
                check(git_config_get_string(&branch_master_merge, snap_cfg, "branch.master.merge"));
                std::string refname(branch_master_merge);
                if (repo_ref.m_branch)
                {
                    size_t  last_slash = refname.find_last_of('/');
                    refname.replace(last_slash + 1, refname.length() - last_slash - 1, repo_ref.m_branch);
                }
                check(git_repository_set_head(repo, refname.c_str()));
                clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
                check(git_checkout_head(repo, &clone_options.checkout_opts));
            }
        }
        if (repo_ref.m_commit_sha)
        {
            git_oid commitish;
            check(git_oid_fromstr(&commitish, repo_ref.m_commit_sha));
            check(git_repository_set_head_detached(repo, &commitish));
            clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
            check(git_checkout_head(repo, &clone_options.checkout_opts));
        }
        m_os << "Update submodules" << std::endl;
        check(git_submodule_foreach(repo, update_submodule, this));
        if (repo_ref.m_commit_user)
        {
            git_config *cfg = NULL;
            std::unique_ptr<git_config, decltype(&::git_config_free)>
                cfg_guard(cfg, &::git_config_free);
            check(git_config_open_default(&cfg));
            cfg_guard.reset(cfg);
            git_config *global_cfg = NULL;
            std::unique_ptr<git_config, decltype(&::git_config_free)>
                global_cfg_guard(global_cfg, &::git_config_free);
            check(git_config_open_level(&global_cfg, cfg, GIT_CONFIG_LEVEL_GLOBAL));
            global_cfg_guard.reset(global_cfg);
            check(git_config_set_string(global_cfg, "user.name", repo_ref.m_commit_user));
        }
    }
    void get(
        repo::gitssh_repo_ref_t const& repo_ref,
        char const* path,
        char const* dirname)
    {
        if (!repo_ref.m_host)
        {
            throw std::logic_error("No host in repository reference defined");
        }
        if (!repo_ref.m_remote_name)
        {
            throw std::logic_error("No remote name in repository reference defined");
        }
        if (!repo_ref.m_local_name)
        {
            throw std::logic_error("No local name in repository reference defined");
        }
        find_identities(repo_ref.m_host, m_identities);
        m_pidentity = m_identities.begin();
        git_repository *repo = NULL;
        std::unique_ptr<git_repository, decltype(&::git_repository_free)>
            repo_guard(repo, &::git_repository_free);
        git_clone_options clone_options = GIT_CLONE_OPTIONS_INIT;
        clone_options.fetch_opts.callbacks.transfer_progress = fetch_progress;
        clone_options.fetch_opts.callbacks.credentials = credentials_cb;
        clone_options.fetch_opts.callbacks.payload = this;
        if (repo_ref.m_commit_sha)
        {
            clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;
        }
        clone_options.checkout_opts.progress_cb = checkout_progress;
        clone_options.checkout_opts.progress_payload = this;
        clone_options.checkout_branch = repo_ref.m_branch;
        m_fetch_state = fetch_state_t::start_count_objects;
        m_checkout_state = checkout_state_t::start;
        std::filesystem::path fullpath(path);
        fullpath /= (dirname ? dirname : repo_ref.m_local_name);
        m_user = repo_ref.m_gituser ? repo_ref.m_gituser : "git";
        if (!std::filesystem::exists(fullpath))
        {
            m_os << "Cloning into '" << fullpath << "'..." << std::endl;
            std::stringstream url;
            // e.g. git@github.com:libgit2/libgit2.git
            url << m_user << '@' << repo_ref.m_host << ':';
            url << (repo_ref.m_subdir ? repo_ref.m_subdir : "");
            url << repo_ref.m_remote_name;
            check(git_clone(&repo, url.str().c_str(), fullpath.string().c_str(), &clone_options));
            repo_guard.reset(repo);
        }
        else
        {
            m_os << "Fetching '" << fullpath << "'..." << std::endl;
            check(git_repository_init(&repo, fullpath.string().c_str(), false));
            repo_guard.reset(repo);
            git_remote *remote = NULL;
            std::unique_ptr<git_remote, decltype(&::git_remote_free)>
                remote_guard(remote, &::git_remote_free);
            check(git_remote_lookup(&remote, repo, "origin"));
            remote_guard.reset(remote);
            check(git_remote_fetch(remote,
                NULL, /* refspecs, NULL to use the configured ones */
                &clone_options.fetch_opts, /* options, empty for defaults */
                NULL)); /* reflog mesage, usually "fetch" or "pull", you can leave it NULL for "fetch" */
            if (!repo_ref.m_commit_sha)
            {
                git_config *snap_cfg = NULL;
                std::unique_ptr<git_config, decltype(&::git_config_free)>
                    snap_cfg_guard(snap_cfg, &::git_config_free);
                check(git_repository_config_snapshot(&snap_cfg, repo));
                snap_cfg_guard.reset(snap_cfg);
                char const* branch_master_merge = NULL;
                check(git_config_get_string(&branch_master_merge, snap_cfg, "branch.master.merge"));
                std::string refname(branch_master_merge);
                if (repo_ref.m_branch)
                {
                    size_t  last_slash = refname.find_last_of('/');
                    refname.replace(last_slash + 1, refname.length() - last_slash - 1, repo_ref.m_branch);
                }
                check(git_repository_set_head(repo, refname.c_str()));
                clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
                check(git_checkout_head(repo, &clone_options.checkout_opts));
            }
        }
        if (repo_ref.m_commit_sha)
        {
            git_oid commitish;
            check(git_oid_fromstr(&commitish, repo_ref.m_commit_sha));
            check(git_repository_set_head_detached(repo, &commitish));
            clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
            check(git_checkout_head(repo, &clone_options.checkout_opts));
        }
        m_os << "Update submodules" << std::endl;
        check(git_submodule_foreach(repo, update_submodule, this));
        if (repo_ref.m_commit_user)
        {
            git_config *cfg = NULL;
            std::unique_ptr<git_config, decltype(&::git_config_free)>
                cfg_guard(cfg, &::git_config_free);
            check(git_config_open_default(&cfg));
            cfg_guard.reset(cfg);
            git_config *global_cfg = NULL;
            std::unique_ptr<git_config, decltype(&::git_config_free)>
                global_cfg_guard(global_cfg, &::git_config_free);
            check(git_config_open_level(&global_cfg, cfg, GIT_CONFIG_LEVEL_GLOBAL));
            global_cfg_guard.reset(global_cfg);
            check(git_config_set_string(global_cfg, "user.name", repo_ref.m_commit_user));
        }
    }
    void get(
        repo::githttps_repo_ref_t const& repo_ref,
        char const* path,
        char const* dirname)
    {
        if (!repo_ref.m_host)
        {
            throw std::logic_error("No host in repository reference defined");
        }
        if (!repo_ref.m_remote_name)
        {
            throw std::logic_error("No remote name in repository reference defined");
        }
        if (!repo_ref.m_local_name)
        {
            throw std::logic_error("No local name in repository reference defined");
        }
        find_identities(repo_ref.m_host, m_identities);
        m_pidentity = m_identities.begin();
        git_repository *repo = NULL;
        std::unique_ptr<git_repository, decltype(&::git_repository_free)>
            repo_guard(repo, &::git_repository_free);
        git_clone_options clone_options = GIT_CLONE_OPTIONS_INIT;
        clone_options.fetch_opts.callbacks.transfer_progress = fetch_progress;
        clone_options.fetch_opts.callbacks.credentials = credentials_cb;
        clone_options.fetch_opts.callbacks.payload = this;
        if (repo_ref.m_commit_sha)
        {
            clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;
        }
        clone_options.checkout_opts.progress_cb = checkout_progress;
        clone_options.checkout_opts.progress_payload = this;
        clone_options.checkout_branch = repo_ref.m_branch;
        clone_options.local = GIT_CLONE_LOCAL;
        m_fetch_state = fetch_state_t::start_count_objects;
        m_checkout_state = checkout_state_t::start;
        std::filesystem::path fullpath(path);
        fullpath /= (dirname ? dirname : repo_ref.m_local_name);
        if (!std::filesystem::exists(fullpath))
        {
            m_os << "Cloning into '" << fullpath << "'..." << std::endl;
            std::stringstream url;
            // e.g. file://../../../procts_repo/git/7594fed3a30c4c7b87eb614d30e71cf9
            url << "https://" << repo_ref.m_host << '/';
            url << (repo_ref.m_subdir ? repo_ref.m_subdir : "");
            url << repo_ref.m_remote_name;
            check(git_clone(&repo, url.str().c_str(), fullpath.string().c_str(), &clone_options));
            repo_guard.reset(repo);
        }
        else
        {
            m_os << "Fetching '" << fullpath << "'..." << std::endl;
            check(git_repository_init(&repo, fullpath.string().c_str(), false));
            repo_guard.reset(repo);
            git_remote *remote = NULL;
            std::unique_ptr<git_remote, decltype(&::git_remote_free)>
                remote_guard(remote, &::git_remote_free);
            check(git_remote_lookup(&remote, repo, "origin"));
            remote_guard.reset(remote);
            check(git_remote_fetch(remote,
                NULL, /* refspecs, NULL to use the configured ones */
                &clone_options.fetch_opts, /* options, empty for defaults */
                NULL)); /* reflog mesage, usually "fetch" or "pull", you can leave it NULL for "fetch" */
            if (!repo_ref.m_commit_sha)
            {
                git_config *snap_cfg = NULL;
                std::unique_ptr<git_config, decltype(&::git_config_free)>
                    snap_cfg_guard(snap_cfg, &::git_config_free);
                check(git_repository_config_snapshot(&snap_cfg, repo));
                snap_cfg_guard.reset(snap_cfg);
                char const* branch_master_merge = NULL;
                check(git_config_get_string(&branch_master_merge, snap_cfg, "branch.master.merge"));
                std::string refname(branch_master_merge);
                if (repo_ref.m_branch)
                {
                    size_t  last_slash = refname.find_last_of('/');
                    refname.replace(last_slash + 1, refname.length() - last_slash - 1, repo_ref.m_branch);
                }
                check(git_repository_set_head(repo, refname.c_str()));
                clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
                check(git_checkout_head(repo, &clone_options.checkout_opts));
            }
        }
        if (repo_ref.m_commit_sha)
        {
            git_oid commitish;
            check(git_oid_fromstr(&commitish, repo_ref.m_commit_sha));
            check(git_repository_set_head_detached(repo, &commitish));
            clone_options.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
            check(git_checkout_head(repo, &clone_options.checkout_opts));
        }
        m_os << "Update submodules" << std::endl;
        check(git_submodule_foreach(repo, update_submodule, this));
        if (repo_ref.m_commit_user)
        {
            git_config *cfg = NULL;
            std::unique_ptr<git_config, decltype(&::git_config_free)>
                cfg_guard(cfg, &::git_config_free);
            check(git_config_open_default(&cfg));
            cfg_guard.reset(cfg);
            git_config *global_cfg = NULL;
            std::unique_ptr<git_config, decltype(&::git_config_free)>
                global_cfg_guard(global_cfg, &::git_config_free);
            check(git_config_open_level(&global_cfg, cfg, GIT_CONFIG_LEVEL_GLOBAL));
            global_cfg_guard.reset(global_cfg);
            check(git_config_set_string(global_cfg, "user.name", repo_ref.m_commit_user));
        }
    }
    static int update_submodule(git_submodule *sm, char const *name, void *payload)
    {
        repo_impl_t* This = static_cast<repo_impl_t*>(payload);
        This->m_pidentity = This->m_identities.begin();
        git_submodule_update_options submodule_update_options = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
        submodule_update_options.fetch_opts.callbacks.transfer_progress = fetch_progress;
        submodule_update_options.fetch_opts.callbacks.credentials = credentials_cb;
        submodule_update_options.fetch_opts.callbacks.payload = This;
        submodule_update_options.checkout_opts.progress_cb = checkout_progress;
        submodule_update_options.checkout_opts.progress_payload = This;
        This->m_fetch_state = fetch_state_t::start_count_objects;
        This->m_checkout_state = checkout_state_t::start;
        This->m_os << "Submodule '" << name << "'..." << std::endl;
        return git_submodule_update(sm, true, &submodule_update_options);
    }
    void check(int error)
    {
        if (error < 0)
        {
            const git_error *e = giterr_last();
            if (e)
            {
                std::stringstream ss;
                ss << error << '/' << e->klass << ": " << e->message;
                throw std::runtime_error(ss.str());
            }
            else if (error == GIT_EUSER)
            {
                throw std::runtime_error("User error");
            }
        }
    }
    enum class fetch_state_t
    {
        start_count_objects,
        count_objects,
        start_count_deltas,
        count_deltas,
        ready
    } m_fetch_state;
    static int fetch_progress(
        git_transfer_progress const * stats,
        void *payload)
    {
        repo_impl_t* This = static_cast<repo_impl_t*>(payload);
        switch (This->m_fetch_state)
        {
        case fetch_state_t::start_count_objects:
        {
            This->m_os << "counting objects : 0\b";
            This->m_os.flush();
            This->m_fetch_state = fetch_state_t::count_objects;
            break;
        }
        case fetch_state_t::count_objects:
        {
            unsigned int received_objects = stats->received_objects;
            This->m_os << received_objects;
            while (received_objects)
            {
                This->m_os << '\b';
                received_objects /= 10;
            }
            if (stats->received_objects == stats->total_objects)
            {
                This->m_os << stats->total_objects << ", done." << std::endl;
                This->m_fetch_state = fetch_state_t::start_count_deltas;
            }
            else
            {
                This->m_os.flush();
            }
            break;
        }
        case fetch_state_t::start_count_deltas:
        {
            This->m_os << "counting deltas : 0\b";
            This->m_os.flush();
            This->m_fetch_state = fetch_state_t::count_deltas;
            break;
        }
        case fetch_state_t::count_deltas:
        {
            unsigned int indexed_deltas = stats->indexed_deltas;
            This->m_os << indexed_deltas;
            while (indexed_deltas)
            {
                This->m_os << '\b';
                indexed_deltas /= 10;
            }
            if (stats->indexed_deltas == stats->total_deltas)
            {
                This->m_os << stats->total_deltas << ", done." << std::endl;
                This->m_fetch_state = fetch_state_t::ready;
            }
            else
            {
                This->m_os.flush();
            }
            break;
        }
        }
        return 0;
    }
    enum class checkout_state_t
    {
        start,
        count,
        ready
    } m_checkout_state;
    static void checkout_progress(
        const char *path,
        size_t cur,
        size_t tot,
        void *payload)
    {
        repo_impl_t* This = static_cast<repo_impl_t*>(payload);
        switch (This->m_checkout_state)
        {
        case checkout_state_t::start:
        {
            This->m_os << "checking out : 0\b";
            This->m_os.flush();
            This->m_checkout_state = checkout_state_t::count;
            break;
        }
        case checkout_state_t::count:
        {
            size_t current = cur;
            This->m_os << current;
            while (current)
            {
                This->m_os << '\b';
                current /= 10;
            }
            if (cur == tot)
            {
                This->m_os << tot << ", done." << std::endl;
                This->m_checkout_state = checkout_state_t::ready;
            }
            else
            {
                This->m_os.flush();
            }
        }
        }
    }
    static int credentials_cb(git_cred **out, const char *url, const char *username_from_url,
        unsigned int allowed_types, void *payload)
    {
        repo_impl_t* This = static_cast<repo_impl_t*>(payload);
        std::string user;
        std::string pass;
         
        if ((allowed_types & GIT_CREDTYPE_SSH_KEY /* = (1u << 1)*/) && (This->m_pidentity != This->m_identities.end()))
        {
            identities_t::const_iterator pidentity = This->m_pidentity++;

            This->m_os << "Authentication with " << pidentity->m_publ << std::endl;
            return git_cred_ssh_key_new(out,
                /* user name */   This->m_user,
                /* public key */  pidentity->m_publ.string().c_str(),
                /* private key */ pidentity->m_priv.string().c_str(),
                /* passphrase */  "");
        }
        else if (allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT /* = (1u << 0)*/)
        {
            This->m_os << "Authentication: user password" << std::endl;
            if (int error = This->ask_user(user, pass, url, username_from_url, allowed_types)) { return error; }
            return git_cred_userpass_plaintext_new(out, user.c_str(), pass.c_str());
        }
        else if (allowed_types & GIT_CREDTYPE_USERNAME /* = (1u << 5)*/)
        {
            This->m_os << "Authentication: username for SSH" << std::endl;
            return git_cred_username_new(out, This->m_user);
        }
        else
        {
            /* not supported:
            GIT_CREDTYPE_SSH_CUSTOM = (1u << 2),
            GIT_CREDTYPE_DEFAULT = (1u << 3),
            GIT_CREDTYPE_SSH_INTERACTIVE = (1u << 4),
            GIT_CREDTYPE_SSH_MEMORY = (1u << 6),
            */
            char errmsg[] = "Unsupported credentials type: 0000000";
            errmsg[30] = '0' + bool(allowed_types & GIT_CREDTYPE_SSH_MEMORY);
            errmsg[31] = '0' + bool(allowed_types & GIT_CREDTYPE_USERNAME);
            errmsg[32] = '0' + bool(allowed_types & GIT_CREDTYPE_SSH_INTERACTIVE);
            errmsg[33] = '0' + bool(allowed_types & GIT_CREDTYPE_DEFAULT);
            errmsg[34] = '0' + bool(allowed_types & GIT_CREDTYPE_SSH_CUSTOM);
            errmsg[35] = '0' + bool(allowed_types & GIT_CREDTYPE_SSH_KEY);
            errmsg[36] = '0' + bool(allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT);
            giterr_set_str(GITERR_SSH, errmsg);
            return GIT_EUSER;
        }
    }
    int ask_user(std::string& user, std::string& pass, char const *url, char const *username_from_url, unsigned int allowed_types)
    {
        try
        {
            m_ask_pwd_user(m_os, m_is, user, pass, url);
        }
        catch (...)
        {
            return GIT_EUSER;
        }
        return 0;
    }
    std::ostream& m_os;
    std::istream& m_is;
    repo::ask_user_pwd_t m_ask_pwd_user;
    identities_t m_identities;
    identities_t::const_iterator m_pidentity;
    char const* m_user;
};

}; // namespace anonymous

namespace repo
{

std::unique_ptr<repo_t> create_repo(std::ostream& os, std::istream& is, ask_user_pwd_t ask_pwd_user)
{
    return std::make_unique<repo_impl_t>(os, is, ask_pwd_user);
}

}; // namespace repo
