/*

What this program does:
1. change mouse cursor(like xkill does)
2. wait for click (or better wait for user-defined key, like <space >
                   and let user cycle through windows with alt-tab
                   before pressing space -> avoids mouse!)
3. get pid of clicked window
4. get cwd from /proc/$pid/...
5. open shell at mousepos with cwd set to $pids cwd

# TODO
- reduce headers
- bind to a key
- xmu method really needed?

Code based on xkill.c and xprop (xprop _NET_WM_PID | cut -d' ' -f3)
*/

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xmu/WinUtil.h>
#include <X11/Xos.h>
#include <X11/Xproto.h>
#include <X11/cursorfont.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <list>
#ifdef TOTAL_REGEX_OVERLOAD
#include <regex>
#endif
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

#include "readdir.hh"

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

// one function takes a ask of type long, the other of type unsigned int..
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

#ifdef TOTAL_REGEX_OVERLOAD
inline std::vector<std::string> split(const std::string &input,
                                      const std::regex &regex)
{
    // passing -1 as the submatch index parameter performs splitting
    std::sregex_token_iterator first{input.begin(), input.end(), regex, -1};
    std::sregex_token_iterator last;
    return {first, last};
}
#endif

inline std::vector<std::string> split(const std::string &input,
                                      const std::string &delim)
{
    std::vector<std::string> tokens;
    std::string::size_type start = 0;
    std::string::size_type end;

    for(;;) {
        end = input.find(delim, start);
        tokens.push_back(input.substr(start, end - start));
        // We just copied the last token
        if(end == std::string::npos)
            break;
        // Exclude the delimiter in the next search
        start = end + delim.size();
    }

    return tokens;
}

/*
inline bool string_starts_with(const std::string &str,
                               const std::string &prefix)
{
    return std::equal(prefix.begin(), prefix.end(), str.begin());
}
*/

inline std::string readlink(const std::string &path)
{
    std::string buffer;
    buffer.resize(1024);
    if(::readlink(path.c_str(), &buffer[0], 1024) == -1)
        return std::string();

    return buffer;
}

// returns empty string on failure
std::string get_pwd_of(pid_t pid)
{
    const std::string path = "/proc/" + std::to_string(pid) + "/cwd";
    const auto cwd = readlink(path);
    std::cout << "readlink(" << path << ") -> " << cwd << "\n";
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
    using pid_type = std::vector<proc_pid_type>;
    pid_type pid;
    struct pid_cmp_type {
        bool operator()(const proc_pid_type &lhs, const proc_pid_type &rhs)
        {
            return lhs.pid > rhs.pid;
        }
    };

    bool insert_pid_type(const proc_pid_type &c)
    {
        pid_cmp_type cmp;
        auto it = std::lower_bound(std::begin(pid), std::end(pid), c, cmp);
        pid.insert(it, c);
        return true;
    }

    const pid_type::iterator get_pid(pid_t p) const
    {
        proc_pid_type tmp; tmp.pid = p;
        pid_cmp_type cmp;
        const auto it
            = std::lower_bound(std::begin(pid), std::end(pid), tmp, cmp);
        if((it != std::end(pid)) && (p < it->pid))
            return std::end(pid);
        return it;
    }

    // returns empty pid_type on failure
    pid_type get_children(pid_t p) const
    {
        const auto &pid_obj = get_pid(p);
        if(pid_obj == std::end(pid))
           return false;
        
    }
};


struct child_processes
{
    using map_entry_type = std::pair<pid_t, std::list<pid_t> >;
    using map_type = std::vector<map_entry_type>;
    map_type map;
    struct cmp_type {
        bool operator()(const map_entry_type &lhs, const map_entry_type &rhs)
        {
            return lhs.first > rhs.second;
        }
    };

    explicit child_processes(const proc_type & proc_content)
    {
        cmp_type cmp;
        for(const auto &pid_content: proc_content.pid) {
            auto it = std::lower_bound(std::begin(map), std::end(map),
                                       proc_content, cmp);
            auto & 
            if
            map.insert(it, proc_content);
        }
    }

// return empty list on failure
    std::list<pid_t> & get_children(pid_t p) const
    {
        const auto tmp = std::make_pair(p, std::list<pid_t>());
        cmp_type cmp;
        auto it
            = std::lower_bound(std::begin(map), std::end(map),
                               tmp, cmp);
        if((it == std::end(map))
           || (it != std::end(map) && (p < it->first)))
           return tmp.second;
        return it->second;
    }
};


bool proc_pid_stat(const std::string &path, proc_pid_stat_type &content)
{
    std::string file_buffer;
    const auto size = read_file(path, file_buffer);
    std::cout << "read " << size << "bytes from " << path << "\n";

    const std::string DELIM = {' '};
    const auto tokens = split(file_buffer, DELIM);
    if(tokens.empty())
        return false;
    content.pid = std::stol(tokens[0]);
    content.comm.assign(tokens[1]);
    content.state = tokens[2][0];
    content.ppid = std::stol(tokens[3]);
    content.pgrp = std::stol(tokens[4]);
    content.session = std::stol(tokens[5]);

    return false;
}

bool proc_iterate(proc_type &proc_content)
{
    auto f = [&proc_content](const std::string &base_path,
                                     const struct dirent *p) {
        /*
                std::cout << "struct dirent = { " << std::endl
                          << "\t.d_ino = " << std::to_string(p->d_ino) <<
           std::endl
                          << "\t.d_off = " << std::to_string(p->d_off) <<
           std::endl
                          << "\t.d_reclen = " << std::to_string(p->d_reclen)
                          << std::endl << "\t.d_name = \"" << p->d_name << "\""
                          << std::endl << "}" << std::endl;
        */
        const std::string path = base_path + "/" + p->d_name;
        if(isdigit(p->d_name[0])) {
            proc_pid_type pid_content;
            proc_pid_stat(path + "/stat", pid_content.stat);
            proc_content.insert_pid_type(pid_content);
        }
    };

    readdir_cxx::dir::readdir("/proc", f);
    return true;
}

/*
// returns empty string on failure
std::string get_pwd_of(pid_t pid)
{
    const std::string path = "/proc/" + std::to_string(pid) + "/environ";
    std::string file_buffer;
    const auto size = read_file(path, file_buffer);
    std::cout << "read " << size << "bytes from " << path << "\n";

#ifdef TOTAL_REGEX_OVERLOAD
    const auto tokens = split(file_buffer, std::regex(R"(('.'|'\\0'))"));
#endif
    const std::string DELIM = {'\0'};
    const auto tokens = split(file_buffer, DELIM);

    for(const auto & token: tokens) {
        const std::string PREFIX = "PWD=";
        if(string_starts_with(token, PREFIX)) {
            std::cout << "PWD-token = \"" << token << "\"\n";
            return token.substr(PREFIX.length(), token.length());
        }
    }
    return std::string();
}
*/

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

    const auto pid = get_pid(display, id);
// TODO: pid is parent terminals pid.
// -> get shell child process
// -> call readlink("/proc/<child-pid>/cwd") instead of parsing environ file

    const auto pwd = get_pwd_of(pid);
    if(pwd.empty())
        std::cerr << "get_pwd_of failed\n";

    proc_type proc_content;
    proc_iterate(proc_content);
// TODO $TERM from environ is interesting too.
// contains wrong binary name for urxvt256c -> TERM=rxvt-unicode-256color
// TODO execl("$TERM","$TERM",param,param1,(char *)0);//EDIT!!!
//  urxvt -e /bin/sh -c 'cd /tmp && /bin/bash'
    

    std::cout << "killing creator of resource " << id << "\n";
    XSync(display, 0); // give xterm a chance
    //XKillClient(display, id);
    XSync(display, 0);

    safe_exit_x11(0, display);
    return 0;
}
