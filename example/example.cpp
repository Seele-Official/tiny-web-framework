#include "web/env.h"
#include "web/loop.h"

int main(){
    web::env::listen_addr() = web::ip::v4::from_string("127.0.0.1:8080");

    web::loop::run();
    return 0;
}