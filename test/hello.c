#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include "hello.skel.h"

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

int main(void)
{
    struct hello_bpf *skel = hello_bpf__open_and_load();
    if (!skel) { fprintf(stderr, "echec open/load\n"); return 1; }
    if (hello_bpf__attach(skel)) {
        fprintf(stderr, "echec attach\n");
        hello_bpf__destroy(skel);
        return 1;
    }
    signal(SIGINT, on_sig);
    printf("Attache. Ctrl-C pour arreter.\n");
    while (!stop) sleep(1);
    hello_bpf__destroy(skel);
    return 0;
}
