# GStreamer video widget for Tk.

This Tcl/Tk extension provides a widget that presents a GStreamer video
pipeline in a Tk window.

This is for exploring the use of GStreamer with the intention to include this
for unix support in Tkvideo. As such the Tcl programming interface exposed
is not final and may change at any time.

Currently this just streams the first video capture device (/dev/video0) to
the embedded window.

There are bugs.

Needs gstreamer-video-1.0 which is provided by the apt package libgstreamer-plugins-base1.0-dev
