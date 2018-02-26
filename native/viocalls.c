/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "viocalls.h"

// os2types.h defines these, but so does curses.
#undef TRUE
#undef FALSE

#include <unistd.h>
#include <ctype.h>

// CMake searches for a whole bunch of different possible curses includes
#if defined(HAVE_NCURSESW_NCURSES_H)
#include <ncursesw/ncurses.h>
#elif defined(HAVE_NCURSESW_CURSES_H)
#include <ncursesw/curses.h>
#elif defined(HAVE_NCURSESW_H)
#include <ncursesw.h>
#elif defined(HAVE_CURSES_H)
#include <curses.h>
#else
#error ncurses gui enabled, but no known header file found
#endif

#include <locale.h>

enum
{
    VIOATTR_BACK_BLACK = 0x00,
    VIOATTR_BACK_BLUE = 0x10,
    VIOATTR_BACK_GREEN = 0x20,
    VIOATTR_BACK_CYAN = 0x30,
    VIOATTR_BACK_RED = 0x40,
    VIOATTR_BACK_MAGENTA = 0x50,
    VIOATTR_BACK_YELLOW = 0x60,
    VIOATTR_BACK_BROWN = 0x60,
    VIOATTR_BACK_WHITE = 0x70,

    VIOATTR_FORE_BLACK = 0x00,
    VIOATTR_FORE_BLUE = 0x01,
    VIOATTR_FORE_GREEN = 0x02,
    VIOATTR_FORE_CYAN = 0x03,
    VIOATTR_FORE_RED = 0x04,
    VIOATTR_FORE_MAGENTA = 0x05,
    VIOATTR_FORE_YELLOW = 0x06,
    VIOATTR_FORE_BROWN = 0x06,
    VIOATTR_FORE_WHITE = 0x07,

    VIOATTR_INTENSITY = 0x08,
    VIOATTR_BLINK = 0x80
};


static uint16 *vio_buffer = NULL;
static uint16 vio_scrw, vio_scrh;
static uint16 vio_curx, vio_cury;
static VIOCURSORINFO vio_cursorinfo;

static int initNcurses(void)
{
    if (vio_buffer != NULL)
        return 1;

    setlocale(LC_CTYPE, ""); // !!! FIXME: we assume you have a UTF-8 terminal.
    if (initscr() == NULL) {
        fprintf(stderr, "ncurses: initscr() failed\n");
        return 0;
    } // if

	cbreak();
	keypad(stdscr, TRUE);
	noecho();
    start_color();
    use_default_colors();

    // map to VIO attributes...
    static const short curses_colormap[] = {
        COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN,
        COLOR_RED, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE
    };

    if (COLORS >= 64) {   // foreground and background
        for (uint32 i = 0; i < 8; i++) {
            for (uint32 j = 0; j < 8; j++) {
                const int pair = (int) (i | (j << 3));
                init_pair(pair, curses_colormap[i], (j == 0) ? -1 : curses_colormap[j]);
            } // for
        } // for
    } else if (COLORS >= 8) {  // just foregrounds
        for (int i = 0; i < 8; i++)
            init_pair(i, curses_colormap[i], COLOR_BLACK);
    } // else if

    // (otherwise, we won't set colors at all.)

    int scrh, scrw;
    getmaxyx(stdscr, scrh, scrw);

    const size_t buflen = scrw * scrh * sizeof (uint16);
    vio_buffer = malloc(buflen);
    if (!vio_buffer) {
        endwin();
        delwin(stdscr);  // not sure if this is safe, but valgrind said it leaks.
        stdscr = NULL;
        return 0;
    } // if

    memset(vio_buffer, '\0', buflen);
    vio_scrw = (uint16) scrw;
    vio_scrh = (uint16) scrh;
    vio_curx = vio_cury = 0;

    FIXME("these are just the default values OS/2 4.52 returns");
    vio_cursorinfo.yStart = 15;
    vio_cursorinfo.cEnd = 15;
    vio_cursorinfo.cx = 1;
    vio_cursorinfo.attr = 0;

    return 1;
} // initNcurses

static void deinitNcurses(void)
{
    if (!vio_buffer)
        return;

    // !!! FIXME: this is wrong
    //endwin();
    reset_shell_mode();

    printf("\n"); fflush(stdout);

    delwin(stdscr);  // not sure if this is safe, but valgrind said it leaks.
    stdscr = NULL;
    free(vio_buffer);
    vio_buffer = NULL;
    vio_curx = vio_cury = vio_scrw = vio_scrh = 0;
} // deinitNcurses

static inline void commitToNcurses(void)
{
    refresh();
} // commitToNcurses

static void pushToNcurses(const int y, const int x, int numcells, const int commit)
{
    const uint8 *src = (const uint8 *) (vio_buffer + ((y * vio_scrw) + x));
    const uint32 avail = (((vio_scrh - y) * vio_scrw) - x);
    if (numcells > avail)
        numcells = avail;

    move(y, x);

    for (int i = 0; i < numcells; i++) {
        const uint8 viochar = *(src++);
        const uint8 vioattr = *(src++);

        chtype ch;
        switch (viochar) {
            case 0xC4: ch = ACS_HLINE; break;
            case 0xB3: ch = ACS_VLINE; break;
            case 0xDA: ch = ACS_ULCORNER; break;
            case 0xBF: ch = ACS_URCORNER; break;
            case 0xC0: ch = ACS_LLCORNER; break;
            case 0xD9: ch = ACS_LRCORNER; break;
            default: ch = (chtype) viochar; break;
        } // switch

        chtype attr = 0;

        if (COLORS >= 64) {   // foreground and background
            const int color = ((vioattr & 0x70) >> 1) | (vioattr & 0x7);
            attr |= COLOR_PAIR(color);
        } else if (COLORS >= 8) {  // just foregrounds
            const int color = (vioattr & 0x7);
            attr |= COLOR_PAIR(color);
        } // else if

        if (vioattr & VIOATTR_INTENSITY)
            attr |= A_BOLD;
        if (vioattr & VIOATTR_BLINK)
            attr |= A_BLINK;

        addch(ch | attr);
    } // for

    move(vio_cury, vio_curx);

    if (commit)
        commitToNcurses();
} // pushToNcurses

APIRET16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio)
{
    TRACE_NATIVE("VioGetMode(%p, %u)", pvioModeInfo, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (pvioModeInfo == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (pvioModeInfo->cb != sizeof (*pvioModeInfo))
        return ERROR_VIO_INVALID_LENGTH;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    memset(pvioModeInfo, '\0', sizeof (*pvioModeInfo));
    pvioModeInfo->cb = sizeof (*pvioModeInfo);
    pvioModeInfo->fbType = VGMT_OTHER;
    pvioModeInfo->color = 4;
    pvioModeInfo->col = vio_scrw;
    pvioModeInfo->row = vio_scrh;
    pvioModeInfo->hres = 640;
    pvioModeInfo->vres = 400;
    pvioModeInfo->fmt_ID = 0;
    pvioModeInfo->attrib = 1;
    FIXME("fill in the rest of these");
    //ULONG buf_addr;
    //ULONG buf_length;
    //ULONG full_length;
    //ULONG partial_length;
    //PCHAR ext_data_addr;

    return NO_ERROR;
} // VioGetMode

static APIRET16 bridge16to32_VioGetMode(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PVIOMODEINFO, pvmi);
    return VioGetMode(pvmi, hvio);
} // bridge16to32_VioGetMode


APIRET16 VioGetCurPos(PUSHORT pusRow, PUSHORT pusColumn, HVIO hvio)
{
    TRACE_NATIVE("VioGetCurPos(%p, %p, %u)", pusRow, pusColumn, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    if (pusRow)
        *pusRow = vio_cury;

    if (pusColumn)
        *pusColumn = vio_curx;

    return NO_ERROR;
} // VioGetCurPos

static APIRET16 bridge16to32_VioGetCurPos(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PUSHORT, pusRow);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PUSHORT, pusColumn);
    return VioGetCurPos(pusRow, pusColumn, hvio);
} // bridge16to32_VioGetCurPos

APIRET16 VioGetBuf(PULONG pLVB, PUSHORT pcbLVB, HVIO hvio)
{
    TRACE_NATIVE("VioGetBuf(%p, %p, %u)", pLVB, pcbLVB, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    if (pLVB)
        *pLVB = (ULONG) vio_buffer;
    if (pcbLVB)
        *pcbLVB = vio_scrw * vio_scrh * sizeof (ULONG);
    return NO_ERROR;
} // VioGetBuf

static APIRET16 bridge16to32_VioGetBuf(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PUSHORT, pcbLVB);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PULONG, pLVB);
    const APIRET16 retval = VioGetBuf(pLVB, pcbLVB, hvio);
    *pLVB = GLoaderState->convert32to1616((void *) *pLVB);
    return retval;
} // bridge16to32_VioGetBuf

APIRET16 VioGetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio)
{
    TRACE_NATIVE("VioGetCurType(%p, %u)", pvioCursorInfo, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (pvioCursorInfo == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    memcpy(pvioCursorInfo, &vio_cursorinfo, sizeof (*pvioCursorInfo));
    return NO_ERROR;
} // VioGetCurType

static APIRET16 bridge16to32_VioGetCurType(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PVIOCURSORINFO, pvioCursorInfo);
    return VioGetCurType(pvioCursorInfo, hvio);
} // bridge16to32_VioGetCurType

APIRET16 VioScrollUp(USHORT usTopRow, USHORT usLeftCol, USHORT usBotRow, USHORT usRightCol, USHORT cbLines, PBYTE pCell, HVIO hvio)
{
    TRACE_NATIVE("VioScrollUp(%u, %u, %u, %u, %u, %p, %u)", (uint) usTopRow, (uint) usLeftCol, (uint) usBotRow, (uint) usRightCol, (uint) cbLines, pCell, (uint) hvio);
FIXME("buggy");
return NO_ERROR;  // !!! FIXME: buggy
    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (pCell == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    if (usLeftCol >= vio_scrw)
        usLeftCol = vio_scrw - 1;
    if (usRightCol >= vio_scrw)
        usRightCol = vio_scrw - 1;
    if (usTopRow >= vio_scrh)
        usTopRow = vio_scrh - 1;
    if (usBotRow >= vio_scrh)
        usBotRow = vio_scrh - 1;

    if (usLeftCol >= usRightCol)
        return NO_ERROR;  // already done.
    else if (usTopRow >= usBotRow)
        return NO_ERROR;  // already done.
    else if (cbLines == 0)
        return NO_ERROR;  // already done.

    const uint32 rowlen = (usRightCol - usLeftCol) + 1;
    const uint32 overlines = (cbLines > usTopRow) ? (cbLines - usTopRow) : 0;
    const uint32 collen = ((usBotRow - usTopRow) + 1) - overlines;
    const uint32 adjust = overlines * vio_scrw;

    uint16 *src = vio_buffer + ((usTopRow * vio_scrw) + usLeftCol) + adjust;
    uint16 *dst = (src - (cbLines * vio_scrw)) + adjust;
    const size_t rowcpylen = rowlen * sizeof (uint16);
    for (uint32 i = 0; i < collen; i++) {
        memcpy(dst, src, rowcpylen);
        src += vio_scrw;
        dst += vio_scrw;
    } // for

    const uint16 clear_cell = *((uint16 *) pCell);
    for (uint32 i = 0; i < cbLines; i++) {
        if (dst >= (vio_buffer + (vio_scrw * vio_scrh)))
            break;  // !!! FIXME: just calculate this outside the loop and adjust cbLines.
        uint16 *origdst = dst;
        for (uint32 j = 0; j < rowlen; j++) {
            *(dst++) = clear_cell;
        }
        dst = origdst + vio_scrw;
    } // for

    int starty = usTopRow - cbLines;
    if (starty < 0)
        starty = 0;
    int endy = starty + collen + cbLines;
    if (endy >= vio_scrh)
        endy = vio_scrh;

    for (int y = starty; y < endy; y++)
        pushToNcurses(y, usLeftCol, rowlen, 0);
    commitToNcurses();

    return NO_ERROR;
} // VioScrollUp

static APIRET16 bridge16to32_VioScrollUp(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PBYTE, pCell);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, cbLines);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usRightCol);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usBotRow);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usLeftCol);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usTopRow);
    return VioScrollUp(usTopRow, usLeftCol, usBotRow, usRightCol, cbLines, pCell, hvio);
} // bridge16to32_VioGetCurType

APIRET16 VioSetCurPos(USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioSetCurPos(%u, %u, %u)", (uint) usRow, (uint) usColumn, (uint) hvio);
    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    vio_cury = usRow;
    vio_curx = usColumn;

    move(vio_cury, vio_curx);
    refresh();

    return NO_ERROR;
} // VioSetCurPos

static APIRET16 bridge16to32_VioSetCurPos(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usColumn);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usRow);
    return VioSetCurPos(usRow, usColumn, hvio);
} // bridge16to32_VioSetCurPos

APIRET16 VioSetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio)
{
    TRACE_NATIVE("VioSetCurType(%p, %u)", pvioCursorInfo, (uint) hvio);
    FIXME("write me");
    return NO_ERROR;
} // VioSetCurType

static APIRET16 bridge16to32_VioSetCurType(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PVIOCURSORINFO, pvioCursorInfo);
    return VioSetCurType(pvioCursorInfo, hvio);
} // bridge16to32_VioSetCurType

APIRET16 VioReadCellStr(PCH pchCellStr, PUSHORT pcb, USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioReadCellStr(%p, %p, %u, %u, %u)", pchCellStr, pcb, (uint) usRow, (uint) usColumn, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint32 maxidx = ((uint32)vio_scrh) * ((uint32)vio_scrw);
    const uint32 idx = (((uint32)usRow) * ((uint32)vio_scrw)) + ((uint32)usColumn);
    const uint32 avail = (maxidx - idx) * sizeof (uint16);
    if (((uint32) *pcb) > avail)
        *pcb = (USHORT) avail;
    memcpy(pchCellStr, vio_buffer + idx, (size_t) *pcb);
    return NO_ERROR;
} // VioReadCellStr

static APIRET16 bridge16to32_VioReadCellStr(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usColumn);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usRow);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PUSHORT, pcb);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PCH, pchCellStr);
    return VioReadCellStr(pchCellStr, pcb, usRow, usColumn, hvio);
} // bridge16to32_VioReadCellStr

APIRET16 VioWrtCellStr(PCH pchCellStr, USHORT cb, USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioWrtCellStr(%p, %u, %u, %u, %u)", pchCellStr, (uint) cb, (uint) usRow, (uint) usColumn, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint16 *src = (uint16 *) pchCellStr;
    uint16 *dst = vio_buffer + ((usRow * vio_scrw) + usColumn);
    const uint32 avail = (((vio_scrh - usRow) * vio_scrw) - usColumn) * sizeof (uint16);
    if (((uint32) cb) > avail)
        cb = (USHORT) avail;

    memcpy(dst, src, cb);  // !!! FIXME: what happens if cb extends into half a cell?

    pushToNcurses(usRow, usColumn, cb / sizeof (uint16), 1);

    return NO_ERROR;
} // VioWrtCellStr

static APIRET16 bridge16to32_VioWrtCellStr(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usColumn);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usRow);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, cb);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PCH, pchCellStr);
    return VioWrtCellStr(pchCellStr, cb, usRow, usColumn, hvio);
} // bridge16to32_VioWrtCellStr

APIRET16 VioWrtCharStrAtt(PCH pch, USHORT cb, USHORT usRow, USHORT usColumn, PBYTE pAttr, HVIO hvio)
{
    TRACE_NATIVE("VioWrtCharStrAtt(%p, %u, %u, %u, %p, %u)", pch, (uint) cb, (uint) usRow, (uint) usColumn, pAttr, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint8 attr = *pAttr;
    const uint8 *src = (uint8 *) pch;
    uint8 *dst = (uint8 *) (vio_buffer + ((usRow * vio_scrw) + usColumn));
    const uint32 avail = (((vio_scrh - usRow) * vio_scrw) - usColumn);
    if (((uint32) cb) > avail)
        cb = (USHORT) avail;

    for (uint32 i = 0; i < cb; i++, src++) {
        *(dst++) = *src;
        *(dst++) = attr;
    } // for

    pushToNcurses(usRow, usColumn, cb, 1);

    return NO_ERROR;
} // VioWrtCharStrAtt

static APIRET16 bridge16to32_VioWrtCharStrAtt(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PBYTE, pAttr);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usColumn);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usRow);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, cb);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PCH, pch);
    return VioWrtCharStrAtt(pch, cb, usRow, usColumn, pAttr, hvio);
} // bridge16to32_VioWrtCharStrAtt


APIRET16 VioWrtNCell(PBYTE pCell, USHORT cb, USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioWrtNCell(%p, %u, %u, %u, %u)", pCell, (uint) cb, (uint) usRow, (uint) usColumn, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint16 cell = *((uint16 *) pCell);
    uint16 *dst = vio_buffer + ((usRow * vio_scrw) + usColumn);
    const uint32 avail = (((vio_scrh - usRow) * vio_scrw) - usColumn);
    if (((uint32) cb) > avail)
        cb = (USHORT) avail;

    for (uint32 i = 0; i < cb; i++)
        *(dst++) = cell;

    pushToNcurses(usRow, usColumn, cb, 1);

    return NO_ERROR;
} // VioWrtNCell

static APIRET16 bridge16to32_VioWrtNCell(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HVIO, hvio);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usColumn);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, usRow);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, cb);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PBYTE, pCell);
    return VioWrtNCell(pCell, cb, usRow, usColumn, hvio);
} // bridge16to32_VioWrtNCell


LX_NATIVE_MODULE_16BIT_SUPPORT()
    LX_NATIVE_MODULE_16BIT_API(VioScrollUp)
    LX_NATIVE_MODULE_16BIT_API(VioGetCurPos)
    LX_NATIVE_MODULE_16BIT_API(VioWrtCellStr)
    LX_NATIVE_MODULE_16BIT_API(VioSetCurPos)
    LX_NATIVE_MODULE_16BIT_API(VioGetMode)
    LX_NATIVE_MODULE_16BIT_API(VioReadCellStr)
    LX_NATIVE_MODULE_16BIT_API(VioGetCurType)
    LX_NATIVE_MODULE_16BIT_API(VioGetBuf)
    LX_NATIVE_MODULE_16BIT_API(VioSetCurType)
    LX_NATIVE_MODULE_16BIT_API(VioWrtCharStrAtt)
    LX_NATIVE_MODULE_16BIT_API(VioWrtNCell)
LX_NATIVE_MODULE_16BIT_SUPPORT_END()

static int initViocalls(void)
{
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT()
        LX_NATIVE_INIT_16BIT_BRIDGE(VioScrollUp, 26)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioGetCurPos, 6)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioWrtCellStr, 12)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioSetCurPos, 6)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioGetMode, 6)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioReadCellStr, 12)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioGetCurType, 6)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioGetBuf, 10)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioSetCurType, 6)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioWrtCharStrAtt, 16)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioWrtNCell, 12)
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END()
    return 1;
} // initViocalls

LX_NATIVE_MODULE_INIT({ if (!initViocalls()) return NULL; })
    LX_NATIVE_EXPORT16(VioScrollUp, 7),
    LX_NATIVE_EXPORT16(VioGetCurPos, 9),
    LX_NATIVE_EXPORT16(VioWrtCellStr, 10),
    LX_NATIVE_EXPORT16(VioSetCurPos, 15),
    LX_NATIVE_EXPORT16(VioGetMode, 21),
    LX_NATIVE_EXPORT16(VioReadCellStr, 24),
    LX_NATIVE_EXPORT16(VioGetCurType, 27),
    LX_NATIVE_EXPORT16(VioGetBuf, 31),
    LX_NATIVE_EXPORT16(VioSetCurType, 32),
    LX_NATIVE_EXPORT16(VioWrtCharStrAtt, 48),
    LX_NATIVE_EXPORT16(VioWrtNCell, 52)
LX_NATIVE_MODULE_INIT_END()

LX_NATIVE_MODULE_DEINIT({
    deinitNcurses();
    LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT();
})

// end of viocalls.c ...

