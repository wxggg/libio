#include <getopt.h>
#include <stdio.h>

#include <iostream>

#include "../core/signal.hh"
#include "../core/socket.hh"
#include "../http/request.hh"

using namespace std;

/* values */
volatile int timerexpired = 0;
int speed = 0;
int failed = 0;
int bytes = 0;

/* globals */
int http10 = 1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method = METHOD_GET;
int clients = 1;
int force = 0;
int force_reload = 0;
int benchtime = 30;

bool keep_alive = false;

/* internal */
int mypipe[2];
string host = "127.0.0.1";
unsigned short port = 80;
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

wxg::request req;

static const struct option long_options[] = {
    {"force", no_argument, &force, 1},
    {"reload", no_argument, &force_reload, 1},
    {"time", required_argument, NULL, 't'},
    {"help", no_argument, NULL, '?'},
    {"http09", no_argument, NULL, '9'},
    {"http10", no_argument, NULL, '1'},
    {"http11", no_argument, NULL, '2'},
    {"get", no_argument, &method, METHOD_GET},
    {"head", no_argument, &method, METHOD_HEAD},
    {"options", no_argument, &method, METHOD_OPTIONS},
    {"trace", no_argument, &method, METHOD_TRACE},
    {"version", no_argument, NULL, 'V'},
    {"proxy", required_argument, NULL, 'p'},
    {"clients", required_argument, NULL, 'c'},
    {NULL, 0, NULL, 0}};

/* prototypes */
static void benchcore(const char *request);
static int bench(void);

static void usage(void) {
    fprintf(
        stderr,
        "webbench [option]... URL\n"
        "  -f|--force               Don't wait for reply from server.\n"
        "  -r|--reload              Send reload request - Pragma: no-cache.\n"
        "  -t|--time <sec>          Run benchmark for <sec> seconds. Default "
        "30.\n"
        "  -p|--proxy <server:port> Use proxy server for request.\n"
        "  -c|--clients <n>         Run <n> HTTP clients at once. Default "
        "one.\n"
        "  -k|--keep                Keep-Alive\n"
        "  -9|--http09              Use HTTP/0.9 style requests.\n"
        "  -1|--http10              Use HTTP/1.0 protocol.\n"
        "  -2|--http11              Use HTTP/1.1 protocol.\n"
        "  --get                    Use GET request method.\n"
        "  --head                   Use HEAD request method.\n"
        "  --options                Use OPTIONS request method.\n"
        "  --trace                  Use TRACE request method.\n"
        "  -?|-h|--help             This information.\n"
        "  -V|--version             Display program version.\n");
}

class http_client {
   public:
    int fd = -1;

    string address;
    unsigned short port;

   public:
    http_client(const string &_addr, unsigned short _port)
        : address(_addr), port(_port) {}

    void connect() { fd = wxg::tcp::connect(address, port); }

    int close() {
        if (fd > 0) return ::close(fd);
        fd = -1;
        return 0;
    }
};

int main(int argc, char *argv[]) {
    int opt = 0;
    int options_index = 0;

    if (argc == 1) {
        usage();
        return 2;
    }

    while ((opt = getopt_long(argc, argv, "912Vfrt:p:c:?hk", long_options,
                              &options_index)) != EOF) {
        switch (opt) {
            case 0:
                break;
            case 'f':
                force = 1;
                break;
            case 'r':
                force_reload = 1;
                break;
            case '9':
                cerr << "error -9 not implemented" << endl;
                exit(1);
                break;
            case '1':
                req.major = 1;
                req.minor = 0;
                break;
            case '2':
                req.major = 1;
                req.minor = 1;
                break;
            case 'V':
                printf(PROGRAM_VERSION "\n");
                exit(0);
            case 't':
                benchtime = atoi(optarg);
                break;
            case 'k':
                keep_alive = true;
                break;
            case 'p':
                /* proxy server parsing server:port */
                cerr << "error -p not implemented" << endl;
                exit(1);
                break;
            case ':':
            case 'h':
            case '?':
                usage();
                return 2;
                break;
            case 'c':
                clients = atoi(optarg);
                break;
        }
    }

    if (optind == argc) {
        fprintf(stderr, "webbench: Missing URL!\n");
        usage();
        return 2;
    }

    if (clients == 0) clients = 1;
    if (benchtime == 0) benchtime = 30;

    /* Copyright */
    fprintf(stderr,
            "Webbench - Simple Web Benchmark " PROGRAM_VERSION
            "\n"
            "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n");

    string url = argv[optind];
    cout << "url=" << url << endl;

    auto i = url.find_first_of("://");
    if (i == string::npos) {
        cerr << url << " is not a valid url." << endl;
        exit(2);
    }

    url = url.substr(i + 3);
    i = url.find_first_of(':');
    if (i != string::npos) {
        host = url.substr(0, i);
        port = stoi(url.substr(i + 1, url.length() - i - 1));
    }

    req.set_request(wxg::GET, "/");
    if (keep_alive)
        req.set_header("Connection", "keep-alive");
    else
        req.set_header("Connection", "close");

    wxg::buffer buf;
    req.send_to(&buf);
    memcpy(request, buf.get(), buf.length());

    cout << "request" << request << endl;

    printf("Runing info: ");

    if (clients == 1)
        printf("1 client");
    else
        printf("%d clients", clients);

    printf(", running %d sec", benchtime);

    if (force) printf(", early socket close");
    if (force_reload) printf(", forcing reload");

    printf(".\n");

    return bench();
}

/* vraci system rc error kod */
static int bench(void) {
    int i, j, k;
    pid_t pid = 0;
    FILE *f;

    /* check avaibility of target server */
    // i = Socket(proxyhost == NULL ? host : proxyhost, proxyport);
    cout << __func__ << "<<" << host << ":" << port << endl;
    http_client hc(host, port);
    hc.connect();
    if (hc.fd < 0) {
        fprintf(stderr, "\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    hc.close();

    /* create pipe */
    if (pipe(mypipe)) {
        perror("pipe failed.");
        return 3;
    }

    /* fork childs */
    for (i = 0; i < clients; i++) {
        pid = fork();
        if (pid <= (pid_t)0) {
            // if (errno == 11 && pid != 0)
            // {
            //     printf("here i = %d\n", i);
            //     while (fork() < 0);
            //     continue;
            // }
            /* child process or error*/
            sleep(1); /* make childs faster */
            break;
        }
    }

    if (pid < (pid_t)0) {
        fprintf(stderr, "problems forking worker no. %d\n", i);
        perror("fork failed.");
        return 3;
    }

    if (pid == (pid_t)0) {
        /* I am a child */
        benchcore(request);

        /* write results to pipe */
        f = fdopen(mypipe[1], "w");
        if (f == NULL) {
            perror("open pipe for writing failed.");
            return 3;
        }
        /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
        fprintf(f, "%d %d %d\n", speed, failed, bytes);
        fclose(f);

        return 0;
    } else {
        f = fdopen(mypipe[0], "r");
        if (f == NULL) {
            perror("open pipe for reading failed.");
            return 3;
        }

        setvbuf(f, NULL, _IONBF, 0);

        speed = 0;
        failed = 0;
        bytes = 0;

        while (1) {
            pid = fscanf(f, "%d %d %d", &i, &j, &k);
            if (pid < 2) {
                fprintf(stderr, "Some of our childrens died.\n");
                break;
            }

            speed += i;
            failed += j;
            bytes += k;

            /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
            if (--clients == 0) break;
        }

        fclose(f);

        printf(
            "\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d succeed, %d "
            "failed.\n",
            (int)((speed + failed) / (benchtime / 60.0f)),
            (int)(bytes / (float)benchtime), speed, failed);
    }

    return i;
}

void benchcore(const char *req) {
    int rlen;
    char buf[1500];

    wxg::signal::set_handler(SIGALRM, []() { timerexpired = 1; });

    alarm(benchtime);  // after benchtime,then exit

    http_client hc(host, port);

    rlen = strlen(req);
nexttry:
    if (keep_alive) hc.connect();
    while (1) {
        if (timerexpired) {
            if (failed > 0) {
                /* fprintf(stderr,"Correcting failed by signal\n"); */
                failed--;
            }
            return;
        }

        if (!keep_alive) hc.connect();
        if (hc.fd < 0) {
            failed++;
            if (keep_alive) hc.connect();
            continue;
        }
        if (rlen != write(hc.fd, req, rlen)) {
            failed++;
            // close(s);
            hc.close();
            if (keep_alive) hc.connect();
            continue;
        }
        if (http10 == 0)
            if (shutdown(hc.fd, 1)) {
                failed++;
                hc.close();
                continue;
            }
        if (force == 0) {
            /* read all available data from socket */
            while (1) {
                if (timerexpired) break;
                int n = read(hc.fd, buf, 1500);
                /* fprintf(stderr,"%d\n",i); */
                if (n < 0) {
                    failed++;
                    hc.close();
                    goto nexttry;
                } else if (n == 0)
                    break;
                else
                    bytes += n;
                if (keep_alive)  // keep alive will not receve eof at once
                    break;
            }
        }
        if (!keep_alive) {
            if (hc.close()) {
                failed++;
                continue;
            }
        }
        speed++;
    }
}