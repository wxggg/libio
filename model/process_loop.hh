#include <signal.h>
#include <sys/time.h>

#include <functional>
#include <iostream>
#include <memory>

#include "reactor.hh"

#include <core/buffer.hh>
#include <core/epoll.hh>
#include <core/select.hh>
#include <core/signal.hh>

namespace wxg {

using Handler = std::function<void(int)>;

struct process_info {
    int pid = -1;

    int channel[2];

    int exited = 0;
    int exiting = 0;
};

class process_loop {
   private:
    std::unique_ptr<reactor<select>> re = nullptr;

    int size = 2;

    Handler readcb = nullptr;
    Handler writecb = nullptr;
    Handler errorcb = nullptr;

    std::vector<std::unique_ptr<process_info>> processes;

    int listenfd = -1;

   public:
    static sig_atomic_t reap;
    static sig_atomic_t sigio;
    static sig_atomic_t sigalrm;
    static sig_atomic_t terminate;
    static sig_atomic_t quit;

   public:
    process_loop() { re = std::make_unique<reactor<wxg::select>>(); }
    ~process_loop() {}

    void resize(int n) { size = n; }

    void set_read_handler(Handler &&f) { readcb = f; }
    void set_write_handler(Handler &&f) { writecb = f; }
    void set_error_handler(Handler &&f) { errorcb = f; }

    void start(const std::string &address, unsigned short port) {
        listenfd = tcp::get_nonblock_socket();
        tcp::bind(listenfd, address, port);
        tcp::listen(listenfd);

        init();

        struct itimerval itv;
        int live = 1;

        wxg::signal::set_handler(SIGINT, []() { terminate = 1; });
        wxg::signal::set_handler(SIGCHLD, [this]() {
            reap = 1;

            int status;
            int pid;
            for (;;) {
                pid = waitpid(-1, &status, WNOHANG);

                if (pid == 0 || pid == -1) return;

                for (int i = 0; i < size; i++) {
                    if (processes[i]->pid == pid) {
                        processes[i]->exited = 1;
                        cout << "child i=" << i << " exited" << endl;
                        break;
                    }
                }
            }
        });

        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGCHLD);
        sigaddset(&set, SIGALRM);
        sigaddset(&set, SIGIO);
        sigaddset(&set, SIGINT);

        if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
            std::cerr << "error sigprocmask\n";

        sigemptyset(&set);

        int delay = 0;
        for (;;) {
            if (delay) {
                itv.it_interval.tv_sec = 0;
                itv.it_interval.tv_usec = 0;
                itv.it_value.tv_sec = delay / 1000;
                itv.it_value.tv_usec = (delay % 1000) * 1000;

                if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
                    std::cerr << "error setitimer\n";
            }

            sigsuspend(&set);

            if (reap) {
                reap = 0;
                live = reap_children();
            }

            if (!live && (terminate || quit)) {
                exit(0);
            }

            if (terminate) {
                if (delay == 0) delay = 50;

                signal_worker_processes(SIGKILL);
            }
        }
    }

   private:
    void init() {
        for (int i = 0; i < size; i++) {
            processes.push_back(std::make_unique<process_info>());

            if (socketpair(AF_UNIX, SOCK_STREAM, 0, processes[i]->channel) ==
                -1)
                cerr << "socketpair error" << endl;

            set_nonblock(processes[i]->channel[0]);
            set_nonblock(processes[i]->channel[1]);

            int pid = fork();
            switch (pid) {
                case -1:  // error
                    cerr << "error fork" << endl;
                    break;
                case 0:  // child
                    worker_process_cycle(i);
                    break;

                default:  // parent
                    break;
            }

            processes[i]->pid = pid;
        }
    }

    void worker_process_cycle(int i) {
        reactor<wxg::epoll> re;
        re.set_read_handler(processes[i]->channel[0], [this, i]() {
            wxg::buffer buf;
            buf.read(processes[i]->channel[1]);
            cout << "msg from master:" << buf.get() << endl;
        });

        re.set_read_handler(listenfd, [this, &re]() {
            std::string addr;
            unsigned short port;
            int clientfd = tcp::accept(listenfd, addr, port);
            if (clientfd <= 0) return;

            if (readcb) re.set_read_handler(clientfd, readcb, clientfd);
            if (writecb) re.set_write_handler(clientfd, writecb, clientfd);
            if (errorcb) re.set_error_handler(clientfd, errorcb, clientfd);
        });

        re.loop();
    }

    void signal_worker_processes(int sig) {
        for (int i = 0; i < size; i++) {
            if (kill(processes[i]->pid, sig) == -1)
                cerr << "error kill" << endl;
        }
    }

    int reap_children() {
        int live = 0;
        for (int i = 0; i < size; i++) {
            if (processes[i]->pid == -1) continue;

            if (processes[i]->exited) {
                processes[i]->pid = -1;
            } else
                live = 1;
        }
        return live;
    }
};

sig_atomic_t process_loop::reap = 0;
sig_atomic_t process_loop::terminate = 0;
sig_atomic_t process_loop::quit = 0;
sig_atomic_t process_loop::sigio = 0;
sig_atomic_t process_loop::sigalrm = 0;

}  // namespace wxg
