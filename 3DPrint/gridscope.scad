// Main Plate Dimensions
width = 109.728;
height = 94.488;
depth = 3;
hole_diameter = 35;
hole_radius = hole_diameter / 2;

// Support Bar Dimensions
support_Width = width;
support_Height = 4;
support_Depth = 2.45;
support_postDiameter = 2; // Updated from 2.4 to 2
support_postHeight = 2.2;
support_arcMargin = 1;
support_arcHeight = 1;

// Smoothness for cylinders
$fn = 100;

// Calculated Arc Parameters
// Distance between centers is 30mm (x-15 to x+15)
// Chord width (w) between the 1mm margins from post edges: 
// 30 - support_postDiameter - (2 * support_arcMargin) = 26.0 for post diameter 2
arc_chord = 30 - support_postDiameter - (2 * support_arcMargin);
// Radius R calculation: R = (w^2 / 8h) + h/2
arc_radius = (pow(arc_chord, 2) / (8 * support_arcHeight)) + (support_arcHeight / 2);

// Hole coordinates grouped by Y-axis
hole_rows_x = [
    [18.073, 54.737, 91.401],
    [18.073, 54.737, 91.401]
];

// Main Plate
difference() {
    translate([0, -height, 0])
        cube([width, height, depth]);

    // Holes
    // Row 1
    translate([18.073, -22.932, -1]) cylinder(h = depth + 2, r = hole_radius);
    translate([54.737, -22.932, -1]) cylinder(h = depth + 2, r = hole_radius);
    translate([91.401, -22.86, -1])  cylinder(h = depth + 2, r = hole_radius);

    // Row 2
    translate([18.073, -67.815, -1]) cylinder(h = depth + 2, r = hole_radius);
    translate([54.737, -67.815, -1]) cylinder(h = depth + 2, r = hole_radius);
    translate([91.401, -67.815, -1]) cylinder(h = depth + 2, r = hole_radius);
}

// Support Bars
for (i = [0 : len(hole_rows_x) - 1]) {
    y_offset = -height - 20 - (i * (support_Height + 20));
    
    translate([0, y_offset - support_Height, 0]) {
        // Bar with arc cutouts from the top
        difference() {
            cube([support_Width, support_Height, support_Depth]);
            
            for (hx = hole_rows_x[i]) {
                // Subtract cylinder to create the shallow arc from the top (z = support_Depth)
                // The center of the arc's circle is at (support_Depth - support_arcHeight) + arc_radius
                translate([hx, support_Height/2, (support_Depth - support_arcHeight) + arc_radius])
                    rotate([-90, 0, 0])
                        cylinder(h = support_Height + 2, r = arc_radius, center = true);
            }
        }
        
        // Two posts for each hole in the row
        for (hx = hole_rows_x[i]) {
            // Post 1: x - 15
            translate([hx - 15, support_Height/2, support_Depth])
                cylinder(h = support_postHeight, d = support_postDiameter);
            
            // Post 2: x + 15
            translate([hx + 15, support_Height/2, support_Depth])
                cylinder(h = support_postHeight, d = support_postDiameter);
        }
    }
}
