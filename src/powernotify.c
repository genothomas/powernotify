#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

#include <ctype.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define THRESHOLD 15

#define CONFIG_SUFFIX "/.config/powernotify/config"
#define STATE_DIR_SUFFIX "/Library/Application Support/PowerNotify"
#define STATE_SUFFIX "/Library/Application Support/PowerNotify/state"

#define MAX_URL 1024
#define MAX_TOKEN 512
#define MAX_PATH 1024
#define MAX_HOST 256

typedef struct {
    char url[MAX_URL];
    char token[MAX_TOKEN];
    int sent;
} App;

static int user_path(const char *suffix, char *out, size_t size)
{
    const char *home = getenv("HOME");
    if (!home)
        return 0;

    int n = snprintf(out, size, "%s%s", home, suffix);
    return n >= 0 && (size_t)n < size;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';

    return s;
}

static int copy_value(char *dst, size_t size, const char *src)
{
    size_t len = strlen(src);
    if (len >= size)
        return 0;

    memcpy(dst, src, len + 1);
    return 1;
}

static int load_config(App *app)
{
    char path[MAX_PATH];
    char line[2048];

    if (!user_path(CONFIG_SUFFIX, path, sizeof(path))) {
        fprintf(stderr, "PowerNotify: invalid config path\n");
        return 0;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "PowerNotify: unable to open config: %s\n", path);
        return 0;
    }

    struct stat st;
    if (fstat(fileno(f), &st) != 0 || (st.st_mode & 077) != 0) {
        fprintf(stderr, "PowerNotify: config must be owner-only (chmod 600 or stricter)\n");
        fclose(f);
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#')
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = trim(p);
        char *value = trim(eq + 1);

        if (!strcmp(key, "NTFY_URL")) {
            if (!copy_value(app->url, sizeof(app->url), value)) {
                fprintf(stderr, "PowerNotify: NTFY_URL is too long\n");
                fclose(f);
                return 0;
            }
        } else if (!strcmp(key, "NTFY_TOKEN")) {
            if (!copy_value(app->token, sizeof(app->token), value)) {
                fprintf(stderr, "PowerNotify: NTFY_TOKEN is too long\n");
                fclose(f);
                return 0;
            }
        }
    }

    fclose(f);

    if (!app->url[0] || !app->token[0]) {
        fprintf(stderr, "PowerNotify: NTFY_URL or NTFY_TOKEN missing\n");
        return 0;
    }

    return 1;
}

static void ensure_state_dir(void)
{
    const char *home = getenv("HOME");
    if (!home)
        return;

    char path[MAX_PATH];

    snprintf(path, sizeof(path), "%s/Library", home);
    mkdir(path, 0700);

    snprintf(path, sizeof(path), "%s/Library/Application Support", home);
    mkdir(path, 0700);

    snprintf(path, sizeof(path), "%s%s", home, STATE_DIR_SUFFIX);
    mkdir(path, 0700);
}

static int load_state(void)
{
    char path[MAX_PATH];

    if (!user_path(STATE_SUFFIX, path, sizeof(path)))
        return 0;

    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    int first = 0, second = 0;
    int count = fscanf(f, "%d %d", &first, &second);
    fclose(f);

    if (count == 2)
        return second != 0;

    return count == 1 && first != 0;
}

static int save_state(int sent)
{
    char path[MAX_PATH], tmp[MAX_PATH + 16];

    if (!user_path(STATE_SUFFIX, path, sizeof(path)))
        return 0;

    ensure_state_dir();

    int n = snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return 0;

    int fd = mkstemp(tmp);
    if (fd == -1)
        return 0;

    if (fchmod(fd, 0600) != 0) {
        close(fd);
        unlink(tmp);
        return 0;
    }

    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp);
        return 0;
    }

    if (fprintf(f, "%d\n", sent ? 1 : 0) < 0) {
        fclose(f);
        unlink(tmp);
        return 0;
    }

    if (fclose(f) != 0) {
        unlink(tmp);
        return 0;
    }

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return 0;
    }

    return 1;
}

static int get_battery(int *percent, int *on_battery)
{
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info)
        return 0;

    CFStringRef providing = IOPSGetProvidingPowerSourceType(info);
    *on_battery = providing &&
                  CFEqual(providing, CFSTR(kIOPMBatteryPowerKey));

    CFArrayRef sources = IOPSCopyPowerSourcesList(info);
    if (!sources) {
        CFRelease(info);
        return 0;
    }

    int found = 0;

    for (CFIndex i = 0; i < CFArrayGetCount(sources); i++) {
        CFTypeRef source = CFArrayGetValueAtIndex(sources, i);
        CFDictionaryRef d = IOPSGetPowerSourceDescription(info, source);

        if (!d)
            continue;

        CFStringRef type = CFDictionaryGetValue(d, CFSTR(kIOPSTypeKey));
        if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType)))
            continue;

        CFNumberRef cur = CFDictionaryGetValue(d, CFSTR(kIOPSCurrentCapacityKey));
        CFNumberRef max = CFDictionaryGetValue(d, CFSTR(kIOPSMaxCapacityKey));
        if (!cur || !max)
            continue;

        int c = 0, m = 0;
        if (!CFNumberGetValue(cur, kCFNumberIntType, &c) ||
            !CFNumberGetValue(max, kCFNumberIntType, &m) || m <= 0)
            continue;

        *percent = (c * 100 + m / 2) / m;
        if (*percent < 0)
            *percent = 0;
        else if (*percent > 100)
            *percent = 100;

        found = 1;
        break;
    }

    CFRelease(sources);
    CFRelease(info);
    return found;
}

static int send_alert(const App *app, int percent)
{
    char hostname[MAX_HOST];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        snprintf(hostname, sizeof(hostname), "Mac");
    hostname[sizeof(hostname) - 1] = '\0';

    char title[512], message[64];
    char auth_header[768], title_header[768];

    snprintf(title, sizeof(title), "%s · Low battery", hostname);
    snprintf(message, sizeof(message), "%d%% remaining", percent);
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: Bearer %s", app->token);
    snprintf(title_header, sizeof(title_header),
             "X-Title: %s", title);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        fprintf(stderr, "PowerNotify: pipe failed\n");
        return 0;
    }

    size_t auth_len = strlen(auth_header);
    ssize_t written = write(pipefd[1], auth_header, auth_len);
    if (written != (ssize_t)auth_len || write(pipefd[1], "\n", 1) != 1) {
        close(pipefd[0]);
        close(pipefd[1]);
        fprintf(stderr, "PowerNotify: failed to prepare auth header\n");
        return 0;
    }
    close(pipefd[1]);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        return 0;
    }

    int action_ok =
        posix_spawn_file_actions_adddup2(&actions, pipefd[0], STDIN_FILENO) == 0 &&
        posix_spawn_file_actions_addclose(&actions, pipefd[0]) == 0;

    if (!action_ok) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        return 0;
    }

    char *argv[] = {
        "/usr/bin/curl",
        "-q",
        "--fail",
        "--silent",
        "--show-error",
        "--connect-timeout", "5",
        "--max-time", "15",
        "-H", "@-",
        "-H", title_header,
        "-H", "X-Priority: 4",
        "-H", "X-Tags: warning,battery",
        "--data-raw", message,
        (char *)app->url,
        NULL
    };

    pid_t pid;
    int result = posix_spawn(&pid, "/usr/bin/curl", &actions, NULL, argv, environ);

    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[0]);

    if (result != 0) {
        fprintf(stderr, "PowerNotify: failed to start curl: %s\n", strerror(result));
        return 0;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "PowerNotify: waitpid failed\n");
        return 0;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "PowerNotify: ntfy request failed\n");
        return 0;
    }

    return 1;
}

static void check_battery(App *app)
{
    int percent, on_battery;
    if (!get_battery(&percent, &on_battery))
        return;

    if (percent > THRESHOLD) {
        if (app->sent) {
            app->sent = 0;
            if (!save_state(0))
                fprintf(stderr, "PowerNotify: failed to save state\n");
        }
        return;
    }

    if (on_battery && !app->sent && send_alert(app, percent)) {
        app->sent = 1;
        if (!save_state(1))
            fprintf(stderr, "PowerNotify: failed to save state\n");
    }
}

static void power_source_changed(void *context)
{
    check_battery((App *)context);
}

int main(void)
{
    App app = {0};

    if (!load_config(&app))
        return 1;

    app.sent = load_state();
    ensure_state_dir();
    check_battery(&app);

    CFRunLoopSourceRef source =
        IOPSNotificationCreateRunLoopSource(
            power_source_changed,
            &app);

    if (!source) {
        fprintf(stderr,
                "PowerNotify: failed to create IOKit notification\n");
        return 1;
    }

    CFRunLoopAddSource(
        CFRunLoopGetCurrent(),
        source,
        kCFRunLoopDefaultMode);

    CFRunLoopRun();

    CFRelease(source);
    return 0;
}
