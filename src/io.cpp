#include "io.h"
#include <cstddef>

fd_wrapper setup_socket(seele::net::ipv4 v4, size_t max_connections, auto (*setup)(int) -> void) {
    fd_wrapper fd_w = socket(PF_INET, SOCK_STREAM, 0);
    setup(fd_w.get());
    sockaddr_in addr = v4.to_sockaddr_in();
    if (bind(fd_w.get(), (sockaddr*)&addr, sizeof(addr)) < 0) {
        return fd_wrapper(-1);
    }
    if (listen(fd_w.get(), max_connections) < 0) {
        return fd_wrapper(-1);
    }
    return fd_w;
}

int64_t get_file_size(const fd_wrapper& fd_w) {
    struct stat st;

    if(fstat(fd_w.get(), &st) < 0) {
        return -1;
    }
    if (S_ISBLK(st.st_mode)) {
        int64_t bytes;
        if (ioctl(fd_w.get(), BLKGETSIZE64, &bytes) != 0) {
            return -1;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode))
        return st.st_size;

    return -1;
}