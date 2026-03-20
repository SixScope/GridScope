from PIL import Image
import sys

def convert(image_path, out_path):
    try:
        img = Image.open(image_path)
    except FileNotFoundError:
        print(f"Error: {image_path} not found.")
        # Create a dummy image if not found just to not break the build
        img = Image.new('RGB', (240, 240), color = 'black')
    img = img.resize((240, 240)).convert('RGB')
    pixels = img.load()
    
    with open(out_path, 'w') as f:
        f.write("#include <pgmspace.h>\n\n")
        f.write("const uint16_t logo_img[] PROGMEM = {\n")
        
        for y in range(240):
            for x in range(240):
                r, g, b = pixels[x, y]
                # RGB565 conversion
                color565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                f.write(f"0x{color565:04X}, ")
            f.write("\n")
            
        f.write("};\n")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
