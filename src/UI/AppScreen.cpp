/**
 * @file AppScreen.cpp
 * @brief AmakerBot application dashboard: info panel, servo table, motor table.
 */

#include "UI/AppScreen.h"
#include <TFT_eSPI.h>
#include <cstdio>
#include <string>

extern TFT_eSPI tft;
extern DFR1216_I2C board;


// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
namespace AppScreenConstants
{

    constexpr int line_height = 8;
    constexpr int char_width = 6;
    constexpr int v_pad = 3; ///< px gap between text bottom and the H-line below it

    // Pre-format the static left-column labels to save some CPU in the display task
    constexpr uint16_t table_amakerbot_line = 0 * line_height;
    constexpr uint16_t table_amakerbot_column = 0 * char_width;
    constexpr uint16_t table_udp_line = 11 * line_height;
    constexpr uint16_t table_udp_column = 0 * char_width;
    constexpr uint16_t table_servo_line = 20 * line_height;
    constexpr uint16_t table_servo_column = 0 * char_width;
    constexpr uint16_t table_motor_line = 20 * line_height;
    constexpr uint16_t table_motor_column = 21 * char_width;
    constexpr uint16_t table_tech_line = 0 * line_height;
    constexpr uint16_t table_tech_column = 0 * char_width;
    constexpr uint16_t table_espinfo_line = 31 * line_height;
    constexpr uint16_t table_espinfo_column = 0 * char_width;
    constexpr uint16_t table_counters_line = 12 * line_height;
    constexpr uint16_t table_counters_column = 0 * char_width;

} // namespace AppScreenConstants

AppScreen::AppScreen(WifiService &wifi,
                     AmakerBotService &amakerbot,
                     BotServerWeb &web,
                     BotServerUDP &udp,
                     BotServerWebSocket &ws,
                     MotorServoService &motor_servo)
    : wifi_(wifi), amakerbot_(amakerbot), web_(web), udp_(udp), ws_(ws), motor_servo_(motor_servo)
{
}

// ---------------------------------------------------------------------------
// IsScreen
// ---------------------------------------------------------------------------

void AppScreen::initScreen()
{
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.fillScreen(UIColors::CLR_BLACK);
    tft.resetViewport();
    tft.setTextDatum(TL_DATUM);

    // Draw all static chrome once (borders + fixed labels/values).
    drawInfoPanel(true);
    drawCountersPanel(true);
    drawServoTable(true);
    drawMotorTable(true);
    drawESPInfo(true);
    drawBatteryInfo(true);
}

void AppScreen::updateScreen()
{
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.resetViewport();
    tft.setTextDatum(TL_DATUM);

    // Repaint only the cells that contain live data.
    drawInfoPanel(false);
    drawCountersPanel(false);
    drawServoTable(false);
    drawMotorTable(false);
    drawESPInfo(false);
    drawBatteryInfo(false);
}

// ---------------------------------------------------------------------------
// colorsForStatus
// ---------------------------------------------------------------------------

void AppScreen::colorsForStatus(ServiceStatus status, uint16_t &fg, uint16_t &bg)
{
    switch (status)
    {
    case STARTED:
        fg = UIColors::CLR_STATUS_STARTED_FG;
        bg = UIColors::CLR_STATUS_STARTED_BG;
        break;
    case STOPPED:
        fg = UIColors::CLR_STATUS_STOPPED_FG;
        bg = UIColors::CLR_STATUS_STOPPED_BG;
        break;
    case INITIALIZED:
        fg = UIColors::CLR_STATUS_INIT_FG;
        bg = UIColors::CLR_STATUS_INIT_BG;
        break;
    default:
        fg = UIColors::CLR_STATUS_ERR_FG;
        bg = UIColors::CLR_STATUS_ERR_BG;
        break;
    }
}

// ---------------------------------------------------------------------------
// servoTypeLabel
// ---------------------------------------------------------------------------

const char *AppScreen::servoTypeLabel(ServoType t)
{
    switch (t)
    {
    case ServoType::SERVO_180:
        return "---";
    case ServoType::SERVO_270:
        return "270";
    case ServoType::CONTINUOUS:
        return "rot";
    default:
        return "---";
    }
}

// ---------------------------------------------------------------------------
// formatThousands
// ---------------------------------------------------------------------------

/**
 * @brief Format a 32-bit unsigned integer with space thousands separators.
 *
 * Examples:
 *   0        → "0"
 *   999      → "999"
 *   1000     → "1 000"
 *   1234567  → "1 234 567"
 *
 * @param value Integer value to format.
 * @return std::string with digit groups of three separated by spaces.
 */
std::string AppScreen::formatThousands(uint32_t value)
{
    // Build digits right-to-left into a small fixed buffer.
    // Maximum uint32_t is 4 294 967 295 — 10 digits + 3 spaces = 13 chars.
    constexpr uint8_t buf_size = 14;
    char buf[buf_size];
    uint8_t pos   = buf_size - 1;
    uint8_t count = 0;

    buf[pos--] = '\0';

    do
    {
        if (count > 0 && count % 3 == 0)
            buf[pos--] = ' ';
        buf[pos--] = static_cast<char>('0' + value % 10);
        value /= 10;
        ++count;
    } while (value > 0);

    return std::string(buf + pos + 1);
}

// ---------------------------------------------------------------------------
// drawInfoPanel — left panel, 40-char wide table
//
// Column layout:  | 16-char label | 21-char value |
// Col |           :              0  17             39
// x pixel        :              0 102            234
// H-lines at y   :    0 (top)  16 (divider)   80 (bottom)
// V-lines        :  left/right 0→80;  mid 16→80
// ---------------------------------------------------------------------------

void AppScreen::drawInfoPanel(bool chrome_only)
{
    const int lh = AppScreenConstants::line_height;  // 8
    const int cw = AppScreenConstants::char_width;   // 6
    const int x0 = AppScreenConstants::table_tech_column;  // 0
    const int y0 = AppScreenConstants::table_tech_line;    // 0

    const int xm  = x0 + 17 * cw;  // mid column border  = 102
    const int xr  = x0 + 39 * cw;  // right border       = 234
    const int vp  = AppScreenConstants::v_pad;
    const int y_div = y0 + 2  * lh + vp; // divider after title
    const int y_bot = y0 + 10 * lh + vp; // bottom border

    tft.setTextDatum(TL_DATUM);

    if (chrome_only)
    {
        const uint16_t c = UIColors::CLR_LINES_COLOR;

        // ── Pixel borders ──────────────────────────────────────────────────
        tft.drawFastHLine(x0, y0,    xr - x0 + 1, c);  // top
        tft.drawFastHLine(x0, y_div, xr - x0 + 1, c);  // title divider
        tft.drawFastHLine(x0, y_bot, xr - x0 + 1, c);  // bottom

        tft.drawFastVLine(x0, y0,    y_bot - y0    + 1, c); // left
        tft.drawFastVLine(xr, y0,    y_bot - y0    + 1, c); // right
        tft.drawFastVLine(xm, y_div, y_bot - y_div + 1, c); // mid (below title)

        // ── Static labels — left cell (16 chars) ───────────────────────────
        tft.setTextColor(c, UIColors::CLR_STATUS_DEFAULT_BG);


                char tbuf[39];
        snprintf(tbuf, sizeof(tbuf), "%-16s", "Bricks Browl"                );
        tft.setCursor(x0 + cw, y0 + lh);
        tft.print(tbuf);

        char lbuf[17];
        char vbuf[22];
        snprintf(lbuf, sizeof(lbuf), "%-16s", "SSID");
        tft.setCursor(x0 + cw, y0 + 3 * lh); tft.print(lbuf);

        snprintf(lbuf, sizeof(lbuf), "%-16s", "IP");
        tft.setCursor(x0 + cw, y0 + 4 * lh); tft.print(lbuf);

        snprintf(lbuf, sizeof(lbuf), "%-16s", "Hostname");
        tft.setCursor(x0 + cw, y0 + 5 * lh); tft.print(lbuf);

        // Port rows: label + value are both static
        snprintf(lbuf, sizeof(lbuf), "%-16s", "UDP port");
        tft.setCursor(x0 + cw,      y0 + 6 * lh); tft.print(lbuf);
        snprintf(vbuf, sizeof(vbuf), "%-21u", udp_.getPort());
        tft.setCursor(x0 + 18 * cw, y0 + 6 * lh); tft.print(vbuf);

        snprintf(lbuf, sizeof(lbuf), "%-16s", "WebSocket port");
        tft.setCursor(x0 + cw,      y0 + 7 * lh); tft.print(lbuf);
        snprintf(vbuf, sizeof(vbuf), "%-21u", ws_.getPort());
        tft.setCursor(x0 + 18 * cw, y0 + 7 * lh); tft.print(vbuf);

        snprintf(lbuf, sizeof(lbuf), "%-16s", "HTTP port");
        tft.setCursor(x0 + cw,      y0 + 8 * lh); tft.print(lbuf);
        snprintf(vbuf, sizeof(vbuf), "%-21u", web_.getPort());
        tft.setCursor(x0 + 18 * cw, y0 + 8 * lh); tft.print(vbuf);

                   snprintf(lbuf, sizeof(lbuf), "%-16s", "Master");
                           tft.setCursor(x0 + cw,       y0 + 9 * lh); tft.print(lbuf);
    }
    else
    {
        char vbuf[22]; // 21-char value cell + NUL

        // Row 1 — bot name title (status colour, full inner width = 38 chars)
        // No mid V-line at this row (starts at y_div), so single print is safe.
        uint16_t fg, bg;
        colorsForStatus(amakerbot_.getStatus(), fg, bg);
        tft.setTextColor(fg, bg);
        char tbuf[39];
        snprintf(tbuf, sizeof(tbuf), "%-16s % 20s ", "D4A",
                 amakerbot_.getBotName().substr(0, 20).c_str());
        tft.setCursor(x0 + cw, y0 + lh);
        tft.print(tbuf);

        // Rows 3–5 — network values (right cell only)
        tft.setTextColor(UIColors::CLR_LINES_COLOR, UIColors::CLR_STATUS_DEFAULT_BG);
        snprintf(vbuf, sizeof(vbuf), "%-21s", wifi_.getSSID().substr(0, 21).c_str());
        tft.setCursor(x0 + 18 * cw, y0 + 3 * lh); tft.print(vbuf);

        snprintf(vbuf, sizeof(vbuf), "%-21s", wifi_.getIP().substr(0, 21).c_str());
        tft.setCursor(x0 + 18 * cw, y0 + 4 * lh); tft.print(vbuf);

        snprintf(vbuf, sizeof(vbuf), "%-21s", wifi_.getHostname().substr(0, 21).c_str());
        tft.setCursor(x0 + 18 * cw, y0 + 5 * lh); tft.print(vbuf);

        // Row 9 — master IP / token (both label and value change)
        const std::string master_ip = amakerbot_.getMasterIP();
        const std::string token     = amakerbot_.getServerToken();
        
        if (master_ip.empty())
        {
             tft.setTextColor(UIColors::CLR_LINES_HIGHLIGHT_COLOR, UIColors::CLR_STATUS_DEFAULT_BG);
            const std::string prompt = "REG: " + token;
            snprintf(vbuf, sizeof(vbuf), "%-21s", prompt.substr(0, 21).c_str());
        }
        else
        {
            snprintf(vbuf, sizeof(vbuf), "%-21s", master_ip.substr(0, 21).c_str());
        }

        tft.setCursor(x0 + 18 * cw,  y0 + 9 * lh); tft.print(vbuf);
    }
}

void AppScreen::drawCountersPanel(bool chrome_only)
{
    // Column layout:  | 8-char name | 9-char #in | 9-char #out | 9-char #drop |
    // Col |           : 0           9           19            29             39
    // x pixel        : 0          54          114           174            234
    // H-lines at y   :  96 (top)  112 (divider)  144 (bottom)
    // Inner V-lines start at y_div (112) — title row (104) safe for per-cell text.
    const int lh = AppScreenConstants::line_height;
    const int cw = AppScreenConstants::char_width;
    const int x0 = AppScreenConstants::table_tech_column;   // 0
    const int y0 = AppScreenConstants::table_counters_line; // 96

    const int xc1 = x0 + 9  * cw; // = 54
    const int xc2 = x0 + 19 * cw; // = 114
    const int xc3 = x0 + 29 * cw; // = 174
    const int xr  = x0 + 39 * cw; // = 234
    const int vp   = AppScreenConstants::v_pad;
    const int y_div = y0 + 2 * lh + vp;
    const int y_bot = y0 + 6 * lh + 2 * vp; // +2×vp: data rows shift with y_div

    tft.setTextDatum(TL_DATUM);

    if (chrome_only)
    {
        const uint16_t c = UIColors::CLR_LINES_COLOR;

        // ── Pixel borders ──────────────────────────────────────────────────
        tft.drawFastHLine(x0, y0,    xr - x0 + 1, c);  // top
        tft.drawFastHLine(x0, y_div, xr - x0 + 1, c);  // data divider
        tft.drawFastHLine(x0, y_bot, xr - x0 + 1, c);  // bottom

        tft.drawFastVLine(x0,  y0,    y_bot - y0    + 1, c); // left
        tft.drawFastVLine(xr,  y0,    y_bot - y0    + 1, c); // right
        tft.drawFastVLine(xc1, y_div, y_bot - y_div + 1, c); // col 9
        tft.drawFastVLine(xc2, y_div, y_bot - y_div + 1, c); // col 19
        tft.drawFastVLine(xc3, y_div, y_bot - y_div + 1, c); // col 29

        // ── Title row: column headers — no inner V-lines at this y ────────
        tft.setTextColor(UIColors::CLR_LINES_COLOR, UIColors::CLR_STATUS_DEFAULT_BG);
        tft.setCursor(x0 + cw,      y0 + lh); tft.print("Services"); // 8 chars
        tft.setCursor(x0 + 10 * cw, y0 + lh); tft.print("      #in"); // 9 chars
        tft.setCursor(x0 + 20 * cw, y0 + lh); tft.print("     #out"); // 9 chars
        tft.setCursor(x0 + 30 * cw, y0 + lh); tft.print("    #drop"); // 9 chars
        int y = y_div + lh;
        char nbuf[9];  // 8-char name + NUL

        snprintf(nbuf, sizeof(nbuf), "%-8s", "UDP");
        tft.setCursor(x0 + cw,      y); tft.print(nbuf);
        y+= lh;
                snprintf(nbuf, sizeof(nbuf), "%-8s", "Web");
        tft.setCursor(x0 + cw,      y); tft.print(nbuf);
        y+= lh;
                snprintf(nbuf, sizeof(nbuf), "%-8s", "WSocket");
        tft.setCursor(x0 + cw,      y); tft.print(nbuf);
    }
    else
    {
        tft.setTextColor(UIColors::CLR_STATUS_DEFAULT_FG, UIColors::CLR_STATUS_DEFAULT_BG);
        char vbuf[10]; // 9-char value + NUL

        // UDP row (y = y_div + lh = 120)
        int y = y_div + lh;

        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(udp_.getRxCount()).c_str());
        tft.setCursor(x0 + 10 * cw, y); tft.print(vbuf);
        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(udp_.getTxCount()).c_str());
        tft.setCursor(x0 + 20 * cw, y); tft.print(vbuf);
        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(udp_.getDroppedCount()).c_str());
        tft.setCursor(x0 + 30 * cw, y); tft.print(vbuf);

        // Web row (y = 128)
        y += lh;

        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(web_.getRxCount()).c_str());
        tft.setCursor(x0 + 10 * cw, y); tft.print(vbuf);
        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(web_.getTxCount()).c_str());
        tft.setCursor(x0 + 20 * cw, y); tft.print(vbuf);
        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(web_.getDroppedCount()).c_str());
        tft.setCursor(x0 + 30 * cw, y); tft.print(vbuf);

        // WebSocket row (y = 136)
        y += lh;

        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(ws_.getRxCount()).c_str());
        tft.setCursor(x0 + 10 * cw, y); tft.print(vbuf);
        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(ws_.getTxCount()).c_str());
        tft.setCursor(x0 + 20 * cw, y); tft.print(vbuf);
        snprintf(vbuf, sizeof(vbuf), "%9s", formatThousands(ws_.getDroppedCount()).c_str());
        tft.setCursor(x0 + 30 * cw, y); tft.print(vbuf);
    }
}

// ---------------------------------------------------------------------------
// drawServoTable — left column, rows 20+
//
// Row format:  | S%u | %3s | %+4d |
// Col |        :  0    5    11    18
// x pixel     :  0   30    66   108
// H-lines at y: 160 (top)  176 (divider)  160+(3+SERVO_COUNT)*8 (bottom)
// Inner V-lines start at y_div (176) — header row (168) safe for full print.
// ---------------------------------------------------------------------------

void AppScreen::drawServoTable(bool chrome_only)
{
    const int lh = AppScreenConstants::line_height;
    const int cw = AppScreenConstants::char_width;
    const int x0 = AppScreenConstants::table_servo_column;  // 0
    const int y0 = AppScreenConstants::table_servo_line;    // 160

    const int xc1 = x0 + 5  * cw;  // = 30
    const int xc2 = x0 + 11 * cw;  // = 66
    const int xr  = x0 + 18 * cw;  // = 108
    const int vp   = AppScreenConstants::v_pad;
    const int y_div = y0 + 2 * lh + vp;
    const int y_bot = y0 + (3 + MotorServoConsts::SERVO_COUNT) * lh + 2 * vp;

    tft.setTextDatum(TL_DATUM);

    if (chrome_only)
    {
        const uint16_t c = UIColors::CLR_LINES_COLOR;

        // ── Pixel borders ──────────────────────────────────────────────────
        tft.drawFastHLine(x0, y0,    xr - x0 + 1, c);  // top
        tft.drawFastHLine(x0, y_div, xr - x0 + 1, c);  // data divider
        tft.drawFastHLine(x0, y_bot, xr - x0 + 1, c);  // bottom

        tft.drawFastVLine(x0,  y0,    y_bot - y0    + 1, c); // left
        tft.drawFastVLine(xr,  y0,    y_bot - y0    + 1, c); // right
        tft.drawFastVLine(xc1, y_div, y_bot - y_div + 1, c); // col 5  (data only)
        tft.drawFastVLine(xc2, y_div, y_bot - y_div + 1, c); // col 11 (data only)
                tft.setCursor(x0 + cw, y0 + lh);
        tft.print(" Servos          "); // 17 chars = inner width

        char ch_buf[5]; // " S%u "  = 4 chars + NUL


        int y = y_div + lh;
        for (uint8_t ch = 0; ch < MotorServoConsts::SERVO_COUNT; ++ch, y += lh)
        {
            int16_t angle = 0;
            motor_servo_.getServosAngle(1u << ch, &angle);
            snprintf(ch_buf, sizeof(ch_buf), " S%u ",  ch);

            tft.setCursor(x0 + cw,      y); tft.print(ch_buf); // cell 1 (4 chars)
        }

    }
    else
    {
        // Row 1 — "Servos" header with service status colour.
        // Inner V-lines start below this row, so a full-width print is safe.
        uint16_t fg, bg;
        colorsForStatus(motor_servo_.getStatus(), fg, bg);
        tft.setTextColor(fg, bg);
        tft.setCursor(x0 + cw, y0 + lh);
        tft.print(" Servos          "); // 17 chars = inner width

        // Data rows — each cell printed individually to stay within V-lines
        tft.setTextColor(UIColors::CLR_INFO, UIColors::CLR_BLACK);
        char ty_buf[6]; // " %3s "  = 5 chars + NUL
        char an_buf[7]; // " %+4d " = 6 chars + NUL

        int y = y_div + lh;
        for (uint8_t ch = 0; ch < MotorServoConsts::SERVO_COUNT; ++ch, y += lh)
        {
            int16_t angle = 0;
            motor_servo_.getServosAngle(1u << ch, &angle);

            snprintf(ty_buf, sizeof(ty_buf), " %3s ",  servoTypeLabel(motor_servo_.getServoType(ch)));
            snprintf(an_buf, sizeof(an_buf), " %+4d ", static_cast<int>(angle));
            tft.setCursor(x0 + 6  * cw, y); tft.print(ty_buf); // cell 2 (5 chars)
            tft.setCursor(x0 + 12 * cw, y); tft.print(an_buf); // cell 3 (6 chars)
        }
    }
}

// ---------------------------------------------------------------------------
// drawMotorTable — right column (col 21+), rows 20+
//
// Row format:  | M%u | %+4d |
// Col |        :  0    5     12   (relative to x0=126)
// x pixel     :126  156    198
// H-lines at y: 160 (top)  176 (divider)  160+(3+MOTOR_COUNT)*8 (bottom)
// Mid V-line starts at y_div (176) so header row (168) is safe for full print.
// ---------------------------------------------------------------------------

void AppScreen::drawMotorTable(bool chrome_only)
{
    const int lh = AppScreenConstants::line_height;
    const int cw = AppScreenConstants::char_width;
    const int x0 = AppScreenConstants::table_motor_column;  // 126
    const int y0 = AppScreenConstants::table_motor_line;    // 160

    const int xm  = x0 + 5  * cw;  // mid border = 156
    const int xr  = x0 + 12 * cw;  // right border = 198
    const int vp   = AppScreenConstants::v_pad;
    const int y_div = y0 + 2 * lh + vp;
    const int y_bot = y0 + (3 + MotorServoConsts::MOTOR_COUNT) * lh + 2 * vp;

    tft.setTextDatum(TL_DATUM);

    if (chrome_only)
    {
        const uint16_t c = UIColors::CLR_LINES_COLOR;
        tft.setTextColor(UIColors::CLR_LINES_COLOR, UIColors::CLR_STATUS_DEFAULT_BG);

        // ── Pixel borders ──────────────────────────────────────────────────
        tft.drawFastHLine(x0, y0,    xr - x0 + 1, c);  // top
        tft.drawFastHLine(x0, y_div, xr - x0 + 1, c);  // data divider
        tft.drawFastHLine(x0, y_bot, xr - x0 + 1, c);  // bottom

        tft.drawFastVLine(x0, y0,    y_bot - y0    + 1, c); // left
        tft.drawFastVLine(xr, y0,    y_bot - y0    + 1, c); // right
        tft.drawFastVLine(xm, y_div, y_bot - y_div + 1, c); // mid (data only)
        tft.setCursor(x0 + cw, y0 + lh);
        tft.print(" DC Motors "); // 11 chars = inner width
        char m_buf[5]; // " M%u "  = 4 chars + NUL
        int y = y_div + lh;
        for (uint8_t motor = 1; motor <= MotorServoConsts::MOTOR_COUNT; ++motor, y += lh)
        {
                        snprintf(m_buf, sizeof(m_buf), " M%u ",  motor);
            tft.setCursor(x0 + cw,     y); tft.print(m_buf); // cell 1 (4 chars)
        }
    }
    else
    {
        // Row 1 — "DC Motors" header with service status colour.
        // Mid V-line starts below this row, so a full-width print is safe.
        uint16_t fg, bg;
        colorsForStatus(motor_servo_.getStatus(), fg, bg);
        tft.setTextColor(fg, bg);
        tft.setCursor(x0 + cw, y0 + lh);
        tft.print(" DC Motors "); // 11 chars = inner width

        // Data rows — each cell printed individually to stay within V-lines
        tft.setTextColor(UIColors::CLR_INFO, UIColors::CLR_BLACK);
        char v_buf[7]; // " %+4d " = 6 chars + NUL

        int y = y_div + lh;
        for (uint8_t motor = 1; motor <= MotorServoConsts::MOTOR_COUNT; ++motor, y += lh)
        {
        
            int8_t speed = 0;
            motor_servo_.getMotorSpeeds(1u << (motor - 1), &speed);
            snprintf(v_buf, sizeof(v_buf), " %+4d ", static_cast<int>(speed));
            tft.setCursor(x0 + 6 * cw, y); tft.print(v_buf); // cell 2 (6 chars)
        }
    }
}

/**
 * @brief Draw a battery-shape icon to the right of the DC motors table.
 *
 * The icon sits in the dead space between the motor table right border (x=198)
 * and the right screen edge (x=239), vertically aligned with the servo table
 * (y=160–238, the taller of the two tables).
 *
 * Shape: a tall body rectangle with a small terminal nub on top, both outlined
 * in white.  The interior is filled bottom-up proportional to the battery
 * percentage.  Fill colour is green ≥50 %, yellow 20–49 %, red <20 %.
 *
 * @param chrome_only  true  → draw the static white outline only (called once
 *                             from initScreen).
 *                     false → repaint the fill only (called every tick from
 *                             updateScreen).
 */
void AppScreen::drawBatteryInfo(bool chrome_only)
{
    // ── Geometry ────────────────────────────────────────────────────────────
    // Servo table (tallest) spans y = 160 → 238 (SERVO_COUNT=6).
    // Motor table right border is at x = 126 + 12*6 = 198.
    // Remaining horizontal space: x = 199 → 239 (41 px).

    const int lh = AppScreenConstants::line_height;   // 8
    const int vp = AppScreenConstants::v_pad;          // 3

    // Vertical extents: match the servo table
    const int y0          = AppScreenConstants::table_servo_line;                          // 160
    const int y_bot_table = y0 + (3 + MotorServoConsts::SERVO_COUNT) * lh + 2 * vp;      // 238

    // Horizontal centre of the available band
    const int motor_xr  = AppScreenConstants::table_motor_column
                          + 12 * AppScreenConstants::char_width;                           // 198
    const int center_x  = motor_xr + (240 - motor_xr) / 2;                               // 219

    // Battery body rectangle
    constexpr int batt_w = 22;  ///< body width  (px)
    constexpr int nub_w  = 10;  ///< terminal nub width  (px)
    constexpr int nub_h  = 5;   ///< terminal nub height (px)
    constexpr int margin = 2;   ///< gap from table borders (px)

    const int body_x     = center_x - batt_w / 2;            // 208
    const int nub_x      = center_x - nub_w  / 2;            // 214

    const int nub_y_top  = y0          + margin;              // 162  — top of nub
    const int body_y_top = nub_y_top   + nub_h;              // 167  — top of body
    const int body_y_bot = y_bot_table - margin;              // 236  — bottom of body

    // Interior fill region (1 px inside the body outline)
    const int fill_x     = body_x      + 1;
    const int fill_w     = batt_w      - 2;
    const int fill_y_top = body_y_top  + 1;
    const int fill_y_bot = body_y_bot  - 1;
    const int fill_h     = fill_y_bot  - fill_y_top + 1;     // ≈ 68 px

    if (chrome_only)
    {
        // ── Static white outline ─────────────────────────────────────────
        tft.drawRect(body_x, body_y_top,
                     batt_w, body_y_bot - body_y_top + 1,
                     TFT_WHITE);                                 // body
        tft.drawFastHLine(nub_x,              nub_y_top, nub_w, TFT_WHITE); // nub top
        tft.drawFastVLine(nub_x,              nub_y_top, nub_h, TFT_WHITE); // nub left
        tft.drawFastVLine(nub_x + nub_w - 1, nub_y_top, nub_h, TFT_WHITE); // nub right
    }
    else
    {
        // ── Dynamic fill ─────────────────────────────────────────────────
        // Clamp to [0,100] in case board.getBattery() returns a raw value > 100.
        const int batt_pct = min(static_cast<int>(board.getBattery()), 100);
        const int filled_h = min((batt_pct * fill_h + 50) / 100, fill_h);
        const int empty_h  = fill_h - filled_h;

        // Pick fill colour by charge level
        uint16_t fill_color;
        if      (batt_pct >= 65) fill_color = TFT_GREEN;
        else if (batt_pct >= 35) fill_color = TFT_YELLOW;
        else if (batt_pct >= 20) fill_color = TFT_ORANGE;
        else                     fill_color = TFT_RED;

        // Erase the empty (top) portion
        if (empty_h  > 0)
            tft.fillRect(fill_x, fill_y_top,               fill_w, empty_h,  TFT_BLACK);

        // Paint the filled (bottom) portion
        if (filled_h > 0)
            tft.fillRect(fill_x, fill_y_bot - filled_h + 1, fill_w, filled_h, fill_color);
    }
}
void AppScreen::drawESPInfo(bool chrome_only)
{
    // Column layout:  | 20-char label | 17-char value |
    // Col |           :  0             21             39
    // x pixel        :  0            126            234
    // H-lines at y   :  248 (top)  264 (divider)  312 (bottom)
    // Mid V-line starts at y_div (264) — chip row (256) safe for full print.
    const int lh = AppScreenConstants::line_height;
    const int cw = AppScreenConstants::char_width;
    const int x0 = AppScreenConstants::table_espinfo_column;  // 0
    const int y0 = AppScreenConstants::table_espinfo_line;    // 248

    const int xm  = x0 + 21 * cw;  // mid border = 126
    const int xr  = x0 + 39 * cw;  // right border = 234
    const int vp   = AppScreenConstants::v_pad;
    const int y_div = y0 + 2 * lh + vp;
    const int y_bot = y0 + 8 * lh + vp;

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(UIColors::CLR_STATUS_DEFAULT_FG, UIColors::CLR_STATUS_DEFAULT_BG);

    if (chrome_only)
    {
        const uint16_t c = UIColors::CLR_LINES_COLOR;       
        tft.setTextColor(UIColors::CLR_LINES_COLOR, UIColors::CLR_STATUS_DEFAULT_BG);

        // ── Pixel borders ──────────────────────────────────────────────────
        tft.drawFastHLine(x0, y0,    xr - x0 + 1, c);  // top
        tft.drawFastHLine(x0, y_div, xr - x0 + 1, c);  // chip/data divider
        tft.drawFastHLine(x0, y_bot, xr - x0 + 1, c);  // bottom

        tft.drawFastVLine(x0, y0,    y_bot - y0    + 1, c); // left
        tft.drawFastVLine(xr, y0,    y_bot - y0    + 1, c); // right
        tft.drawFastVLine(xm, y_div, y_bot - y_div + 1, c); // mid (below chip row)

        // ── Row 1: chip model — spans full inner width (38 chars), safe ────
        // No mid V-line at this y, outer Vs at x=0 and x=234 not in range.
        char buf[39];
        snprintf(buf, sizeof(buf), " %16s %-4uMHz %1u core(s)",
                 ESP.getChipModel(), ESP.getCpuFreqMHz(), ESP.getChipCores());
        tft.setCursor(x0 + cw, y0 + lh);
        tft.print(buf);

        // ── Static label + value rows ───────────────────────────────────────
        char lbuf[21];
        char vbuf[18];


        snprintf(lbuf, sizeof(lbuf), "%-20s", "Free heap");
        tft.setCursor(x0 + cw,      y0 + 3 * lh); tft.print(lbuf);
        snprintf(lbuf, sizeof(lbuf), "%-20s", "Free PSRAM");
        tft.setCursor(x0 + cw, y0 + 4 * lh); tft.print(lbuf);
        
        // snprintf(lbuf, sizeof(lbuf), "%-20s", "Battery %");
        // tft.setCursor(x0 + cw, y0 + 6 * lh); tft.print(lbuf);
        


    }
    else
    {
        // Only the two values that actually change each tick
        char rbuf[18];
        

        uint32_t heap_pct = (ESP.getFreeHeap() * 100) / ESP.getHeapSize();
        if (heap_pct > 99) heap_pct = 99;
        snprintf(rbuf, sizeof(rbuf), "%02u %% %11s", heap_pct, formatThousands(ESP.getFreeHeap()).c_str());
        tft.setCursor(x0 + 22 * cw, y0 + 3 * lh); tft.print(rbuf);

        uint32_t psram_pct = (ESP.getFreePsram() * 100) / ESP.getPsramSize();
        if (psram_pct > 99) psram_pct = 99;
        snprintf(rbuf, sizeof(rbuf), "%02u %% %11s", psram_pct, formatThousands(ESP.getFreePsram()).c_str());
        tft.setCursor(x0 + 22 * cw, y0 + 4 * lh); tft.print(rbuf);

        
        // snprintf(rbuf, sizeof(rbuf), "%02u %%", board.getBattery());
        // tft.setCursor(x0 + 22 * cw, y0 + 6 * lh); tft.print(rbuf);
    }
}