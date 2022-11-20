#include "tkgst.h"
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>
#include <gst/video/colorbalance.h>
#include <gst/gstparse.h>
#include <string.h>

#define DEF_VIDEO_BACKGROUND   "white"
#define DEF_VIDEO_WIDTH        "100"
#define DEF_VIDEO_HEIGHT       "100"
#define DEF_VIDEO_SOURCE       ""
#define DEF_VIDEO_SCROLL_CMD   ""
#define DEF_VIDEO_STRETCH      "0"
#define DEF_VIDEO_CURSOR       ""
#define DEF_VIDEO_TAKE_FOCUS   "0"
#define DEF_VIDEO_OUTPUT       ""
#define DEF_VIDEO_ANCHOR       "center"
#define DEF_VIDEO_DEVICE       "/dev/video0"

#define VIDEO_SOURCE_CHANGED   0x01
#define VIDEO_GEOMETRY_CHANGED 0x02
#define VIDEO_OUTPUT_CHANGED   0x04

static Tk_OptionSpec optionSpec[] = {
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor",
        DEF_VIDEO_ANCHOR, Tk_Offset(WidgetData, anchorPtr), -1, 0, 0, VIDEO_GEOMETRY_CHANGED },
     {TK_OPTION_SYNONYM, "-bg", (char *) NULL, (char *) NULL,
        (char *) NULL, 0, -1, 0, (ClientData) "-background"},
    {TK_OPTION_BORDER, "-background", "background", "Background",
        DEF_VIDEO_BACKGROUND, Tk_Offset(WidgetData, bgPtr), -1, 0, 0, 0},
    {TK_OPTION_STRING, "-height", "height", "Height",
        DEF_VIDEO_HEIGHT, Tk_Offset(WidgetData, heightPtr), -1, 0, 0, VIDEO_GEOMETRY_CHANGED},
    {TK_OPTION_STRING, "-width", "width", "Width",
        DEF_VIDEO_WIDTH, Tk_Offset(WidgetData, widthPtr), -1, 0, 0, VIDEO_GEOMETRY_CHANGED},
    {TK_OPTION_STRING, "-device", "device", "Device",
        DEF_VIDEO_DEVICE, Tk_Offset(WidgetData, devicePtr), -1, 0, 0, 0},
    {TK_OPTION_END, (char *)NULL, (char *)NULL, (char*)NULL,
        (char *)NULL, 0, 0, 0, 0}
};

typedef struct {
    GList *busses;
    GstDeviceMonitor *monitor;
    GList *devices;
} PackageData;

typedef struct {
    Tcl_Event event;
    PackageData *package;
    GstBus *bus;
} GstTclEvent ;

static int WorldChanged(ClientData clientData);
static void CalculateGeometry(WidgetData *dataPtr);
static int Configure(Tcl_Interp *interp, WidgetData *dataPtr, int objc, Tcl_Obj *CONST objv[]);

static int GstWidgetCgetCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int GstWidgetConfigureCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int GstWidgetPlayCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int GstWidgetPauseCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int GstWidgetStopCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int GstWidgetDevicesCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int GstWidgetBalanceCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

struct Ensemble {
    const char *name;          /* subcommand name */
    Tcl_ObjCmdProc *command;   /* subcommand implementation OR */
    struct Ensemble *ensemble; /* subcommand ensemble */
};

struct Ensemble WidgetEnsemble[] = {
    { "configure", GstWidgetConfigureCmd, NULL },
    { "cget",      GstWidgetCgetCmd, NULL },
    { "play",      GstWidgetPlayCmd, NULL },
    { "pause",     GstWidgetPauseCmd, NULL },
    { "stop",      GstWidgetStopCmd, NULL },
    { "devices",   GstWidgetDevicesCmd, NULL },
    { "balance",   GstWidgetBalanceCmd, NULL },
    { NULL, NULL, NULL }
};

static int SetColorBalance(Tcl_Interp *interp, Tcl_Obj *valueObj, GstColorBalance *balance, GstColorBalanceChannel *channel)
{
    double value = 0;
    int r = Tcl_GetDoubleFromObj(interp, valueObj, &value);
    if (TCL_OK == r) {
        gst_color_balance_set_value(balance, channel, (gint)(value + 0.5));
        gint rval = gst_color_balance_get_value(balance, channel);
        Tcl_Obj *rObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, rObj, Tcl_NewStringObj(channel->label, -1));
        Tcl_ListObjAppendElement(interp, rObj, Tcl_NewIntObj(rval));
        Tcl_SetObjResult(interp, rObj);
    }
    return r;
}

static int GstWidgetBalanceCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    enum {OPT_BRIGHTNESS, OPT_CONTRAST, OPT_HUE, OPT_SATURATION};
    static const char *opts[] = {
        "-brightness", "-contrast", "-hue", "-saturation", NULL
    };
    static const char *chanNames[] = {
        "XV_BRIGHTNESS", "XV_CONTRAST", "XV_HUE", "XV_SATURATION", NULL
    };
    WidgetData *dataPtr = (WidgetData *)clientData;
    PackageData *packagePtr = (PackageData *)dataPtr->packageData;
    GstPipeline *pipeline = (GstPipeline *)dataPtr->platformData;
    int r;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "balance ?options?");
        return TCL_ERROR;
    }

    if (pipeline == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("pipeline is not created", -1));
        return TCL_ERROR;
    }

    GstElement *elt = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_COLOR_BALANCE);
    if (elt == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("pipeline does not support color balance", -1));
        return TCL_ERROR;
    }
    GstColorBalance *balance = GST_COLOR_BALANCE(elt);
    GData *channelMap = (GData *)dataPtr->channelMap;

    for (int optindex = 2; optindex < objc; optindex += 2) {
        double value = 0;
        int index = 0;
        if (Tcl_GetIndexFromObj(interp, objv[2], opts, "option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        GstColorBalanceChannel *channel = GST_COLOR_BALANCE_CHANNEL(g_datalist_get_data(&channelMap, chanNames[index]));
        if (optindex +1 < objc) {
            SetColorBalance(interp, objv[optindex+1], balance, channel);
        } else {
            Tcl_SetObjResult(interp, Tcl_NewIntObj(gst_color_balance_get_value(balance, channel)));
        }
    }
    return TCL_OK;
}

static int GstWidgetDevicesCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "devices"); // TODO: add category audio|video to select source types.
        return TCL_ERROR;
    }
    WidgetData *dataPtr = (WidgetData *)clientData;
    PackageData *packagePtr = (PackageData *)dataPtr->packageData;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
    GList *devices = gst_device_monitor_get_devices(GST_DEVICE_MONITOR(packagePtr->monitor));
    for (GList *dev = packagePtr->devices; dev != NULL; dev = dev->next)
    {
        GstDevice *device = GST_DEVICE(dev->data);
        gchar *name = gst_device_get_display_name(device);
        gchar *devclass = gst_device_get_device_class(device);
        GstCaps *caps = gst_device_get_caps(device);
        {
            gchar *capsstr = gst_caps_serialize(caps, GST_SERIALIZE_FLAG_NONE);
            g_message("caps: %s\n", capsstr);
            g_free(capsstr);
        }
        GstStructure *props = gst_device_get_properties(device);
        const gchar *device_path = gst_structure_get_string(props, "device.path");
        {
            gchar *propsstr = gst_structure_serialize(props, GST_SERIALIZE_FLAG_NONE);
            g_message("device props: %s\n", propsstr);
            g_free(propsstr);
        }

        Tcl_Obj *devObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, devObj, Tcl_NewStringObj("name", 4));
        Tcl_ListObjAppendElement(interp, devObj, Tcl_NewStringObj(name, -1));
        Tcl_ListObjAppendElement(interp, devObj, Tcl_NewStringObj("device_class", 12));
        Tcl_ListObjAppendElement(interp, devObj, Tcl_NewStringObj(devclass, -1));
        Tcl_ListObjAppendElement(interp, devObj, Tcl_NewStringObj("path", 4));
        Tcl_ListObjAppendElement(interp, devObj, Tcl_NewStringObj(device_path, -1));

        gst_structure_free(props);
        g_free(name);
        g_free(devclass);
        Tcl_ListObjAppendElement(interp, resultObj, devObj);
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

static GData *GetColorBalanceChannelMap(GstPipeline *pipeline)
{
    GData *channelMap = NULL;
    GstElement *elt = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_COLOR_BALANCE);
    if (elt) {
        GstColorBalance *balance = GST_COLOR_BALANCE(elt);
        const GList *channels = gst_color_balance_list_channels(balance);
        g_datalist_init(&channelMap);
        for (const GList *chan = channels; chan != NULL; chan = chan->next) {
            GstColorBalanceChannel *channel = GST_COLOR_BALANCE_CHANNEL(chan->data);
            //g_message("channel %s [%d .. %d]", channel->label, channel->min_value, channel->max_value);
            g_datalist_set_data_full(&channelMap, channel->label, channel, NULL);
        }
    }
    return channelMap;
}

static GstPipeline *CreateVideoPipeline(WidgetData *dataPtr, const gchar *name, guintptr window_id)
{
    const char *desc = "v4l2src device=%s ! videoconvert ! videoscale ! videobalance ! xvimagesink";
    Tcl_Obj *descObj = Tcl_ObjPrintf(desc, Tcl_GetString(dataPtr->devicePtr));

    GError *err = NULL;
    GstParseFlags flags = GST_PARSE_FLAG_NONE;
    GstParseContext *parseContext = gst_parse_context_new();
    GstElement *parsed = gst_parse_launch_full(Tcl_GetString(descObj), parseContext, flags, &err);
    gst_parse_context_free(parseContext);
    if (err) {
        GST_ERROR("pipeline error: %s\n", err->message);
        g_error_free(err);
        return NULL;
    }
    GstElement *sink = gst_bin_get_by_interface(GST_BIN(parsed), GST_TYPE_VIDEO_OVERLAY);
    //GstElement *sink = gst_bin_get_by_name(GST_BIN(parsed), "sink");
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (sink), window_id);
    GstPipeline *pipeline = GST_PIPELINE(parsed);

#ifdef MANUAL_CONSTRUCTION
    GstPipeline *pipeline = GST_PIPELINE(gst_pipeline_new(name));

    // TODO: select video device from config.
    // TODO: look into parsing a string description (gst_parse_launch)
    //       the gst_get_by_name(GST_BIN(pipeline), name); to get elements
    GstElement *src = gst_element_factory_make ("v4l2src", NULL);
    g_object_set(src, "device", "/dev/video0", NULL);
    GstElement *cnv = gst_element_factory_make ("videoconvert", NULL);
    GstElement *scale = gst_element_factory_make ("videoscale", NULL);
    GstElement *bal = gst_element_factory_make ("videobalance", NULL);
    GstElement *sink = gst_element_factory_make ("xvimagesink", NULL);
    gst_bin_add_many (GST_BIN (pipeline), src, cnv, scale, bal, sink, NULL);
    gst_element_link_many (src, cnv, scale, bal, sink, NULL);
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (sink), window_id);
#endif
    return pipeline;
}

static int GstWidgetPlayCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    WidgetData *dataPtr = (WidgetData *)clientData;
    PackageData *packagePtr = (PackageData *)dataPtr->packageData;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "play");
        return TCL_ERROR;
    }
    GstPipeline *pipeline = (GstPipeline *)dataPtr->platformData;
    if (pipeline == NULL) {
        pipeline = CreateVideoPipeline(dataPtr, Tk_Name(dataPtr->tkwin), Tk_WindowId(dataPtr->tkwin));
    }
    if (pipeline != NULL) {
        dataPtr->platformData = (ClientData)pipeline;
        // Register the pipeline bus with the Tcl notifier
        GstBus *bus = gst_pipeline_get_bus(pipeline);
        packagePtr->busses = g_list_append(packagePtr->busses, bus);
        dataPtr->channelMap = (ClientData)GetColorBalanceChannelMap(pipeline);
    } else {
        return TCL_ERROR;
    }

    GstStateChangeReturn r = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
    static const char *states[] = { "failure", "success", "async", "no_preroll", NULL };
    Tcl_SetObjResult(interp, Tcl_NewStringObj(states[(int)r], -1));
    return TCL_OK;
}

static int GstWidgetPauseCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "pause");
        return TCL_ERROR;
    }
    WidgetData *dataPtr = (WidgetData *)clientData;
    GstPipeline *pipeline = (GstPipeline *)dataPtr->platformData;
    GstStateChangeReturn r = gst_element_set_state (GST_ELEMENT(pipeline), GST_STATE_PAUSED);
    return TCL_OK;
}

static int GstWidgetStopCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "stop");
        return TCL_ERROR;
    }
    WidgetData *dataPtr = (WidgetData *)clientData;
    PackageData *packagePtr = (PackageData *)dataPtr->packageData;
    GstPipeline *pipeline = (GstPipeline *)dataPtr->platformData;
    GstStateChangeReturn r = gst_element_set_state (GST_ELEMENT(pipeline), GST_STATE_NULL);
    return TCL_OK;
}

static int
GstWidgetCgetCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    WidgetData *dataPtr = (WidgetData *)clientData;
    Tcl_Obj *resultPtr = NULL;
    int r = TCL_OK;

    Tcl_Preserve(clientData);

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "option");
        r = TCL_ERROR;
    } else {
        resultPtr = Tk_GetOptionValue(interp, (char *)dataPtr,
            dataPtr->optionTable, objv[2], dataPtr->tkwin);
        if (resultPtr == NULL)
            r = TCL_ERROR;
        else
            Tcl_SetObjResult(interp, resultPtr);
    }

    Tcl_Release(clientData);
    return r;
}

static int
GstWidgetConfigureCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    WidgetData *dataPtr = (WidgetData *)clientData;
    Tcl_Obj *resultPtr = NULL;
    int r = TCL_OK;

    Tcl_Preserve(clientData);

    if (objc < 4) {
        Tcl_Obj * const optionPtr = (objc == 3) ? objv[2] : NULL;
        resultPtr = Tk_GetOptionInfo(interp, (char *)dataPtr,
            dataPtr->optionTable, optionPtr, dataPtr->tkwin);
        r = (resultPtr != NULL) ? TCL_OK : TCL_ERROR;
    } else {
        r = Configure(interp, dataPtr, objc - 2, objv + 2);
    }
    if (resultPtr != NULL)
        Tcl_SetObjResult(interp, resultPtr);

    Tcl_Release(clientData);
    return r;
}

static int Configure(Tcl_Interp *interp, WidgetData *dataPtr, int objc, Tcl_Obj *CONST objv[])
{
    Tk_Window tkwin = dataPtr->tkwin;
    Tk_SavedOptions savedOptions;
    int flags = 0, r = TCL_OK;

    r = Tk_SetOptions(interp, (char *)dataPtr, dataPtr->optionTable, objc, objv,
                      dataPtr->tkwin, &savedOptions, &flags);
    if (r == TCL_OK)
        r = WorldChanged((ClientData) dataPtr);
    else
        Tk_RestoreSavedOptions(&savedOptions);
    Tk_FreeSavedOptions(&savedOptions);

    if (r == TCL_OK)
        r = Tk_GetAnchorFromObj(dataPtr->interp, dataPtr->anchorPtr, &dataPtr->anchor);

    if (r == TCL_OK) {
        Tk_3DBorder bg = Tk_Get3DBorderFromObj(tkwin, dataPtr->bgPtr);
        Tk_SetWindowBackground(tkwin, Tk_3DBorderColor(bg)->pixel);

        if (Tk_GetPixelsFromObj(interp, tkwin, dataPtr->widthPtr, &dataPtr->width) != TCL_OK) {
            Tcl_AddErrorInfo(interp, "\n    (processing -width option)");
        }
        if (Tk_GetPixelsFromObj(interp, tkwin, dataPtr->heightPtr, &dataPtr->height) != TCL_OK) {
            Tcl_AddErrorInfo(interp, "\n    (processing -height option)");
        }

        CalculateGeometry(dataPtr);

        r = WorldChanged((ClientData)dataPtr);
    }
    return r;
}

static void GstWidgetDisplay(ClientData clientData)
{
    WidgetData *dataPtr = (WidgetData *)clientData;
    Tk_Window tkwin = dataPtr->tkwin;

    dataPtr->flags &= ~REDRAW_PENDING;
    if (!Tk_IsMapped(tkwin)) {
        return;
    }

    if (dataPtr->platformData == NULL) {
        Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, dataPtr->bgPtr);
        Tk_Fill3DRectangle(tkwin, Tk_WindowId(tkwin), border, 0, 0,
            Tk_Width(tkwin), Tk_Height(tkwin), 0, TK_RELIEF_FLAT);
    }
}

static void CalculateGeometry(WidgetData *dataPtr)
{
    Tk_GeometryRequest(dataPtr->tkwin, dataPtr->width, dataPtr->height);
}

static int WorldChanged(ClientData clientData)
{
    WidgetData *dataPtr = (WidgetData *)clientData;

    if (!(dataPtr->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(GstWidgetDisplay, clientData);
        dataPtr->flags |= REDRAW_PENDING;
    }

    return TCL_OK;
}

static void GstWidgetCleanup(char *memPtr)
{
    g_message("tk widget cleanup");
    //Clear up the GStreamer pipeline and unregister the bus
    WidgetData *dataPtr = (WidgetData *)memPtr;
    GData *channelMap = (GData *)dataPtr->channelMap;
    g_datalist_clear(&channelMap);
    ckfree(memPtr);
}

static void GstWidgetEventProc(ClientData clientData, XEvent *eventPtr)
{
    WidgetData *dataPtr = (WidgetData *)clientData;

    if (eventPtr->type == Expose) {

        if (!(dataPtr->flags & REDRAW_PENDING)) {
            Tcl_DoWhenIdle(GstWidgetDisplay, clientData);
            dataPtr->flags |= REDRAW_PENDING;
        }

    } else if (eventPtr->type == ConfigureNotify) {

        CalculateGeometry(dataPtr);
        WorldChanged(clientData);

    } else if (eventPtr->type == DestroyNotify) {
        if (dataPtr->tkwin != NULL) {
            g_message("tk event proc destroynotify %s", Tk_Name(dataPtr->tkwin));
            Tk_FreeConfigOptions((char *)dataPtr, dataPtr->optionTable, dataPtr->tkwin);
            Tcl_DeleteCommandFromToken(dataPtr->interp, dataPtr->widgetCmd);
        }
        if (dataPtr->flags & REDRAW_PENDING) {
            Tcl_CancelIdleCall(GstWidgetDisplay, clientData);
            dataPtr->flags &= ~REDRAW_PENDING;
        }
        Tcl_EventuallyFree(clientData, GstWidgetCleanup);
    }
}

static void GstWidgetDeleteProc(ClientData clientData)
{
    WidgetData *dataPtr = (WidgetData *)clientData;
    if (dataPtr->tkwin != NULL) {
        g_message("tk widget delete proc %s", Tk_Name(dataPtr->tkwin));
        if (dataPtr->platformData != NULL) {
            g_message("cleanup gst pipeline");
            gst_object_unref(GST_ELEMENT(dataPtr->platformData));
        }
        Tk_DestroyWindow(dataPtr->tkwin);
        dataPtr->tkwin = NULL;
    }
}

static int GstWidgetObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    struct Ensemble *ensemble = WidgetEnsemble;
    int optPtr = 1;
    int index;

    while (optPtr < objc) {
        if (Tcl_GetIndexFromObjStruct(interp, objv[optPtr], ensemble, sizeof(ensemble[0]), "command", 0, &index) != TCL_OK)
        {
            return TCL_ERROR;
        }

        if (ensemble[index].command) {
            return ensemble[index].command(clientData, interp, objc, objv);
        }
        ensemble = ensemble[index].ensemble;
        ++optPtr;
    }
    Tcl_WrongNumArgs(interp, optPtr, objv, "option ?arg arg...?");
    return TCL_ERROR;
}

static int GstObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    PackageData *packagePtr = (PackageData *)clientData;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?options?");
        return TCL_ERROR;
    }

    Tk_Window tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp),
        Tcl_GetStringFromObj(objv[1], NULL), (char *)NULL);
    if (tkwin == NULL) {
        return TCL_ERROR;
    }

    Tk_SetClass(tkwin, "Gst");
    Tk_OptionTable optionTable = Tk_CreateOptionTable(interp, optionSpec);

    WidgetData *dataPtr = (WidgetData *)Tcl_Alloc(sizeof(WidgetData));
    memset(dataPtr, 0, sizeof(WidgetData));
    dataPtr->tkwin = tkwin;
    dataPtr->interp = interp;
    dataPtr->packageData = clientData;
    dataPtr->optionTable = optionTable;
    dataPtr->widgetCmd = Tcl_CreateObjCommand(interp, Tk_PathName(tkwin), GstWidgetObjCmd, (ClientData)dataPtr, GstWidgetDeleteProc);

    if (Tk_InitOptions(interp, (char *)dataPtr, optionTable, tkwin) != TCL_OK) {
        Tk_DestroyWindow(tkwin);
        Tcl_Free((char *)dataPtr);
        return TCL_ERROR;
    }

    Tk_CreateEventHandler(tkwin, ExposureMask | StructureNotifyMask, GstWidgetEventProc, (ClientData)dataPtr);

    if (Configure(interp, dataPtr, objc - 2, objv + 2) != TCL_OK) {
        Tk_DestroyWindow(tkwin);
        Tcl_Free((char *)dataPtr);
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(Tk_PathName(tkwin), -1));
    return TCL_OK;
}
/*
 * Free the package data structure.
 * All the GStreamer bus items need to be released and the device monitor
 * stopped and unreferenced.
 * Also deinitialize the GStreamer library itself.
 */
static void GstPkgCleanup(void *clientData)
{
    PackageData *packagePtr = (PackageData *)clientData;
    gst_device_monitor_stop(packagePtr->monitor);
    gst_object_unref(packagePtr->monitor);
    for (GList *bus = packagePtr->busses; bus != NULL; bus = bus->next) {
        gst_object_unref(bus);
    }
    g_list_free(packagePtr->busses);
    g_list_free(packagePtr->devices);
    gst_deinit();
    Tcl_Free((char *)packagePtr);
}

// Handle GStreamer message bus events.
static int EventProc(Tcl_Event *evPtr, int flags)
{
    GstTclEvent *event = (GstTclEvent *)evPtr;
    GstMessage *message = NULL;
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return 0;
    }
    while ((message = gst_bus_pop(GST_BUS(event->bus))) != NULL) {
        switch (GST_MESSAGE_TYPE(message))
        {
            case GST_MESSAGE_DEVICE_ADDED:
                {
                    GstDevice *device;
                    gst_message_parse_device_added(message, &device);
                    gchar *name = gst_device_get_display_name(device);
                    g_message("device add \"%s\"", name);
                    g_free(name);
                    gst_object_unref(device);
                }
                break;
            case GST_MESSAGE_DEVICE_REMOVED:
                {
                    GstDevice *device;
                    gst_message_parse_device_removed(message, &device);
                    gchar *name = gst_device_get_display_name(device);
                    g_message("device removed \"%s\"", name);
                    g_free(name);
                    gst_object_unref(device);
                }
                break;
            case GST_MESSAGE_DEVICE_CHANGED:
                {
                    GstDevice *device;
                    gst_message_parse_device_changed(message, &device, NULL);
                    gchar *name = gst_device_get_display_name(device);
                    g_message("device change \"%s\"", name);
                    g_free(name);
                    gst_object_unref (device);
                }
                break;
            case GST_MESSAGE_ELEMENT:
                {
                    const GstStructure *s = gst_message_get_structure(message);
                    GstNavigationMessageType mt = gst_navigation_message_get_type(message);
                    if (mt == GST_NAVIGATION_MESSAGE_EVENT)
                    {
                        GstEvent *ge;
                        if (gst_navigation_message_parse_event(message, &ge))
                        {
                            gboolean br = False, button_press = False;
                            gint button = 0;
                            gdouble x = 0, y = 0;
                            const gchar *keys = NULL;

                            switch (gst_navigation_event_get_type(ge))
                            {
                                case GST_NAVIGATION_EVENT_MOUSE_MOVE:
                                    br = gst_navigation_event_parse_mouse_move_event(ge, &x, &y);
                                    if (br)
                                        g_message("mouse move @%.1f,%.1f", x, y);
                                    break;
                                case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
                                    button_press = True;
                                    /* FALL THROUGH */
                                case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
                                    br = gst_navigation_event_parse_mouse_button_event(ge, &button, &x, &y);
                                    if (br)
                                        g_message("mouse event @%.1f,%.1f %d %s", x, y, button, button_press ? "press" : "release");
                                    break;
                                case GST_NAVIGATION_EVENT_KEY_PRESS:
                                case GST_NAVIGATION_EVENT_KEY_RELEASE:
                                    br = gst_navigation_event_parse_key_event(ge, &keys);
                                    if (br)
                                        g_message("key event %s", keys);
                                    break;
                            }
                            gst_event_unref(ge);
                        }
                    }
                    else {
                        g_message("\"%s\"", gst_structure_get_name(s));
                    }
                }
                break;
            case GST_MESSAGE_STATE_CHANGED:
                {
                    GstState oldstate, newstate, pending;
                    gst_message_parse_state_changed(message, &oldstate, &newstate, &pending);
                    g_message("gst state-changed %d -> %d pending %d", oldstate, newstate, pending);
                }
                break;
            case GST_MESSAGE_ERROR:
                {
                    GError *err = NULL;
                    gchar *debugInfo = NULL;
                    gst_message_parse_error(message, &err, &debugInfo);
                    g_message("error from '%s': %s", GST_OBJECT_NAME(message->src), err->message);
                    if (debugInfo) {
                        g_message("%s", debugInfo);
                    }
                    g_error_free(err);
                    g_free(debugInfo);
                }
                break;
            case GST_MESSAGE_EOS:
            case GST_MESSAGE_WARNING:
                //Tcl_QueueEvent(TkGstErrEvent, TCL_QUEUE_TAIL);
                //break;
            default:
                g_message("gst message \"%s\"", gst_message_type_get_name(GST_MESSAGE_TYPE(message)));
        }
        gst_message_unref(message);
    }
    return 1;
}

// Called at intervals determined by SetupProc to check the gst bus
// for messages and if any schedule a Tcl event to get them processed.
static void CheckProc(ClientData clientData, int flags)
{
    PackageData *packagePtr = (PackageData *)clientData;
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }
    for (GList *node = packagePtr->busses; node != NULL; node = node->next) {
        if (gst_bus_have_pending(GST_BUS(node->data))) {
            GstTclEvent *event = (GstTclEvent *)Tcl_Alloc(sizeof(GstTclEvent));
            event->event.proc = EventProc;
            event->bus = GST_BUS(node->data);
            Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);
        }
    }
}

// Check the gst bus for pending events and set the block interval
// to either immediate or 10ms for calling CheckProp
static void SetupProc(ClientData clientData, int flags)
{
    PackageData *packagePtr = (PackageData *)clientData;
    Tcl_Time block_time = {0, 10000};
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }
    for (GList *node = packagePtr->busses; node != NULL; node = node->next) {
        if (gst_bus_have_pending(GST_BUS(node->data))) {
            block_time.usec = 0;
            break;
        }
    }
    Tcl_SetMaxBlockTime(&block_time);
}

int Tkgst_Init(Tcl_Interp *interp)
{
    int r = TCL_OK;
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL)
        return TCL_ERROR;
    if (Tk_InitStubs(interp, TK_VERSION, 0) == NULL)
        return TCL_ERROR;
    if (r == TCL_OK) {

        gst_init(NULL, NULL);

        // NOTE: each pipeline has one bus. The event source will need to be a list of bus objects,
        //       as we should have one pipeline per widget therefore each video widget will have its
        //       own bus.
        PackageData *packagePtr = (PackageData *)Tcl_Alloc(sizeof(PackageData));
        memset(packagePtr, 0, sizeof(PackageData));

        // start a device monitor for use with the widget "devices" command.
        packagePtr->monitor = gst_device_monitor_new();
        gst_device_monitor_start(packagePtr->monitor);
        packagePtr->devices = gst_device_monitor_get_devices(packagePtr->monitor); // FIX ME: maybe not needed

        packagePtr->busses = g_list_append(packagePtr->busses, gst_device_monitor_get_bus(packagePtr->monitor));

        Tcl_CreateEventSource(SetupProc, CheckProc, (ClientData)packagePtr);
        Tcl_CreateObjCommand(interp, "gst", GstObjCmd, (ClientData)packagePtr, GstPkgCleanup);
        r = Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION);
    }
    return r;
}
