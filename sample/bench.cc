#include <sys/resource.h>

#include <unistd.h>

#include <chrono>
#include <iostream>

#include <core/epoll.hh>
#include <core/poll.hh>
#include <core/select.hh>
#include <model/reactor.hh>

using namespace std;
using namespace chrono;

using Clock = chrono::high_resolution_clock;

static int num_pipes, num_active, num_writes;

static int *pipes;

static wxg::reactor<wxg::epoll> re;

static int countread = 0, fired = 0, writes = 0;

void readcb(int i) {
    u_char ch;
    countread += read(pipes[2 * i], &ch, sizeof(ch));
    if (writes) {
        int nexti = i + 1;
        if (nexti >= num_pipes) nexti -= num_pipes;
        write(pipes[2 * nexti + 1], "a", 1);
        writes--, fired++;
    }
}

int run_once() {
    cout << __func__ << endl;
    countread = fired = 0, writes = num_writes;

    int space = num_pipes / num_active;
    space *= 2;

    re.loop(true, true);

    for (int i = 0; i < num_active; i++, fired++) {
        write(pipes[i * space + 1], "a", 1);
    }

    auto t1 = Clock::now();

    int xcount = 0;
    do {
        re.loop(true, true);
        xcount++;
    } while (countread != fired);

    auto t2 = Clock::now();
    auto time_span = duration_cast<duration<double>>(t2 - t1);

    if (xcount != countread)
        cerr << "Xcount: " << xcount << ", Rcount: " << countread << endl;

    return 1000 * 1000 * time_span.count();
}

int main(int argc, char *const argv[]) {
    num_pipes = 100;
    num_active = 1;
    num_writes = num_pipes / 2;

    int c;
    extern char *optarg;
    while ((c = getopt(argc, argv, "n:a:w:")) != -1) {
        switch (c) {
            case 'n':
                num_pipes = atoi(optarg);
                break;
            case 'a':
                num_active = atoi(optarg);
                break;
            case 'w':
                num_writes = atoi(optarg);
                break;

            default:
                cerr << "illegal argument" << endl;
                break;
        }
    }

    struct rlimit rl;
    rl.rlim_cur = rl.rlim_max = num_pipes * 2 + 50;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
        cerr << "setrlimit\n";
        exit(1);
    }

    pipes = new int[num_pipes * 2];

    for (int *cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
        if (pipe(cp) == -1) {
            cerr << "error pipe errno=" << errno << endl;
            exit(-1);
        }
        re.set_read_handler(pipes[2 * i], [=]() { readcb(i); });
    }

    for (int i = 0; i < 25; i++) cout << run_once() << endl;

    delete[] pipes;
    return 0;
}
