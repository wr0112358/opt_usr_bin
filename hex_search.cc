/*
Searching for byte patterns in a hex-editor is not always possible.

e.g. searching for h.264 NALU startcode 0x000001
hex_search video.h264 000001
*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include <libaan/byte.hh>
#include <libaan/debug.hh>
#include <libaan/fd.hh>
#include <libaan/string.hh>

template<typename lambda_t>
bool foreach_blob(const char *filename, size_t blobsize, lambda_t lambda)
{
    int fd = open(filename, O_RDONLY, NULL);
    if(fd == -1) {
        perror("open");
        return false;
    }

    std::vector<char> buf(4096, 0);
    int ret;
    do {
        ret = libaan::readall(fd, &buf[0], buf.size());
        std::cout << "readall(" << filename << ") -> " << ret << "\n";
        if(!lambda(buf, ret))
            break;
    } while(ret > 0);

    return true;
}

int main(int argc, char *argv[])
{
    if(argc != 3 && argc != 4) {
        std::cout << "Usage: " << argv[0] << " <file> <hex-pattern>\n";
        return -1;
    }

//    size_t print_before = 0;
//    size_t print_after = 0;
//    if(std::string(argv[3]) == "-A") ...

    const auto p_len = std::strlen(argv[2]);
    if(p_len % 2)
        return -1;

    const auto bin_pattern = libaan::hex2bin(argv[2], p_len);
    if(!bin_pattern.first)
        return -1;

    for(auto x: bin_pattern.second)
        std::cout << (int)x << " ";
    std::cout << "\n";

    size_t total_off = 0;

    std::vector<size_t> found;
    if(!foreach_blob(argv[1], 4096,
                        [&bin_pattern, &total_off, &found]
                        (const std::vector<char> &buf, size_t fill) {
                            // return true to continue, false to break
                            const auto x = memmem(&buf
                                                  [0], fill, &bin_pattern.second[0],
                                                  bin_pattern.second.size());
                            if(x == NULL) {
                                total_off += fill;
                                return true;
                            }
                            const size_t off = (uintptr_t)x - (uintptr_t)&buf[0];
                            std::cout << "memem: " << off << "\n";
                            assert(off <= fill);
                            found.push_back(total_off + off);
                            total_off += fill;
                            return true;
                     }))
        return -1;

    for(const auto off: found)
        std::cout << libaan::to_hex_string(libaan::roundtolast16(off)) << "\n";
    return 0;
}
