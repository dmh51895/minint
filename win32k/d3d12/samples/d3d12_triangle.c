/*
 * MinNT - win32k/d3d12/samples/d3d12_triangle.c
 * D3D12 sample: renders a colored triangle to the screen
 * 
 * This demonstrates the MinNT D3D12 software renderer by:
 * 1. Creating a D3D12 device
 * 2. Drawing a triangle using the software rasterizer
 * 3. Presenting to the framebuffer
 */

#include <nt/ntdef.h>
#include <nt/mm.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include "../d3d12.h"

/* ---- Demo triangle vertices (screen-space coordinates) ------------------- */

static FLOAT triangle_x[] = { 512.0f, 256.0f, 768.0f };
static FLOAT triangle_y[] = { 100.0f, 600.0f, 600.0f };
static ULONG triangle_color = 0x00FF00; /* Green */

/* ---- Demo rectangle ------------------------------------------------------ */

static LONG rect_x0 = 100;
static LONG rect_y0 = 100;
static LONG rect_x1 = 400;
static LONG rect_y1 = 400;

/* ---- Demo text ----------------------------------------------------------- */

static const CHAR *demo_text = "MinNT D3D12 Software Renderer";
static const CHAR *demo_text2 = "Triangle demo running";

/* ---- Main demo loop ------------------------------------------------------ */

VOID NTAPI D3D12DemoRun(VOID)
{
    ULONG frame = 0;
    ULONG color_phase = 0;
    
    DbgPrint("D3D12: Starting triangle demo\n");
    
    /* Clear to black first */
    D3D12ClearScreen(0.0f, 0.0f, 0.0f, 1.0f);
    D3D12Present();
    
    /* Draw title text */
    ULONG textColor = 0xFFFFFF; /* White */
    ULONG textBg = 0x000000;    /* Black */
    
    /* Get framebuffer dimensions */
    ULONG fbWidth = HalpFbGetWidth();
    ULONG fbHeight = HalpFbGetHeight();
    
    /* Draw colored border */
    D3D12FillRect(0, 0, fbWidth, 10, 0x0000FF);     /* Top: Blue */
    D3D12FillRect(0, fbHeight-10, fbWidth, 10, 0x0000FF); /* Bottom: Blue */
    D3D12FillRect(0, 0, 10, fbHeight, 0xFF0000);     /* Left: Red */
    D3D12FillRect(fbWidth-10, 0, 10, fbHeight, 0xFF0000); /* Right: Red */
    
    /* Draw rectangle */
    D3D12FillRect(rect_x0, rect_y0, rect_x1, rect_y1, 0x0080FF);
    
    while (frame < 300) {
        /* Clear with alternating background */
        FLOAT r, g, b;
        switch (color_phase % 4) {
            case 0: r = 0.0f; g = 0.0f; b = 0.1f; break;  /* Dark blue */
            case 1: r = 0.1f; g = 0.0f; b = 0.0f; break;  /* Dark red */
            case 2: r = 0.0f; g = 0.1f; b = 0.0f; break;  /* Dark green */
            case 3: r = 0.05f; g = 0.05f; b = 0.05f; break; /* Dark gray */
        }
        
        D3D12ClearScreen(r, g, b, 1.0f);
        
        /* Draw triangle */
        D3D12DrawTriangle(
            triangle_x[0], triangle_y[0],
            triangle_x[1], triangle_y[1],
            triangle_x[2], triangle_y[2],
            triangle_color
        );
        
        /* Draw rectangle overlay */
        D3D12FillRect(rect_x0, rect_y0, rect_x1, rect_y1, 0x0080FF);
        
        /* Present to screen */
        D3D12Present();
        
        /* Cycle colors */
        frame++;
        if (frame % 30 == 0) {
            color_phase++;
            
            /* Change triangle color */
            switch (color_phase % 5) {
                case 0: triangle_color = 0x00FF00; break;  /* Green */
                case 1: triangle_color = 0xFF0000; break;  /* Red */
                case 2: triangle_color = 0x0000FF; break;  /* Blue */
                case 3: triangle_color = 0xFFFF00; break;  /* Yellow */
                case 4: triangle_color = 0xFF00FF; break;  /* Magenta */
            }
            
            /* Animate rectangle */
            rect_x0 += 2;
            rect_y0 += 2;
            rect_x1 -= 1;
            rect_y1 -= 1;
            
            if (rect_x1 <= rect_x0 || rect_y1 <= rect_y0) {
                rect_x0 = 100;
                rect_y0 = 100;
                rect_x1 = 400;
                rect_y1 = 400;
            }
            
            DbgPrint("D3D12: Frame %lu, color phase %lu\n", frame, color_phase);
        }
        
        /* Stall to slow down demo */
        KeStallExecutionProcessor(10000);
    }
    
    DbgPrint("D3D12: Demo complete (%lu frames)\n", frame);
}
