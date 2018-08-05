#include "repo/repo.h"
#include "platform_specific.h"
#include <iostream>
#include <string>
#include <filesystem>

// projects and/or products are called 'procts'
#define PROCTS "procts"
#define FLYING_START "flying_start"

namespace // anonymous
{

std::filesystem::path get_path(int argc, char const* argv[])
{
    if (argc < 1)
    {
        throw std::runtime_error("Cannot obtain executable path");
    }
    std::filesystem::path path(argv[0]);
    path = std::filesystem::canonical(path);
    std::string filename = path.filename().stem().string();
    std::string flying_start = FLYING_START "_";
    if(filename.substr(0, flying_start.size()) != flying_start)
    {
        throw std::runtime_error("Illegal name of '" FLYING_START "' executable");
    }
    path.remove_filename();
	path = path.parent_path();
    if (path.filename().string() != TARGET_DIR)
    {
        throw std::runtime_error("Illegal executable path (1). Flying start application must be executed from '.../" PROCTS "/" FLYING_START "/" TARGET_DIR "/'");
    }
    path.remove_filename();
	path = path.parent_path();
	if (path.filename().string() != FLYING_START)
    {
        throw std::runtime_error("Illegal executable path (2). Flying start application must be executed from '.../" PROCTS "/" FLYING_START "/" TARGET_DIR "/'" );
    }
    path.remove_filename();
	path = path.parent_path();
	if (path.filename().string() != PROCTS)
    {
        throw std::runtime_error("Illegal executable path (3). Flying start application must be executed from '.../" PROCTS "/" FLYING_START "/" TARGET_DIR "/'");
    }
    return path;
}

void ask_commit_user(std::ostream& os, std::istream& is, std::string& commit_user)
{
    os << "For identification of your commits," << std::endl;
    os << "please enter your full name, e.g. John Doe: ";
    std::getline(std::cin, commit_user);
}

void ask_user_pwd(std::ostream& os, std::istream& is, std::string& user, std::string& pass, char const *url)
{
    os << "For: " << url << std::endl;
    os << "Please enter user name: ";
    is >> user;
    os << "Please enter password: ";
    repo::set_stdin_echo(false);
    is >> pass;
    repo::set_stdin_echo(true);
    os << std::endl;
}

void cppmake(std::filesystem::path path, std::string const& stem)
{
    /* this is fake!
       this is to be replaced by a hardcoded compile script which compiles:
        comp/cppmake/executor.cpp
        comp/cppmake/process.cpp
        comp/cppmake/makeables.cpp
        comp/cppmake/policy.cpp
        comp/cppmake/target.cpp
        comp/cppmake/depgraph.cpp
        comp/cppmake/ide_filter.cpp
        comp/cppmake/path.cpp
       then links this to the cppmake.lib
       then compiles all makefile.cpp files which are present under comp
       then links these makefile.obj with cppmake.lib to makefile.dll
       then imports makefile.dll
       then calls the execute(...) function in the makefile.dll

       Then we no longer need the (platform dependent) make scripts
       And with some additional effort we can get rid of the microsoft
       visual studio solution and project files, as we can generate them.
       */
    path /= stem;
    std::filesystem::current_path(path);
	std::filesystem::path tgt = path / "tgt";
	if (std::filesystem::exists(tgt))
	{
		std::cout << "Skip building repository '" << stem << "', because a 'tgt' subdirectory has been spotted." << std::endl;
	}
	else
	{
		std::cout << "Build stem repository '" << stem << "'" << std::endl;
		std::system("make.cmd");
	}
}

void flying_start(
    repo::repo_t& repo,
    repo::gitfile_repo_ref_t& git_repo_ref,
    std::filesystem::path const& path,
    char const* remote_name,
    char const* local_name)
{
    git_repo_ref.m_remote_name = remote_name;
    git_repo_ref.m_local_name = local_name;
    repo.get(git_repo_ref, path.string().c_str(), nullptr);
}

void flying_start(
    repo::repo_t& repo,
    repo::githttps_repo_ref_t& git_repo_ref,
    std::filesystem::path const& path,
    char const* remote_name,
    char const* local_name)
{
    git_repo_ref.m_remote_name = remote_name;
    git_repo_ref.m_local_name = local_name;
    repo.get(git_repo_ref, path.string().c_str(), nullptr);
}

void flying_start(
    repo::repo_t& repo, 
    repo::gitssh_repo_ref_t& git_repo_ref, 
    std::filesystem::path const& path,
    char const* remote_name,
    char const* local_name)
{
    git_repo_ref.m_gituser = repo::get_username();
    git_repo_ref.m_remote_name = remote_name;
    git_repo_ref.m_local_name = local_name;
    repo.get(git_repo_ref, path.string().c_str(), nullptr);
}

}; // anonymous

namespace repo {

void flying_start(std::vector<repo::repository_t> const& repositories, int argc, char const* argv[])
{
    try
    {
        std::filesystem::path path = get_path(argc, argv);
        std::filesystem::current_path(path);
        std::unique_ptr<repo::repo_t> prepo = repo::create_repo(std::cout, std::cin, ask_user_pwd);
        std::string commit_user;
#if REPO_ARCHIVE_TYPE == REPO_ARCHIVE_USB
        repo::gitfile_repo_ref_t git_repo_ref;
#elif REPO_ARCHIVE_TYPE == REPO_ARCHIVE_GITHUB_HTTPS
        repo::githttps_repo_ref_t git_repo_ref;
#elif REPO_ARCHIVE_TYPE == REPO_ARCHIVE_GITHUB_SSH
        repo::gitssh_repo_ref_t git_repo_ref;
#endif
        if (!prepo->has_commit_user())
        {
            ask_commit_user(std::cout, std::cin, commit_user);
            git_repo_ref.m_commit_user = commit_user.c_str();
        }
        for (repo::repository_t const& repository : repositories)
        {
            git_repo_ref.m_host = repository.m_host;
            git_repo_ref.m_subdir = repository.m_subdir;
            ::flying_start(*prepo, git_repo_ref, path, repository.m_remote, repository.m_local);
        }
        for (repo::repository_t const& repository : repositories)
        {
            cppmake(path, repository.m_local);
        }
        std::cout << "done" << std::endl;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
    }
    std::cout << "Press enter to exit\n";
    std::cin.sync();
    std::cin.ignore();
}

}; // namespace repo
