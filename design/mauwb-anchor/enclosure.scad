/* Makerfabs original STM32 AoA ANCHOR, camera-top case, prototype E. Units mm.
 * Carrier XY from vendor V1.1 CAD; radio module 33 x 36 x 3.5 from X3 manual.
 * User measured battery: 35.5 x 29 x 4.65; radio top 42.11 above carrier underside.
 * Radio front is flush with PCB front per user; depth is approximate, header is provisional.
 * Camera forward = +X; antenna baseline = Y; PCB/display up = +Z.
 * Battery charged externally. See README for fit, mounting and orientation.
 */
part = "assembly"; // [assembly,exploded,base,cover,hood,shield,fit_gauge,mount_gauge,insert_coupon,print_plate,electronics,battery,radio,wire,collision,case_collision,fastener_collision,fastener_electronics_collision,driver_collision,lid_lift_collision,radio_sweep_collision,wire_exit_collision]
show_references = true;

/* [Measured/source carrier dimensions] */
pcb_length = 72;
pcb_width = 32.6;
pcb_thickness = 1.6; // Provisional.
pcb_holes = [[2,2],[2,30.6],[70,2],[70,30.6]];
bottom_clearance = 3;
top_clearance = 10;

/* [Upright radio - VERIFY INSTALLED POSITION] */
radio_x = 68; // User: front face flush with 72 mm PCB edge; subtract 4 mm module depth.
radio_y = -0.2;
radio_width = 33;
radio_height = 36;
radio_depth = 4; // User measured lower module including chip/shield.
radio_installed_height = 42.11; // User: carrier underside to highest antenna point.
header_x = 66.8; // Provisional: keep previous 1.2 mm offset behind module's rear face.
header_width_x = 4.2;
header_height = 8;
antenna_air_gap = 3;
hood_wall = 1.2;

/* [Packed cell, NOT bare pouch dimensions] */
battery_length = 35.5;
battery_width = 29;
battery_height = 4.65;
battery_x = 5.5;
battery_y = 0.8;
battery_side_clearance = 1;
battery_vertical_clearance = 1;
battery_pad = 0.3;
shield_thickness = 1.2;
cradle_wall = 1;

/* [Case] */
case_x_min = -3.7;
case_x_max = 84;
case_y_min = -13;
case_y_max = 45;
wall = 1.8;
base_thickness = 2.8;
roof = 1.6;
corner_radius = 4;
joint_fit = 0.25;
lip_height = 1.5;
support_diameter = 5;
// PCB retention is incomplete: these supports only have old locating recesses.
// Do not treat them as threaded mounts or heat-set insert pockets.
// No PCB locating pins or clamping pillars descend from this lid.

/* [Top-access closure: existing M3 x 8 countersunk screws, inserts in BASE] */
case_screws = [[8,-6.2],[45,-6.2],[22,38],[45,38]];
insert_hole = 4.6; // Trial, test coupon; not supplier specified.
insert_length = 4;
insert_depth = 5;
boss_diameter = 10; // Hood bosses, unchanged.
case_boss_diameter = 9.4; // 0.3 mm gaps to cover wall and battery cradle.
case_cap_thickness = 2.8;
case_axial_gap = 0.2;
case_screw_length = 8; // Countersunk length includes head.
screw_clearance = 3.4;
head_diameter = 7;
driver_diameter = 8; // Straight shaft above lid; keep hood clear.
lid_lift_checks = [0.2,1.6,10,30,60]; // Sampled vertical removal positions.
hood_screw_x = 75;
hood_screw_y = [-8,40];
hood_ear_thickness = 2.8;

/* [Camera cage: two clearance holes, screws enter from inside the tray] */
mount_x = 57;
mount_y = 16.3; // Pair midpoint on antenna centerline, NOT either mounting hole.
mount_pitch = 26.48; // User measured center-to-center, camera left/right (Y).
mount_hole = 6.8; // Smooth clearance for user's approximately 6.23 mm screw shaft.
mount_head_pocket = 10.2;
mount_boss_diameter = 17;
mount_seat_thickness = 2.4; // Recess 0.4 mm into floor to clear measured tall heads.
mount_screw_diameter = 6.23; // User measured; thread designation still unknown.
mount_head_diameter = 9.38;
mount_head_height = 6.23;
battery_deck_height = 6.5; // Preserve prototype-A cover/hood and PCB height.

/* [Access] */
screen_window = true;
usb_openings = false; // Stock charge circuit is unsuitable for this 500mAh cell.
button_holes = true;
cable_notches = true;
cable_exit_y = 16.3; // Rear (-X / USB end), opposite antenna; one shared wire exit.
cable_exit_width = 7;
cable_exit_height = 4;
cable_tie_x = 0.5;
cable_tie_span = 10;
cable_tie_recess = 1; // Underside channel keeps tie band within the base footprint.
wire_check_diameter = 3; // Trial bundle envelope through exit, not measured wire size.

/* [Hidden] */
$fn = 40;
eps = 0.02;
case_length = case_x_max-case_x_min;
case_width = case_y_max-case_y_min;
shield_z = base_thickness+max(battery_height+battery_vertical_clearance,battery_deck_height);
pcb_z = shield_z+shield_thickness+bottom_clearance;
pcb_top = pcb_z+pcb_thickness;
body_height = pcb_top+top_clearance+roof;
case_boss_top = body_height-case_cap_thickness-case_axial_gap;
radio_z = pcb_z+radio_installed_height-radio_height;
hood_inner_x0 = radio_x-antenna_air_gap;
hood_inner_x1 = radio_x+radio_depth+antenna_air_gap;
hood_inner_y0 = radio_y-antenna_air_gap;
hood_inner_y1 = radio_y+radio_width+antenna_air_gap;
hood_x0 = hood_inner_x0-hood_wall;
hood_x1 = hood_inner_x1+hood_wall;
hood_y0 = hood_inner_y0-hood_wall;
hood_y1 = hood_inner_y1+hood_wall;
hood_top = radio_z+radio_height+antenna_air_gap+hood_wall;
cradle_x = battery_x-battery_side_clearance-cradle_wall;
cradle_y = battery_y-battery_side_clearance-cradle_wall;
cradle_l = battery_length+2*(battery_side_clearance+cradle_wall);
cradle_w = battery_width+2*(battery_side_clearance+cradle_wall);
mount_points = [[mount_x,mount_y-mount_pitch/2],[mount_x,mount_y+mount_pitch/2]];
shield_x1 = mount_x+mount_boss_diameter/2;
shield_y_min = min(cradle_y,mount_y-mount_pitch/2-mount_boss_diameter/2);

// Explicit guard works on installed OpenSCAD 2015 as well as current versions.
parameters_valid = battery_height > 0 && battery_vertical_clearance >= battery_pad+0.5
    && mount_seat_thickness >= 2.4 && mount_seat_thickness <= base_thickness
    && mount_seat_thickness+mount_head_height+0.5 <= shield_z
    && mount_hole > mount_screw_diameter
    && mount_head_diameter > mount_hole+2
    && mount_head_pocket >= mount_head_diameter+0.5
    && mount_boss_diameter >= mount_head_pocket+3
    && mount_pitch > mount_boss_diameter
    && mount_x-mount_boss_diameter/2 >= cradle_x+cradle_l
    && mount_x+mount_boss_diameter/2 < case_x_max-wall-2
    && mount_y-mount_pitch/2-mount_boss_diameter/2 > case_y_min+wall+2
    && mount_y+mount_pitch/2+mount_boss_diameter/2 < case_y_max-wall-2
    && hood_x1+0.5 < case_x_max
    && hood_top > body_height+hood_ear_thickness+1
    && insert_hole > screw_clearance && insert_depth > insert_length
    && case_boss_top-insert_depth-1.2 > base_thickness+1
    && case_cap_thickness >= (head_diameter-screw_clearance)/2+0.8
    && case_screw_length >= body_height-case_boss_top+insert_length
    && body_height-case_screw_length >= case_boss_top-insert_depth-1.2+0.5
    && radio_x > header_x
    && hood_inner_x0 > case_screws[1][0]+case_boss_diameter/2+joint_fit
    && cable_exit_width > wire_check_diameter && cable_exit_height > wire_check_diameter
    && base_thickness-cable_tie_recess >= 1.6
    && cable_tie_x+1.6 < cradle_x
    && cable_tie_x-1.6 > case_x_min+wall
    && cable_tie_span/2-0.9 > cable_exit_width/2+0.3;
echo("BODY L/W/H",case_length,case_width,body_height);
echo("OVERALL L/W/H",case_length,case_width,hood_top);
echo("PCB Z",pcb_z,"SHIELD Z",shield_z);
echo("CAGE HOLE CENTERS XY",mount_points,"CLEARANCE DIAMETER",mount_hole);
echo("TOP CLOSURE CENTERS XY",case_screws,"BASE INSERT TOP Z",case_boss_top);
echo("RADIO REAR/FRONT X",radio_x,radio_x+radio_depth);
echo("REAR WIRE EXIT Y/W/H",cable_exit_y,cable_exit_width,cable_exit_height);
echo("PROTOTYPE: user-reported flush antenna placement; verify header envelope and physical fit");

module rr2(x,y,w,h,r) {
    hull() for (a=[x+r,x+w-r],b=[y+r,y+h-r]) translate([a,b]) circle(r=r);
}
module outline(inset=0) {
    rr2(case_x_min+inset,case_y_min+inset,case_length-2*inset,
        case_width-2*inset,max(0.5,corner_radius-inset));
}
module at(points) { for(p=points) translate([p[0],p[1],0]) children(); }
module bosses2() {
    at(case_screws) circle(d=case_boss_diameter);
}
module cage_mount_bosses() {
    at(mount_points) translate([0,0,base_thickness-eps])
        cylinder(d=mount_boss_diameter,h=shield_z-base_thickness+eps);
}
module cage_mount_holes() {
    at(mount_points) {
        translate([0,0,-eps]) cylinder(d=mount_hole,h=shield_z+1);
        // Flat seats put heads inside; shafts point down into cage.
        translate([0,0,mount_seat_thickness])
            cylinder(d=mount_head_pocket,h=shield_z-mount_seat_thickness+eps);
    }
}
module rear_cable_notch() {
    if(cable_notches)
        translate([case_x_min-1,cable_exit_y-cable_exit_width/2,base_thickness-eps])
            cube([wall+joint_fit+3,cable_exit_width,cable_exit_height+eps]);
}
module cradle() {
    difference() {
        translate([cradle_x,cradle_y,base_thickness-eps])
            cube([cradle_l,cradle_w,shield_z-base_thickness+eps]);
        translate([battery_x-battery_side_clearance,battery_y-battery_side_clearance,base_thickness-2*eps])
            cube([battery_length+2*battery_side_clearance,battery_width+2*battery_side_clearance,shield_z+1]);
        translate([cradle_x-eps,battery_y+battery_width-2,shield_z-3])
            cube([10,6,5]);
    }
}
module base_assembly() {
    difference() {
        union() {
            linear_extrude(base_thickness) outline();
            translate([0,0,base_thickness-eps]) linear_extrude(lip_height+eps)
                difference() {
                    outline(wall+joint_fit);
                    outline(wall+joint_fit+1);
                    offset(delta=0.3) bosses2();
                }
            cradle();
            at(pcb_holes) translate([0,0,base_thickness-eps])
                cylinder(d=support_diameter,h=pcb_z-base_thickness+eps);
            // Tall posts stay inside the removable cover; no underside access.
            translate([0,0,base_thickness-eps])
                linear_extrude(case_boss_top-base_thickness+eps) bosses2();
            cage_mount_bosses();
        }
        // Legacy locating sockets: 3.1 mm diameter, only 1.2 mm into the posts.
        // These do not provide screw engagement for PCB retention.
        at(pcb_holes) translate([0,0,pcb_z-1.2]) cylinder(d=3.1,h=2);
        at(case_screws) {
            // Install the four heat-set inserts from above into blind pockets.
            translate([0,0,case_boss_top-insert_depth]) cylinder(d=insert_hole,h=insert_depth+eps);
            translate([0,0,case_boss_top-insert_depth-1.2])
                cylinder(d=screw_clearance,h=insert_depth+1.2+eps);
        }
        cage_mount_holes();
        rear_cable_notch();
        if(cable_notches) {
            // Tie crosses the rearward wire bundle; thread before cage mounting.
            for(y=[cable_exit_y-cable_tie_span/2,cable_exit_y+cable_tie_span/2])
                translate([cable_tie_x-1.6,y-0.9,-eps])
                    cube([3.2,1.8,base_thickness+lip_height+1]);
            translate([cable_tie_x-1.6,cable_exit_y-cable_tie_span/2-0.9,-eps])
                cube([3.2,cable_tie_span+1.8,cable_tie_recess+eps]);
        }
    }
}
module roof_slot() {
    translate([hood_inner_x0,hood_inner_y0,body_height-roof-eps])
        cube([hood_inner_x1-hood_inner_x0,hood_inner_y1-hood_inner_y0,roof+2*eps]);
}
module forward_mark(z) {
    translate([52,10,z]) linear_extrude(0.45)
        polygon([[0,2],[6,2],[6,0],[11,4],[6,8],[6,6],[0,6]]);
}
module cover_assembly() {
    difference() {
        union() {
            difference() {
                translate([0,0,base_thickness]) linear_extrude(body_height-base_thickness) outline();
                translate([0,0,base_thickness-eps])
                    linear_extrude(body_height-roof-base_thickness+eps) outline(wall);
                roof_slot();
            }
            // The PCB is secured before the antenna is installed. Keep all four
            // PCB-hole locations free of descending lid pillars and locating pins.
            // Thickened seats under the roof support top-facing countersinks.
            translate([0,0,body_height-case_cap_thickness])
                linear_extrude(case_cap_thickness) bosses2();
            for(y=hood_screw_y) translate([hood_screw_x,y,body_height-7])
                cylinder(d=boss_diameter,h=7);
        }
        at(case_screws) {
            translate([0,0,body_height-case_cap_thickness-eps])
                cylinder(d=screw_clearance,h=case_cap_thickness+2*eps);
            translate([0,0,body_height-(head_diameter-screw_clearance)/2])
                cylinder(d1=screw_clearance,d2=head_diameter,
                    h=(head_diameter-screw_clearance)/2+eps);
        }
        for(y=hood_screw_y) {
            translate([hood_screw_x,y,body_height-insert_depth]) cylinder(d=insert_hole,h=insert_depth+eps);
            translate([hood_screw_x,y,body_height-6.2]) cylinder(d=screw_clearance,h=6.2+eps);
        }
        // Recessed OLED window and optional 0.5 mm PET lens seat.
        if(screen_window) {
            translate([9.0284,9.411,body_height-roof-eps])
                cube([30,17.5,roof+2*eps]);
            translate([7.5284,7.911,body_height-0.65]) cube([33,20.5,0.7]);
        }
        if(button_holes) for(x=[42.4688,47.2186])
            translate([x,21.9136,body_height-roof-eps]) cylinder(d=2.4,h=roof+1);
        if(usb_openings) for(y=[9.7214,21.3868])
            translate([case_x_min-1,y-5.4,pcb_top-1.3]) cube([6,10.8,6]);
        rear_cable_notch();
        // Recessed arrow is on the lid, so the print face remains flat.
        forward_mark(body_height-0.35);
    }
}
module hood_assembly() {
    difference() {
        union() {
            difference() {
                translate([hood_x0,hood_y0,body_height])
                    cube([hood_x1-hood_x0,hood_y1-hood_y0,hood_top-body_height]);
                translate([hood_inner_x0,hood_inner_y0,body_height-eps])
                    cube([hood_inner_x1-hood_inner_x0,hood_inner_y1-hood_inner_y0,
                          hood_top-hood_wall-body_height+eps]);
            }
            for(y=hood_screw_y) translate([hood_screw_x,y,body_height])
                cylinder(d=boss_diameter,h=hood_ear_thickness);
        }
        for(y=hood_screw_y) translate([hood_screw_x,y,body_height-eps])
            cylinder(d=screw_clearance,h=hood_ear_thickness+2*eps);
    }
}
module shield_assembly() {
    difference() {
        translate([0,0,shield_z]) linear_extrude(shield_thickness) union() {
            translate([cradle_x,cradle_y]) square([shield_x1-cradle_x,cradle_w]);
            // Cover both recessed screw heads; rounded ends clear closure bosses.
            hull() for(p=mount_points) translate(p) circle(d=mount_boss_diameter);
        }
        at(pcb_holes) translate([0,0,shield_z-eps]) cylinder(d=support_diameter+0.5,h=shield_thickness+2*eps);
        translate([cradle_x-eps,battery_y+battery_width-2,shield_z-eps]) cube([10,6,shield_thickness+2*eps]);
    }
}
module pcb_reference() {
    difference() {
        translate([0,0,pcb_z]) linear_extrude(pcb_thickness) rr2(0,0,pcb_length,pcb_width,1.5);
        at(pcb_holes) translate([0,0,pcb_z-eps]) cylinder(d=3,h=pcb_thickness+2*eps);
    }
}
module battery_reference() {
    translate([battery_x,battery_y,base_thickness+battery_pad])
        cube([battery_length,battery_width,battery_height]);
}
module radio_reference() {
    translate([radio_x,radio_y,radio_z]) cube([radio_depth,radio_width,radio_height]);
    translate([header_x,0.3,pcb_top]) cube([header_width_x,32,header_height]);
}
module radio_insertion_sweep() {
    // Continuous straight-up lid removal = radio moving down relative to lid.
    // Exact sweep for the two rectangular trial envelopes, not a hardware scan.
    travel = max(lid_lift_checks);
    translate([radio_x,radio_y,radio_z-travel])
        cube([radio_depth,radio_width,radio_height+travel]);
    translate([header_x,0.3,pcb_top-travel])
        cube([header_width_x,32,header_height+travel]);
}
module wire_exit_reference() {
    if(cable_notches)
        translate([case_x_min-7,cable_exit_y,base_thickness+cable_exit_height/2])
            rotate([0,90,0]) cylinder(d=wire_check_diameter,
                h=cable_tie_x+2-case_x_min+7);
}
module electronics() {
    color("firebrick") pcb_reference();
    color("silver") battery_reference();
    color("seagreen") radio_reference();
    color([0.125,0.157,0.2]) {
        translate([7.82,7.481,pcb_top]) cube([32.42,21.36,3]);
        translate([5,5,pcb_z-2]) cube([59,22.5,2]);
    }
    color("silver") for(y=[9.7214,21.3868])
        translate([-0.7,y-4.5,pcb_top]) cube([7.7,9,3.3]);
    color("ivory") translate([4.5,28.8,pcb_top]) cube([8,10,8]);
    color([0.333,0.333,0.333]) for(x=[42.4688,47.2186])
        translate([x-1.5,19.9,pcb_top]) cube([3,4,3.5]);
}
module printed_assembly() {
    base_assembly(); cover_assembly(); shield_assembly(); hood_assembly();
}
module fastener_references() {
    // Screw envelopes; heat-set knurls intentionally excluded.
    at(case_screws) {
        translate([0,0,body_height-1.75]) cylinder(d1=3,d2=6.5,h=1.75);
        translate([0,0,body_height-case_screw_length])
            cylinder(d=3,h=case_screw_length-1.75+eps);
    }
    for(y=hood_screw_y) {
        translate([hood_screw_x,y,body_height+hood_ear_thickness-8]) cylinder(d=3,h=8);
        translate([hood_screw_x,y,body_height+hood_ear_thickness]) cylinder(d=5.7,h=2);
    }
    at(mount_points) {
        translate([0,0,mount_seat_thickness]) cylinder(d=mount_head_diameter,h=mount_head_height);
        // Check the shaft only inside the base. Cage depth/screw length unmeasured.
        cylinder(d=mount_screw_diameter,h=mount_seat_thickness+eps);
    }
}
module lid_driver_references() {
    at(case_screws) {
        // Model the narrow bit in the head recess, and shaft above the lid.
        translate([0,0,body_height-1]) cylinder(d=2.5,h=3);
        translate([0,0,body_height]) cylinder(d=driver_diameter,h=70);
    }
}
module fit_gauge() {
    difference() {
        union() {
            linear_extrude(1.2) difference() {
                rr2(-2,-2,76,36.6,2);
                translate([5,5]) square([62,22.6]);
            }
            at(pcb_holes) cylinder(d=6,h=1.2);
            translate([hood_inner_x0,-3,0]) cube([hood_inner_x1-hood_inner_x0,2,1.2]);
            translate([hood_inner_x0,33.6,0]) cube([hood_inner_x1-hood_inner_x0,2,1.2]);
        }
        at(pcb_holes) translate([0,0,-eps]) cylinder(d=3,h=1.2+2*eps);
    }
}
module insert_coupon() {
    difference() {
        cube([65,16,7]);
        for(i=[0:4]) translate([8+12*i,8,2]) cylinder(d=4.4+i*0.1,h=5+eps);
    }
}
module mount_gauge() {
    difference() {
        union() {
            linear_extrude(base_thickness) union() {
                difference() { outline(); outline(3); }
                translate([mount_x-mount_boss_diameter/2,case_y_min])
                    square([mount_boss_diameter,case_width]);
            }
            cage_mount_bosses();
        }
        cage_mount_holes();
        forward_mark(base_thickness-0.35);
    }
}
module print_base() { translate([-case_x_min,-case_y_min,0]) base_assembly(); }
module print_cover() { translate([-case_x_min,case_y_max,body_height]) rotate([180,0,0]) cover_assembly(); }
// Open end down; the default roof bridges the 10 mm internal depth.
module print_hood() { translate([-hood_x0,-case_y_min,-body_height]) hood_assembly(); }
module print_shield() { translate([-cradle_x,-shield_y_min,-shield_z]) shield_assembly(); }

if(!parameters_valid) echo("ERROR: invalid cell/mount clearance, insert depth, or radio/support geometry. Review parameters.");
else if(part=="base") print_base();
else if(part=="cover") print_cover();
else if(part=="hood") print_hood();
else if(part=="shield") print_shield();
else if(part=="fit_gauge") translate([2,3,0]) fit_gauge();
else if(part=="mount_gauge") translate([-case_x_min,-case_y_min,0]) mount_gauge();
else if(part=="insert_coupon") insert_coupon();
else if(part=="print_plate") {
    print_base();
    translate([0,case_width+8,0]) print_cover();
    translate([0,2*(case_width+8),0]) print_shield();
    translate([75,2*(case_width+8),0]) print_hood();
}
else if(part=="electronics") electronics();
else if(part=="battery") battery_reference();
else if(part=="radio") radio_reference();
else if(part=="wire") wire_exit_reference();
else if(part=="collision") intersection() { printed_assembly(); electronics(); }
else if(part=="fastener_collision") intersection() { printed_assembly(); fastener_references(); }
else if(part=="fastener_electronics_collision") intersection() { electronics(); fastener_references(); }
else if(part=="driver_collision") intersection() {
    union() { printed_assembly(); electronics(); }
    lid_driver_references();
}
else if(part=="lid_lift_collision") {
    // Closure screws removed; hood stays attached to the cover.
    for(lift=lid_lift_checks) intersection() {
        translate([0,0,lift]) union() { cover_assembly(); hood_assembly(); }
        union() { base_assembly(); shield_assembly(); electronics(); }
    }
}
else if(part=="radio_sweep_collision") intersection() {
    union() { cover_assembly(); hood_assembly(); }
    radio_insertion_sweep();
}
else if(part=="wire_exit_collision") intersection() {
    union() { printed_assembly(); electronics(); }
    wire_exit_reference();
}
else if(part=="case_collision") {
    intersection() { base_assembly(); cover_assembly(); }
    intersection() { base_assembly(); shield_assembly(); }
    intersection() { cover_assembly(); shield_assembly(); }
    intersection() { hood_assembly(); cover_assembly(); }
    intersection() { hood_assembly(); shield_assembly(); }
}
else if(part=="assembly" || part=="exploded") {
    e = part=="exploded" ? 1 : 0;
    color([0.325,0.478,0.537]) base_assembly();
    color([0.831,0.675,0.380]) translate([0,0,10*e]) shield_assembly();
    if(show_references) translate([0,0,20*e]) electronics();
    color([0.392,0.553,0.612]) translate([0,0,42*e]) cover_assembly();
    color([0.549,0.682,0.718]) translate([0,0,58*e]) hood_assembly();
}
