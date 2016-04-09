/*
"stalling pidof"

like pidof -s, but waits for process to show up

Usage example:
sudo perf top -p $(/opt/usr/bin/spidof h264dec)
htop -p $(/opt/usr/bin/spidof h264dec)

Problems:
 - must iterate over procfs again and again.
   possible workarounds:
     + "proc connector" polling api is perfect for the job, but needs root(CAP_NET_ADMIN).
     + inotify on /lib64/ld-2.21.so could reduce interval of busy wait and amount of folders to read?

*/

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <experimental/filesystem>

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
    auto const f = [&name, &have](const std::experimental::filesystem::path &path) {
        const auto pid = std::atoi(path.filename().c_str());
        if(pid == 0)
            return true;
        std::string buff(512, '\0');
        {
            std::ifstream fp((path.string() + "/cmdline").c_str());
            buff.resize(fp.read(&*buff.begin(), buff.length()).gcount());
        }

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
        for(auto &it: std::experimental::filesystem::directory_iterator("/proc")) {
            std::error_code ec;
            if(!std::experimental::filesystem::is_directory(it, ec))
                continue;
            f(it.path());
            if(have)
                break;
        }
        if(have)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cerr << "." << std::flush;
    }
    std::cerr << "\n";
}
