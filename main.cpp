#include "coro_actor.h"

int main(int argc, char* argv[]) {
    coro_actor server(argc, argv);
    server.run();
    return 0;
}