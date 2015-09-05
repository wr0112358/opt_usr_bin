/*

Read linewise from stdin and match regex. apply color to matched lineparts or to whole_line and print it.

ls | ./color_regex '\.'
ls | ./color_regex .
echo "/bin/ls abc" | cr
sudo tail -f /var/log/messages | ./color_regex --regex 'Sep  5 [[:digit:]]7:' --color cyan

//std::regex("meow", std::regex::grep)
*/

#include <cstring>
#include <iostream>
#include <regex>
#include <string>
//#include <tuple>

#include "../fragments/include_common/term_color.hh"

namespace {
struct opt_t {
    common::color_type color {common::RED};
    std::string regex;
    bool whole_line {false};
};

std::pair<bool, opt_t> parse_args(int argc, char *argv[])
{
    opt_t ret;
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--color") == 0) {
            ++i;
            if(i >= argc)
                return std::make_pair(false, ret);
            ret.color = common::string_to_color(argv[i]);
        } else if(strcmp(argv[i], "--regex") == 0) {
            ++i;
            if(i >= argc)
                return std::make_pair(false, ret);
            if(!ret.regex.empty())
                std::cerr << "invalid args: only one of regex and pattern will be used.\n";

            ret.regex.assign(argv[i]);
        //} else if(strcmp(argv[i], "--path") == 0) { ; //ret.regex.assign(<path_regex>);
        } else if(strcmp(argv[i], "--whole_line") == 0) {
            ret.whole_line = true;
        } else {
            if(!ret.regex.empty())
                std::cerr << "invalid args: only one of regex and pattern will be used.\n";
            ret.regex.assign(argv[i]);
        }
    }

    return std::make_pair(true, ret);
}

void replace(std::string& str, const std::string& from, const std::string& to)
{
    if(from.empty())
        return;
    size_t idx = 0;
    while((idx = str.find(from, idx)) != std::string::npos) {
        str.replace(idx, from.length(), to);
        idx += to.length();
    }
}

inline std::string colorize_if(std::string &&line, const std::regex &reg,
                               const opt_t &opts)
{
    std::smatch match;
    std::regex_search(line, match, reg);
    for(auto m: match)
        std::cout << "\"" << m << "\"\n";

    if(!match.empty()) {
        if(opts.whole_line)
            return common::colorize(opts.color, std::move(line));

        for(auto m: match)
            replaceAll(line, m, common::colorize(opts.color, m));
    }
    return line;
}

bool run(const opt_t &opts)
{
    std::cout << "reg: \"" << opts.regex << "\"\n";
    std::regex reg(opts.regex, std::regex_constants::egrep);
    std::string line;
    while(std::getline(std::cin, line))
        std::cout << colorize_if(std::move(line), reg, opts) << "\n";

    return true;
}
}

int main(int argc, char *argv[])
{
    const auto opts = parse_args(argc, argv);
    if(!opts.first)
        return -1;

    return run(opts.second) ? 0 : -1;
}
