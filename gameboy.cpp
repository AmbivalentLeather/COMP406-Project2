// Nicholas Young, 2024-03-8
#include <stdio.h>
#include <fstream>
#include <conio.h>

#include "Z80.h"
#include "screen.h"

Z80* z80;
char* rom;

using namespace std;

// Function prototypes
void renderScreen(void);
unsigned char memoryRead(int address);
void memoryWrite(int address, unsigned char b);
unsigned char getKey(void);
void setRomMode(int address, unsigned char b);
void setControlByte(unsigned char b);
void setPalette(unsigned char b);
unsigned char getVideoState(void);

// Take user input
//void keypressHandler(char key);
void keydown(int key);
void keyup(int key);

int upkey = 0xf, downkey = 0xf;

// Global variables to hold video memory
unsigned char graphicsRAM[8192];
int palette[4];
int tileset, tilemap, scrollx, scrolly;

// Global variables introduced in part 3
int HBLANK=0, VBLANK=1, SPRITE=2, VRAM=3;
unsigned char workingRAM[0x2000];
unsigned char page0RAM[0x80];

// Global variables to hold screen info and the instruction count
int line=0, cmpline=0, videostate=0, keyboardColumn=0, horizontal=0;
int gpuMode=HBLANK;
int romOffset = 0x4000;
long totalInstructions=0;
int romSize;


extern QApplication* app;

int main(int argc, char** argv)
{
    setup(argc, argv);

    // Part 1 Code
    // Code provided directly by Dr. Black
    ifstream romfile("opus5.gb", ios::in | ios::binary | ios::ate);
    streampos size=romfile.tellg();
    rom = new char[size];
    romSize=size;
    romfile.seekg(0, ios::beg);
    romfile.read(rom, size);
    romfile.close();
    // End of code provided directly by Dr. Black

    z80 = new Z80 (memoryRead, memoryWrite);
    z80 -> reset();

    // Load screen information into graphics memory
    int n;
    ifstream vidfile("screendump.txt", ios::in);
    for (int i = 0; i < 8192; i++) {
        vidfile >> n;
        graphicsRAM[i] = (unsigned char) n;
    }
    vidfile >> tileset;
    vidfile >> tilemap;
    vidfile >> scrollx;
    vidfile >> scrolly;
    vidfile >> palette[0];
    vidfile >> palette[1];
    vidfile >> palette[2];
    vidfile >> palette[3];

    // Fetch-execute for gameboy code
    while (true) {
        if (z80 -> halted)
            break;
        else
            z80 -> doInstruction();

        // Check for interrupts
        if (z80 -> interrupt_deferred > 0) {
            z80 -> interrupt_deferred--;
            if (z80 -> interrupt_deferred == 1) {
                z80 -> interrupt_deferred = 0;
                z80 -> FLAG_I = 1;
            }
        }
        z80 -> checkForInterrupts();

        totalInstructions++;
        horizontal = (int) ((totalInstructions + 1) % 61);

        // Set gpu modes based on current line or column
        if (line >= 145) {
            gpuMode = VBLANK;
        } else if (horizontal <= 30) {
            gpuMode = HBLANK;
        } else if (horizontal < 40 && horizontal > 30) {
            gpuMode = SPRITE;
        } else {
            gpuMode = VRAM;
        }

        // Throw interrupts or render screen based on current line
        if (horizontal == 0) {
            line++;
            if (line == 144)
                z80 -> throwInterrupt(1);
            if (line % 153 == cmpline && (videostate & 0x40) != 0)
                z80 -> throwInterrupt(2);
            if (line == 153) {
                line = 0;
                renderScreen();
            }
        }

    }
    return 0;
}

void renderScreen(void)
{
    // These don't necessarily need to be ints, it was just how I did it the first time and it worked
    int color, pixel;
    int tilex, tiley, tileindex, tileaddress;
    int xoffset, yoffset;
    unsigned char row0, row1;
    unsigned char row0shifted, row1shifted, row0capturepixel, row1capturepixel;

    for (int row = 0; row < 144; row++) {
        for (int column = 0; column < 160; column++) {
            int x = row, y = column;

            // Apply the scroll to shift the image
            x = (x + scrollx) & 255;
            y = (y + scrolly) & 255;

            // Figure out which tile the pixels belong to
            tilex = x / 8;
            tiley = y / 8;

            // Determine tile position in map
            int tileposition = tiley * 32 + tilex; // Does this need to be an int or unsigned char?

            // Determine tile map
            if (tilemap == 0)
                tileindex = graphicsRAM[0x1800 + tileposition]; // Should be a signed number
            else if (tilemap == 1)
                tileindex = graphicsRAM[0xc00 + tileposition];

            // Determine tile encoding based on tile map
            if (tileset == 1) {
                tileaddress = tileindex * 16; // Int or unsigned char?
            }
            if (tileset == 0) {
                if (tileindex >= 128)
                    tileindex = tileindex - 256;
                tileaddress = tileindex * 16 + 0x1000;
            }

            // Calculate the bytes of each pixel
            xoffset = x % 8;
            yoffset = y % 8;

            row0 = graphicsRAM[tileaddress + yoffset * 2];
            row1 = graphicsRAM[tileaddress + yoffset * 2 + 1];

            // Find pixel colors
            row0shifted = row0 >> (7 - xoffset);
            row0capturepixel = row0shifted & 1;

            row1shifted = row1 >> (7 - xoffset); // Should this be yoffset?
            row1capturepixel = row1shifted & 1;

            // Put the two togethers and assign color
            pixel = row1capturepixel * 2 + row0capturepixel;
            color = palette[pixel];
            updateSquare(row, column, color);
        }
    }

    onFrame();
}

unsigned char memoryRead(int address) 
{
    switch (address) {
        case 0x0000 ... 0x3FFF: return rom[address];
                                break;
        case 0x4000 ... 0x7FFF: return rom[romOffset + address % 0x4000];
                                break;
        case 0x8000 ... 0x9FFF: return graphicsRAM[address % 0x2000];
                                break;
        case 0xC000 ... 0xDFFF: return workingRAM[address % 0x2000];
                                break;
        case 0xFF80 ... 0xFFFF: return page0RAM[address % 0x80];
                                break;
        case 0xFF00: return getKey();
                     break;
        case 0xFF41: return getVideoState();
                     break;
        case 0xFF42: return scrolly;
                     break;
        case 0xFF43: return scrollx;
                     break;
        case 0xFF44: return line;
                     break;
        case 0xFF45: return cmpline;
                     break;
        default: return 0;
                 break;
    }
    return 0;
}

void memoryWrite(int address, unsigned char b)
{    
    switch (address) {
        case 0x0000 ... 0x3FFF: setRomMode(address, b);
                                break;
        case 0x4000 ... 0x7FFF: ;
                                break;
        case 0x8000 ... 0x9FFF: graphicsRAM[address % 0x2000] = b;
                                break;
        case 0xC000 ... 0xDFFF: workingRAM[address % 0x2000] = b;
                                break;
        case 0xFF80 ... 0xFFFF: page0RAM[address % 0x80] = b;
                                break;
        case 0xFF00: keyboardColumn = b;
                     break;
        case 0xFF40: setControlByte(b);
                     break;
        case 0xFF41: videostate = b;
                     break;
        case 0xFF42: scrolly = b;
                     break;
        case 0xFF43: scrollx = b;
                     break;
        case 0xFF44: line = b;
                     break;
        case 0xFF45: cmpline = b;
                     break;
        case 0xFF47: setPalette(b);
                     break;
        default: ;
                 break;
    }

} 

void keydown(int key)
{
    qDebug("%d", key);
    switch(key)
    {
        case 331: downkey &= 0xE; break; // right
        case 336: downkey &= 0x7; break; // down
        case 333: downkey &= 0xD; break; // left
        case 328: downkey &= 0xB; break; // up
        case 44: upkey &= 0xE; break; // z
        case 45: upkey &= 0xD; break; // x
        case 57: upkey &= 0xB; break; // space
        case 28: upkey &= 0x7; break; // enter
    default: break;
    }
    z80 -> throwInterrupt(0x10);
}

void keyup(int key)
{
    switch(key)
    {
        case 331: downkey |= 0x1; break;
        case 336: downkey |= 0x8; break;
        case 333: downkey |= 0x2; break;
        case 328: downkey |= 0x4; break;
        case 44: upkey |= 0x1; break;
        case 45: upkey |= 0x2; break;
        case 57: upkey |= 0x4; break;
        case 28: upkey |= 0x8; break;
    default: break;
    }
    z80 -> throwInterrupt(0x10);
}

// Start of functions provided directly by Dr. Black
unsigned char getKey(void) {
    // Modified 2024-03-20 by Nick Young
    if ((keyboardColumn & 0x30) == 0x10)
        return upkey;
    else
        return downkey;

}
void setRomMode(int address, unsigned char b) { }
void setControlByte(unsigned char b) {
        tilemap=(b&8)!=0?1:0;

        tileset=(b&16)!=0?1:0;
 }
void setPalette(unsigned char b) {
	palette[0]=b&3; palette[1]=(b>>2)&3; palette[2]=(b>>4)&3; palette[3]=(b>>6)&3;}

unsigned char getVideoState() {
        int by=0;

        if(line==cmpline) by|=4;

        if(gpuMode==VBLANK) by|=1;

        if(gpuMode==SPRITE) by|=2;

        if(gpuMode==VRAM) by|=3;

        return (unsigned char)((by|(videostate&0xf8))&0xff);
 }
// End of functions provided directly by Dr. Black
