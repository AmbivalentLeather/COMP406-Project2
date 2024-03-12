// Nicholas Young, 2024-03-8
#include <stdio.h>
#include <fstream>

#include "Z80.h"
#include "screen.h"

Z80* z80;
char* rom;

using namespace std;

// Function prototypes
void renderScreen(void);
unsigned char memoryRead(int address);
void memoryWrite(int address, unsigned char b);

// Global variables to hold video memory
unsigned char graphicsRAM[8192];
int palette[4];
int tileset, tilemap, scrollx, scrolly;

// Global variables introduced in part 3
#define HBLANK 0
#define VBLANK 1
#define SPRITE 2
#define VRAM 3
//int HBLANK=0, VBLANK=1, SPRITE=2, VRAM=3; // I didn't like this so I changed it to defines
unsigned char workingRAM[0x2000];
unsigned char page0RAM[0x80];

int line=0, cmpline=0, videostate=0, keyboardColumn=0, horizontal=0;
int gpuMode=HBLANK;
int romOffset = 0x4000;
long totalInstructions=0;
int romSize;

// Function prototypes introduced in part 3
unsigned char getKey(void);
void setRomMode(int address, unsigned char b);
void setControlByte(unsigned char b);
void setPalette(unsigned char b);
unsigned char getVideoState(void);

extern QApplication* app;

int main(int argc, char** argv)
{
    setup(argc, argv);

    // Part 1 Code
    // Code provided directly by Dr. Black
    ifstream romfile("testrom.gb", ios::in | ios::binary | ios::ate);
    streampos size=romfile.tellg();
    rom = new char[size];
    romSize=size;
    romfile.seekg(0, ios::beg);
    romfile.read(rom, size);
    romfile.close();
    // End of code provided directly by Dr. Black

    z80 = new Z80 (memoryRead, memoryWrite);
    z80 -> reset();

    // Part 2 Code
    int n;
    ifstream vidfile("screendump.txt", ios::in);
    for (int i = 0; i < 8192; i++) {
        // int n;   // This is written twice in the provided code so I picked one
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
    // End of Part 2 Code

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

        // Set gpu modes -- This could also be wrong
        if (line >= 145) {
            gpuMode = VBLANK;
        } else if (horizontal <= 30) {
            gpuMode = HBLANK;
        } else if (horizontal < 40 && horizontal > 30) {
            gpuMode = SPRITE;
        } else {
            gpuMode = VRAM;
        }

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

        printf("PC: %d, instruction: %s, A: %d, B: %d\n", z80 -> PC, z80 -> instruction, z80 -> A, z80 -> B);
    }

    renderScreen();
    app -> exec();
}

void renderScreen(void)
{
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
            if (tilemap == 1) {
                tileaddress = tileindex * 16; // Int or unsigned char?
            }
            if (tilemap == 0) {
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
        case 0xFF00: getKey();
                     break;
        case 0xFF41: getVideoState();
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


unsigned char getKey() { return 0xf; }
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
