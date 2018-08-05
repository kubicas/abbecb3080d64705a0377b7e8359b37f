#ifndef REPO_PLATFORM_SPECIFIC
#define REPO_PLATFORM_SPECIFIC

namespace repo
{

void set_stdin_echo(bool enable);
char const* get_username();

}; // namespace repo

#endif // REPO_PLATFORM_SPECIFIC