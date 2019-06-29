#pragma once

#include <sys/wait.h>

#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>

using std::cerr;
using std::cout;
using std::endl;

namespace wxg {

using Callback = std::function<void()>;
class signal {
   private:
   public:
    static struct sigaction sa;
    static sigset_t sigmask;

    static std::map<int, std::pair<Callback, bool>> callback;

   public:
    signal() { __init(); }
    ~signal() {}

    static void set_persistent(int sig) {
        if (!callback.count(sig)) return;
        callback[sig].second = true;
    }

    static void set_unpersistent(int sig) {
        if (!callback.count(sig)) return;
        callback[sig].second = false;
    }

    template <typename F, typename... Rest>
    static void set_handler(int sig, F&& f, Rest&&... rest) {
        set_handler(sig, false, f, rest...);
    }

    template <typename F, typename... Rest>
    static void set_handler(int sig, bool persistent, F&& f, Rest&&... rest) {
        callback[sig] =
            std::make_pair([f, rest...]() { f(rest...); }, persistent);

        sigaddset(&sigmask, sig);
        sa.sa_mask = sigmask;

        if (sigaction(sig, &sa, nullptr) == -1)
            cerr << "sigaction error" << endl;
    }

    static void remove(int sig) {
        if (!callback.count(sig)) return;

        sigdelset(&sigmask, sig);
        callback.erase(sig);

        if (sigaction(sig, (struct sigaction*)SIG_DFL, nullptr))
            cerr << "sigaction error" << endl;
    }

    static void block() {
        if (sigprocmask(SIG_BLOCK, &sigmask, nullptr) == -1)
            cerr << "sigprocmask error" << endl;
    }

    static void unblock() {
        if (sigprocmask(SIG_UNBLOCK, &sigmask, nullptr) == -1)
            cerr << "sigprocmask error" << endl;
    }

   private:
    static void __handler(int sig) {
        if (!callback.count(sig)) return;

        callback[sig].first();
        if (!callback[sig].second) remove(sig);
    }

   public:
    static void __init() {
        sigemptyset(&sigmask);

        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = __handler;
        sa.sa_flags |= SA_RESTART;
    }
};
std::map<int, std::pair<Callback, bool>> signal::callback;
struct sigaction signal::sa;
sigset_t signal::sigmask;

signal s;

}  // namespace wxg
