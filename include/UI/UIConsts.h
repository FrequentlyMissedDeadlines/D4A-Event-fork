/**
 * @file UIConsts.h
 * @brief Shared TFT display layout constants and colour palette for all screens.
 *
 * Centralises the numeric values that AppScreen, LogScreen and the service
 * manager all need so they never drift out of sync.
 */
#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// UILayout — TFT geometry (font-1 at 1× scale, 240×320 screen)
// ---------------------------------------------------------------------------
namespace UILayout
{
    constexpr int SCREEN_W         = 240;             ///< TFT width in pixels
    constexpr int SCREEN_H         = 320;             ///< TFT height in pixels
    constexpr int CHAR_W           = 6;               ///< Font-1 character pixel width
    constexpr int LINE_H           = 8;               ///< Font-1 line pixel height
    constexpr int CHARS_PER_LINE   = SCREEN_W / CHAR_W;   ///< 40 chars per row
    constexpr int LINES_PER_SCREEN = SCREEN_H / LINE_H;   ///< 40 lines per screen
    constexpr int RIGHT_PANEL_PX   = 21 * CHAR_W;    ///< x-pixel where right column begins (126 px)
} // namespace UILayout

// ---------------------------------------------------------------------------
// UIColors — 16-bit RGB565 colour palette
// ---------------------------------------------------------------------------
namespace UIColors
{
    // Base colours
    constexpr uint16_t CLR_BLACK       = 0x0000;
    constexpr uint16_t CLR_WHITE       = 0xFFFF;
    constexpr uint16_t CLR_CYAN        = 0x07FF;
    constexpr uint16_t CLR_GREEN       = 0x07E0;
    constexpr uint16_t CLR_RED         = 0xF800;
    constexpr uint16_t CLR_YELLOW      = 0xFFE0;
    constexpr uint16_t CLR_LIGHTGREY   = 0xC618;
    constexpr uint16_t CLR_DARKGREY    = 0x7BEF;

    // Service status colours (header rows coloured by service health)
    constexpr uint16_t CLR_STATUS_STARTED_FG  = CLR_GREEN;
    constexpr uint16_t CLR_STATUS_STARTED_BG  = CLR_BLACK;
    constexpr uint16_t CLR_STATUS_STOPPED_FG  = CLR_RED;
    constexpr uint16_t CLR_STATUS_STOPPED_BG  = CLR_BLACK;
    constexpr uint16_t CLR_STATUS_INIT_FG     = CLR_YELLOW;
    constexpr uint16_t CLR_STATUS_INIT_BG     = CLR_BLACK;
    constexpr uint16_t CLR_STATUS_ERR_FG      = CLR_RED;
    constexpr uint16_t CLR_STATUS_ERR_BG      = CLR_BLACK;
    constexpr uint16_t CLR_STATUS_DEFAULT_FG  = CLR_WHITE;
    constexpr uint16_t CLR_STATUS_DEFAULT_BG  = CLR_BLACK;
    constexpr uint16_t CLR_LINES_COLOR  = CLR_LIGHTGREY;
    constexpr uint16_t CLR_LINES_HIGHLIGHT_COLOR  = CLR_YELLOW;
    // Content / decoration colours
    constexpr uint16_t CLR_INFO  = CLR_CYAN;
    constexpr uint16_t CLR_TITLE = CLR_CYAN;
} // namespace UIColors
