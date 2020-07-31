#ifndef _tkgst_h_INCLUDE
#define _tkgst_h_INCLUDE

#include <tk.h>

#define REDRAW_PENDING   0x01
#define UPDATE_V_SCROLL  0x02
#define UPDATE_H_SCROLL  0x04

typedef struct {
                           /* widget core */
    Tk_Window tkwin;
    Tcl_Interp *interp;
    Tcl_Command widgetCmd;
    Tk_OptionTable optionTable;

    int      flags;        /* set of flags for the next draw */

    Tcl_Obj  *widthPtr;    /* widget options */
    int       width;
    Tcl_Obj  *heightPtr;
    int       height;
    Tcl_Obj  *anchorPtr;
    Tk_Anchor anchor;
    Tcl_Obj  *bgPtr;

    ClientData packageData;
    ClientData platformData;
    ClientData channelMap;

} WidgetData;


#endif /* !_tkgst_h_INCLUDE */
