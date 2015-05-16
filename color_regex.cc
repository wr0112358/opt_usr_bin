/*
echo "/bin/ls abc" | cr
echo "/bin/ls abc" | cr --path
echo "/bin/ls abc" | cr --path --color cyan
sudo cat "/var/log/messages" | cr --regex 'Mar [[:digit:]]7 ' --color cyan

//std::regex("meow", std::regex::grep)
*/

#include <iostream>
#include <string>
#include <tuple>

#include "../fragments/include_common/term_color.hh"

namespace {
struct opt_t {
    common::color_type color {common::RED};
    std::string regex;
    bool whole_line {false};
};

std::tuple<bool, opt_t> parse_args(int argc, char *argv[])
{
    opt_t ret;
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--color") == 0) {
            ++i;
            if(i >= argc)
                return std::make_tuple(false, ret);
            ret.color = string_to_color(argv[i]);
        } else if(strcmp(argv[i], "--regex") == 0) {
            ++i;
            if(i >= argc)
                return std::make_tuple(false, ret);
            if(!ret.regex.empty())
                std::cerr << "invalid args: only one of regex and pattern will be used.\n";

            ret.regex.assign(argv[i]);
        } else if(strcmp(argv[i], "--path") == 0) {
            ret.regex.assign(<path_regex>);
        } else if(strcmp(argv[i], "--whole_line") == 0) {
            ret.whole_line = true;
        } else {
            if(!ret.regex.empty())
                std::cerr << "invalid args: only one of regex and pattern will be used.\n";
            ret.regex.assign(argv[i]);
        }
    }
}

inline std::string colorize_if(std::string &&line, const opt_t &opts)
{
    std::smatch match;
    std::regex_search(line, m, reg);
    // opts.whole_line
    if(!match.empty())
        return colorize(std::move(line));
    return line;
}

// TODO: read linewise from stdin nad match regex. apply color to matched lineparts or to whole_line and print it.
bool run(const opt_t &opts)
{
    std::regex reg(opts.regex, std::regex_constants::egrep);
    std::string line;
    while(std::getline(std::cin, line)) {
        std::cout << colorize_if(std::move(line, opts) << "\n";
    }

    return true;
}
}

int main(int argc, char *argv[])
{
    const auto opts = parse_args(argc, argv);
    if(!std::get<0>(opts))
        return -1;

    return run(opts) ? 0 : -1;
}
