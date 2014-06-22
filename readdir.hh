#ifndef _READDIR_DIR_HH_
#define _READDIR_DIR_HH_

#include <cstdint>
#include <dirent.h>
#include <functional>
#include <fstream>
#include <string>

namespace readdir_cxx {

class dir {
public:
  /* EXAMPLE:
  void procfs::parser::parse(procfs & p) const
  {
      auto const f = [](const std::string & base_path, const struct dirent * p)
  {
          std::cout << "struct dirent = { " << std::endl
          << "\t" << std::to_string(p->d_ino) << std::endl
          << "\t" << std::to_string(p->d_off) << std::endl
          << "\t" << std::to_string(p->d_reclen) << std::endl
          << "\t\"" << p->d_name << "\"" << std::endl
          << "}" << std::endl;
      };

      readdir_cxx::dir::readdir("/proc", f);
  }
  */
  static void
  readdir(const std::string &path,
          const std::function<void(const std::string &, dirent *)> &f);

private:
  static size_t dirent_buf_size(DIR *dirp);
};

}

#include <cstddef>
#include <iostream>
#include <memory>

// copied from:
// http://lists.grok.org.uk/pipermail/full-disclosure/2005-November/038295.html
inline size_t readdir_cxx::dir::dirent_buf_size(DIR *dirp)
{
    long name_max;
#if defined(HAVE_FPATHCONF) && defined(HAVE_DIRFD) && defined(_PC_NAME_MAX)
    name_max = fpathconf(dirfd(dirp), _PC_NAME_MAX);
    if (name_max == -1)
#if defined(NAME_MAX)
        name_max = NAME_MAX;
#else
        return (size_t)(-1);
#endif
#else
#if defined(NAME_MAX)
    name_max = NAME_MAX;
#else
#error "buffer size for readdir_r cannot be determined"
#endif
#endif
    return (size_t)offsetof(struct dirent, d_name) + name_max + 1;
}

// TODO: possible to add template argument to lambda function?
// TODO: use dt::error exception!
inline void readdir_cxx::dir::readdir(
    const std::string &path,
    const std::function<void(const std::string &, dirent *)> &f)
{
    DIR *dirp = opendir(path.c_str());
    if (!dirp)
        return; // TODO: throw dt::exception(errno);

    size_t size = dirent_buf_size(dirp);
    if (size == static_cast<size_t>(-1)) {
        // TODO
    }

    std::unique_ptr<uint8_t[]> tmp_buf(new uint8_t[size]);
    struct dirent *buf = reinterpret_cast<struct dirent *>(tmp_buf.get());
    if (!buf) {
        // TODO
    }

    int error;
    struct dirent *ent;
    while ((error = readdir_r(dirp, buf, &ent) == 0) && ent) {
        // std::cout << std::string(ent->d_name) << std::endl;
        f(path, ent);
    }
    if (error) {
        errno = error;
        perror("readdir_r");
        // TODO: eg
        // readdir_r: Operation not permitted
    }
}

#endif

