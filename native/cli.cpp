#include "cli.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdlib>

namespace mjxl {
namespace {
enum class Option { CopyAudio, NoOriginal, Effort, Help };

struct OptionSpec {
    Option id;
    std::string_view shortName;
    std::string_view longName;
    std::string_view label;
    std::string_view description;
};

struct CommandSpec {
    std::string_view name;
    std::string_view paths;
    size_t pathCount;
    std::string_view summary;
    std::vector<Option> options;
};

// Keep command and option definitions in these tables so parsing and help stay consistent.
const std::vector<OptionSpec> kOptions = {
    {Option::CopyAudio, "", "--copy-audio", "--copy-audio", "Copy the source audio unchanged instead of storing it as lossless FLAC"},
    {Option::NoOriginal, "", "--no-original", "--no-original", "Do not require data to restore the original file"},
    {Option::Effort, "-e", "--effort", "-e, --effort <1-10>", "JPEG XL encoder effort (default: 7)"},
    {Option::Help, "-h", "--help", "-h, --help", "Show this help"},
};

const std::vector<CommandSpec> kCommands = {
    {"encode", "<input> <output.avi>", 2, "Compress an MJPEG video into an MJXL AVI and verify it", {Option::CopyAudio, Option::NoOriginal, Option::Effort, Option::Help}},
    {"verify", "<original> <encoded.avi>", 2, "Compare an MJXL AVI with its original file", {Option::NoOriginal, Option::Help}},
    {"restore-original", "<encoded.avi> <output>", 2, "Rebuild the original file byte for byte", {Option::Help}},
    {"restore", "<encoded.avi> <output.avi>", 2, "Write the original JPEG frames and PCM as an MJPEG AVI", {Option::Help}},
    {"bench", "<encoded.avi>", 1, "Measure JXL decoding speed", {Option::Help}},
};

const CommandSpec* find_command(std::string_view name)
{
    const auto found = std::find_if(kCommands.begin(), kCommands.end(), [&](const CommandSpec& spec) { return spec.name == name; });
    return found == kCommands.end() ? nullptr : &*found;
}

const OptionSpec& option_spec(Option id)
{
    return *std::find_if(kOptions.begin(), kOptions.end(), [&](const OptionSpec& spec) { return spec.id == id; });
}

bool is_help(std::string_view text)
{
    return text == "-h" || text == "--help";
}

int parse_effort(const std::string& value)
{
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    if (value.empty() || *end != '\0' || number < 1 || number > 10) {
        throw UsageError("--effort requires a value from 1 to 10");
    }
    return static_cast<int>(number);
}

std::string column(std::string_view left, size_t width)
{
    std::string text = "  ";
    text += left;
    text.append(text.size() < width ? width - text.size() : 1, ' ');
    return text;
}
}

CommandLine parse_command_line(const std::vector<std::string>& arguments)
{
    CommandLine result;
    if (arguments.empty()) {
        result.help = true;
        result.missingCommand = true;
        return result;
    }
    const std::string& first = arguments[0];
    if (is_help(first)) {
        result.help = true;
        return result;
    }
    if (first == "help") {
        result.help = true;
        if (arguments.size() > 1) {
            if (!find_command(arguments[1])) {
                throw UsageError("Unknown command: " + arguments[1]);
            }
            result.command = arguments[1];
        }
        return result;
    }
    if (first.starts_with('-')) {
        throw UsageError("Options must come after the command: " + first);
    }
    const CommandSpec* command = find_command(first);
    if (!command) {
        throw UsageError("Unknown command: " + first);
    }
    result.command = first;

    bool optionsEnded = false;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (optionsEnded || !argument.starts_with('-') || argument == "-") {
            result.paths.push_back(argument);
            continue;
        }
        if (argument == "--") {
            optionsEnded = true;
            continue;
        }
        // Split --name=value into its name and value before looking it up in the table.
        const size_t equals = argument.starts_with("--") ? argument.find('=') : std::string::npos;
        const std::string name = argument.substr(0, equals);
        const auto found = std::find_if(command->options.begin(), command->options.end(), [&](Option id) {
            const auto& spec = option_spec(id);
            return name == spec.longName || (!spec.shortName.empty() && name == spec.shortName);
        });
        if (found == command->options.end()) {
            throw UsageError("Unknown option for " + std::string(command->name) + ": " + name);
        }
        if (*found != Option::Effort && equals != std::string::npos) {
            throw UsageError("Option does not take a value: " + name);
        }
        if (*found == Option::Help) {
            result.help = true;
        } else if (*found == Option::CopyAudio) {
            result.copyAudio = true;
        } else if (*found == Option::NoOriginal) {
            result.noOriginal = true;
        } else if (equals != std::string::npos) {
            result.effort = parse_effort(argument.substr(equals + 1));
        } else {
            if (++i >= arguments.size()) {
                throw UsageError("--effort requires a value from 1 to 10");
            }
            result.effort = parse_effort(arguments[i]);
        }
    }
    if (!result.help && result.paths.size() != command->pathCount) {
        throw UsageError(std::string(command->name) + " requires " + std::string(command->paths));
    }
    return result;
}

std::string help_text(std::string_view command)
{
    std::string text;
    if (const CommandSpec* spec = find_command(command)) {
        text += "Usage: mjxl " + std::string(spec->name) + ' ' + std::string(spec->paths) + " [options]\n\n";
        text += std::string(spec->summary) + "\n\nOptions:\n";
        for (const Option id : spec->options) {
            const auto& option = option_spec(id);
            text += column(option.label, 26) + std::string(option.description) + '\n';
        }
        return text;
    }
    text += "MJXL: reversible JPEG XL recompression of MJPEG video\n\n";
    text += "Usage: mjxl <command> [options] <paths>\n\nCommands:\n";
    for (const auto& spec : kCommands) {
        text += column(spec.name, 20) + std::string(spec.summary) + '\n';
    }
    text += "\nRun \"mjxl <command> --help\" for the paths and options of a command.\n";
    return text;
}
}
