/*
Copyright (C) 2014 Reiter Wolfgang wr0112358@gmail.com

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*

Example output:
tokenizer timings(349901 words):
	file-io: 1.22925 ms
	split: 26.8858 ms
	split2: 11.5316 ms

CATCHED:
	idx = 349900
	split2_in[idx].second = 18446744073706345535
	sizeof(split2_in) = 349901
	corresponding string split_in[idx] = 

*/
#include <algorithm>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "../../wr011_lib/chrono_util.hh"

inline size_t get_file_length(std::ifstream & fp)
{
    fp.seekg(0, fp.end);
    const auto length = fp.tellg();
    if(length == std::fstream::pos_type(-1))
        return 0;
    fp.seekg(0, fp.beg);
    return length;
}

inline bool read_file(const std::string &file_name,
                      std::string &buff)
{
    std::ifstream fp(file_name);
    const size_t length = get_file_length(fp);
    buff.resize(length);
    char *begin = &*buff.begin();
    fp.read(begin, length);

    return true;
}

// a better aproach using a writeable input string:
// - replace first char of every delim with '\0'
// - save pointer to first char of everytoken in vector<char *>
// or with non writeable input:
// - save pointer to first char of every token together with string length in
//   vector<pair<char *, int> >
// need to test the timings
inline std::vector<std::string> split(const std::string &input,
                                      const std::string &delim)
{
    std::vector<std::string> tokens;
    std::string::size_type start = 0;
    std::string::size_type end;

    for(;;) {
        end = input.find(delim, start);
        tokens.push_back(input.substr(start, end - start));
        // We just copied the last token
        if(end == std::string::npos)
            break;
        // Exclude the delimiter in the next search
        start = end + delim.size();
    }

    return tokens;
}

inline std::vector<std::pair<const char *, size_t> >
split2(const std::string &input, const std::string &delim)
{
    std::vector<std::pair<const char *, size_t> > tokens;
    std::string::size_type start = 0;
    std::string::size_type end;

    for(;;) {
        end = input.find(delim, start);
        tokens.push_back(std::make_pair(&input.data()[start], end - start));
        // We just copied the last token
        if(end == std::string::npos)
            break;
        // Exclude the delimiter in the next search
        start = end + delim.size();
    }

    return tokens;
}

bool
test_for_equality(const std::vector<std::string> &split_in,
                  const std::vector<std::pair<const char *, size_t>> &split2_in)
{
    if(split_in.size() != split2_in.size()) {
        std::cerr << "test_for_equality failed -> 1\n";
        return false;
    }

    std::size_t error_count = 0;
    const auto size = split_in.size();
    for(std::size_t idx = 0; idx < size; idx++) {
        const std::string &in1 = split_in[idx];
        std::string in2;
        try {
            in2 = std::string(
            split2_in[idx].first, split2_in[idx].first + split2_in[idx].second);
        } catch(const std::length_error &e) {
            std::cout << "CATCHED:\n\tidx = " << idx
                      << "\n\tsplit2_in[idx].second = " << split2_in[idx].second
                      << "\n\tsizeof(split2_in) = " << size
                      << "\n\tcorresponding string split_in[idx] = "
                      << split_in[idx]
                      << "\n";
        };
        if(in1 != in2) {
            error_count++;
            std::cout << "(\"" << in1 << "\", \"" << in2 << "\")\n";
        }
    }

    return error_count != 0;
}

bool split_test_file(const std::string &path)
{
    std::string file_buffer;
    wr011_lib::chrono_util::time_me_ns timer;
    read_file(path, file_buffer);
    const double io_time = timer.duration();
    const std::string DELIM = {' '};
    timer.restart();
    const auto tokens = split(file_buffer, DELIM);
    const double split_time = timer.duration();
    if(tokens.empty())
        return false;

    timer.restart();
    const auto tokens2 = split2(file_buffer, DELIM);
    const double split2_time = timer.duration();
    if(tokens2.empty())
        return false;

    std::cout << "tokenizer timings(" << tokens2.size() << " words):\n"
              << "\tfile-io: " << io_time / (1000.0 * 1000.0) << " ms\n"
              << "\tsplit: " << split_time / (1000.0 * 1000.0) << " ms\n"
              << "\tsplit2: " << split2_time / (1000.0 * 1000.0) << " ms\n\n";

    test_for_equality(tokens, tokens2);    

    return true;
}

int main(int argc, char *argv[])
{
    split_test_file("words.test");
    exit(EXIT_SUCCESS);
}
