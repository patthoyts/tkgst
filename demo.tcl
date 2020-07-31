package require Tcl 8.6
package require Tk 8.6

set auto_path [linsert $auto_path 0 [pwd]/build]
package require tkgst 0.1.0

proc Exit {} {
    variable forever 1
}

proc Devices {gst} {
    foreach dev [$gst devices] {
        puts "$dev"
    }
}

proc OnBalance {gst op value} {
    $gst balance $op $value
}

proc Main {} {
    variable forever 0

    wm geometry . 800x600
    . configure -background grey40

    set f [ttk::frame .f]
    set gst [gst $f.gst -width 640 -height 480 -background black]
    set box [ttk::frame $f.box]
    foreach name {brightness contrast saturation hue} {
        set label[set name] [ttk::label $box.label$name -text "${name}:"]
        set $name [ttk::scale $box.$name -from -1000 -to 1000 -length 100 -value 0 \
            -command "OnBalance $gst -$name"]
    }
    set play [ttk::button $box.play -text "Play" -command [list $gst play]]
    set pause [ttk::button $box.pause -text "Pause" -command [list $gst pause]]
    set stop [ttk::button $box.stop -text "Stop" -command [list $gst stop]]
    set exit [ttk::button $box.exit -text "Exit" -command [list Exit]]

    grid x $labelbrightness $brightness \
           $labelcontrast $contrast \
           $labelsaturation $saturation \
           $labelhue $hue \
           $play $pause $stop $exit -sticky se
    grid rowconfigure  $box 1 -weight 1
    grid columnconfigure $box 0 -weight 1

    grid $gst -sticky news
    grid $box -sticky news
    grid rowconfigure  $f 0 -weight 1
    grid columnconfigure $f 0 -weight 1

    grid $f -sticky news
    grid rowconfigure . 0 -weight 1
    grid columnconfigure . 0 -weight 1

    bind $gst <Map> {bind %W <Map> {}; after 100 {%W play}}
    wm protocol . WM_DELETE_WINDOW Exit
    after 100 [list Devices $gst]

    vwait forever
    $gst stop
    destroy .
}

Main