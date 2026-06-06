/**
 * @file ESPLogToRolling.cpp
 * @brief Capture ESP-IDF log output and forward it to a RollingLogger instance.
 *
 * @details
 * Installs a custom `vprintf` handler via `esp_log_set_vprintf()`.  The
 * handler formats each log line into a fixed stack buffer, strips ANSI color
 * escape codes emitted by the ESP-IDF formatter, maps the leading level
 * character (E/W/I/D/V) to the matching `RollingLogger::LogLevel`, and calls
 * `RollingLogger::log()`.
 *
 * Memory: all formatting uses a stack-allocated buffer — no heap allocation.
 */

#include "ESPLogToRolling.h"
#include "RollingLogger.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <esp_log.h>

namespace
{
    constexpr const char logger_source[] = "ESP";
}

#define trace(message, ...) trace(message, logger_source)
#define debug(message, ...) debug(message, logger_source)
#define info(message, ...) info(message, logger_source)
#define warning(message, ...) warning(message, logger_source)
#define error(message, ...) error(message, logger_source)

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------

namespace
{
    /// Active target logger; nullptr disables capture.
    RollingLogger *esp_logger_instance = nullptr;

    /// Maximum formatted line length (bytes including NUL).
    constexpr size_t max_log_line = 256;
} // namespace

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Strip ANSI SGR escape sequences from @p input.
 *
 * The ESP-IDF logger emits colour codes of the form `ESC [ <digits> m`.
 * This function copies every character except those sequences into a new
 * `std::string`.
 *
 * @param input NUL-terminated source string (may be nullptr → empty result).
 * @return Cleaned string without ANSI codes.
 */
static std::string strip_ansi_codes(const char *input)
{
    if (!input)
        return {};

    const size_t len = strlen(input);
    std::string  output;
    output.reserve(len);

    for (size_t i = 0; i < len; ++i)
    {
        // ESC [ ... m  — skip the whole sequence
        if (input[i] == '\x1B' && i + 1 < len && input[i + 1] == '[')
        {
            i += 2; // skip ESC and '['
            while (i < len && input[i] != 'm')
                ++i;
            // loop's ++i will step past 'm'
        }
        else
        {
            output += input[i];
        }
    }

    return output;
}

/**
 * @brief Map the leading level character of an ESP-IDF log line to a
 *        `RollingLogger::LogLevel`.
 *
 * ESP-IDF prefixes every formatted line with one of:
 *   'E' (error), 'W' (warn), 'I' (info), 'D' (debug), 'V' (verbose).
 *
 * @param c First character of the stripped log line.
 * @return Corresponding LogLevel; defaults to INFO for unknown prefixes.
 */
static RollingLogger::LogLevel map_level_char(char c)
{
    switch (c)
    {
        case 'E': return RollingLogger::ERROR;
        case 'W': return RollingLogger::WARNING;
        case 'I': return RollingLogger::INFO;
        case 'D': return RollingLogger::DEBUG;
        case 'V': return RollingLogger::TRACE;
        default:  return RollingLogger::INFO;
    }
}

// ---------------------------------------------------------------------------
// Custom vprintf handler
// ---------------------------------------------------------------------------

/**
 * @brief vprintf-compatible handler installed via `esp_log_set_vprintf()`.
 *
 * Falls back to the default `vprintf` if no logger has been registered yet,
 * so early boot messages are not silently lost.
 *
 * @param format printf format string provided by ESP-IDF.
 * @param args   Matching variadic argument list.
 * @return Number of characters that would have been written (as per vsnprintf).
 */
static int esp_log_vprintf_handler(const char *format, va_list args)
{
    // No logger yet — pass through to default output so we don't lose messages.
    if (!esp_logger_instance)
        return vprintf(format, args);

    char buf[max_log_line];
    const int ret = vsnprintf(buf, sizeof(buf), format, args);

    if (ret > 0)
    {
        std::string clean = strip_ansi_codes(buf);

        // Remove trailing newline (ESP-IDF always appends one).
        if (!clean.empty() && clean.back() == '\n')
            clean.pop_back();

        if (!clean.empty())
        {
            // Strip the "S (0000) " prefix — keep only the message part.
            const auto sep = clean.find(") ");
            const std::string msg = (sep != std::string::npos) ? clean.substr(sep + 2) : clean;

            const RollingLogger::LogLevel lvl = map_level_char(clean[0]);
            esp_logger_instance->log(msg, lvl,"ESPs3");
        }
    }

    return ret;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void esp_log_to_rolling_init(RollingLogger *logger)
{
    if (!logger)
        return;

    esp_logger_instance = logger;
    esp_log_set_vprintf(esp_log_vprintf_handler);

    logger->info("[ESPLog] ESP-IDF log redirection initialized");
}
