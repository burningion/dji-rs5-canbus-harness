/* Makerfabs original STM32 AoA TAG pocket enclosure, prototype rev C (M3 inserts), mm.
 * Board XY from vendor V1.1 Eagle file, commit 34b9705edcb7feca83f652280047847d4bc03c34.
 * U2 footprint is an envelope, not a verified 3D model of the shipped X3-MAX.
 * Component Z heights, plugged battery connector and print fit need measurement.
 * Read README.md before buying/connecting a battery. Stock charger is ~1 A.
 * No header strips fitted. Intended battery: protected 1S 500 mAh, 36 x 29 x 4.75.
 * Source coordinates: USB end x=0, antenna end +X, battery connector +Y.
 * Compatible with OpenSCAD 2015.03 and later; no external CAD libraries.
 */

/* [Output] */
part = "print_plate"; // [print_plate,front,rear,shield,fit_gauge,insert_coupon,assembly,exploded,electronics,collision,case_collision,pcb,battery,radio]
show_references = true;

/* [Board - vendor XY, provisional Z] */
pcb_length = 72;
pcb_width = 32.6;
pcb_thickness = 1.6;
pcb_hole_diameter = 3;
pcb_corner_radius = 1.5;
bottom_component_height = 2.0;
bottom_clearance = 3.0;
top_clearance = 10.0; // Measure tallest part INCLUDING a plugged-in battery lead.
radio_x = 50;
radio_y = 10.0354;
radio_length = 32;   // Vendor U2 footprint runs to x=82, past the 72 mm carrier.
radio_width = 13;
radio_z_offset = 2;
radio_height = 4;    // Placeholder; keep separate from top_clearance.

/* [Battery] */
battery_length = 36;
battery_width = 29;
battery_height = 4.75;
battery_x = 5.5;
battery_y = 1.8;
battery_side_clearance = 1.0;
battery_vertical_clearance = 1.0; // Includes any thin protective pad, no compression.
battery_pad = 0.3;
shield_thickness = 1.2;
cradle_wall = 1.0;

/* [Case] */
wall = 1.6;
rear_thickness = 2.8;
front_thickness = 1.6;
corner_radius = 4.5;
edge_bevel = 0.5;
case_x_min = -3.7;
case_y_min = -10.0;
case_x_max = 86;
case_y_max = 44.3;
joint_fit = 0.25;
lip_height = 1.5;
lip_wall = 1.0;
locator_diameter = 2.5;
support_diameter = 5.0;
pcb_axial_play = 0.15;

/* [Access] */
// External charging is the no-board-modification default: USB is covered.
// Enable only after reducing/confirming charge current, or for a battery-free case.
usb_openings = false;
usb_window_width = 10.8;
usb_window_height = 6.0;
usb_center_above_pcb = 1.7;
screen_window = true;
screen_width = 30;
screen_height = 17.5;
screen_x = 24.0284;
screen_y = 18.161;
screen_recess = 0.6; // Optional 0.5 mm clear PET sheet + thin perimeter adhesive.
button_holes = true;
button_hole_diameter = 2.4;
lanyard_holes = true; // Soft cord holes in rear plate, outside battery/RF footprint.

/* [Fasteners] */
// Four M3 x 8 mm screws into M3 x 4 x 5 mm heat-set inserts.
// Default 90-degree countersink accepts heads up to 6.72 mm (ISO 10642).
// For button/pan/socket heads use "flat": heads then stand proud of the rear.
case_screw_seat = "countersunk"; // [countersunk,flat]
case_screw_clearance = 3.4;
case_screw_pilot = 2.5;
case_screw_head = 7.0;
case_boss_diameter = 10.0;
case_boss_height = 8.0;

/* [JROUTH M3 x 4 x 5 mm heat-set inserts] */
use_heat_set_inserts = true;
insert_hole_diameter = 4.6; // Starting fit only: test the coupon with your filament/printer.
insert_outer_diameter = 4.99; // User-measured OD; label gives 5 mm nominal.
insert_length = 4.0;
insert_bottom_clearance = 1.0;
insert_coupon_diameters = [4.4,4.5,4.6,4.7,4.8];

/* [Hidden] */
$fn = 40;
eps = 0.02;
case_length = case_x_max - case_x_min;
case_width = case_y_max - case_y_min;
shield_z = rear_thickness + battery_height + battery_vertical_clearance;
pcb_z = shield_z + shield_thickness + bottom_clearance;
pcb_top = pcb_z + pcb_thickness;
case_height = pcb_top + top_clearance + front_thickness;
pcb_holes = [[2,2], [2,30.6], [70,2], [70,30.6]];
case_screws = [[8,-4.5], [46,-4.5], [8,38.8], [46,38.8]];
cradle_x = battery_x - battery_side_clearance - cradle_wall;
cradle_y = battery_y - battery_side_clearance - cradle_wall;
cradle_length = battery_length + 2*(battery_side_clearance + cradle_wall);
cradle_width = battery_width + 2*(battery_side_clearance + cradle_wall);
insert_dimensions_valid = insert_hole_diameter > case_screw_clearance
    && insert_outer_diameter >= insert_hole_diameter
    && 2*insert_outer_diameter <= case_boss_diameter
    && insert_length > 0
    && insert_length+insert_bottom_clearance <= case_boss_height-0.7;

echo("PROTOTYPE: verify component heights / plugged BAT connector before final print.");
echo("Outer case L/W/H (mm)", case_length, case_width, case_height);
echo("Cell envelope L/W/H (mm)", battery_length, battery_width, battery_height);
echo("Battery ends at X; radio envelope begins at X", battery_x+battery_length, radio_x);
echo("USB openings", usb_openings, "Stock ~1A charging is unsuitable for proposed 500mAh cell.");
echo("Fasteners: four M3 x 8 mm; seat", case_screw_seat);
echo("Heat-set inserts enabled", use_heat_set_inserts);

module rr2(x,y,w,h,r) {
    hull() for (px=[x+r,x+w-r], py=[y+r,y+h-r])
        translate([px,py]) circle(r=r);
}

module outline(inset=0) {
    rr2(case_x_min+inset,case_y_min+inset,
        case_length-2*inset,case_width-2*inset,max(0.5,corner_radius-inset));
}

module at_holes(holes) {
    for (p=holes) translate([p[0],p[1],0]) children();
}

module case_boss_profiles(extra=0) {
    r = case_boss_diameter/2 + extra;
    for (p=case_screws) {
        translate(p) circle(r=r);
        // Full-width webs join the larger bosses to the outside wall.
        if (p[1] < pcb_width/2)
            translate([p[0]-r,case_y_min-extra])
                square([2*r,p[1]-case_y_min+extra]);
        else
            translate([p[0]-r,p[1]])
                square([2*r,case_y_max-p[1]+extra]);
    }
}

module case_boss_clearances(z,h,extra=0.3) {
    translate([0,0,z]) linear_extrude(h) case_boss_profiles(extra);
}

module front_outer() {
    hull() {
        translate([0,0,rear_thickness]) linear_extrude(eps) outline(edge_bevel);
        translate([0,0,rear_thickness+edge_bevel]) linear_extrude(eps) outline();
        translate([0,0,case_height-edge_bevel-eps]) linear_extrude(eps) outline();
        translate([0,0,case_height-eps]) linear_extrude(eps) outline(edge_bevel);
    }
}

module front_shell() {
    difference() {
        union() {
            difference() {
                front_outer();
                translate([0,0,rear_thickness-eps])
                    linear_extrude(case_height-front_thickness-rear_thickness+eps)
                        outline(wall);
            }
            // Board rests against these front pads; pegs enter its four holes.
            at_holes(pcb_holes) {
                translate([0,0,pcb_top])
                    cylinder(d=support_diameter,h=case_height-front_thickness-pcb_top+eps);
                translate([0,0,pcb_z-0.65])
                    cylinder(d=locator_diameter,h=pcb_thickness+0.65+eps);
            }
            // Short closure bosses are below the PCB, outside the cell volume.
            intersection() {
                front_outer();
                translate([0,0,rear_thickness]) linear_extrude(case_boss_height)
                    case_boss_profiles();
            }
        }
        at_holes(case_screws) {
            translate([0,0,rear_thickness-eps])
                cylinder(d=use_heat_set_inserts ? case_screw_clearance : case_screw_pilot,
                         h=case_boss_height-0.7);
            if (use_heat_set_inserts)
                translate([0,0,rear_thickness-eps])
                    cylinder(d=insert_hole_diameter,
                             h=insert_length+insert_bottom_clearance+eps);
        }
        if (usb_openings) for (cy=[9.7214,21.3868])
            translate([case_x_min-1,cy-usb_window_width/2,
                       pcb_top+usb_center_above_pcb-usb_window_height/2])
                cube([wall+5,usb_window_width,usb_window_height]);
        if (screen_window) {
            translate([0,0,case_height-front_thickness-eps])
                linear_extrude(front_thickness+2*eps)
                    rr2(screen_x-screen_width/2,screen_y-screen_height/2,
                        screen_width,screen_height,0.7);
            translate([0,0,case_height-screen_recess])
                linear_extrude(screen_recess+eps)
                    rr2(screen_x-screen_width/2-1.5,screen_y-screen_height/2-1.5,
                        screen_width+3,screen_height+3,0.8);
        }
        if (button_holes) for (cx=[42.4688,47.2186])
            translate([cx,21.9136,case_height-front_thickness-eps])
                cylinder(d=button_hole_diameter,h=front_thickness+2*eps);
    }
}

module rear_lip() {
    difference() {
        translate([0,0,rear_thickness-eps]) linear_extrude(lip_height+eps)
            difference() {
                outline(wall+joint_fit);
                outline(wall+joint_fit+lip_wall);
            }
        case_boss_clearances(rear_thickness-2*eps,lip_height+3*eps);
    }
}

module cradle() {
    difference() {
        translate([cradle_x,cradle_y,rear_thickness-eps])
            cube([cradle_length,cradle_width,shield_z-rear_thickness+eps]);
        translate([battery_x-battery_side_clearance,battery_y-battery_side_clearance,
                   rear_thickness-eps*2])
            cube([battery_length+2*battery_side_clearance,
                  battery_width+2*battery_side_clearance,shield_z+1]);
        // Wire exit at the battery connector side. No wire crosses a lid joint.
        translate([cradle_x-eps,battery_y+battery_width-2,shield_z-2.5])
            cube([battery_x+8-cradle_x+eps,5,4]);
        case_boss_clearances(rear_thickness-eps*2,case_height);
    }
}

module rear_cover() {
    difference() {
        union() {
            hull() {
                linear_extrude(eps) outline(edge_bevel);
                translate([0,0,edge_bevel]) linear_extrude(eps) outline();
                translate([0,0,rear_thickness-eps]) linear_extrude(eps) outline();
            }
            rear_lip();
            cradle();
            at_holes(pcb_holes) translate([0,0,rear_thickness-eps])
                cylinder(d=support_diameter,h=pcb_z-pcb_axial_play-rear_thickness+eps);
        }
        // Clearance for front locating pegs; board capture has 0.15 mm axial play.
        at_holes(pcb_holes) translate([0,0,pcb_z-1.5])
            cylinder(d=locator_diameter+0.6,h=2);
        at_holes(case_screws) {
            translate([0,0,-eps]) cylinder(d=case_screw_clearance,h=rear_thickness+2*eps);
            if (case_screw_seat == "countersunk")
                translate([0,0,-eps]) cylinder(d1=case_screw_head,d2=case_screw_clearance,
                                               h=(case_screw_head-case_screw_clearance)/2+eps);
        }
        if (lanyard_holes) for (cx=[55,61])
            translate([cx,35.5,-eps]) cylinder(d=2.5,h=rear_thickness+2*eps);
    }
}

module battery_shield() {
    // Loose rigid separator seats on the cradle rim. Rear removal releases it.
    difference() {
        translate([cradle_x,cradle_y,shield_z])
            cube([cradle_length,cradle_width,shield_thickness]);
        at_holes(pcb_holes) translate([0,0,shield_z-eps])
            cylinder(d=support_diameter+0.6,h=shield_thickness+2*eps);
        case_boss_clearances(shield_z-eps,shield_thickness+2*eps);
        translate([cradle_x-eps,battery_y+battery_width-2,shield_z-eps])
            cube([battery_x+8-cradle_x+eps,5,shield_thickness+2*eps]);
    }
}

module pcb_reference() {
    difference() {
        translate([0,0,pcb_z]) linear_extrude(pcb_thickness)
            rr2(0,0,pcb_length,pcb_width,pcb_corner_radius);
        at_holes(pcb_holes) translate([0,0,pcb_z-eps])
            cylinder(d=pcb_hole_diameter,h=pcb_thickness+2*eps);
    }
}

module battery_reference() {
    translate([battery_x,battery_y,rear_thickness+battery_pad])
        cube([battery_length,battery_width,battery_height]);
}

module radio_reference() {
    translate([radio_x,radio_y,pcb_top+radio_z_offset])
        cube([radio_length,radio_width,radio_height]);
}

module electronics(include_battery=true) {
    color([0.70,0.12,0.12]) pcb_reference();
    if (include_battery) color([0.68,0.69,0.71]) battery_reference();
    color([0.18,0.42,0.28]) radio_reference();
    // Approximate bodies for clearance visualization, not vendor 3D component data.
    color([0.06,0.09,0.13]) translate([screen_x-16.21,screen_y-10.68,pcb_top])
        cube([32.42,21.36,3.0]);
    color([0.4,0.4,0.4]) for (cy=[9.7214,21.3868])
        translate([-0.71,cy-4.32,pcb_top]) cube([7.35,8.64,3.3]);
    color([0.85,0.85,0.80]) translate([5.001,28.95,pcb_top]) cube([6,4.8,6]);
    // Mated plug and bent lead are an explicit provisional clearance envelope.
    color([0.95,0.7,0.3,0.5]) translate([4.5,28.5,pcb_top+5.5]) cube([7,8,3.5]);
    color([0.2,0.2,0.2]) translate([5,4,pcb_z-bottom_component_height])
        cube([59,25,bottom_component_height]);
}

module fit_gauge() {
    // XY hole/outline check only. Never press electronics flat onto this gauge.
    difference() {
        linear_extrude(1.2) difference() {
            rr2(-1,-1,pcb_length+2,pcb_width+2,2.5);
            rr2(4,4,pcb_length-8,pcb_width-8,1);
        }
        // One continuous USB relief avoids leaving an isolated middle fragment.
        translate([-2,5,-eps]) cube([6.1,22.6,2]);
    }
    at_holes(pcb_holes) {
        cylinder(d=5,h=1.2);
        translate([0,0,1.2]) cylinder(d=locator_diameter,h=1.8);
    }
}

module insert_coupon() {
    // Separate trial piece; same 10 mm bosses, vertical holes and blind depths.
    // Labels are modeled hole diameters, not measured printed hole sizes.
    base = 1.6;
    pitch = case_boss_diameter + 2;
    coupon_length = len(insert_coupon_diameters)*pitch + 2;
    linear_extrude(base) rr2(0,0,coupon_length,23,2);
    for (i=[0:len(insert_coupon_diameters)-1]) {
        cx = 1 + pitch/2 + i*pitch;
        d = insert_coupon_diameters[i];
        translate([cx,7,base-eps]) difference() {
            cylinder(d=case_boss_diameter,h=case_boss_height+eps);
            translate([0,0,case_boss_height-insert_length-insert_bottom_clearance+eps])
                cylinder(d=d,h=insert_length+insert_bottom_clearance+eps);
            translate([0,0,0.7+eps])
                cylinder(d=case_screw_clearance,h=case_boss_height);
        }
        translate([cx,17,base-eps]) linear_extrude(0.5+eps)
            text(str(d),size=3.5,font="Arial",halign="center",valign="center");
    }
}

module print_front() {
    translate([-case_x_min,case_y_max,case_height]) rotate([180,0,0]) front_shell();
}
module print_rear() { translate([-case_x_min,-case_y_min,0]) rear_cover(); }
module print_shield() { translate([-cradle_x,-cradle_y,-shield_z]) battery_shield(); }

// Fail closed on unknown/oversize inserts instead of exporting a guess.
if (use_heat_set_inserts && !insert_dimensions_valid)
    echo("ERROR: Enter supplier insert hole/outer diameters and length. Boss diameter must be >=2x insert OD; pocket must leave >=0.7 mm blind end wall.");
else if (part == "front") print_front();
else if (part == "rear") print_rear();
else if (part == "shield") print_shield();
else if (part == "fit_gauge") translate([1,1,0]) fit_gauge();
else if (part == "insert_coupon") insert_coupon();
else if (part == "print_plate") {
    print_front();
    translate([case_length+8,0,0]) print_rear();
    translate([0,case_width+8,0]) print_shield();
}
else if (part == "assembly" || part == "exploded") {
    explode = part == "exploded" ? 1 : 0;
    color([0.20,0.43,0.53]) translate([0,0,52*explode]) front_shell();
    color([0.28,0.54,0.61]) rear_cover();
    color([0.83,0.69,0.40]) translate([0,0,9*explode]) battery_shield();
    if (show_references) {
        translate([0,0,18*explode]) electronics(false);
        color([0.68,0.69,0.71]) battery_reference();
    }
}
else if (part == "electronics") electronics();
else if (part == "pcb") pcb_reference();
else if (part == "battery") battery_reference();
else if (part == "radio") radio_reference();
else if (part == "collision") intersection() {
    union() { front_shell(); rear_cover(); battery_shield(); }
    electronics();
}
else if (part == "case_collision") union() {
    intersection() { front_shell(); rear_cover(); }
    intersection() { front_shell(); battery_shield(); }
    intersection() { rear_cover(); battery_shield(); }
}
