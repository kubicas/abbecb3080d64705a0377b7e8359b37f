#include "platform_specific.h"

#ifdef _WIN32
#include <windows.h>
#include <Lmcons.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace repo
{

void set_stdin_echo(bool enable)
{
#ifdef _WIN32
    HANDLE hstdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hstdin, &mode);
    if (!enable)
    {
        mode &= ~ENABLE_ECHO_INPUT;
    }
    else
    {
        mode |= ENABLE_ECHO_INPUT;
    }
    SetConsoleMode(hstdin, mode);
#else
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if (!enable)
    {
        tty.c_lflag &= ~ECHO;
    }
    else
    {
        tty.c_lflag |= ECHO;
    }
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty);
#endif
}

char const* get_username()
{
#ifdef _WIN32
    static char username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;
    GetUserNameA(username, &username_len);
    return username;
#else
#endif
}

}; // namespace repo
