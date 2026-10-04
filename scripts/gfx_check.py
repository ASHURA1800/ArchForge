#!/usr/bin/env python3
import sys
import os

def parse_ppm(filepath):
    with open(filepath, 'rb') as f:
        magic = f.readline().decode('ascii').strip()
        if magic != 'P6':
            raise ValueError(f"Not a P6 PPM file: {magic}")
        
        line = f.readline().decode('ascii').strip()
        while line.startswith('#'):
            line = f.readline().decode('ascii').strip()
            
        width, height = map(int, line.split())
        maxval = int(f.readline().decode('ascii').strip())
        
        data = f.read()
        return width, height, maxval, data

def verify_colors(shots_dir):
    errors = []
    for color, expected_r, expected_g, expected_b in [
        ('red', 255, 0, 0),
        ('green', 0, 255, 0),
        ('blue', 0, 0, 255)
    ]:
        ppm_path = os.path.join(shots_dir, f"{color}.ppm")
        if not os.path.exists(ppm_path):
            errors.append(f"Missing {color}.ppm")
            continue
            
        width, height, maxval, data = parse_ppm(ppm_path)
        
        pixels_to_check = [
            (0, 0),
            (width // 2, height // 2),
            (width - 1, height - 1)
        ]
        
        for x, y in pixels_to_check:
            idx = (y * width + x) * 3
            r, g, b = data[idx], data[idx+1], data[idx+2]
            if r != expected_r or g != expected_g or b != expected_b:
                errors.append(f"{color}.ppm: pixel ({x},{y}) is ({r},{g},{b}), expected ({expected_r},{expected_g},{expected_b})")
                
    if errors:
        print("❌ COLOR VERIFICATION FAILED:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print("✅ COLOR VERIFICATION PASSED")

if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] != "verify_colors":
        print("Usage: gfx_check.py verify_colors <shots_dir>")
        sys.exit(1)
    verify_colors(sys.argv[2])
