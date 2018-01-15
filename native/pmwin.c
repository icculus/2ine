#include "os2native.h"
#include "pmwin.h"
#include "SDL.h"

// NOTE: PM reference manual says OS/2 generally ignores the HAB you pass
//  to functions, instead getting that info from the current thread, but
//  other IBM platforms might not do that, so programs should always pass
//  a valid HAB to everything. Things like WinCreateWindow() probably rely
//  on the current thread's HAB without requiring the API call to specify
//  it in any case.
// So current approach: keep a pointer to the anchor block in the TIB, and
//  the anchor block keeps a copy of it's HAB. When the anchor block is
//  needed, we pull it from the TIB directly, and if it's in a function
//  where a HAB is specified, we fail if the handles don't match.
// If I later find out that OS/2 doesn't even care even if you give it a
//  bogus HAB, we can relax this check.


// !!! FIXME: You can send messages to HWNDs on a different thread, which
// !!! FIXME:  means HWND values must be unique across threads. Technically,
// !!! FIXME:  they have to be unique across processes, too.  :/


#define FIRST_HWND_VALUE 10
#define FIRST_HPS_VALUE 10

// This is the keyname we use on an heavyweight SDL window to find our
//  associated Window*.
#define WINDATA_WINDOWPTR_NAME "2ine_windowptr"

static SDL_atomic_t GHABCounter;
static SDL_atomic_t GHMQCounter;

typedef struct WindowClass
{
    char *name;
    PFNWP window_proc;
    ULONG style;
    ULONG data_len;
    struct WindowClass *next;
} WindowClass;

// these get shared with all children, but they're owned by the topmost parent.
typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} HeavyWeightWindow;

typedef struct Window
{
    PFNWP window_proc;
    HeavyWeightWindow heavy;
    struct Window *parent;
    struct Window *children;
    struct Window *sibling;
    struct Window *owner;
    const char *window_class_name;  // Class at creation time. styles and window procs might change later, though! This is just to make debugging easier.
    char *text;
    void *data;
    size_t data_len;
    ULONG id;
    ULONG class_style;
    ULONG style;
    HWND hwnd;
    LONG x;
    LONG y;
    LONG w;
    LONG h;
} Window;

typedef struct MessageQueueItem
{
    QMSG qmsg;
    struct MessageQueueItem *next;
} MessageQueueItem;

typedef struct
{
    HMQ hmq;
    MessageQueueItem *head;
    MessageQueueItem *tail;
    MessageQueueItem *free_pool;
} MessageQueue;

typedef struct
{
    HPS hps;
    Window *window;
    // !!! FIXME: this is gonna be WAY more complicated.
} PresentationSpace;

typedef struct
{
    HAB hab;  // actual HAB value for this block, for comparison.
    ERRORID last_error;
    WindowClass *registered_classes;
    MessageQueue message_queue;
    Window *windows;
    size_t windows_array_len;
    PresentationSpace *pres_spaces;
    size_t pres_spaces_array_len;
} AnchorBlock;

#define SET_WIN_ERROR_AND_RETURN(anchor, err, rc) { anchor->last_error = (err); return (rc); }

static Window desktop_window;  // !!! FIXME: initialize and upkeep on this.

// !!! FIXME: duplication from doscalls.c
static LxTIB2 *getTib2(void)
{
    // just read the FS register, since we have to stick it there anyhow...
    LxTIB2 *ptib2;
    __asm__ __volatile__ ( "movl %%fs:0xC, %0  \n\t" : "=r" (ptib2) );
    return ptib2;
} // getTib2

static inline LxPostTIB *getPostTib(void)
{
    // we store the LxPostTIB struct right after the TIB2 struct on the stack,
    //  so get the TIB2's linear address from %fs:0xC, then step over it
    //  to the LxPostTIB's linear address.
    return (LxPostTIB *) (getTib2() + 1);
} // getPostTib

static inline AnchorBlock *getAnchorBlockNoHAB(void)
{
    return (AnchorBlock *) getPostTib()->anchor_block;
} // getAnchorBlockNoHAB

static AnchorBlock *getAnchorBlock(const HAB hab)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return NULL;
    } else if (anchor->hab != hab) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HAB, NULL);
    }
    return anchor;
} // getAnchorBlock

static Window *getWindowFromHWND(const AnchorBlock *anchor, const HWND hwnd)
{
    if (hwnd >= FIRST_HWND_VALUE) {
        const size_t idx = (size_t) (hwnd - FIRST_HWND_VALUE);
        if (idx >= anchor->windows_array_len) {
            return NULL;
        }
        Window *win = &anchor->windows[idx];
        return (win->hwnd == hwnd) ? win : NULL;
    } else if (hwnd == HWND_DESKTOP) {
        return &desktop_window;
    }
    return NULL;
} // getWindowFromHWND

static PresentationSpace *getPresentationSpaceFromHPS(const AnchorBlock *anchor, const HPS hps)
{
    if (hps >= FIRST_HPS_VALUE) {
        const size_t idx = (size_t) (hps - FIRST_HPS_VALUE);
        if (idx >= anchor->pres_spaces_array_len) {
            return NULL;
        }
        PresentationSpace *ps = &anchor->pres_spaces[idx];
        return (ps->hps == hps) ? ps : NULL;
    }
    return NULL;
} // getPresentationSpaceFromHPS

static const char *messageName(const ULONG msg)
{
    switch (msg) {
        #define MSGCASE(m) case m: return #m
        MSGCASE(WM_NULL);
        MSGCASE(WM_CREATE);
        MSGCASE(WM_DESTROY);
        MSGCASE(WM_ENABLE);
        MSGCASE(WM_SHOW);
        MSGCASE(WM_MOVE);
        MSGCASE(WM_SIZE);
        MSGCASE(WM_ADJUSTWINDOWPOS);
        MSGCASE(WM_CALCVALIDRECTS);
        MSGCASE(WM_SETWINDOWPARAMS);
        MSGCASE(WM_QUERYWINDOWPARAMS);
        MSGCASE(WM_HITTEST);
        MSGCASE(WM_ACTIVATE);
        MSGCASE(WM_SETFOCUS);
        MSGCASE(WM_SETSELECTION);
        MSGCASE(WM_PPAINT);
        MSGCASE(WM_PSETFOCUS);
        MSGCASE(WM_PSYSCOLORCHANGE);
        MSGCASE(WM_PSIZE);
        MSGCASE(WM_PACTIVATE);
        MSGCASE(WM_PCONTROL);
        MSGCASE(WM_COMMAND);
        MSGCASE(WM_SYSCOMMAND);
        MSGCASE(WM_HELP);
        MSGCASE(WM_PAINT);
        MSGCASE(WM_TIMER);
        MSGCASE(WM_SEM1);
        MSGCASE(WM_SEM2);
        MSGCASE(WM_SEM3);
        MSGCASE(WM_SEM4);
        MSGCASE(WM_CLOSE);
        MSGCASE(WM_QUIT);
        MSGCASE(WM_SYSCOLORCHANGE);
        MSGCASE(WM_SYSVALUECHANGED);
        MSGCASE(WM_APPTERMINATENOTIFY);
        MSGCASE(WM_PRESPARAMCHANGED);
        MSGCASE(WM_CONTROL);
        MSGCASE(WM_VSCROLL);
        MSGCASE(WM_HSCROLL);
        MSGCASE(WM_INITMENU);
        MSGCASE(WM_MENUSELECT);
        MSGCASE(WM_MENUEND);
        MSGCASE(WM_DRAWITEM);
        MSGCASE(WM_MEASUREITEM);
        MSGCASE(WM_CONTROLPOINTER);
        MSGCASE(WM_QUERYDLGCODE);
        MSGCASE(WM_INITDLG);
        MSGCASE(WM_SUBSTITUTESTRING);
        MSGCASE(WM_MATCHMNEMONIC);
        MSGCASE(WM_SAVEAPPLICATION);
        MSGCASE(WM_FLASHWINDOW);
        MSGCASE(WM_FORMATFRAME);
        MSGCASE(WM_UPDATEFRAME);
        MSGCASE(WM_FOCUSCHANGE);
        MSGCASE(WM_SETBORDERSIZE);
        MSGCASE(WM_TRACKFRAME);
        MSGCASE(WM_MINMAXFRAME);
        MSGCASE(WM_SETICON);
        MSGCASE(WM_QUERYICON);
        MSGCASE(WM_SETACCELTABLE);
        MSGCASE(WM_QUERYACCELTABLE);
        MSGCASE(WM_TRANSLATEACCEL);
        MSGCASE(WM_QUERYTRACKINFO);
        MSGCASE(WM_QUERYBORDERSIZE);
        MSGCASE(WM_NEXTMENU);
        MSGCASE(WM_ERASEBACKGROUND);
        MSGCASE(WM_QUERYFRAMEINFO);
        MSGCASE(WM_QUERYFOCUSCHAIN);
        MSGCASE(WM_OWNERPOSCHANGE);
        MSGCASE(WM_CALCFRAMERECT);
        MSGCASE(WM_WINDOWPOSCHANGED);
        MSGCASE(WM_ADJUSTFRAMEPOS);
        MSGCASE(WM_QUERYFRAMECTLCOUNT);
        MSGCASE(WM_QUERYHELPINFO);
        MSGCASE(WM_SETHELPINFO);
        MSGCASE(WM_ERROR);
        MSGCASE(WM_REALIZEPALETTE);
        MSGCASE(WM_RENDERFMT);
        MSGCASE(WM_RENDERALLFMTS);
        MSGCASE(WM_DESTROYCLIPBOARD);
        MSGCASE(WM_PAINTCLIPBOARD);
        MSGCASE(WM_SIZECLIPBOARD);
        MSGCASE(WM_HSCROLLCLIPBOARD);
        MSGCASE(WM_VSCROLLCLIPBOARD);
        MSGCASE(WM_DRAWCLIPBOARD);
        MSGCASE(WM_MOUSEMOVE);
        MSGCASE(WM_BUTTON1DOWN);
        MSGCASE(WM_BUTTON1UP);
        MSGCASE(WM_BUTTON1DBLCLK);
        MSGCASE(WM_BUTTON2DOWN);
        MSGCASE(WM_BUTTON2UP);
        MSGCASE(WM_BUTTON2DBLCLK);
        MSGCASE(WM_BUTTON3DOWN);
        MSGCASE(WM_BUTTON3UP);
        MSGCASE(WM_BUTTON3DBLCLK);
        MSGCASE(WM_CHAR);
        MSGCASE(WM_VIOCHAR);
        MSGCASE(WM_JOURNALNOTIFY);
        MSGCASE(WM_MOUSEMAP);
        MSGCASE(WM_VRNDISABLED);
        MSGCASE(WM_VRNENABLED);
        MSGCASE(WM_DDE_INITIATE);
        MSGCASE(WM_DDE_REQUEST);
        MSGCASE(WM_DDE_ACK);
        MSGCASE(WM_DDE_DATA);
        MSGCASE(WM_DDE_ADVISE);
        MSGCASE(WM_DDE_UNADVISE);
        MSGCASE(WM_DDE_POKE);
        MSGCASE(WM_DDE_EXECUTE);
        MSGCASE(WM_DDE_TERMINATE);
        MSGCASE(WM_DDE_INITIATEACK);
        MSGCASE(WM_QUERYCONVERTPOS);
        MSGCASE(WM_MSGBOXINIT);
        MSGCASE(WM_MSGBOXDISMISS);
        MSGCASE(WM_CTLCOLORCHANGE);
        MSGCASE(WM_QUERYCTLTYPE);
        MSGCASE(WM_CHORD);
        MSGCASE(WM_BUTTON1MOTIONSTART);
        MSGCASE(WM_BUTTON1MOTIONEND);
        MSGCASE(WM_BUTTON1CLICK);
        MSGCASE(WM_BUTTON2MOTIONSTART);
        MSGCASE(WM_BUTTON2MOTIONEND);
        MSGCASE(WM_BUTTON2CLICK);
        MSGCASE(WM_BUTTON3MOTIONSTART);
        MSGCASE(WM_BUTTON3MOTIONEND);
        MSGCASE(WM_BUTTON3CLICK);
        MSGCASE(WM_BEGINDRAG);
        MSGCASE(WM_ENDDRAG);
        MSGCASE(WM_SINGLESELECT);
        MSGCASE(WM_OPEN);
        MSGCASE(WM_CONTEXTMENU);
        MSGCASE(WM_CONTEXTHELP);
        MSGCASE(WM_TEXTEDIT);
        MSGCASE(WM_BEGINSELECT);
        MSGCASE(WM_ENDSELECT);
        MSGCASE(WM_PICKUP);
        MSGCASE(WM_SEMANTICEVENT);
        MSGCASE(WM_USER);
        #undef MSGCASE
        default: break;
    }

    return "???";
} // messageName

static WindowClass *findRegisteredClass(AnchorBlock *anchor, const char *classname)
{
    const ULONG intclass = (ULONG) (size_t) classname;
    if ((intclass >= 0xffff0001) && (intclass <= 0xffff0070)) {
        // built-in class, like WC_BUTTON or whatever.
        switch (intclass) {
            // !!! FIXME: write me.
            case WC_FRAME:
            case WC_COMBOBOX:
            case WC_BUTTON:
            case WC_MENU:
            case WC_STATIC:
            case WC_ENTRYFIELD:
            case WC_LISTBOX:
            case WC_SCROLLBAR:
            case WC_TITLEBAR:
            case WC_MLE:
            case WC_APPSTAT:
            case WC_KBDSTAT:
            case WC_PECIC:
            case WC_DBE_KKPOPUP:
            case WC_SPINBUTTON:
            case WC_CONTAINER:
            case WC_SLIDER:
            case WC_VALUESET:
            case WC_NOTEBOOK:
            case WC_CIRCULARSLIDER:
            default:
                return NULL;
        }
    } else {
        for (WindowClass *i = anchor->registered_classes; i; i = i->next) {
            if (strcmp(i->name, classname) == 0) {
                return i;
            }
        }
    }
    return NULL;
} // findRegisteredClass

static ULONG currentSystemTicks(void)
{
    FIXME("this isn't correct");  // see notes in processSDLEvent() for details.
    return SDL_GetTicks();
} // currentSystemTicks

HAB WinInitialize(ULONG flOptions)
{
    TRACE_NATIVE("WinInitialize(%u)", (unsigned int) flOptions);

    if (flOptions != 0) {
        return NULLHANDLE;  // reserved; must be zero.
    }

    LxPostTIB *posttib = getPostTib();
    if (posttib->anchor_block != NULL) {
        return NULLHANDLE;  // fail if thread already has a HAB
    }

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO) == -1) {
            fprintf(stderr, "SDL_INIT_VIDEO failed: %s\n", SDL_GetError());
            return NULLHANDLE;
        }
    }

    FIXME("OS/2 doesn't support multi-monitor");  // this might not be true, actually.
    SDL_Rect desktoprect;
    SDL_GetDisplayBounds(0, &desktoprect);
    desktop_window.hwnd = HWND_DESKTOP;
    desktop_window.w = desktoprect.w;
    desktop_window.h = desktoprect.h;

    AnchorBlock *anchor = calloc(1, sizeof (AnchorBlock));
    if (anchor == NULL) {
        return NULLHANDLE;
    }

    anchor->windows_array_len = 16;
    anchor->windows = (Window *) malloc(sizeof (Window) * anchor->windows_array_len);
    if (!anchor->windows) {
        free(anchor);
        return NULLHANDLE;
    }
    memset(anchor->windows, '\0', sizeof (Window) * anchor->windows_array_len);

    anchor->pres_spaces_array_len = 16;
    anchor->pres_spaces = (PresentationSpace *) malloc(sizeof (PresentationSpace) * anchor->pres_spaces_array_len);
    if (!anchor->pres_spaces) {
        free(anchor->windows);
        free(anchor);
        return NULLHANDLE;
    }
    memset(anchor->pres_spaces, '\0', sizeof (PresentationSpace) * anchor->pres_spaces_array_len);

    const int ihab = SDL_AtomicAdd(&GHABCounter, 1) + 1;
    if (ihab <= 0) {  // this is clearly a pathological program.
        free(anchor->pres_spaces);
        free(anchor->windows);
        free(anchor);
        return NULLHANDLE;
    }

    const HAB hab = (HAB) ihab;
    anchor->hab = hab;
    posttib->anchor_block = anchor;

    return hab;
} // WinInitialize

static MRESULT sendMessage(Window *win, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    PFNWP winproc = win->window_proc;
    if (!winproc) {
        winproc = WinDefWindowProc;
    }
    TRACE_EVENT("EVENT SEND { hwnd=%u, msg=%u (%s), mp1=%p, mp2=%p, proc=%p }", (unsigned int) win->hwnd, (unsigned int) msg, messageName(msg), mp1, mp2, winproc);
    return winproc(win->hwnd, msg, mp1, mp2);
} // sendMessage

static BOOL destroyMessageQueue(AnchorBlock *anchor)
{
    MessageQueueItem *next = NULL;
    for (MessageQueueItem *i = anchor->message_queue.head; i; i = next) {
        next = i->next;
        free(i);
    }
    for (MessageQueueItem *i = anchor->message_queue.free_pool; i; i = next) {
        next = i->next;
        free(i);
    }
    TRACE_EVENT("HMQ %u destroyed", (unsigned int) anchor->message_queue.hmq);
    memset(&anchor->message_queue, '\0', sizeof (MessageQueue));
    return TRUE;
} // destroyMessageQueue

static void destroyWindow(AnchorBlock *anchor, Window *win)
{
    if (!win || (win->hwnd == NULLHANDLE)) {
        return;
    }

    // !!! FIXME: hide window first, and optionally send WM_ACTIVATE message if losing focus.
    // !!! FIXME: send WM_RENDERALLFMTS if we are clipboard owner and unrendered formats are in the clipboard.

    // send WM_DESTROY. This is a simple notification, and cannot prevent destruction.
    sendMessage(win, WM_DESTROY, 0, 0);

    // Next, destroy all child windows.
    Window *next = NULL;
    for (Window *i = win->children; i != NULL; i = next) {
        next = i->sibling;
        destroyWindow(anchor, i);
    }

    // !!! FIXME: see notes in SDK reference about disassociating/destroying HPS objects.

    // if this is a heavyweight window, destroy those parts.
    if (win->parent == &desktop_window) {
        SDL_DestroyTexture(win->heavy.texture);
        SDL_DestroyRenderer(win->heavy.renderer);
        SDL_DestroyWindow(win->heavy.window);
    }

    if (win->parent->children == win) {
        win->parent->children = win->sibling;
    } else {
        for (Window *i = win->parent->children; i != NULL; i = i->sibling) {
            assert(i != win);
            if (i->sibling == win) {
                i->sibling = win->sibling;
                break;
            }
        }
    }
    free(win->text);
    free(win->data);
    memset(win, '\0', sizeof (*win));
} // destroyWindow

static void destroyPresentationSpace(AnchorBlock *anchor, PresentationSpace *ps)
{
    if (!ps || (ps->hps == NULLHANDLE)) {
        return;
    }
    FIXME("this will be more complicated later");
    memset(ps, '\0', sizeof (*ps));
} // destroyPresentationSpace

BOOL WinTerminate(HAB hab)
{
    TRACE_NATIVE("WinTerminate(%u)", (unsigned int) hab);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor) {
        return FALSE;
    }

    for (size_t i = 0; i < anchor->pres_spaces_array_len; i++) {
        destroyPresentationSpace(anchor, &anchor->pres_spaces[i]);
    }
    free(anchor->pres_spaces);

    for (size_t i = 0; i < anchor->windows_array_len; i++) {
        destroyWindow(anchor, &anchor->windows[i]);
    }
    free(anchor->windows);

    WindowClass *winclassnext = NULL;
    for (WindowClass *i = anchor->registered_classes; i; i = winclassnext) {
        winclassnext = i->next;
        free(i->name);
        free(i);
    }

    if (anchor->message_queue.hmq != NULLHANDLE) {
        destroyMessageQueue(anchor);
    }

    free(anchor);
    getPostTib()->anchor_block = NULL;

    SDL_Quit();  // !!! FIXME: does this reference count?

    return TRUE;
} // WinTerminate

ERRORID WinGetLastError(HAB hab)
{
    TRACE_NATIVE("WinGetLastError(%u)", (unsigned int) hab);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor) {
        return PMERR_INVALID_HAB;
    }
    const ERRORID retval = anchor->last_error;
    SET_WIN_ERROR_AND_RETURN(anchor, NO_ERROR, retval);
} // WinGetLastError

HMQ WinCreateMsgQueue(HAB hab, LONG cmsg)
{
    TRACE_NATIVE("WinCreateMsgQueue(%u, %d)", (unsigned int) hab, (int) cmsg);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor) {
        return NULLHANDLE;
    } else if (anchor->message_queue.hmq != NULLHANDLE) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_MSG_QUEUE_ALREADY_EXISTS, NULLHANDLE);
    }

    // as of OS/2 Warp 4.0, cmsg is ignored (the queue is always dynamically allocated), so we ignore it too.

    assert(anchor->message_queue.head == NULL);
    assert(anchor->message_queue.tail == NULL);
    assert(anchor->message_queue.free_pool == NULL);

    const int ihmq = SDL_AtomicAdd(&GHMQCounter, 1) + 1;
    if (ihmq <= 0) {  // this is clearly a pathological program.
        return NULLHANDLE;
    }

    const HMQ hmq = (HMQ) ihmq;
    anchor->message_queue.hmq = hmq;
    TRACE_EVENT("HMQ %u created", (unsigned int) hmq);
    return hmq;
} // WinCreateMsgQueue

static ERRORID postMessage(AnchorBlock *anchor, const QMSG *qmsg)
{
    TRACE_EVENT("EVENT POST { hwnd=%u, msg=%u (%s), mp1=%p, mp2=%p, time=%u, ptl={%d,%d}, reserved=%u }", (unsigned int) qmsg->hwnd, (unsigned int) qmsg->msg, messageName(qmsg->msg), qmsg->mp1, qmsg->mp2, (unsigned int) qmsg->time, (int) qmsg->ptl.x, (int) qmsg->ptl.y, (unsigned int) qmsg->reserved);

    MessageQueueItem *item = anchor->message_queue.free_pool;
    if (item) {
        anchor->message_queue.free_pool = item->next;
    } else {
        item = (MessageQueueItem *) malloc(sizeof (*item));
        if (item == NULL) {
            return FALSE;
        }
    }

    memcpy(&item->qmsg, qmsg, sizeof (*qmsg));
    item->next = NULL;

    assert(!anchor->message_queue.head == !anchor->message_queue.tail);
    if (anchor->message_queue.tail) {
        anchor->message_queue.tail->next = item;
    } else {
        anchor->message_queue.head = item;
    }
    anchor->message_queue.tail = item;

    return TRUE;
} // postMessage

static inline Window *getWindowFromSDLWindow(SDL_Window *sdlwin)
{
    return (Window *) sdlwin ? SDL_GetWindowData(sdlwin, WINDATA_WINDOWPTR_NAME) : NULL;
} // getWindowFromSDLWindow


#if 0
static Window *hittestWindow(SDL_Window *sdlwin, int x, int y)
{
    if (!sdlwin) {
        return NULL;
    }

    Window *win = getWindowFromSDLWindow(sdlwin);
    if (!win) {
        return NULL;
    }

    // (win) is currently the oldest parent, which is associated with the
    //  SDL_Window. We need to see which lightweight child window, if any,
    //  this event was meant for.

    // y coordinates are flipped on OS/2. zero is the bottom of the window.
    y = sdlwin->h - y;
// !!! FIXME WRITE ME
    for (Window *i =
    if (win->class_style & CS_HITTEST) {
        sendMessage(win, WM_HITTEST,
    }

} // findWindowFromSDL
#endif

static BOOL postTimestampedMsg(AnchorBlock *anchor, HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2, ULONG ticks)
{
    const QMSG qmsg = { hwnd, msg, mp1, mp2, currentSystemTicks(), { 0, 0 }, 0 };
    return postMessage(anchor, &qmsg);
} // postTimestampedMsg

#if 0
static BOOL postSimpleMsg(AnchorBlock *anchor, HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    return postTimestampedMsg(anchor, hwnd, msg, mp1, mp2, currentSystemTicks());
} // postSimpleMsg

#endif

static BOOL postExposedMessages(AnchorBlock *anchor, Window *win, const ULONG ticks)
{
    FIXME("this should WinInvalidateRect() the windows, not send paint messages");
    FIXME("this is a recursive hell");  // we need to start at the end of the sibling list, which is the bottom of the stack.
    BOOL retval = FALSE;
    if (win) {
        retval |= postExposedMessages(anchor, win->sibling, ticks);
        if (win->style & WS_VISIBLE) {
            FIXME("for CS_SYNCPAINT classes, this should send the WM_PAINT message right now instead of posting");
            retval |= postTimestampedMsg(anchor, win->hwnd, WM_PAINT, 0, 0, ticks);
            retval |= postExposedMessages(anchor, win->children, ticks);
        }
    }
    return retval;
} // postExposedMessages


static const char *sdlEventName(const SDL_EventType ev)
{
    switch (ev) {
        #define EVCASE(m) case m: return #m
        EVCASE(SDL_QUIT);
        EVCASE(SDL_APP_TERMINATING);
        EVCASE(SDL_APP_LOWMEMORY);
        EVCASE(SDL_APP_WILLENTERBACKGROUND);
        EVCASE(SDL_APP_DIDENTERBACKGROUND);
        EVCASE(SDL_APP_WILLENTERFOREGROUND);
        EVCASE(SDL_APP_DIDENTERFOREGROUND);
        EVCASE(SDL_WINDOWEVENT);
        EVCASE(SDL_SYSWMEVENT);
        EVCASE(SDL_KEYDOWN);
        EVCASE(SDL_KEYUP);
        EVCASE(SDL_TEXTEDITING);
        EVCASE(SDL_TEXTINPUT);
        EVCASE(SDL_KEYMAPCHANGED);
        EVCASE(SDL_MOUSEMOTION);
        EVCASE(SDL_MOUSEBUTTONDOWN);
        EVCASE(SDL_MOUSEBUTTONUP);
        EVCASE(SDL_MOUSEWHEEL);
        EVCASE(SDL_JOYAXISMOTION);
        EVCASE(SDL_JOYBALLMOTION);
        EVCASE(SDL_JOYHATMOTION);
        EVCASE(SDL_JOYBUTTONDOWN);
        EVCASE(SDL_JOYBUTTONUP);
        EVCASE(SDL_JOYDEVICEADDED);
        EVCASE(SDL_JOYDEVICEREMOVED);
        EVCASE(SDL_CONTROLLERAXISMOTION);
        EVCASE(SDL_CONTROLLERBUTTONDOWN);
        EVCASE(SDL_CONTROLLERBUTTONUP);
        EVCASE(SDL_CONTROLLERDEVICEADDED);
        EVCASE(SDL_CONTROLLERDEVICEREMOVED);
        EVCASE(SDL_CONTROLLERDEVICEREMAPPED);
        EVCASE(SDL_FINGERDOWN);
        EVCASE(SDL_FINGERUP);
        EVCASE(SDL_FINGERMOTION);
        EVCASE(SDL_DOLLARGESTURE);
        EVCASE(SDL_DOLLARRECORD);
        EVCASE(SDL_MULTIGESTURE);
        EVCASE(SDL_CLIPBOARDUPDATE);
        EVCASE(SDL_DROPFILE);
        EVCASE(SDL_DROPTEXT);
        EVCASE(SDL_DROPBEGIN);
        EVCASE(SDL_DROPCOMPLETE);
        EVCASE(SDL_AUDIODEVICEADDED);
        EVCASE(SDL_AUDIODEVICEREMOVED);
        EVCASE(SDL_RENDER_TARGETS_RESET);
        EVCASE(SDL_RENDER_DEVICE_RESET);
        EVCASE(SDL_USEREVENT);
        #undef EVCASE
        default: break;
    }
    return "???";
} // sdlEventName

static const char *sdlWindowEventName(const SDL_WindowEventID ev)
{
    switch (ev) {
        #define EVCASE(m) case m: return #m
        EVCASE(SDL_WINDOWEVENT_SHOWN);
        EVCASE(SDL_WINDOWEVENT_HIDDEN);
        EVCASE(SDL_WINDOWEVENT_EXPOSED);
        EVCASE(SDL_WINDOWEVENT_MOVED);
        EVCASE(SDL_WINDOWEVENT_RESIZED);
        EVCASE(SDL_WINDOWEVENT_SIZE_CHANGED);
        EVCASE(SDL_WINDOWEVENT_MINIMIZED);
        EVCASE(SDL_WINDOWEVENT_MAXIMIZED);
        EVCASE(SDL_WINDOWEVENT_RESTORED);
        EVCASE(SDL_WINDOWEVENT_ENTER);
        EVCASE(SDL_WINDOWEVENT_LEAVE);
        EVCASE(SDL_WINDOWEVENT_FOCUS_GAINED);
        EVCASE(SDL_WINDOWEVENT_FOCUS_LOST);
        EVCASE(SDL_WINDOWEVENT_CLOSE);
        EVCASE(SDL_WINDOWEVENT_TAKE_FOCUS);
        EVCASE(SDL_WINDOWEVENT_HIT_TEST);
        #undef EVCASE
        default: break;
    }
    return "???";
} // sdlWindowEventName

static BOOL processSDLEvent(AnchorBlock *anchor, const SDL_Event *sdlevent)
{
    // !!! FIXME: we just drop events if we run out of memory, etc.

    // !!! FIXME: QMSG::time appears to be milliseconds since the system
    // !!! FIXME:  booted (presumably rolling over every 38 days). SDL's
    // !!! FIXME:  event timestamps (and SDL_GetTicks()) are milliseconds
    // !!! FIXME:  since SDL_Init(), which is more or less since app startup.
    // !!! FIXME:  We might need a way to map these, so they're consistent
    // !!! FIXME:  across processes.
    // !!! FIXME: Also, it's well known that SDL event timestamps are when
    // !!! FIXME:  SDL gets the event from the OS, not the timestamp of when
    // !!! FIXME:  the OS generated it, which might be a problem.

/*
sdfsdf
    HWND    hwnd;
    ULONG   msg;
    MPARAM  mp1;
    MPARAM  mp2;
    ULONG   time;
    POINTL  ptl;
    ULONG   reserved;
sdfsdf
*/

//SDL_GetWindowFromID(window) sdfsdf

    const ULONG ticks = (ULONG) sdlevent->common.timestamp;

    TRACE_EVENT("EVENT SDL { type=0x%X (%s) }", (unsigned int) sdlevent->type, sdlEventName(sdlevent->type));

    switch (sdlevent->type) {
        case SDL_QUIT:
            return postTimestampedMsg(anchor, NULLHANDLE, WM_QUIT, 0, 0, ticks);

        case SDL_WINDOWEVENT:
            TRACE_EVENT("EVENT SDL WINDOW { type=0x%X (%s) }", (unsigned int) sdlevent->window.event, sdlWindowEventName(sdlevent->window.event));
            switch (sdlevent->window.event) {
                case SDL_WINDOWEVENT_SHOWN:
                case SDL_WINDOWEVENT_EXPOSED: {
                    // maybe this doesn't matter in a world of compositing window managers, but it would be nice to completely avoid sending WM_PAINT to child windows that otherwise don't apply.
                    //  This only applies for heavyweight windows the OS is marking as exposed, not OS/2 apps marking OS/2 HWNDs as invalid for various reasons.
                    FIXME("SDL doesn't report invalid subregions, just that a window needs repainting");
                    Window *win = getWindowFromSDLWindow(SDL_GetWindowFromID(sdlevent->window.windowID));
                    if (!win) {
                        return FALSE;
                    }
                    return postExposedMessages(anchor, win, ticks);
                }

                default: break;  // not (currently) supported.
            }
            return FALSE;



#if 0
                case SDL_WINDOWEVENT_SHOWN:
                case SDL_WINDOWEVENT_HIDDEN:
                case SDL_WINDOWEVENT_MOVED,          /**< Window has been moved to data1, data2
                                     */
                case SDL_WINDOWEVENT_RESIZED,        /**< Window has been resized to data1xdata2 */
                case SDL_WINDOWEVENT_SIZE_CHANGED,   /**< The window size has changed, either as
                                         a result of an API call or through the
                                         system or user changing the window size. */
                case SDL_WINDOWEVENT_MINIMIZED,      /**< Window has been minimized */
                case SDL_WINDOWEVENT_MAXIMIZED,      /**< Window has been maximized */
                case SDL_WINDOWEVENT_RESTORED,       /**< Window has been restored to normal size
                                         and position */
                case SDL_WINDOWEVENT_ENTER,          /**< Window has gained mouse focus */
                case SDL_WINDOWEVENT_LEAVE,          /**< Window has lost mouse focus */
                case SDL_WINDOWEVENT_FOCUS_GAINED,   /**< Window has gained keyboard focus */
                case SDL_WINDOWEVENT_FOCUS_LOST,     /**< Window has lost keyboard focus */
                case SDL_WINDOWEVENT_CLOSE,          /**< The window manager requests that the window be closed */
                case SDL_WINDOWEVENT_TAKE_FOCUS,     /**< Window is being offered a focus (should SetWindowInputFocus() on itself or a subwindow, or ignore) */
                //case SDL_WINDOWEVENT_HIT_TEST        /**< Window had a hit test that wasn't SDL_HITTEST_NORMAL. */


//    SDL_MOUSEMOTION    = 0x400, /**< Mouse moved */
//    SDL_MOUSEBUTTONDOWN,        /**< Mouse button pressed */
//    SDL_MOUSEBUTTONUP,          /**< Mouse button released */
//    SDL_MOUSEWHEEL,             /**< Mouse wheel motion */
//    SDL_KEYDOWN        = 0x300, /**< Key pressed */
//    SDL_KEYUP,                  /**< Key released */
//    SDL_TEXTINPUT,              /**< Keyboard text input */
//    SDL_CLIPBOARDUPDATE = 0x900, /**< The clipboard changed */
//    SDL_DROPFILE        = 0x1000, /**< The system requests a file open */
//    SDL_DROPTEXT,                 /**< text/plain drag-and-drop event */
//    SDL_DROPBEGIN,                /**< A new set of drops is beginning (NULL filename) */
//    SDL_DROPCOMPLETE,             /**< Current set of drops is now complete (NULL filename) */


        case SDL_MOUSEBUTTONDOWN:
            
        case SDL_MOUSEBUTTONUP:
            if (sdlevent->button.clicks == 1) {}
#endif


        default: return FALSE;  // not (currently) supported.
    }

    assert(!"shouldn't hit this");
    return TRUE;
} // processSDLEvent

static void pumpEvents(AnchorBlock *anchor)
{
    SDL_Event sdlevent;
    BOOL queued = FALSE;

    // run through anything pending. If we don't post anything to the OS/2
    //  event queue, we need to block until we do, though, so then we'll use
    //  SDL_WaitEvent() in hopes that someday it's fully waitable.
    while (SDL_PollEvent(&sdlevent)) {
        queued |= processSDLEvent(anchor, &sdlevent);
    }

    while (!queued) {  // block until stuff shows up.
        while (!SDL_WaitEvent(&sdlevent)) {
            SDL_Delay(10);  // dunno what else to do here...
        }
        queued |= processSDLEvent(anchor, &sdlevent);
        while (SDL_PollEvent(&sdlevent)) {  // catch anything else pending.
            queued |= processSDLEvent(anchor, &sdlevent);
        }
    }
} // waitForMessage

static BOOL skipMessage(const PQMSG qmsg, HWND hwndFilter, ULONG msgFilterFirst, ULONG msgFilterLast)
{
    if (hwndFilter && (qmsg->hwnd != hwndFilter)) {
        return TRUE;
    } else if (msgFilterFirst || msgFilterLast) {
        const ULONG msg = qmsg->msg;
        if (msgFilterFirst > msgFilterLast) {  // reject if between first and last.
            if ((msg >= msgFilterLast) && (msg <= msgFilterFirst)) return TRUE;
        } else {  // reject if not between first and last.
            if ((msg < msgFilterFirst) || (msg > msgFilterLast)) return TRUE;
        }
    }
    return FALSE;
} // skipMessage

BOOL WinGetMsg(HAB hab, PQMSG pqmsg, HWND hwndFilter, ULONG msgFilterFirst, ULONG msgFilterLast)
{
    TRACE_NATIVE("WinGetMsg(%u, %p, %u, %u, %u)", (unsigned int) hab, pqmsg, (unsigned int) hwndFilter, (unsigned int) msgFilterFirst, (unsigned int) msgFilterLast);
    const BOOL bIsFiltering = (hwndFilter || msgFilterFirst || msgFilterLast);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor) {
        return FALSE;
    } else if (hwndFilter) {
        FIXME("fail with PMERR_INVALID_HWND if this is a bogus HWND.");
    }

    while (1) {
        MessageQueueItem *prev = NULL;
        MessageQueueItem *i = anchor->message_queue.head;
        if (bIsFiltering) {
            for (; i != NULL; prev = i, i = i->next) {
                if (!skipMessage(&i->qmsg, hwndFilter, msgFilterFirst, msgFilterLast)) {
                    break;
                }
            }
        }

        // Found an event? Remove from queue, return that info.
        if (i != NULL) {
            memcpy(pqmsg, &i->qmsg, sizeof (QMSG));

            TRACE_EVENT("EVENT GET { hwnd=%u, msg=%u (%s), mp1=%p, mp2=%p, time=%u, ptl={%d,%d}, reserved=%u }", (unsigned int) pqmsg->hwnd, (unsigned int) pqmsg->msg, messageName(pqmsg->msg), pqmsg->mp1, pqmsg->mp2, (unsigned int) pqmsg->time, (int) pqmsg->ptl.x, (int) pqmsg->ptl.y, (unsigned int) pqmsg->reserved);

            if (prev) {
                prev->next = i->next;
            } else {
                anchor->message_queue.head = i->next;
            }

            if (!anchor->message_queue.head) {
                assert(anchor->message_queue.tail == i);
                anchor->message_queue.tail = NULL;
            }

            i->next = anchor->message_queue.free_pool;
            anchor->message_queue.free_pool = i;

            return (pqmsg->msg != WM_QUIT) ? TRUE : FALSE;
        }

        pumpEvents(anchor);  // may block.
    }

    assert(!"shouldn't hit this.");
    return TRUE;  // shouldn't hit this.
} // WinGetMsg

MRESULT WinDispatchMsg(HAB hab, PQMSG pqmsg)
{
    TRACE_NATIVE("WinDispatchMsg(%u, %p)", (unsigned int) hab, pqmsg);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (anchor) {
        Window *win = getWindowFromHWND(anchor, pqmsg->hwnd);
        if (win) {
            return sendMessage(win, pqmsg->msg, pqmsg->mp1, pqmsg->mp2);
        }
    }
    return 0;
} // WinDispatchMsg

BOOL WinDestroyMsgQueue(HMQ hmq)
{
    TRACE_NATIVE("WinDestroyMsgQueue(%u)", (unsigned int) hmq);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return FALSE;
    } else if ((hmq == NULLHANDLE) || (anchor->message_queue.hmq != hmq))  {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HMQ, FALSE);
    }

    return destroyMessageQueue(anchor);
} // WinDestroyMsgQueue

BOOL WinRegisterClass(HAB hab, PSZ pszClassName, PFNWP pfnWndProc, ULONG flStyle, ULONG cbWindowData)
{
    TRACE_NATIVE("WinRegisterClass(%u, '%s', %p, %u, %u)", (unsigned int) hab, pszClassName, pfnWndProc, (unsigned int) flStyle, (unsigned int) cbWindowData);

    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor) {
        return FALSE;
    }

    WindowClass *winclass = findRegisteredClass(anchor, pszClassName);
    if (!winclass) {  // PM docs say this call can replace existing class.
        winclass = (WindowClass *) malloc(sizeof (WindowClass));
        if (!winclass) {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, FALSE);
        } else if ((winclass->name = strdup(pszClassName)) == NULL) {
            free(winclass);
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, FALSE);
        }
        winclass->next = anchor->registered_classes;
        anchor->registered_classes = winclass;
    }

    winclass->window_proc = pfnWndProc;
    winclass->style = flStyle;
    winclass->data_len = cbWindowData;

    return TRUE;
} // WinRegisterClass

MRESULT WinSendMsg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    // !!! FIXME: This needs to be able to send to windows in different
    // !!! FIXME:  threads (and processes!), so this will need some effort to
    // !!! FIXME:  support.

    TRACE_NATIVE("WinSendMsg(%u, %u, %p, %p)", (unsigned int) hwnd, (unsigned int) msg, mp1, mp2);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (anchor) {
        Window *win = getWindowFromHWND(anchor, hwnd);
        if (win) {
            return sendMessage(win, msg, mp1, mp2);
        }
    }
    return (MRESULT) 0;
} // WinSendMsg

BOOL WinPostMsg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    // !!! FIXME: SDL docs say you can call this, but not WinSendMsg() if
    // !!! FIXME:  the thread doesn't have a message queue, but that has to
    // !!! FIXME:  be a typo, right? How do you post a message to a queue that
    // !!! FIXME:  doesn't exist? But one could call a window procedure
    // !!! FIXME:  directly...

    // !!! FIXME: This needs to be able to post to windows in different
    // !!! FIXME:  threads (and processes!), so this will need some effort to
    // !!! FIXME:  support.

    TRACE_NATIVE("WinPostMsg(%u, %u, %p, %p)", (unsigned int) hwnd, (unsigned int) msg, mp1, mp2);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return FALSE;
    }
    if (anchor->message_queue.hmq == NULLHANDLE) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_NO_MSG_QUEUE, FALSE);
    }

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }

    int mousex, mousey;
    SDL_GetGlobalMouseState(&mousex, &mousey);  FIXME("track this ourselves?");
    const QMSG qmsg = { hwnd, msg, mp1, mp2, currentSystemTicks(), { mousex, mousey }, 0 };
    const ERRORID rc = postMessage(anchor, &qmsg);
    if (rc) {
        SET_WIN_ERROR_AND_RETURN(anchor, rc, FALSE);
    }

    return TRUE;
} // WinPostMsg

BOOL WinPostQueueMsg(HMQ hmq, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    TRACE_NATIVE("WinPostMsgQueue(%u, %u, %p, %p)", (unsigned int) hmq, (unsigned int) msg, mp1, mp2);
    // I assume this works like win32's PostThreadMessage(), in that there
    //  isn't a target HWND, so the target thread would have to deal with this
    //  message after a call to WinGetMsg() and not in a window procedure
    //  during a call to WinDispatchMsg().

    // (!!! FIXME: right now these are stored on the anchor block and not
    //             anywhere global I can reach it.)
    FIXME("need to be able to access other threads' message queues");
    return FALSE;
} // WinPostQueueMsg

HWND WinCreateWindow(HWND hwndParent, PSZ pszClass, PSZ pszName, ULONG flStyle, LONG x, LONG y, LONG cx, LONG cy, HWND hwndOwner, HWND hwndInsertBehind, ULONG id, PVOID pCtlData, PVOID pPresParams)
{
    TRACE_NATIVE("WinCreateWindow(%u, '%s', '%s', %u, %d, %d, %d, %d, %u, %u, %u, %p, %p)", (unsigned int) hwndParent, pszClass, pszName, (unsigned int) flStyle, (int) x, (int) y, (int) cx, (int) cy, (unsigned int) hwndOwner, hwndInsertBehind, (unsigned int) id, pCtlData, pPresParams);

    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return NULLHANDLE;
    }

    if (anchor->message_queue.hmq == NULLHANDLE) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_NO_MSG_QUEUE, NULLHANDLE);
    }

    const WindowClass *winclass = findRegisteredClass(anchor, pszClass);
    if (!winclass) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_PARM, NULLHANDLE);
    }

    Window *parent = getWindowFromHWND(anchor, hwndParent);
    if (!parent) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
    }

    Window *owner = NULL;
    if (hwndOwner != NULLHANDLE) {
        owner = getWindowFromHWND(anchor, hwndParent);
        if (!owner) {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
        }
    }

    // make space for the new window struct in the anchor block
    Window *win = NULL;
    FIXME("optimize this");
    for (size_t i = 0; i < anchor->windows_array_len; i++) {
        if (anchor->windows[i].hwnd == NULLHANDLE) {
            win = &anchor->windows[i];
            break;
        }
    }

    if (!win) {
        const size_t len = anchor->windows_array_len;
        void *ptr = realloc(anchor->windows, sizeof (Window) * len * 2);
        if (!ptr) {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
        }
        anchor->windows = (Window *) ptr;
        memset(&anchor->windows[len], '\0', sizeof (Window) * len);
        win = &anchor->windows[len];
        anchor->windows_array_len *= 2;
    }

    char *text = NULL;
    if (pszName) {
        // !!! FIXME: text isn't necessarily a string. It's window-class-specific data.
        text = strdup(pszName);
        if (!text) {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
        }
    }

    void *data = NULL;
    if (winclass->data_len > 0) {
        data = malloc(winclass->data_len);
        if (!data) {
            free(text);
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
        }
    }

    if (hwndParent == HWND_DESKTOP) {  // toplevel? build a heavy weight window.
        Uint32 sdlflags = SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS;
        // !!! FIXME   SDL_WINDOW_SKIP_TASKBAR  = 0x00010000,      /**< window should not be added to the taskbar */
        // !!! FIXME: text isn't necessarily a string. It's window-class-specific data.
        // The Y coordinate is the bottom of the window, counted from the bottom of the parent window (desktop, in this case) :/
        SDL_Window *window = SDL_CreateWindow(text ? text : "", x, (desktop_window.h - y) - cy, cx, cy, sdlflags);
        if (!window) {
            free(text);
            free(data);
            FIXME("error code?");
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INTERNAL_ERROR_1, NULLHANDLE);
        }

        SDL_SetWindowData(window, WINDATA_WINDOWPTR_NAME, win);

        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE);
        if (!renderer) {
            free(text);
            free(data);
            SDL_DestroyWindow(window);
            FIXME("error code?");
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INTERNAL_ERROR_1, NULLHANDLE);
        }

        SDL_RendererInfo info;
        SDL_GetRendererInfo(renderer, &info);  FIXME("handle failure here");

        SDL_Texture *texture = SDL_CreateTexture(renderer, info.texture_formats[0], SDL_TEXTUREACCESS_TARGET, cx, cy);
        if (!texture) {
            free(text);
            free(data);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            FIXME("error code?");
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INTERNAL_ERROR_1, NULLHANDLE);
        }

        SDL_SetRenderTarget(renderer, texture);

        win->heavy.window = window;
        win->heavy.renderer = renderer;
        win->heavy.texture = texture;
    } else {  // use same heavy as parent.
        win->heavy.window = parent->heavy.window;
        win->heavy.renderer = parent->heavy.renderer;
        win->heavy.texture = parent->heavy.texture;
    }

    win->parent = parent;
    win->children = NULL;
    win->hwnd = (HWND) (((size_t) (win - anchor->windows)) + FIRST_HWND_VALUE);
    win->window_class_name = winclass->name;  // this pointer lives until Anchor Block terminates.
    win->data = data;
    win->text = text;
    win->data_len = winclass->data_len;
    win->window_proc = winclass->window_proc;
    win->class_style = winclass->style;
    win->style = flStyle;
    win->id = id;
    win->owner = owner;
    win->sibling = NULL;  // we'll insert this later.
    win->x = win->y = win->w = win->h = 0;  // we'll set these later.

    // we're ready, send WM_CREATE. Note that pCtlData has to be a pointer (not
    //  an integer cast to a pointer), because OS/2 would dereference it, and the
    //  16 bits at the start of the data it points to tell you how many bytes are
    //  pointed at, so it could deal with 16-bit code. We don't (currently) care,
    //  so just pass it on.
    if (sendMessage(win, WM_CREATE, pCtlData, pPresParams) != FALSE) {
        // response to WM_CREATE was to cancel window creation!!
        destroyWindow(anchor, win);
        return NULLHANDLE;
    }

    FIXME("should I set swp.fl here?");
    SWP swp = { 0, cy, cx, y, x, hwndInsertBehind, win->hwnd, 0, 0 };
    MRESULT adjust = sendMessage(win, WM_ADJUSTWINDOWPOS, &swp, 0);

    (void) adjust; FIXME("adjust based on WM_ADJUSTWINDOWPOS message result");

    win->x = swp.x;
    win->y = swp.y;
    win->w = swp.cx;
    win->h = swp.cy;
    hwndInsertBehind = swp.hwndInsertBehind;

    Window *insertBehind = NULL;
    if ((hwndInsertBehind != HWND_TOP) && (hwndInsertBehind != HWND_BOTTOM)) {
        insertBehind = getWindowFromHWND(anchor, hwndInsertBehind);
        if (!insertBehind || (insertBehind->parent != parent)) {
            FIXME("should we fail or just insert at top/bottom?");
            hwndInsertBehind = HWND_BOTTOM;
            insertBehind = NULL;
        }
    }

    if (parent->children == NULL) {  // nothing to insert behind, just make it the only child.
        parent->children = win;
    } else if (hwndInsertBehind == HWND_TOP) {
        win->sibling = parent->children;
        parent->children = win;
    } else if (hwndInsertBehind == HWND_BOTTOM) {
        Window *prev = NULL;
        for (Window *i = parent->children; i != NULL; i = i->sibling) {
            prev = i;
        }
        prev->sibling = win;
    } else {  // insert behind a specific window.
        win->sibling = insertBehind->sibling;
        insertBehind->sibling = win;
    }

    if (hwndParent == HWND_DESKTOP) {
        SDL_SetWindowPosition(win->heavy.window, win->x, (parent->h - win->y) - win->h);
        SDL_SetWindowSize(win->heavy.window, win->w, win->h);
        if (win->style & WS_VISIBLE) {
            // !!! FIXME: mark window region invalid
            SDL_ShowWindow(win->heavy.window);
        }
    }

    return win->hwnd;
} // WinCreateWindow

HWND WinCreateStdWindow(HWND hwndParent, ULONG flStyle, PULONG pflCreateFlags, PSZ pszClientClass, PSZ pszTitle, ULONG styleClient, HMODULE hmod, ULONG idResources, PHWND phwndClient)
{
    TRACE_NATIVE("WinCreateStdWindow(%u, %u, %p, '%s', '%s', %u, %u, %u, %p)", (unsigned int) hwndParent, (unsigned int) flStyle, pflCreateFlags, pszClientClass, pszTitle, (unsigned int) styleClient, (unsigned int) hmod, (unsigned int) idResources, phwndClient);

    if (phwndClient) {
        *phwndClient = NULLHANDLE;
    }

    FRAMECDATA fcd = { sizeof (FRAMECDATA), *pflCreateFlags, hmod, idResources };
    HWND retval = WinCreateWindow(hwndParent, (PSZ) WC_FRAME, pszTitle, 0, 0, 0, 0, 0, NULLHANDLE, HWND_TOP, idResources, &fcd, NULL);
    if (!retval) {
        return NULLHANDLE;
    }

    if (pszClientClass) {
        HWND clientwin = WinCreateWindow(retval, pszClientClass, NULL, 0, 0, 0, 0, 0, retval, HWND_BOTTOM, FID_CLIENT, NULL, NULL);
        if (!clientwin) {
            WinDestroyWindow(retval);
            return NULLHANDLE;
        }
        if (phwndClient) {
            *phwndClient = clientwin;
        }
    }

    return retval;
} // WinCreateStdWindow

BOOL WinDestroyWindow(HWND hwnd)
{
    TRACE_NATIVE("WinDestroyWindow(%u)", (unsigned int) hwnd);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return FALSE;
    }

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }

    destroyWindow(anchor, win);

    return TRUE;
} // WinDestroyWindow

MRESULT WinDefWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    TRACE_NATIVE("WinDefWindowProc(%u, %u, %p, %p)", (unsigned int) hwnd, (unsigned int) msg, mp1, mp2);

    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return (MRESULT) 0;
    }
    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win) {
        return (MRESULT) 0;
    }

    switch (msg) {
        // this whole block just returns zero without further processing.
        case WM_ACTIVATE:
        case WM_APPTERMINATENOTIFY:
        case WM_ADJUSTWINDOWPOS:
        case WM_CALCFRAMERECT:
        case WM_COMMAND:
        case WM_CONTROL:
        case WM_CREATE:
        case WM_CTLCOLORCHANGE:
        case WM_DESTROY:
        case WM_DRAWITEM:
        case WM_ENABLE:
        case WM_ERASEBACKGROUND:
        case WM_ERROR:
        case WM_FORMATFRAME:
        case WM_HSCROLL:
        case WM_INITDLG:
        case WM_INITMENU:
        case WM_JOURNALNOTIFY:
        case WM_MATCHMNEMONIC:
        case WM_MEASUREITEM:
        case WM_MENUEND:
        case WM_MINMAXFRAME:
        case WM_MOUSEMAP:
        case WM_MOVE:
        case WM_NEXTMENU:
        case WM_NULL:
        case WM_PCONTROL:
        case WM_PRESPARAMCHANGED:
        case WM_PSETFOCUS:
        case WM_PSIZE:
        case WM_PSYSCOLORCHANGE:
        case WM_QUERYACCELTABLE:
        case WM_QUIT:
        case WM_SAVEAPPLICATION:
        case WM_SEM1:
        case WM_SEM2:
        case WM_SEM3:
        case WM_SEM4:
        case WM_SETACCELTABLE:
        case WM_SETFOCUS:
        case WM_SETHELPINFO:
        case WM_SETSELECTION:
        case WM_SETWINDOWPARAMS:
        case WM_SHOW:
        case WM_SIZE:
        case WM_SUBSTITUTESTRING:
        case WM_SYSCOLORCHANGE:
        case WM_SYSCOMMAND:
        case WM_SYSVALUECHANGED:
        case WM_TIMER:
        case WM_TRACKFRAME:
        case WM_TRANSLATEACCEL:
        //case WM_TRANSLATEMNEMONIC:  // !!! FIXME: this is in the SDK reference but not the SDK headers.
        case WM_UPDATEFRAME:
        case WM_VRNDISABLED:
        case WM_VRNENABLED:
        case WM_VSCROLL:

        // the SDK reference manual doesn't list default actions for these messages, but it's probably just "return zero".
        case WM_MSGBOXINIT:
        case WM_MSGBOXDISMISS:

            return (MRESULT) 0;

        // this whole block just returns one without further processing.
        case WM_MENUSELECT:
            return (MRESULT) 1;

        // this block sends the message to the window owner (if one exists), or otherwise returns 0.
        case WM_BEGINDRAG:
        case WM_BEGINSELECT:
        case WM_BUTTON1CLICK:
        case WM_BUTTON1DBLCLK:
        case WM_BUTTON1MOTIONSTART:
        case WM_BUTTON1MOTIONEND:
        case WM_BUTTON1UP:
        case WM_BUTTON2CLICK:
        case WM_BUTTON2DBLCLK:
        case WM_BUTTON2MOTIONSTART:
        case WM_BUTTON2MOTIONEND:
        case WM_BUTTON2UP:
        case WM_BUTTON3CLICK:
        case WM_BUTTON3DBLCLK:
        case WM_BUTTON3MOTIONSTART:
        case WM_BUTTON3MOTIONEND:
        case WM_BUTTON3UP:
        case WM_CHAR:
        case WM_CHORD:
        case WM_CONTEXTMENU:
        case WM_ENDDRAG:
        case WM_ENDSELECT:
        case WM_OPEN:
        case WM_PACTIVATE:
        case WM_QUERYCTLTYPE:
        case WM_QUERYHELPINFO:
        case WM_QUERYTRACKINFO:
        case WM_SINGLESELECT:
        case WM_TEXTEDIT:
            return win->owner ? sendMessage(win->owner, msg, mp1, mp2) : (MRESULT) 0;

        // These messages actually do something specific in default processing...
        case WM_HITTEST:
            return HT_NORMAL;

        case WM_BUTTON1DOWN:
        case WM_BUTTON2DOWN:
        case WM_BUTTON3DOWN:
        case WM_CALCVALIDRECTS:
        case WM_CLOSE:
        case WM_CONTROLPOINTER:
        case WM_FOCUSCHANGE:
        case WM_HELP:
        case WM_MOUSEMOVE:
        case WM_PAINT:
        case WM_PPAINT:
        case WM_QUERYCONVERTPOS:
        case WM_QUERYWINDOWPARAMS:
        case WM_REALIZEPALETTE:
        case WM_WINDOWPOSCHANGED:
            fprintf(stderr, "Message that doesn't something other than return 0, %u, in WinDefWindowProc!\n", (unsigned int) msg);
            break;

        default:
            fprintf(stderr, "Unhandled message %u in WinDefWindowProc!\n", (unsigned int) msg);
            break;
    }

    return (MRESULT) 0;
} // WinDefWindowProc

HPS WinBeginPaint(HWND hwnd, HPS hps, PRECTL prclPaint)
{
    TRACE_NATIVE("WinBeginPaint(%u, %u, %p)", (unsigned int) hwnd, (unsigned int) hps, prclPaint);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return NULLHANDLE;
    }
    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
    }

    PresentationSpace *ps = NULL;
    if (hps != NULLHANDLE) {
        ps = getPresentationSpaceFromHPS(anchor, hps);
        if (!ps) {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
        }
        FIXME("check if this is already assigned to this drawable");
    } else {
        // make space for the new presentation struct in the anchor block
        FIXME("optimize this");
        FIXME("cut-and-paste from WinCreateWindow");
        for (size_t i = 0; i < anchor->pres_spaces_array_len; i++) {
            if (anchor->pres_spaces[i].hps == NULLHANDLE) {
                ps = &anchor->pres_spaces[i];
                break;
            }
        }

        if (!ps) {
            const size_t len = anchor->pres_spaces_array_len;
            void *ptr = realloc(anchor->pres_spaces, sizeof (PresentationSpace) * len * 2);
            if (!ptr) {
                SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
            }
            anchor->pres_spaces = (PresentationSpace *) ptr;
            memset(&anchor->pres_spaces[len], '\0', sizeof (PresentationSpace) * len);
            ps = &anchor->pres_spaces[len];
            anchor->pres_spaces_array_len *= 2;
        }
        hps = (HPS) (((size_t) (ps - anchor->pres_spaces)) + FIRST_HPS_VALUE);
    }

    ps->hps = hps;
    ps->window = win;

    if (prclPaint) {
        FIXME("report the update region, not the whole window");
        prclPaint->xLeft = 0;
        prclPaint->yBottom = 0;
        prclPaint->xRight = win->w;
        prclPaint->yTop = win->h;
    }

    return hps;
} // WinBeginPaint

BOOL WinEndPaint(HPS hps)
{
    TRACE_NATIVE("WinEndPaint(%u)", (unsigned int) hps);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return NULLHANDLE;
    }
    PresentationSpace *ps = getPresentationSpaceFromHPS(anchor, hps);
    if (!ps) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
    }

    Window *win = ps->window;
    SDL_Renderer *renderer = win->heavy.renderer;
    SDL_Texture *texture = win->heavy.texture;
    assert(SDL_GetRenderTarget(renderer) == texture);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    SDL_SetRenderTarget(renderer, texture);

    destroyPresentationSpace(anchor, ps);
    return TRUE;
} // WinEndPaint

static void os2ColorIndexToRGB(const LONG os2color, Uint8 *r, Uint8 *g, Uint8 *b)
{
    assert(r != NULL); assert(g != NULL); assert(b != NULL);
    #define RETURNRGB(rr,gg,bb) { *r = rr; *g = gg; *b = bb; return; }
    switch (os2color) {
        case CLR_FALSE: FIXME("this should reverse on a printer"); RETURNRGB(0x00, 0x00, 0x00);
        case CLR_TRUE: FIXME("this should reverse on a printer"); RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_DEFAULT: FIXME("Let users specify this on command line or system-wide .ini?"); RETURNRGB(0x00, 0x00, 0x00);
        case CLR_WHITE: RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_BLACK: RETURNRGB(0x00, 0x00, 0x00);
        case CLR_BACKGROUND: FIXME("Let users specify this on command line or system-wide .ini?"); RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_BLUE: RETURNRGB(0x00, 0x00, 0xFF);
        case CLR_RED: RETURNRGB(0xFF, 0x00, 0x00);
        case CLR_PINK: RETURNRGB(0xFF, 0x00, 0xFF);
        case CLR_GREEN: RETURNRGB(0x00, 0xFF, 0x00);
        case CLR_CYAN: RETURNRGB(0x00, 0xFF, 0xFF);
        case CLR_YELLOW: RETURNRGB(0xFF, 0xFF, 0x00);
        case CLR_NEUTRAL: FIXME("Let users specify this on command line or system-wide .ini?"); RETURNRGB(0x00, 0x00, 0x00);
        case CLR_DARKGRAY: RETURNRGB(0x80, 0x80, 0x80);
        case CLR_DARKBLUE: RETURNRGB(0x00, 0x00, 0x80);
        case CLR_DARKRED: RETURNRGB(0x80, 0x00, 0x00);
        case CLR_DARKPINK: RETURNRGB(0x80, 0x00, 0x80);
        case CLR_DARKGREEN: RETURNRGB(0x00, 0x80, 0x00);
        case CLR_DARKCYAN: RETURNRGB(0x00, 0x80, 0x80);
        case CLR_BROWN: RETURNRGB(0x80, 0x80, 0x00);
        case CLR_PALEGRAY: RETURNRGB(0xCC, 0xCC, 0xCC);
        default: break;
    }

    FIXME("unknown color index");
    RETURNRGB(0x00, 0x00, 0x00);
    #undef RETURNRGB
} // os2ColorIndexToRGB

static void calcHeavyweightRect(const Window *win, SDL_Rect* rect)
{
    int x = 0, y = 0;

    rect->w = win->w;
    rect->h = win->h;

    while (win->parent != &desktop_window) {
        x += win->x;
        y += win->y;
        win = win->parent;
    }

    rect->x = x;
    rect->y = (win->h - y) - rect->h;
} // calcHeavyweightRect

BOOL WinFillRect(HPS hps, PRECTL prcl, LONG lColor)
{
    TRACE_NATIVE("WinFillRect(%u, %p, %d)", (unsigned int) hps, prcl, (int) lColor);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor) {
        return NULLHANDLE;
    }
    PresentationSpace *ps = getPresentationSpaceFromHPS(anchor, hps);
    if (!ps) {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
    }
    Window *win = ps->window;
    SDL_Renderer *renderer = win->heavy.renderer;
    assert(SDL_GetRenderTarget(renderer) == win->heavy.texture);

    SDL_Rect cliprect;
    calcHeavyweightRect(win, &cliprect);

    SDL_Rect rect;
    rect.x = cliprect.x + prcl->xLeft;
    rect.y = cliprect.y + (cliprect.h - prcl->yTop);
    rect.w = prcl->xRight - prcl->xLeft;
    rect.h = prcl->yTop - prcl->yBottom;

    Uint8 r, g, b;
    os2ColorIndexToRGB(lColor, &r, &g, &b);

    //printf("fill { x=%d, y=%d, w=%d, h=%d, r=0x%X, g=0x%X, b=0x%X }\n", (int) rect.x, (int) rect.y, (int) rect.w, (int) rect.h, (unsigned int) r, (unsigned int) g, (unsigned int) b);
    SDL_RenderSetClipRect(renderer, &cliprect);
    SDL_SetRenderDrawColor(renderer, r, g, b, 0xFF);
    SDL_RenderFillRect(renderer, &rect);
    SDL_RenderSetClipRect(renderer, NULL);
    return TRUE;
} // WinFillRect

LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(WinBeginPaint, 703),
    LX_NATIVE_EXPORT(WinCreateMsgQueue, 716),
    LX_NATIVE_EXPORT(WinDestroyMsgQueue, 726),
    LX_NATIVE_EXPORT(WinDestroyWindow, 728),
    LX_NATIVE_EXPORT(WinEndPaint, 738),
    LX_NATIVE_EXPORT(WinFillRect, 743),
    LX_NATIVE_EXPORT(WinGetLastError, 753),
    LX_NATIVE_EXPORT(WinInitialize, 763),
    LX_NATIVE_EXPORT(WinTerminate, 888),
    LX_NATIVE_EXPORT(WinPostQueueMsg, 902),
    LX_NATIVE_EXPORT(WinCreateStdWindow, 908),
    LX_NATIVE_EXPORT(WinCreateWindow, 909),
    LX_NATIVE_EXPORT(WinDefWindowProc, 911),
    LX_NATIVE_EXPORT(WinDispatchMsg, 912),
    LX_NATIVE_EXPORT(WinGetMsg, 915),
    LX_NATIVE_EXPORT(WinPostMsg, 919),
    LX_NATIVE_EXPORT(WinSendMsg, 920),
    LX_NATIVE_EXPORT(WinRegisterClass, 926)
LX_NATIVE_MODULE_INIT_END()

// end of pmwin.c ...

