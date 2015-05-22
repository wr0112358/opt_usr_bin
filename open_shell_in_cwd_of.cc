/*
Copyright (C) 2014 Reiter Wolfgang wr0112358@gmail.com

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
Open a shell window with pwd set to working dir of current window.

Usecase
  Working in a shell window with emacs the need for an additional shell
  instance set to the same pwd regularly arises.


add keybindings
===============

lxde:
  $ emacs -nw ~/.config/openbox/lxde-rc.xml
  ...
  <keybind key="C-1">
      <action name="Execute">
          <command>/opt/usr/bin/open_shell_in_cwd_of --child-id --urxvt256c</command>
      </action>
  </keybind>
  ...
  $ openbox-lxde --reconfigure

awesome wm:
    awful.key({ ctrlkey, }, "1", function () awful.util.spawn("/opt/usr/bin/open_shell_in_cwd_of --child-id --urxvt256c") end),

xmonad:
... , ((modm .|. controlMask, xK_1), spawn "eval \"exec /opt/usr/bin/open_shell_in_cwd_of  --child-id --gnome_terminal\"") ...


For gnome-terminal, a different window identification mode must be used.
Keyboard shortcut can be set in gnome-control-center.
./open_shell_in_cwd_of --wm_name-id --gnome_terminal

What this program does:
1. change mouse cursor(like xkill does)
2. wait for click
3. get pid of clicked window
4. get cwd from procfs
5. open shell at mousepos with cwd set to $pids cwd

# TODO

- see TODOs in code
- instead of mouse-click better wait for user-defined key(maybe <space>) and
  let user cycle through windows with alt-tab before keypress.
  -> avoids mouse!
- make magic values like urxvt256 easier replacable

Code based on xkill.c and xprop (xprop _NET_WM_PID | cut -d' ' -f3)
*/

#include <X11/Xatom.h> //XA_CARDINAL
#include <X11/Xlib.h>
#include <X11/Xmu/WinUtil.h>
#include <X11/cursorfont.h> //XC_pirate
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <stack>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h> // fork/execv
#include <vector>

#include "libaan/file_util.hh"
#include "libaan/string_util.hh"

namespace {
void _X_NORETURN safe_exit_x11(int code, Display *display,
                               const std::string &error_msg = std::string())
{
    if(code && !error_msg.empty())
        std::cerr << error_msg << "\n";
    if(display) {
        XCloseDisplay(display);
    }
    exit(code);
}

XID get_window_id(Display *display, int screen, int button, const char *msg)
{
    Cursor cursor;        /* cursor to use when selecting */
    Window root;          /* the current root */
    Window retwin = None; /* the window that got selected */
    int retbutton = -1;   /* button used to select window */
    int pressed = 0;      /* count of number of buttons pressed */

// one function takes a mask of type long, the other of type unsigned int..
#define MASK (ButtonPressMask | ButtonReleaseMask)

    root = RootWindow(display, screen);
    cursor = XCreateFontCursor(display, XC_pirate);
    if(cursor == None)
        safe_exit_x11(1, display, "unable to create selection cursor");

    // give xterm a chance
    XSync(display, False);

    if(XGrabPointer(display, root, False, MASK, GrabModeSync, GrabModeAsync,
                    None, cursor, CurrentTime) != GrabSuccess)
        safe_exit_x11(1, display, "unable to grab cursor");

    while(retwin == None || pressed != 0) {
        XEvent event;
        XAllowEvents(display, SyncPointer, CurrentTime);
        XWindowEvent(display, root, MASK, &event);
        switch(event.type) {
        case ButtonPress:
            if(retwin == None) {
                retbutton = event.xbutton.button;
                retwin = ((event.xbutton.subwindow != None)
                              ? event.xbutton.subwindow
                              : root);
            }
            pressed++;
            continue;
        case ButtonRelease:
            if(pressed > 0)
                pressed--;
            continue;
        }
    }

    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);
    XSync(display, 0);

    return ((button == -1 || retbutton == button) ? retwin : None);
}

// returns 0 on failure
unsigned long get_pid(Display *display, Window window)
{
    Atom pid_atom = XInternAtom(display, "_NET_WM_PID", True);
    if(pid_atom == None) {
        std::cerr << "_NET_WM_PID Atom not found.\n";
        return 0;
    }

    Atom type;
    int format;
    unsigned long items_count;
    unsigned long bytes_after;
    unsigned char *prop = 0;
    unsigned long pid = 0;
    if((XGetWindowProperty(display, window, pid_atom, 0, 1, False, XA_CARDINAL,
                           &type, &format, &items_count, &bytes_after, &prop)
        != Success) || !prop)
        return 0;

    pid = *((unsigned long *)prop);
    XFree(prop);
    std::cout << "Found pid " << pid << " corresponding to window " << window << "\n";

    return pid;
}

inline size_t read_file(const std::string &file_name, std::string &buff)
{
    std::ifstream fp(file_name);
    buff.resize(4096);
    char *begin = &*buff.begin();
    fp.read(begin, 4096);

    return fp.gcount();
}

// it is assumed, we have access permissions to path.
inline std::string readlink(const std::string &path)
{
    std::string buffer;
    buffer.resize(1024);
    const ssize_t byte_count = ::readlink(path.c_str(), &buffer[0], 1024);
    if(byte_count == -1)
        return std::string();

    // readlink(3) not necessarily returns a null-terminated string. Use
    // byte_count to determine length.
    buffer.resize(byte_count);
    return buffer;
}

// returns empty string on failure
std::string get_pwd_of(pid_t pid)
{
    const std::string path = "/proc/" + std::to_string(pid) + "/cwd";
    const auto cwd = readlink(path);
    return cwd;
}

struct proc_pid_stat_type
{
    pid_t pid;
    std::string comm;
    char state; //enum { R, S, D, Z, T, W } state;
    pid_t ppid;
    pid_t pgrp;
    pid_t session;
};

struct proc_pid_type
{
    pid_t pid;
    proc_pid_stat_type stat;
};

struct proc_type
{
    // or better a std::set, with custom compare over proc_pid_type.pid?
    using pid_type = std::map<pid_t, proc_pid_type>;
    pid_type pid;

    bool insert_pid_type(const proc_pid_type &c)
    {
        const auto ret = pid.insert(std::make_pair(c.pid, c));
        if(!ret.second)
            std::cerr << "insert_pid_type failed for pid = " << c.pid << ".\n";
        return ret.second;
    }

    const pid_type::const_iterator get_pid(pid_t p) const
    {
        const auto ret = pid.find(p);
        if(ret == std::end(pid))
            std::cerr << "get_pid failed.\n";
        return ret;
    }
};

// given an initialized object of type proc_type, this class constructs
// a list of child processes for 
class child_processes
{
private:
    using map_type = std::map<pid_t, std::list<pid_t> >;
    map_type map;
    const std::list<pid_t> empty_list;

public:
    // TODO: number of entries is known..
    child_processes(const proc_type &proc_content)
    {
        for(const auto &pid_content: proc_content.pid)
            map[pid_content.second.stat.ppid].push_back(
                pid_content.second.stat.pid);
    }

    // return empty list on failure
    const std::list<pid_t> & get(pid_t p) const
    {
        const auto ret = map.find(p);
        if(ret == std::end(map)) {
            std::cerr << "child_processes::get: " << p
                      << " has no children. Stopping search.\n";
            return empty_list;
        }
        return ret->second;
    }
};

std::string sstrerror(int last_errno)
{
    std::string buf(128, 0);
    auto ret = strerror_r(last_errno, &buf[0], buf.size());
    if(!ret)
        return "unknown error.";
    if(ret != &buf[0])
        buf.assign(ret);

    return buf;
}

// TODO: if user clicks on shell running a root shell. readlink will fail on
//       the returned pid.
//       Provide switch to turn on access rights check.
pid_t get_last_child_pid_before_branch(const proc_type &proc_content,
                                       pid_t ppid)
{
    // - needed for stuff like:
    //     firefox(why should I need that)
    //     many subshells
    const child_processes c(proc_content);
    const child_processes &child_searcher = c;
    auto pid_of_interest = ppid;
    std::stack<pid_t> ancestors;
    do {
        ancestors.push(pid_of_interest);
        const auto child_list = child_searcher.get(pid_of_interest);

        {
            std::cout << pid_of_interest << ":";
            for(const auto &c: child_list)
                std::cout << " " << c;
            std::cout << "\n";
        }

        if(child_list.size() != 1)
            break;
        pid_of_interest = child_list.back();
    } while(true);

    // iterate back over all ancestors to find first acccessible.
    do {
        pid_of_interest = ancestors.top();
        struct stat s;
        if(::stat(std::string("/proc/" + std::to_string(pid_of_interest) + "/cwd").c_str(), &s) == 0) {
            std::cout << "stating cwd of " << pid_of_interest << " suceeded.\n";
            break;
        } else {
            auto last_errno = errno;
            std::cout << "stating cwd of " << pid_of_interest << " failed with error: " << sstrerror(last_errno) << "\n";
        }
        ancestors.pop();
    } while(!ancestors.empty());
    return pid_of_interest;
}

bool proc_pid_stat(const std::string &path, proc_pid_stat_type &content)
{
    std::string file_buffer;
    read_file(path, file_buffer);
    const std::string DELIM = {' '};
    const auto tokens = libaan::util::split2(file_buffer, DELIM);
    if(tokens.empty())
        return false;

    content.pid = std::strtol(tokens[0].first, nullptr, 10);
    if(content.pid == 0)
        return false;
    content.comm.assign(
        std::string(tokens[1].first, tokens[1].first + tokens[1].second));
    content.state = tokens[2].first[0];
    content.ppid = std::strtol(tokens[3].first, nullptr, 10);
    content.pgrp = std::strtol(tokens[4].first, nullptr, 10);
    content.session = std::strtol(tokens[5].first, nullptr, 10);

    return true;
}

bool proc_iterate(proc_type &proc_content)
{
    auto callback = [&proc_content](const std::string &base_path,
                                     const struct dirent *p) {
        const std::string path = base_path + "/" + p->d_name;
        if(isdigit(p->d_name[0])) {
            proc_pid_type pid_content;
            if(!proc_pid_stat(path + "/stat", pid_content.stat))
                return;
            pid_content.pid = pid_content.stat.pid;
            proc_content.insert_pid_type(pid_content);
        }
    };

    libaan::util::file::dir::readdir("/proc", callback);
    return true;
}

void usage(const char *argv0)
{
    std::cout << "Usage: " << argv0 << " <window-identification-mode> <optional-shell-type>\n"
              << "\twindow-identification-mode: one of --wm_name-id | --child-id\n"
              << "\toptional-shell-type: one of --urxvt256c | --gnome_terminal | --xterm | --lxterminal\n";
}

enum shelltype {
    URXVT256C, GNOME_TERMINAL, XTERM, LXTERMINAL
};

std::string get_gnome_wm_name_path(Display *display, XID window_id)
{
    Atom name_atom = XInternAtom(display, "WM_NAME", True);
    if(name_atom == None) {
        std::cerr << "WM_NAME Atom not found.\n";
        return "";
    }

    Atom string_atom = XInternAtom(display, "STRING", True);
    if(string_atom == None) {
        std::cerr << "STRING Atom not found.\n";
        return "";
    }

    Atom type;
    int format;
    unsigned long items_count;
    unsigned long bytes_after;
    unsigned char *prop = (unsigned char *)1313;
    const long long_length = 1024;
    const auto r = XGetWindowProperty(display, window_id, name_atom, 0,
                                        long_length, False, string_atom,
                                        &type, &format, &items_count,
                                        &bytes_after, &prop);
    if(r != Success) {
        std::cerr << "get_gnome_wm_name_path: XGetWindowProperty failed(" << r << ").\n";
        return "";
    }
    if(!prop) {
        std::cerr << "get_gnome_wm_name_path: XGetWindowProperty returned wrong prop. ("
                  "items_count=" << items_count << ", bytes_after=" << bytes_after
                  << ")(" << r << ") prop = \"" << (long)prop << "\", &prop = \"" << (long)&prop << "\"\n";

        // retry for _NET_WM_NAME
        name_atom = XInternAtom(display, "_NET_WM_NAME", True);
        if(name_atom == None)
            return "";
        string_atom = XInternAtom(display, "UTF8_STRING", True);
        if(string_atom == None)
            return "";
        const auto rr = XGetWindowProperty(display, window_id, name_atom, 0,
                                           long_length, False, string_atom,
                                           &type, &format, &items_count,
                                           &bytes_after, &prop);
        if(rr != Success) {
            std::cout << "get_gnome_wm_name_path: retry failed\n";
            return "";
        }
        if(!prop) {
            std::cout << "get_gnome_wm_name_path: retry returned null\n";
            return "";
        }
    }

    // https://people.gnome.org/~tthurman/bugs/same.c
    auto size = (format / 8) * items_count;
    if(format == 32)
        size *= sizeof(long) / 4;

    std::cout << "XGetWindowProperty -> prop = \"" << prop << "\", items_count = " << items_count
              << ", bytes_after = " << bytes_after << ", size = " << size << "\n";

    char *prop_c = reinterpret_cast<char *>(prop);
    auto tmp = std::strstr(prop_c, ":");
    if(!tmp || (tmp + 1) == '\0')
        return "";
    const std::string ret(tmp + 1);
    XFree(prop);
    std::cout << "Found path " << ret << " corresponding to window " << window_id << "\n";
    return ret;
}

bool execute(char * const *args)
{
    const pid_t fork_pid = fork();
    if(fork_pid == -1) {
        perror("fork error");
        return false;
    } else if(fork_pid == 0) {
        if(execv(args[0], args)) {
            perror("execv");
            return false;
        }
    }
    return true;
}

bool open_shell_in(const std::string &pwd, shelltype shell)
{
    if(shell == URXVT256C) {
        std::array<std::string, 11> cpp_args(
            {{"/bin/urxvt256c",
                        "-bg", "black",
                        "-fg", "white",
                        "-cr", "white",
                        "-e", "/bin/bash",
                        "-c",
                        "cd " + pwd + " && /bin/bash"}});
        char * const args[] = {
            const_cast<char *const>(cpp_args[0].c_str()),
            const_cast<char *const>(cpp_args[1].c_str()),
            const_cast<char *const>(cpp_args[2].c_str()),
            const_cast<char *const>(cpp_args[3].c_str()),
            const_cast<char *const>(cpp_args[4].c_str()),
            const_cast<char *const>(cpp_args[5].c_str()),
            const_cast<char *const>(cpp_args[6].c_str()),
            const_cast<char *const>(cpp_args[7].c_str()),
            const_cast<char *const>(cpp_args[8].c_str()),
            const_cast<char *const>(cpp_args[9].c_str()),
            const_cast<char *const>(cpp_args[10].c_str()),
            NULL
        };

        return execute(args);
    } else if(shell == GNOME_TERMINAL) {
        std::array<std::string, 3> cpp_args(
            {{"/usr/bin/gnome-terminal",
                        "-e",
                        "/bin/bash -c 'cd " + pwd + " && /bin/bash'"}});
        char * const args[] = {
            const_cast<char *const>(cpp_args[0].c_str()),
            const_cast<char *const>(cpp_args[1].c_str()),
            const_cast<char *const>(cpp_args[2].c_str()),
            NULL
        };

        return execute(args);
    } else if(shell == XTERM) {
        std::array<std::string, 3> cpp_args(
            {{"/usr/bin/xterm",
                        "-e",
                        "/bin/bash -c 'cd " + pwd + " && /bin/bash'"}});
        char * const args[] = {
            const_cast<char *const>(cpp_args[0].c_str()),
            const_cast<char *const>(cpp_args[1].c_str()),
            const_cast<char *const>(cpp_args[2].c_str()),
            NULL
        };

        return execute(args);
    } else if(shell == LXTERMINAL) {
        std::array<std::string, 3> cpp_args(
            {{"/bin/lxterminal",
                        "-e",
                        "/bin/bash -c 'cd " + pwd + " && /bin/bash'"}});
        char * const args[] = {
            const_cast<char *const>(cpp_args[0].c_str()),
            const_cast<char *const>(cpp_args[1].c_str()),
            const_cast<char *const>(cpp_args[2].c_str()),
            NULL
        };

        return execute(args);
    }
    return true;
}

}

int main(int argc, char *argv[])
{
    if(argc != 2 && argc != 3) {
        usage(argv[0]);
        return -1;
    }

/* old child-id mode does not work for gnome-terminal since there is only one
   instance of it around. but gnome-terminal sets WM_NAME to:
   WM_NAME(STRING) = "user@host:/path/of/interest"


|-gnome-terminal-(13118)-+-bash(13122)
 |                        |-bash(16984)---emacs(25908)---{gmain}(25909)
 |                        |-bash(26022)---emacs(27684)---{gmain}(27685)
 |                        |-bash(27703)-+-less(30159)
 |                        |             `-pstree(30158)
 |                        |-gnome-pty-helpe(13121)
 |                        |-{dconf worker}(13120)
 |                        |-{gdbus}(13119)
 |                        `-{gmain}(13123)
*/
    enum window_id_mode {
        WM_NAME_ID, CHILD_ID
    } wid_mode;
    const std::string a1(argv[1]);
    if(a1 == "--wm_name-id") {
        wid_mode = WM_NAME_ID;
    } else if(a1 == "--child-id") {
        wid_mode = CHILD_ID;
    } else {
        usage(argv[0]);
        return -1;
    }

    shelltype shell = GNOME_TERMINAL;
    if(argc ==3) {
        const std::string a2(argv[2]);
        if(a2 == "--urxvt256c") {
            shell = URXVT256C;
        } else if(a2 == "--gnome_terminal") {
            shell = GNOME_TERMINAL;
        } else if(a2 == "--xterm") {
            shell = XTERM;
        } else if(a2 == "--lxterminal") {
            shell = LXTERMINAL;
        } else {
            usage(argv[0]);
            return -1;
        }
    }

    Display *display = XOpenDisplay(nullptr);
    if(!display)
        safe_exit_x11(1, display, std::string("unable to open display \"")
                                  + std::string(XDisplayName(nullptr))
                                  + std::string("\"\n"));

    // choose a window
    unsigned char pointer_map[256];
    if(XGetPointerMapping(display, pointer_map, 256) <= 0)
        safe_exit_x11(1, display, "no pointer mapping, can't select window");

    // select first button
    const unsigned int button = pointer_map[0];
    const int screenno = DefaultScreen(display);
    XID id = get_window_id(display, screenno, button,
                           "the window whose client you wish to kill");
    if(id == None)
        safe_exit_x11(1, display, "No window found.");
    if(id == RootWindow(display, screenno))
        safe_exit_x11(1, display, "Root window is ignored.");

    std::cout << "Trying window id: " << id << '\n';

    // gnome-terminal makes it easy for us:
    if(wid_mode == WM_NAME_ID) {
        const auto pwd = get_gnome_wm_name_path(display, id);
        if(pwd.empty())
            safe_exit_x11(1, display, "get_gnome_wm_name_path failed");
        if(!open_shell_in(pwd, shell))
            safe_exit_x11(1, display,
                          std::string("open_shell_in(" + pwd
                                      + "GNOME_TERMINAL) failed").c_str());
        safe_exit_x11(0, display);
    }

    // CHILD_ID mode starting here.
    // the pid of the containing window
    const auto window_pid = get_pid(display, id);

    proc_type proc_content;
    proc_iterate(proc_content);
    const child_processes children(proc_content);

    {
        std::cout << window_pid << ":";
        const auto &child_list = children.get(window_pid);
        for(const auto &c: child_list)
            std::cout << " " << c;
        std::cout << "\n";
    }

    std::cout << "\n#######################################################\n"
              << "Trying new algorithm:\n";
    const auto pid_of_interest
        = get_last_child_pid_before_branch(proc_content, window_pid);
    std::cout << "\n#######################################################\n"
              << "-> " << pid_of_interest << "\n"
              << "getpid(" << argv[0] << ") = " << getpid() << "\n";

    const auto pwd = get_pwd_of(pid_of_interest);
    if(pwd.empty())
        std::cerr << "get_pwd_of failed\n";
    std::cout << "Using parameters:\n\twindow_pid: " << window_pid
              << "\n\tchild_pid: " << pid_of_interest
              << "\n\tdirectory: " << pwd << "\n";


    if(!open_shell_in(pwd, shell))
        safe_exit_x11(1, display,
                      std::string("open_shell_in(" + pwd
                                  + "URXVT256C) failed").c_str());
    safe_exit_x11(0, display);
    return 0;
}
