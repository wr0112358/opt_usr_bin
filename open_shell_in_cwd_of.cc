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

add keybinding under lxde:
  $ emacs -nw ~/.config/openbox/lxde-rc.xml
  ...
  <keybind key="C-1">
      <action name="Execute">
          <command>/opt/usr/bin/open_shell_in_cwd_of</command>
      </action>
  </keybind>
  ...
  $ openbox-lxde --reconfigure

What this program does:
1. change mouse cursor(like xkill does)
2. wait for click
3. get pid of clicked window
4. get cwd from procfs
5. open shell at mousepos with cwd set to $pids cwd

# TODO
- see TODOs in code
- move all non app-specific functions to lib
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
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>
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

    XSync(display, False); // give xterm a chance
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
    std::cout << "Found pid " << pid << "\n";

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
    do {
        const auto &child_list = child_searcher.get(pid_of_interest);

        {
            std::cout << pid_of_interest << ":";
            for(const auto &c: child_list)
                std::cout << " " << c;
            std::cout << "\n";
        }

        if(child_list.size() != 1)
            return pid_of_interest;
        pid_of_interest = child_list.back();
    } while(true);
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
}

int main(int argc, char *argv[])
{
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

    // TODO:xmu method really needed?
    // The XmuClientWindow function finds a window, at or below the specified
    // window, that has a WM_STATE property. If such a window is found, it is
    // returned; otherwise the argument window is returned.
    //
    // WM_STATE is set by the window manager when a toplevel window is first
    // mapped (or perhaps earlier), and then kept up-to-date. Generally no
    // WM_STATE property or a WM_STATE set to WithdrawnState means the window
    // manager is not managing the window, or not yet doing so.
    const XID indicated = id;
    if((id = XmuClientWindow(display, indicated)) == indicated)
        safe_exit_x11(1, display, "Wrong selection.");

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


    std::array<std::string, 6> cpp_args(
        {{"/bin/urxvt256c", "-e", "/bin/bash", "-c",
          "cd " + pwd + " && /bin/bash"}});
    char * const args[] = {
        const_cast<char *const>(cpp_args[0].c_str()),
        const_cast<char *const>(cpp_args[1].c_str()),
        const_cast<char *const>(cpp_args[2].c_str()),
        const_cast<char *const>(cpp_args[3].c_str()),
        const_cast<char *const>(cpp_args[4].c_str()),
        NULL
    };

    const pid_t fork_pid = fork();
    if(fork_pid == -1)
        perror("fork error");
    else if(fork_pid == 0)
        if(execv("/bin/urxvt256c", args))
            perror("execv");

    safe_exit_x11(0, display);
    return 0;
}
