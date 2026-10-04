#!/usr/bin/env python3
"""
Graphics Check Script for ArchForge OS.
Parses PPM (P6) files and provides assertions for testing.
"""
import sys
import struct
import os

def parse_ppm(filename):
    """Parse a P6 PPM file and return width, height, max_val, and pixel data (bytearray)."""
    with open(filename, 'rb') as f:
        # Read magic number
        magic = f.readline().strip()
        if magic != b'P6':
            raise ValueError(f"Not a P6 PPM file: {magic}")
        
        # Skip comments and read dimensions
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        
        parts = line.split()
        if len(parts) >= 2:
            width = int(parts[0])
            height = int(parts[1])
        else:
            # Might be on separate lines
            width = int(parts[0])
            height = int(f.readline().strip())
            
        max_val = int(f.readline().strip())
        
        # Read pixel data
        pixel_data = f.read()
        
    return width, height, max_val, bytearray(pixel_data)

def get_pixel(data, width, x, y, bytes_per_pixel=3):
    """Get the RGB tuple of a pixel at (x, y)."""
    idx = (y * width + x) * bytes_per_pixel
    if idx + 2 < len(data):
        return (data[idx], data[idx+1], data[idx+2])
    return None

def pixel_equals(data, width, x, y, expected_color, tolerance=0):
    """Check if pixel at (x, y) matches expected_color (R, G, B) within tolerance."""
    color = get_pixel(data, width, x, y)
    if color is None:
        return False
    for i in range(3):
        if abs(color[i] - expected_color[i]) > tolerance:
            return False
    return True

def region_is_uniform(data, width, rect, color, tolerance=0):
    """Check if all pixels in rect (x, y, w, h) match color."""
    x, y, w, h = rect
    for cy in range(y, y + h):
        for cx in range(x, x + w):
            if not pixel_equals(data, width, cx, cy, color, tolerance):
                return False
    return True

def region_not_blank(data, width, rect):
    """Check if any pixel in rect is not black (0, 0, 0)."""
    x, y, w, h = rect
    for cy in range(y, y + h):
        for cx in range(x, x + w):
            color = get_pixel(data, width, cx, cy)
            if color and color != (0, 0, 0):
                return True
    return False

def count_pixels(data, width, height, color, tolerance=0):
    """Count how many pixels match the given color."""
    count = 0
    for y in range(height):
        for x in range(width):
            if pixel_equals(data, width, x, y, color, tolerance):
                count += 1
    return count

def find_color_bbox(data, width, height, color, tolerance=0):
    """Find the bounding box of all pixels matching the color."""
    min_x, min_y = width, height
    max_x, max_y = -1, -1
    found = False
    
    for y in range(height):
        for x in range(width):
            if pixel_equals(data, width, x, y, color, tolerance):
                found = True
                if x < min_x: min_x = x
                if x > max_x: max_x = x
                if y < min_y: min_y = y
                if y > max_y: max_y = y
                
    if found:
        return (min_x, min_y, max_x - min_x + 1, max_y - min_y + 1)
    return None

def main():
    if len(sys.argv) < 3:
        print("Usage: gfx_check.py <ppm_file> <assertion> [args...]")
        print("Assertions: pixel <x> <y> <r> <g> <b> [tolerance]")
        print("            uniform <x> <y> <w> <h> <r> <g> <b> [tolerance]")
        print("            notblank <x> <y> <w> <h>")
        print("            count <r> <g> <b> [tolerance]")
        sys.exit(1)
        
    filename = sys.argv[1]
    assertion = sys.argv[2]
    
    if not os.path.exists(filename):
        print(f"FAIL: File not found: {filename}")
        sys.exit(1)
        
    try:
        width, height, max_val, data = parse_ppm(filename)
    except Exception as e:
        print(f"FAIL: Error parsing PPM: {e}")
        sys.exit(1)
        
    if assertion == "pixel":
        x, y, r, g, b = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7])
        tol = int(sys.argv[8]) if len(sys.argv) > 8 else 0
        if pixel_equals(data, width, x, y, (r, g, b), tol):
            print(f"PASS: pixel({x},{y}) == ({r},{g},{b})")
            sys.exit(0)
        else:
            actual = get_pixel(data, width, x, y)
            print(f"FAIL: pixel({x},{y}) expected ({r},{g},{b}), got {actual}")
            sys.exit(1)
            
    elif assertion == "uniform":
        x, y, w, h = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
        r, g, b = int(sys.argv[7]), int(sys.argv[8]), int(sys.argv[9])
        tol = int(sys.argv[10]) if len(sys.argv) > 10 else 0
        if region_is_uniform(data, width, (x, y, w, h), (r, g, b), tol):
            print(f"PASS: region({x},{y},{w},{h}) is uniform ({r},{g},{b})")
            sys.exit(0)
        else:
            print(f"FAIL: region({x},{y},{w},{h}) is not uniform ({r},{g},{b})")
            sys.exit(1)
            
    elif assertion == "notblank":
        x, y, w, h = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
        if region_not_blank(data, width, (x, y, w, h)):
            print(f"PASS: region({x},{y},{w},{h}) is not blank")
            sys.exit(0)
        else:
            print(f"FAIL: region({x},{y},{w},{h}) is blank")
            sys.exit(1)
            
    elif assertion == "count":
        r, g, b = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
        tol = int(sys.argv[6]) if len(sys.argv) > 6 else 0
        count = count_pixels(data, width, height, (r, g, b), tol)
        print(f"PASS: count of ({r},{g},{b}) is {count}")
        # We just print the count, caller can check it if needed, or we can add expected count
        sys.exit(0)
        
    else:
        print(f"FAIL: Unknown assertion: {assertion}")
        sys.exit(1)

if __name__ == "__main__":
    main()