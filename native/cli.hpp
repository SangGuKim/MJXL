#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mjxl {
/** @brief Parsed command line of the mjxl tool. Text is UTF-8 on every platform. */
struct CommandLine {
    std::string command;            /**< Selected command, empty for general help. */
    std::vector<std::string> paths; /**< Path arguments in command order. */
    bool help = false;              /**< True when help was requested or no command was given. */
    bool missingCommand = false;    /**< True when no arguments were given at all. */
    bool flac = false;              /**< Store audio as FLAC. */
    bool noOriginal = false;        /**< Allow output or input without original file data. */
    int effort = 7;                 /**< JPEG XL encoder effort from 1 to 10. */
};

/** @brief Reports a command line that does not follow the command definitions. */
class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Parses arguments using the command and option tables.
 * @param arguments UTF-8 arguments without the program name.
 * @return Parsed command line.
 * @throws UsageError If the command, an option or the path count is invalid.
 *
 * The command comes first. Options may appear anywhere after it, and "--" ends the options.
 */
CommandLine parse_command_line(const std::vector<std::string>& arguments);

/**
 * @brief Builds the help text.
 * @param command Command to describe, or an empty string for the general help.
 * @return Text ready to print.
 */
std::string help_text(std::string_view command);
}
