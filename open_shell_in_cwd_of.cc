/*
Code based on xkill.c

What this program does:
1. change mouse cursor(like xkill does)
2. wait for click (or better wait for user-defined key, like <space >
                   and let user cycle through windows with alt-tab
                   before pressing space -> avoids mouse!)
3. get pid of clicked window
4. get cwd from /proc/$pid/...
5. open shell at mousepos with cwd set to $pids cwd

# TODO
- xkill does something very similar
- bind to a key

- xprop does it too:
xprop _NET_WM_PID | cut -d' ' -f3

*/

#include <iostream>
#include <cstdlib>
#include <cctype>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>
#include <X11/Xmu/WinUtil.h>

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
    (void)pid;
    std::cout << "killing creator of resource " << id << "\n";
    XSync(display, 0); // give xterm a chance
    XKillClient(display, id);
    XSync(display, 0);

    safe_exit_x11(0, display);
    return 0;
}
