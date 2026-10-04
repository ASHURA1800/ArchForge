#!/usr/bin/env python3
"""
Convert PPM (P6) to PNG using only Python standard library (zlib + struct).
"""
import sys
import zlib
import struct

def parse_ppm(filename):
    with open(filename, 'rb') as f:
        magic = f.readline().strip()
        if magic != b'P6':
            raise ValueError(f"Not a P6 PPM file: {magic}")
        
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        
        parts = line.split()
        if len(parts) >= 2:
            width = int(parts[0])
            height = int(parts[1])
        else:
            width = int(parts[0])
            height = int(f.readline().strip())
            
        max_val = int(f.readline().strip())
        pixel_data = f.read()
        
    return width, height, max_val, pixel_data

def create_png(width, height, pixel_data):
    """Create a PNG file from raw RGB pixel data."""
    def png_chunk(chunk_type, data):
        chunk_len = struct.pack('>I', len(data))
        chunk_crc = struct.pack('>I', zlib.crc32(chunk_type + data) & 0xffffffff)
        return chunk_len + chunk_type + data + chunk_crc

    # PNG Signature
    signature = b'\x89PNG\r\n\x1a\n'
    
    # IHDR chunk
    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    ihdr = png_chunk(b'IHDR', ihdr_data)
    
    # IDAT chunk (image data)
    raw_data = b''
    for y in range(height):
        raw_data += b'\x00'  # Filter type: None
        row_start = y * width * 3
        raw_data += pixel_data[row_start:row_start + width * 3]
        
    compressed_data = zlib.compress(raw_data, 9)
    idat = png_chunk(b'IDAT', compressed_data)
    
    # IEND chunk
    iend = png_chunk(b'IEND', b'')
    
    return signature + ihdr + idat + iend

def main():
    if len(sys.argv) != 3:
        print("Usage: ppm2png.py <input.ppm> <output.png>")
        sys.exit(1)
        
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    try:
        width, height, max_val, pixel_data = parse_ppm(input_file)
        if max_val != 255:
            print(f"Warning: max_val is {max_val}, expected 255. Scaling not implemented.")
            
        png_data = create_png(width, height, pixel_data)
        
        with open(output_file, 'wb') as f:
            f.write(png_data)
            
        print(f"Successfully converted {input_file} to {output_file}")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()