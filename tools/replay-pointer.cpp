// Standalone Linux test helper. Build with: c++ -O2 -std=c++20 replay-pointer.cpp -o replay-pointer
// Requires access to /dev/uinput. Emits motion only, never clicks or keys.
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <ctime>

static volatile sig_atomic_t stopped = 0;
static void stop(int) { stopped = 1; }
static double now() {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void until(double deadline) {
    timespec t{time_t(deadline), long((deadline - std::floor(deadline)) * 1e9)};
    while (!stopped && clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr) == EINTR) {}
}
int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        std::fprintf(stderr, "Usage: %s RATE_HZ PERIOD_SECONDS SPAN_COUNTS DURATION_SECONDS [START_X_COUNTS]\n"
            "Creates temporary virtual mouse, waits up to 120s for /tmp/spacedock-replay-go.\n"
            "Homes to bottom-left, offsets 350 counts right and 24 up, then sweeps.\n", argv[0]);
        return 1;
    }
    const double rate = std::atof(argv[1]), period = std::atof(argv[2]);
    const double span = std::atof(argv[3]), duration = std::atof(argv[4]);
    const double startX = argc == 6 ? std::atof(argv[5]) : 350;
    if (!(rate >= 30 && rate <= 1000 && period >= .25 && period <= 30
          && span >= 10 && span <= 2000 && duration >= 1 && duration <= 120
          && startX >= 0 && startX <= 2000)) return 1;
    std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { std::perror("uinput open"); return 1; }
    auto enable = [&](unsigned long request, int value) {
        if (ioctl(fd, request, value) < 0) { std::perror("uinput capability"); return false; }
        return true;
    };
    if (!enable(UI_SET_EVBIT, EV_KEY) || !enable(UI_SET_KEYBIT, BTN_LEFT)
        || !enable(UI_SET_EVBIT, EV_REL) || !enable(UI_SET_RELBIT, REL_X)
        || !enable(UI_SET_RELBIT, REL_Y)) { close(fd); return 1; }
    uinput_setup setup{};
    setup.id.bustype = BUS_USB; setup.id.vendor = 0x1234; setup.id.product = 0x5344;
    std::strcpy(setup.name, "SpaceDock Repeatable Mouse");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        std::perror("uinput create"); close(fd); return 1;
    }
    auto emit = [&](int type, int code, int value) {
        input_event event{}; event.type = type; event.code = code; event.value = value;
        if (write(fd, &event, sizeof(event)) != sizeof(event)) { std::perror("input write"); stopped = 1; }
    };
    auto move = [&](int x, int y) {
        if (x) emit(EV_REL, REL_X, x);
        if (y) emit(EV_REL, REL_Y, y);
        emit(EV_SYN, SYN_REPORT, 0);
    };
    std::puts("Virtual mouse ready; waiting for /tmp/spacedock-replay-go"); std::fflush(stdout);
    const double readyDeadline = now() + 120;
    while (!stopped && access("/tmp/spacedock-replay-go", F_OK) != 0 && now() < readyDeadline) usleep(10000);
    if (!stopped && access("/tmp/spacedock-replay-go", F_OK) == 0) {
        move(-32767, 32767); until(now() + .3);
        move(std::lround(startX), -24); until(now() + .3);
        const double start = now(); int previous = 0, late = 0, reports = 0;
        for (int i = 0; !stopped && i < int(duration * rate); ++i) {
            until(start + i / rate);
            if (now() - (start + i / rate) > .5 / rate) ++late;
            const double phase = std::fmod(i / rate, period) / period;
            const int target = std::lround(span * (1 - std::abs(2 * phase - 1)));
            move(target - previous, 0); previous = target; ++reports;
        }
        std::printf("Replay completed: reports=%d late_over_half_period=%d elapsed=%.3f\n", reports, late, now() - start);
    }
    ioctl(fd, UI_DEV_DESTROY); close(fd);
    std::puts("Virtual mouse removed");
}
