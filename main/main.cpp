#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#define PANEL_RES_X 64    // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 14      // Total number of panels chained one to another

MatrixPanel_I2S_DMA *dma_display = nullptr;

extern "C" void app_main() {
    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
    mxconfig.driver = HUB75_I2S_CFG::FM6124;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_15M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_16M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;

    mxconfig.clkphase = 0;
    mxconfig.double_buff = 1;

    mxconfig.setPixelColorDepthBits(3);
    mxconfig.min_refresh_rate = 100;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    dma_display->begin();
    dma_display->setBrightness8(255);
    dma_display->clearScreen();

    dma_display->setTextSize(3);     // size 1 == 8 pixels high
    dma_display->setTextWrap(false); // Don't wrap at end of line - will do ourselves

    //dma_display->setTextColor(dma_display->color565(0xFF, 0x82, 0));
    dma_display->setTextColor(dma_display->color565(0xFF, 0x70, 0));

    while (1)
    {
        for (int i = 0; i < 900; i += 1)
        {
            dma_display->clearScreen();
            dma_display->setCursor(900 - i, 5);
            dma_display->print("Test message");
            dma_display->flipDMABuffer();
            delay(20);
        }
    }

}
