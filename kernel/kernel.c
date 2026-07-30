#define VGA_BUFFER 0xB8000
volatile unsigned short *vga_buffer;

enum vga_color {
    VGA_COLOR_BLACK = 0x0,
    VGA_COLOR_BLUE = 0x1,
    VGA_COLOR_GREEN = 0x2,
    VGA_COLOR_CYAN = 0x3,
    VGA_COLOR_RED = 0x4,
    VGA_COLOR_MAGENTA = 0x5,
    VGA_COLOR_BROWN = 0x6,
    VGA_COLOR_LIGHT_GRAY = 0x7,
    VGA_COLOR_DARK_GRAY = 0x8,
    VGA_COLOR_LIGHT_BLUE = 0x9,
    VGA_COLOR_LIGHT_GREEN = 0xA,
    VGA_COLOR_LIGHT_CYAN = 0xB,
    VGA_COLOR_LIGHT_RED = 0xC,
    VGA_COLOR_LIGHT_MAGENTA = 0xD,
    VGA_COLOR_LIGHT_BROWN = 0xE,
    VGA_COLOR_WHITE = 0xF,
};

void init()
{
    vga_buffer = (volatile unsigned short *)VGA_BUFFER;
}

void putc(char c, enum vga_color color)
{
    if (!vga_buffer)
        return;

    *vga_buffer = c | (color << 8);
    vga_buffer++;
}

void write(char *str, enum vga_color color)
{
    while (*str) {
        putc(*str, color);
        str++;
    }
}

void kernel_main()
{
    init();
    write("Hello World!", VGA_COLOR_WHITE);

    for (;;) {}
}