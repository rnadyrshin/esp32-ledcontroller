#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#define PANEL_RES_X 64    // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 13      // Total number of panels chained one to another
#define PANEL_WIDTH     (PANEL_RES_X * PANEL_CHAIN)

MatrixPanel_I2S_DMA *dma_display = nullptr;

extern "C" void app_main() {
    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
    mxconfig.driver = HUB75_I2S_CFG::FM6124;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_16M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_15M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;

    mxconfig.clkphase = 0;
    mxconfig.double_buff = 1;

    mxconfig.setPixelColorDepthBits(3);
    mxconfig.min_refresh_rate = 200;//255;//100;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    dma_display->begin();
    dma_display->setBrightness8(255);
    dma_display->clearScreen();

    dma_display->setTextSize(3);     // size 1 == 8 pixels high
    dma_display->setTextWrap(false); // Don't wrap at end of line - will do ourselves

    dma_display->setTextColor(dma_display->color565(0xFF, 0x70, 0));

    bool finish = false;
    int idx = 0;
    int len_pix = 0;
    int idx_start = 0;

    while (1)
    {
        dma_display->clearScreen();
        finish = false;
        idx = idx_start;

        while (!finish)
        {
            dma_display->setCursor(idx, 5);
            len_pix = dma_display->print("Flipper Hackspace    ");
            len_pix *= 18;
            idx += len_pix;
            if (idx > PANEL_WIDTH)
                finish = true;
        }

        dma_display->flipDMABuffer();
        idx_start--;
        if (idx_start <= -len_pix)
            idx_start += len_pix;

        delay(25);
    }
}
