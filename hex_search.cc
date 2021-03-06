/*
Searching for byte patterns in a hex-editor is not always possible.

e.g. searching for h.264 NALU startcode 0x000001

$ hex_search video.h264 000001 -A 1 -B 1

00000000: 00 00 00 01 67 42 00 28 e9 00 a0 04 0c 80 00 00
00000010: 00 01 68 ce 38 80 00 00 00 01 65 88 85 40 ff ff

0000e280: 27 90 e1 87 00 21 e9 2f da 83 b6 33 0e e0 9f ac
0000e290: be 2a 40 00 2e c6 fa ef 57 80 00 00 00 01 65 00
0000e2a0: 14 02 22 15 03 f8 18 cc e7 3f e0 04 70 5a 81 c0

0001ea70: e3 08 79 19 cc 2f a3 a5 71 75 1b f7 fb 8f 4c 2f
0001ea80: dc bb e0 00 00 00 01 41 9a 2c 03 5f 04 56 d7 ef
0001ea90: ab fc 11 ea f7 9b 82 29 e6 9d 82 cf 89 52 2f 4e

00022dc0: 77 c2 fd 37 2f fd 59 e6 56 da d2 f0 b4 b0 e9 55
00022dd0: 73 86 0d 5f 4f fd 4f c4 00 00 00 01 41 00 14 02
00022de0: 68 b0 0d f0 fc 70 07 60 11 b7 ba da 62 7d 14 f6

*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

template<typename T>
inline std::string to_hex_string(const T &value)
{
    std::ostringstream os;
    os << std::hex << value << std::dec;
    return os.str();
}

inline std::size_t roundtolast16(std::size_t val) { return val & ~15ull; }

// Src must have an even number of [0-9a-f] characters.
// In the uneven case it returns false and converted input.
std::pair<bool, std::vector<unsigned char> > hex2bin(const char *src, size_t size_hint)
{
    auto ascii2bin = [](char input) {
        if(input >= '0' && input <= '9')
            return input - '0';
        else if(input >= 'A' && input <= 'F')
            return input - 'A' + 10;
        else if(input >= 'a' && input <= 'f')
            return input - 'a' + 10;
        else
            return -1;
    };
    std::vector<unsigned char> target;
    if(size_hint) {
        target.reserve(size_hint / 2);
        size_hint = 0;
    }

    while(*src && src[1]) {
        const auto a = ascii2bin(*src) * 16;
        const auto b = ascii2bin(src[1]);
        if(a < 0 || b < 0)
            return std::make_pair(false, decltype(target)());
        target.push_back(a + b);
        src += 2;
        size_hint++;
    }

    target.resize(size_hint);
    return std::make_pair(!*src, target);
}

int readall(int fd, void *buff, size_t len)
{
    int n;
    size_t nread = 0;

    do {
        n = read(fd, &((char *)buff)[nread], len-nread);
        if(n == -1) {
            if(errno == EINTR)
                continue;
            else
                return -1;
        }
        if(n == 0)
            return nread;
        nread += n;
    } while(nread < len);

    return nread;
}

template<typename lambda_t>
bool foreach_blob(const char *filename, size_t blobsize, lambda_t lambda)
{
    int fd = open(filename, O_RDONLY, NULL);
    if(fd == -1) {
        perror("open");
        return false;
    }

    std::vector<char> buf(blobsize, 0);
    int ret;
    do {
        ret = readall(fd, &buf[0], buf.size());
        if(!lambda(buf, ret))
            break;
    } while(ret > 0);

    close(fd);

    return true;
}

std::string leftpad_0(size_t cnt, std::string &&s)
{
    if(s.length() >= cnt)
        return s;
    return std::string(cnt - s.length(), '0') + s;
}

bool printx_at(int fd, size_t off, size_t len)
{
    std::vector<unsigned char> buf(len, 0);
    const auto r = pread(fd, &buf[0], buf.size(), off);
    if(r == -1) {
        perror("pread");
        return false;
    }

    for(size_t i = 0; i < buf.size(); i++) {
        if(i % 16 == 0)
            std::cout << leftpad_0(8, to_hex_string(roundtolast16(off + i))) << ": ";
        std::cout << leftpad_0(2, to_hex_string((unsigned)buf[i]));
        if((i + 1) % 16 == 0)
            std::cout << "\n";
        else
            std::cout << " ";
    }

    return size_t(r) == len;
}

bool print(const std::vector<size_t> &found,
           const size_t before, const size_t after,
           const char *filename)
{
    int fd = open(filename, O_RDONLY, NULL);
    if(fd == -1) {
        perror("open");
        return false;
    }

    for(const auto off: found) {
        const auto o16 = roundtolast16(off);
        if(before) {
            if(o16 >= (before * 16))
                printx_at(fd, o16 - before * 16, before * 16);
        }

        printx_at(fd, o16, after == 0 ? 16 : (after * 16 + 16));
        std::cout << "\n";
    }

    close(fd);
    return 0;
}

bool ends_with(const void *haystack, size_t haystack_len,
               const void *needle, size_t needle_len)
{
    return haystack_len >= needle_len
        && memcmp((char *)haystack + haystack_len - needle_len, needle, needle_len) == 0;
}

// returns 0 if haystack contains the complete needle
// returns 1 if haystack contains the complete needle - 1
// returns 2 if haystack contains the complete needle - 2
// ...
// returns needle_len if needle is not found
size_t ends_with_prefix(const void *haystack, size_t haystack_len,
                        const void *needle, size_t needle_len)
{
    for(size_t i = 0; i < needle_len; i++) {
        //std::cout << "prefix: " << std::string((char *)needle, needle_len - i) << "\n";
        //std::cout << "hay: " << std::string((char *)haystack, haystack_len) << "\n";
        if(ends_with(haystack, haystack_len,
                     needle, needle_len - i))
            return i;
    }
    return needle_len;
}

}

int main(int argc, char *argv[])
{
    if(argc != 3 && argc != 5 && argc != 7) {
        std::cout << "Usage: " << argv[0]
                  << " <file> <hex-pattern> <optional_arg>\n";
        return -1;
    }

    size_t print_before = 0;
    size_t print_after = 0;
    if(argc >= 5) {
        const std::string a3(argv[3]);
        if(a3 == "-A")
            print_after = std::atoi(argv[4]);
        else if(a3 == "-B")
            print_before = std::atoi(argv[4]);
        else
            return -1;
    }
    if(argc >= 7) {
        const std::string a5(argv[5]);
        if(a5 == "-A")
            print_after = std::atoi(argv[6]);
        else if(a5 == "-B")
            print_before = std::atoi(argv[6]);
        else
            return -1;
    }

    // arbitrary limit
    if(print_after > 10 || print_before > 10)
        return -1;

    const auto p_len = std::strlen(argv[2]);
    if(p_len % 2)
        return -1;

    const auto bin_pattern = hex2bin(argv[2], p_len);
    if(!bin_pattern.first)
        return -1;

    size_t total_off = 0;

    std::vector<size_t> found;
    std::vector<char> old;
    if(!foreach_blob
       (argv[1], 1024,
        [&bin_pattern, &total_off, &found, &old]
        (const std::vector<char> &buf, size_t fill) {
           const char *data = buf.data();
           size_t size = fill;
           if(!old.empty()) {
               old.insert(std::end(old), std::begin(buf), std::end(buf));
               data = old.data();
               size = old.size();
           }

           size_t off = 0;
           while(true) {
               const auto x = memmem(data + off, size - off,
                                     &bin_pattern.second[0],
                                     bin_pattern.second.size());
               if(x == NULL)
                   break;

               assert(x >= (data + off));
               off = (uintptr_t)x - (uintptr_t)data;
               assert(off <= size);

               found.push_back(total_off + off);
               off += bin_pattern.second.size();
               if(size == off)
                   break;
           }
           old.clear();

           total_off += size;
           // prefix of searched pattern at end of blob? keep it.
           if(ends_with_prefix(data, size,
                               bin_pattern.second.data(),
                               bin_pattern.second.size())
              != bin_pattern.second.size()) {
               assert(old.empty());
               old.insert(std::begin(old), buf.data() + size
                          - bin_pattern.second.size(),
                          buf.data() + size);
               total_off -= bin_pattern.second.size();
           }
           return true;
       }))
        return -1;

    return print(found, print_before, print_after, argv[1]) ? 0 : -1;
}
