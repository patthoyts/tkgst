# Demonstrate simple use of tkgst video widget
#
# eg: tkcon -master .tkcon -eval '' -- source ../demo.tcl
#

package require Tcl 8.6
package require Tk 8.6

set build_path [file normalize [file join [file dirname [info script]] build]]
set auto_path [linsert $auto_path 0 $build_path]

package require tkgst 0.1.0

proc Exit {} {
    variable forever 1
}

proc Devices {gst} {
    foreach dev [$gst devices] {
        puts "$dev"
    }
}

proc OnBalance {scalew labelw gst op value} {
    $gst balance $op $value
    SetScaleLabel $scalew $labelw $value
}

# called from variable trace on the scale widget
proc SetScaleLabel {scalew labelw value} {
    $labelw configure -text [format {%.1f} $value]
    foreach {x y} [$scalew coords] break
    foreach {sw sh sx sy} [split [winfo geometry $scalew] {x +}] break
    set tw [winfo width $labelw]
    set x [expr {$x - ($tw / 2)}]
    if {$x < $sx} {set x $sx}
    if {$x + $tw > $sx + $sw} {set x [expr {$sx + $sw - $tw}]}
    place $labelw -x $x -y [expr {$sy - [winfo height $labelw]}]
}

proc Main {{args {}}} {
    variable forever 0

    wm geometry . 800x600
    . configure -background grey40

    set f [ttk::frame .f]
    set gst [gst $f.gst -width 640 -height 480 -background black]
    if {$args ne {}} {
        $gst configure -device [lindex $args 0]
    }

    set box [ttk::frame $f.box]
    foreach name {brightness contrast saturation hue} {
        set label[set name] [ttk::label $box.label$name -text "${name}:"]
        ttk::label $box.val$name -text ""
        set $name [ttk::scale $box.$name -from -1000 -to 1000 -length 100 -value 0 \
            -command "OnBalance $box.$name $box.val$name $gst -$name"]
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

Main $argv