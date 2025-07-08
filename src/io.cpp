#include "io.h"

fd_wrapper setup_socket(net::ipv4 v4){
    fd_wrapper fd_w = socket(PF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = v4.to_sockaddr_in();
    if (bind(fd_w, (sockaddr*)&addr, sizeof(addr)) < 0) {
        return fd_wrapper(-1);
    }
    if (listen(fd_w, SOMAXCONN) < 0) {
        return fd_wrapper(-1);
    }
    return fd_w;
}

int64_t get_file_size(const fd_wrapper& fd_w) {
    struct stat st;

    if(fstat(fd_w, &st) < 0) {
        return -1;
    }
    if (S_ISBLK(st.st_mode)) {
        int64_t bytes;
        if (ioctl(fd_w, BLKGETSIZE64, &bytes) != 0) {
            return -1;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode))
        return st.st_size;

    return -1;
}