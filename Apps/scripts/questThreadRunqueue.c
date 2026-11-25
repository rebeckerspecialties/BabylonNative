// Read cumulative Linux scheduler accounting without enabling kernel or GPU tracing.
// Build with the NDK's aarch64-linux-android29-clang -O2, then:
//   adb push questThreadRunqueue /data/local/tmp/
//   adb exec-out /data/local/tmp/questThreadRunqueue PID JS_TID > run.csv
//   adb shell rm /data/local/tmp/questThreadRunqueue
// Start just after the Babylon.js startup log (threadtime identifies JS_TID).
// Correlate successive counter differences with PID-filtered logcat -v epoch.
// Run-queue time is charged when a thread runs again. Adjacent 25 ms bins
// identify scheduler delays, not their preemptor or the GPU timeline.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int parse_id(const char* argument)
{
    char* end;
    errno = 0;
    long value = strtol(argument, &end, 10);
    return !errno && *argument && !*end && value > 0 && value <= INT_MAX ? (int)value : -1;
}

static int open_thread(int pid, int tid)
{
    char path[96];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/schedstat", pid, tid);
    return open(path, O_RDONLY | O_CLOEXEC);
}

static int sample(int fd, unsigned long long values[3])
{
    char buffer[192];
    ssize_t n = pread(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return 0;
    buffer[n] = 0;
    return sscanf(buffer, "%llu %llu %llu", &values[0], &values[1], &values[2]) == 3;
}

int main(int argc, char** argv)
{
    int pid = argc == 3 ? parse_id(argv[1]) : -1;
    int js = argc == 3 ? parse_id(argv[2]) : -1;
    if (pid < 1 || js < 1 || pid == js) {
        fprintf(stderr, "Usage: questThreadRunqueue RENDERER_PID JS_TID (renderer on main thread)\n");
        return 2;
    }
    int host_fd = open_thread(pid, pid), js_fd = open_thread(pid, js);
    if (host_fd < 0 || js_fd < 0) { perror("open schedstat"); return 1; }
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline)) return 1;
    puts("epoch_ms,renderer_cpu_ns,renderer_runqueue_ns,renderer_slices,js_cpu_ns,js_runqueue_ns,js_slices");
    for (int i = 0; i < 2100; ++i) {
        unsigned long long h[3], j[3];
        struct timespec wall;
        if (!sample(host_fd, h) || !sample(js_fd, j) || clock_gettime(CLOCK_REALTIME, &wall)) {
            fprintf(stderr, "Incomplete thread counters\n");
            return 1;
        }
        printf("%lld.%03ld,%llu,%llu,%llu,%llu,%llu,%llu\n",
            (long long)wall.tv_sec * 1000 + wall.tv_nsec / 1000000, wall.tv_nsec / 1000 % 1000,
            h[0], h[1], h[2], j[0], j[1], j[2]);
        deadline.tv_nsec += 25000000L;
        if (deadline.tv_nsec >= 1000000000L) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L; }
        int err;
        do { err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL); } while (err == EINTR);
        if (err) return 1;
    }
    return 0;
}
