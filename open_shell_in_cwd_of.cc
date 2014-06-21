/*
Code based on xkill.c

What this program does:
1. change mouse cursor(like xkill does)
2. wait for click
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

    std::cout << "Select " << msg << " with ";
    if(button == -1)
        std::cout << "any button";
    else
        std::cout << "button " << button;
    std::cout << "....\n";
    XSync(display, 0); /* give xterm a chance */

    if(XGrabPointer(display, root, False, MASK, GrabModeSync, GrabModeAsync, None,
                    cursor, CurrentTime) != GrabSuccess)
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
        } /* end switch */
    }     /* end for */

    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);
    XSync(display, 0);

    return ((button == -1 || retbutton == button) ? retwin : None);
}
}

int main(int argc, char *argv[])
{
    Display *display = NULL;
    XID id = None;            /* resource to kill */

    display = XOpenDisplay(nullptr);
    if(!display)
        safe_exit_x11(1, display, std::string("unable to open display \"")
                                  + std::string(XDisplayName(nullptr))
                                  + std::string("\"\n"));

    // choose a window
    unsigned char pointer_map[256]; /* 8 bits of pointer num */
    if(XGetPointerMapping(display, pointer_map, 256) <= 0)
        safe_exit_x11(1, display, "no pointer mapping, can't select window");

    // select first button
    int button = (int)((unsigned int)pointer_map[0]);

    const int screenno = DefaultScreen(display);
    if((id = get_window_id(display, screenno, button,
                           "the window whose client you wish to kill"))) {
        if(id == RootWindow(display, screenno))
            id = None;
        else {
            std::cout << "1. " << id << "\n";
            XID indicated = id;
            if((id = XmuClientWindow(display, indicated)) == indicated) {
                safe_exit_x11(1, display, "Wrong selection.");
            }
        }
    }

    if(id == None)
        safe_exit_x11(1, display, "No window found.");
    std::cout << "killing creator of resource " << id << "\n";
    XSync(display, 0); /* give xterm a chance */
    XKillClient(display, id);
    XSync(display, 0);

    safe_exit_x11(0, display);
    return 0;
}
