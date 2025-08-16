#!/usr/bin/env -S uv run
# /// script
# dependencies = [
#     "pillow"
# ]
# ///

"""
PNG to RGB666 32-bit converter
Converts a PNG image to a custom RGB666 format with specific bit layout:
- bits 25-20: RED (6 bits, bit 25 is MSB)
- bits 19-14: GREEN (6 bits, bit 19 is MSB)  
- bits 13-8:  BLUE (6 bits, bit 13 is MSB)
- All other bits are 0

Output is saved as 'out.raw' in little-endian format.
"""

import sys
import struct
from PIL import Image

def convert_8bit_to_6bit(value):
    """Convert 8-bit color value to 6-bit by right-shifting 2 bits"""
    return value >> 2

def create_rgb666_pixel(r, g, b):
    """
    Create a 32-bit RGB666 pixel with the specified bit layout:
    |[BYTE 3]|[BYTE 2]|[BYTE 1]|[BYTE 0]|
    |31   25 |  20  16|  12   8|       0|
    |v     v |   v   v|   v   v|       v|
    |xxxCCC54|3210xxxx|xxxx5432|10543210|
    |   SGDrr|rrrrHHHH|HHHHgggg|ggbbbbbb|
    |   |||
    |   |||_DCLK - Pixel Clock [REQ]
    |   ||___GSP  - Global Start Pulse? Start of frame [REQ]
    |   |____SPL  - Horizontal Start Pulse? Active pixels after 2 CLK 

    - bits 25-20: RED (6 bits)
    - bits 11-6: GREEN (6 bits)
    - bits 5-0:  BLUE (6 bits)
    - All other bits: 0
    """
    # Convert 8-bit values to 6-bit
    r6 = (r >> 2) & 0x3F
    g6 = (g >> 2) & 0x3F
    b6 = (b >> 2) & 0x3F
    
    # Pack into 32-bit value according to bit layout
    pixel = (r6 << 20) | (g6 << 6) | (b6 << 0)
    # pixel = (r6 << 0) | (g6 << 6) | (b6 << 12)
    
    return pixel

def convert_png_to_rgb666(input_filename, output_filename="out.raw"):
    """Convert PNG image to RGB666 raw binqary format"""
    
    try:
        # Open and convert image to RGB if necessary
        with Image.open(input_filename) as img:
            # Convert to RGB if image has alpha channel or is in different mode
            if img.mode != 'RGB':
                img = img.convert('RGB')
            
            width, height = img.size
            print(f"Converting {input_filename}: {width}x{height} pixels")
            
            # Get pixel data
            pixels = list(img.getdata())
            
            # Convert pixels to RGB666 format
            rgb666_data = []
            for r, g, b in pixels:
                pixel_value = create_rgb666_pixel(r, g, b)
                rgb666_data.append(pixel_value)
            
            # Write to binary file (little-endian 32-bit integers)
            with open(output_filename, 'wb') as outfile:
                for pixel in rgb666_data:
                    # Pack as little-endian 32-bit unsigned integer
                    outfile.write(struct.pack('<I', pixel))
            
            print(f"Conversion complete! Output saved to {output_filename}")
            print(f"File size: {len(rgb666_data) * 4} bytes ({len(rgb666_data)} pixels)")
            
    except FileNotFoundError:
        print(f"Error: File '{input_filename}' not found.")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False
    
    return True

def print_usage():
    """Print usage information"""
    print("Usage: python3 png_to_rgb666.py <input_png_file>")
    print("Output will be saved as 'out.raw'")
    print("\nBit format (32-bit little-endian):")
    print("  bits 25-20: RED (6 bits)")
    print("  bits 19-14: GREEN (6 bits)")
    print("  bits 13-8:  BLUE (6 bits)")
    print("  All other bits: 0")

def main():
    """Main function"""
    if len(sys.argv) != 2:
        print_usage()
        sys.exit(1)
    
    input_filename = sys.argv[1]
    
    # Check if input file has png extension (case insensitive)
    if not input_filename.lower().endswith('.png'):
        print("Warning: Input file doesn't have .png extension")
    
    success = convert_png_to_rgb666(input_filename)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
