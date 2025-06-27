#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <string>

namespace beamsim {
  // https://jameshfisher.com/2017/01/27/mmap-file-read/
  struct Mmap {
    Mmap(std::string path) : fd{open(path.c_str(), O_RDONLY)} {
      if (not good()) {
        return;
      }
      // https://stackoverflow.com/a/6537560
      auto size = lseek(fd, 0, SEEK_END);
      if (size == -1) {
        ::close(fd);
        return;
      }
      auto ptr = mmap(nullptr, size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
      if (ptr == MAP_FAILED) {
        ::close(fd);
        return;
      }
      data = {static_cast<char *>(ptr), static_cast<size_t>(size)};
    }
    ~Mmap() {
      if (not good()) {
        return;
      }
      munmap(const_cast<char *>(data.data()), data.size());
      ::close(fd);
    }
    void sequential() {
      if (not good()) {
        return;
      }
      madvise(const_cast<char *>(data.data()),
              data.size(),
              MADV_SEQUENTIAL | MADV_WILLNEED);
    }
    bool good() const {
      return fd != -1;
    }

    int fd;
    std::string_view data;
  };
}  // namespace beamsim
