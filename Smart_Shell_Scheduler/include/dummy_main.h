#include <signal.h>
#include <unistd.h>

int dummy_main();

int main() {
    raise(SIGSTOP);

    int ret = dummy_main();
    return ret;
}

#define main dummy_main