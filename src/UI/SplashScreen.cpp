#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <PNGdec.h>
#include "UI/SplashScreen.h"
#include "UI/UIConsts.h"
#include "RollingLogger.h"

extern TFT_eSPI tft;
extern RollingLogger debug_logger;

// ---------------------------------------------------------------------------
// PROGMEM constants
// ---------------------------------------------------------------------------
namespace SplashScreenConsts
{
    constexpr const char splash_path[]     PROGMEM = "/www/splash.png";
    constexpr const char partition_label[] PROGMEM = "voice_data";
    constexpr uint16_t   max_img_width             = UILayout::SCREEN_W;
    constexpr uint8_t    max_open_files            = 10;
} // namespace SplashScreenConsts

// ---------------------------------------------------------------------------
// File-scope state shared with C-style PNGdec callbacks
// ---------------------------------------------------------------------------
static fs::File s_png_file;
static PNG      s_png;
static int16_t  s_draw_x = 0;
static int16_t  s_draw_y = 0;

/** @brief PNGdec open callback — opens the file on LittleFS and returns its size. */
static void *png_open_cb(const char *filename, int32_t *size)
{
    s_png_file = LittleFS.open(filename, "r");
    *size      = s_png_file.size();
    debug_logger.debug(std::string("png_open_cb: file=") + filename + " size=" + std::to_string(*size), "init");
    return &s_png_file;
}

/** @brief PNGdec close callback. */
static void png_close_cb(void *handle)
{
    fs::File *f = static_cast<fs::File *>(handle);
    if (*f) f->close();
    // debug_logger.debug("png_close_cb: file closed","splash");
}

/** @brief PNGdec read callback. */
static int32_t png_read_cb(PNGFILE * /*page*/, uint8_t *buffer, int32_t length)
{
    return s_png_file.read(buffer, length);
}

/** @brief PNGdec seek callback. */
static int32_t png_seek_cb(PNGFILE * /*page*/, int32_t position)
{
    return s_png_file.seek(position);
}

/**
 * @brief PNGdec per-row draw callback — pushes one RGB565 line to the TFT.
 *
 * Transparent pixels (alpha < 128) are replaced with TFT_BLACK.
 * Callback invoked by PNGdec for each decoded row.
 */
static void png_draw_cb(PNGDRAW *pDraw)
{
    uint16_t line_buf[SplashScreenConsts::max_img_width];
    s_png.getLineAsRGB565(pDraw, line_buf, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
    tft.pushImage(s_draw_x, s_draw_y + pDraw->y, pDraw->iWidth, 1, line_buf);
    // if (pDraw->y == 0) {
    //     debug_logger.debug(std::string("png_draw_cb: starting draw at x=") + std::to_string(s_draw_x) + " y=" + std::to_string(s_draw_y) + " width=" + std::to_string(pDraw->iWidth), "init");
    // }
}

// ---------------------------------------------------------------------------
// SplashScreen::initScreen
// ---------------------------------------------------------------------------

/**
 * @brief Draw /www/splash.png centred on the 240×320 display.
 *
 * Mounts LittleFS if not already mounted (BotServerWeb will reuse the
 * already-mounted filesystem — LittleFS.begin() is idempotent).
 * Silently returns if the file is missing or too wide for the line buffer.
 */
void SplashScreen::initScreen()
{
    debug_logger.debug("initScreen", "init");
    
    // Mount LittleFS — same partition used by BotServerWeb.
    // Idempotent: safe to call even if already mounted.
    bool fs_ok = LittleFS.begin(false,
                   "/littlefs",
                   SplashScreenConsts::max_open_files,
                   reinterpret_cast<const char *>(FPSTR(SplashScreenConsts::partition_label)));
    // debug_logger.debug(std::string("LittleFS.begin: ") + (fs_ok ? "OK" : "FAILED"), "splash");

    const char *path = reinterpret_cast<const char *>(FPSTR(SplashScreenConsts::splash_path));
    // debug_logger.debug(std::string("attempting to open: ") + path, "splash");
    
    // Check if file exists before opening
    bool file_exists = LittleFS.exists(path);
    // debug_logger.debug(std::string("file exists check: ") + (file_exists ? "YES" : "NO"), "splash");
    
    if (!file_exists) {
        // List directory contents for debugging
        fs::File root = LittleFS.open("/");
        debug_logger.debug("contents of /www:", "init");
        if (root && root.isDirectory()) {
            fs::File file = root.openNextFile();
            while (file) {
                debug_logger.debug(std::string(" - ") + file.name() + " (" + std::to_string(file.size()) + " bytes)", "splash");
                file = root.openNextFile();
            }
        }
        // debug_logger.error("splash.png not found in /www", "splash");
        return;
    }
    
    int open_result = s_png.open(path, png_open_cb, png_close_cb, png_read_cb, png_seek_cb, png_draw_cb);
    if (open_result != PNG_SUCCESS) {
        debug_logger.error(std::string("PNG open failed: result=") + std::to_string(open_result));
        return; // file not found or corrupt
    }
    // debug_logger.debug("PNG open success", "splash");

    const int16_t img_w = static_cast<int16_t>(s_png.getWidth());
    const int16_t img_h = static_cast<int16_t>(s_png.getHeight());
    // debug_logger.debug(std::string("Image dimensions: width=") + std::to_string(img_w) + " height=" + std::to_string(img_h));

    if (img_w > static_cast<int16_t>(SplashScreenConsts::max_img_width))
    {
        debug_logger.error(std::string("image too wide: ") + std::to_string(img_w) + " > " + std::to_string(SplashScreenConsts::max_img_width));
        s_png.close();
        return; // image wider than line buffer — refuse to render garbage
    }

    // Centre the image on the screen
    s_draw_x = (UILayout::SCREEN_W - img_w) / 2;
    s_draw_y = (UILayout::SCREEN_H - img_h) / 2;
    // debug_logger.debug(std::string("[SplashScreen] image centered at x=") + std::to_string(s_draw_x) + " y=" + std::to_string(s_draw_y));

    tft.startWrite();
    // debug_logger.debug("[SplashScreen] starting PNG decode");
    int decode_result = s_png.decode(nullptr, 0);
    // debug_logger.debug(std::string("[SplashScreen] PNG decode complete: result=") + std::to_string(decode_result));
    tft.endWrite();
    s_png.close();
    // debug_logger.debug("[SplashScreen] initScreen: complete");
}