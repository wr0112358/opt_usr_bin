/*
What this program should do:
1. change mouse cursor(like xkill does)
2. wait for click
3. get pid of clicked window
4. get cwd from /proc/$pid/...
5. open shell at mousepos with cwd set to $pids cwd!

# HOWTO
- xkill does something very similar
- bind to a key

- xprop does it too:
xprop _NET_WM_PID | cut -d' ' -f3

*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>
#include <X11/Xmu/WinUtil.h>

static char *ProgramName;

#define SelectButtonAny (-1)
#define SelectButtonFirst (-2)

static XID get_window_id(Display *dpy, int screen, int button, const char *msg);
static Bool wm_state_set(Display *dpy, Window win);
static Bool wm_running(Display *dpy, int screenno);

static void _X_NORETURN Exit(int code, Display *dpy)
{
    if(dpy) {
        XCloseDisplay(dpy);
    }
    exit(code);
}

int main(int argc, char *argv[])
{
    Display *dpy = NULL;
    char *displayname = NULL; /* name of server to contact */
    XID id = None;            /* resource to kill */
    int button;               /* button number or negative for all */

    ProgramName = argv[0];
    button = SelectButtonFirst;

    dpy = XOpenDisplay(displayname);
    if(!dpy) {
        fprintf(stderr, "%s:  unable to open display \"%s\"\n", ProgramName,
                XDisplayName(displayname));
        Exit(1, dpy);
    }
    const int screenno = DefaultScreen(dpy);

    // if no id was given, we need to choose a window
    if(id == None) {
        unsigned char pointer_map[256]; /* 8 bits of pointer num */
        int count, j;
        unsigned int ub = (unsigned int)button;

        count = XGetPointerMapping(dpy, pointer_map, 256);
        if(count <= 0) {
            fprintf(stderr, "%s:  no pointer mapping, can't select window\n",
                    ProgramName);
            Exit(1, dpy);
        }

        if(button >= 0) { /* check button */
            for(j = 0; j < count; j++) {
                if(ub == (unsigned int)pointer_map[j])
                    break;
            }
            if(j == count) {
                fprintf(stderr, "%s:  no button number %u in pointer map, "
                                "can't select window\n",
                        ProgramName, ub);
                Exit(1, dpy);
            }
        } else { /* get first entry */
            button = (int)((unsigned int)pointer_map[0]);
        }

        if((id = get_window_id(dpy, screenno, button,
                               "the window whose client you wish to kill"))) {
            if(id == RootWindow(dpy, screenno))
                id = None;
            else {
                XID indicated = id;
                if((id = XmuClientWindow(dpy, indicated)) == indicated) {

                    /* Try not to kill the window manager when the user
                     * indicates an icon to xkill.
                     */

                    if(!wm_state_set(dpy, id) && wm_running(dpy, screenno))
                        id = None;
                }
            }
        }
    }

    if(id != None) {
        printf("%s:  killing creator of resource 0x%lx\n", ProgramName, id);
        XSync(dpy, 0); /* give xterm a chance */
        XKillClient(dpy, id);
        XSync(dpy, 0);
    }

    Exit(0, dpy);
    return 0;
}

static XID get_window_id(Display *dpy, int screen, int button, const char *msg)
{
    Cursor cursor;        /* cursor to use when selecting */
    Window root;          /* the current root */
    Window retwin = None; /* the window that got selected */
    int retbutton = -1;   /* button used to select window */
    int pressed = 0;      /* count of number of buttons pressed */

#define MASK (ButtonPressMask | ButtonReleaseMask)

    root = RootWindow(dpy, screen);
    cursor = XCreateFontCursor(dpy, XC_pirate);
    if(cursor == None) {
        fprintf(stderr, "%s:  unable to create selection cursor\n",
                ProgramName);
        Exit(1, dpy);
    }

    printf("Select %s with ", msg);
    if(button == -1)
        printf("any button");
    else
        printf("button %d", button);
    printf("....\n");
    XSync(dpy, 0); /* give xterm a chance */

    if(XGrabPointer(dpy, root, False, MASK, GrabModeSync, GrabModeAsync, None,
                    cursor, CurrentTime) != GrabSuccess) {
        fprintf(stderr, "%s:  unable to grab cursor\n", ProgramName);
        Exit(1, dpy);
    }

    /* from dsimple.c in xwininfo */
    while(retwin == None || pressed != 0) {
        XEvent event;

        XAllowEvents(dpy, SyncPointer, CurrentTime);
        XWindowEvent(dpy, root, MASK, &event);
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

    XUngrabPointer(dpy, CurrentTime);
    XFreeCursor(dpy, cursor);
    XSync(dpy, 0);

    return ((button == -1 || retbutton == button) ? retwin : None);
}

/* Return True if the property WM_STATE is set on the window, otherwise
 * return False.
 */
static Bool wm_state_set(Display *dpy, Window win)
{
    puts("wm_state_set");
    Atom wm_state;
    Atom actual_type;
    int success;
    int actual_format;
    unsigned long nitems, remaining;
    unsigned char *prop = NULL;

    wm_state = XInternAtom(dpy, "WM_STATE", True);
    if(wm_state == None)
        return False;
    success = XGetWindowProperty(dpy, win, wm_state, 0L, 0L, False,
                                 AnyPropertyType, &actual_type, &actual_format,
                                 &nitems, &remaining, &prop);
    if(prop)
        XFree((char *)prop);
    printf("wm_state_set success = %s\n",
           ((success == Success && actual_type != None && actual_format)
                ? "true"
                : "false"));
    return (success == Success && actual_type != None && actual_format);
}

/* Using a heuristic method, return True if a window manager is running,
 * otherwise, return False.
 */
static Bool wm_running(Display *dpy, int screenno)
{
    puts("wm_running");
    XWindowAttributes xwa;
    Status status;

    status = XGetWindowAttributes(dpy, RootWindow(dpy, screenno), &xwa);
    printf("wm_running status = %d\n", status);
    return (status && ((xwa.all_event_masks & SubstructureRedirectMask)
                       || (xwa.all_event_masks & SubstructureNotifyMask)));
}
