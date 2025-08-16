#!/usr/bin/env -S uv run
# /// script
# dependencies = [
#     "pillow"
# ]
# ///

"""
Rainbow Gradient Generator
Creates a 320x240 PNG image with:
- Beautiful rainbow colors across the X-axis (horizontal)
- Fade to black along the Y-axis (vertical)
- 24-bit color depth (RGB)

The rainbow uses HSV color space for smooth color transitions,
cycling through the full hue spectrum from left to right.
"""

import math
from PIL import Image

def hsv_to_rgb(h, s, v):
    """
    Convert HSV color values to RGB.
    h: hue (0-360 degrees)
    s: saturation (0-1)
    v: value/brightness (0-1)
    Returns: (r, g, b) tuple with values 0-255
    """
    h = h % 360  # Ensure hue is in 0-360 range
    c = v * s
    x = c * (1 - abs((h / 60) % 2 - 1))
    m = v - c
    
    if 0 <= h < 60:
        r_prime, g_prime, b_prime = c, x, 0
    elif 60 <= h < 120:
        r_prime, g_prime, b_prime = x, c, 0
    elif 120 <= h < 180:
        r_prime, g_prime, b_prime = 0, c, x
    elif 180 <= h < 240:
        r_prime, g_prime, b_prime = 0, x, c
    elif 240 <= h < 300:
        r_prime, g_prime, b_prime = x, 0, c
    else:  # 300 <= h < 360
        r_prime, g_prime, b_prime = c, 0, x
    
    # Convert to 0-255 range
    r = int((r_prime + m) * 255)
    g = int((g_prime + m) * 255)
    b = int((b_prime + m) * 255)
    
    return (r, g, b)

def generate_rainbow_gradient(width=320, height=240, output_filename="rainbow_gradient.png"):
    """
    Generate a rainbow gradient image that:
    - Shows full rainbow spectrum horizontally (x-axis)
    - Fades to black vertically (y-axis)
    
    Args:
        width: Image width in pixels (default: 320)
        height: Image height in pixels (default: 240)
        output_filename: Output PNG filename
    """
    
    # Create new RGB image
    img = Image.new('RGB', (width, height))
    
    print(f"Generating {width}x{height} rainbow gradient...")
    
    # Generate pixel data
    pixels = []
    
    for y in range(height):
        for x in range(width):
            # Calculate hue based on x position (0-360 degrees across width)
            hue = (x / width) * 360
            
            # Full saturation for vibrant colors
            saturation = 1.0
            
            # Value (brightness) decreases from top to bottom
            # Top row (y=0) is full brightness, bottom row fades to black
            value = 1.0 - (y / height)
            
            # Convert HSV to RGB
            r, g, b = hsv_to_rgb(hue, saturation, value)
            
            # r = 0
            # g = 0
            # b = 0

            # if x < 64:
            #     r = x << 2
            # elif x < 64 * 2:
            #     x -= 64
            #     g = x << 2
            # elif x < 64 * 3:
            #     x -= 64 * 2
            #     b = x << 2
            # elif x < 64 * 4:
            #     x -= 64 * 3
            #     r = x << 2
            #     g = x << 2
            # elif x < 64 * 5:
            #     x -= 64 * 4
            #     g = x << 2
            #     b = x << 2

            # r = 0
            # g = 0
            # b = 0

            pixels.append((r, g, b))
    
    # Set pixel data
    img.putdata(pixels)
    
    # Save as PNG with maximum quality
    img.save(output_filename, "PNG", optimize=False)
    
    print(f"Rainbow gradient saved as '{output_filename}'")
    print(f"Image size: {width}x{height} pixels")
    print(f"Color depth: 24-bit RGB")
    print(f"File saved successfully!")

def main():
    """Main function"""
    print("Rainbow Gradient Generator")
    print("=" * 30)
    
    # Generate the rainbow gradient
    generate_rainbow_gradient()

if __name__ == "__main__":
    main()