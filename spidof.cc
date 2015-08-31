/*
"stalling pidof"

like pidof -s, but waits for process to show up

Usage example:
sudo perf top -p $(/opt/usr/bin/spidof h264dec)
htop -p $(/opt/usr/bin/spidof h264dec)

Problems:
 - must iterate over procfs again and again.
   possible workarounds:
     + "proc connector" polling api is perfect for the job, but needs root capabilities.
     + inotify on /lib64/ld-2.21.so could reduce interval of busy wait and amount of folders to read?

*/

#include <libaan/file.hh>

#include <cstring>
#include <iostream>
#include <string>
#include <thread>

static const char *get_basename(const char *filename)
{
    const char *result = filename;
    while(*filename != '\0')
        if(*(filename++) == '/')
            result = filename;
    return result;
}

int main(int argc, char *argv[])
{
    if(argc != 2)
        return -1;

    const std::string name(argv[1]);
    bool have = false;
    auto const f = [&name, &have](const std::string &path, const struct dirent *p) {
        const auto pid = std::atoi(p->d_name);
        if(pid == 0)
            return true;
        std::string buff;
        buff.resize(libaan::read_file((path + p->d_name + "/cmdline").c_str(), buff, 512));

        const char * base = get_basename(buff.c_str());

        // oneshot
        if(std::strncmp(name.c_str(), base, name.length()) == 0) {
            std::cout << pid << "\n" << std::flush;
            have = true;
            return false;
        }
        return true;
    };

    for(size_t i = 0; i < 50; i++) {
        libaan::readdir2("/proc/", f);
        if(have)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cerr << "." << std::flush;
    }
    std::cerr << "\n";
}
