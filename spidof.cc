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
    auto const f = [&name](const std::string &path, const struct dirent *p) {
        const auto pid = std::atoi(p->d_name);
        if(pid == 0)
            return;
        std::string buff;
        buff.resize(libaan::read_file((path + p->d_name + "/cmdline").c_str(), buff, 512));
        //std::cout << (path + p->d_name + "/cmdline") << " -> " << buff.length() << "\n";
        //std::cout << path + p->d_name << ": \"" << buff.c_str() << "\", base=\""
        //<< get_basename(buff.c_str()) << "\"\n";

        const char * base = get_basename(buff.c_str());

        // oneshot
        if(std::strncmp(name.c_str(), base, name.length()) == 0) {
            std::cout << pid << "\n" << std::flush;
            exit(0);
        }
    };

    for(size_t i = 0; i < 50; i++) {
        libaan::readdir("/proc/", f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cerr << "." << std::flush;
    }
    std::cerr << "\n";
}
