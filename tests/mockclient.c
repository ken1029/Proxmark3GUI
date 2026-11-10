/*
 * Mock Proxmark3 client for the GUI process tests.
 *
 * Default ("pipe") mode reproduces the v4.23346 client when stdin is a pipe:
 *   - no prompt at startup,
 *   - the prompt and the command are echoed before execution,
 *   - no empty prompt is printed after a command completes.
 * "rem <token>" answers with a "remark: <token>" line, the completion marker
 * used by the GUI handshake and by short commands.
 *
 * MOCK_MODE=interactive restores the old terminal behaviour (startup prompt and
 * a prompt after every command) so interactive-client compatibility stays
 * covered.
 *
 * Environment:
 *   MOCK_OS_VERSION       firmware version; "offline" (or empty) prints no OS line
 *   MOCK_MODE             "pipe" (default) or "interactive"
 *   MOCK_SPLIT            "1" write output in small chunks
 *   MOCK_ANSI             "1" wrap output in ANSI colour sequences
 *   MOCK_CRLF             "1" use CRLF line endings
 *   MOCK_REM_DELAY_MS     delay before the rem completion line
 *   MOCK_ECHO_SPLIT_MS    delay between the prompt and the command echo
 *   MOCK_STARTUP_DELAY_MS delay before reading the first command
 *   MOCK_EXIT_AFTER_START "1" exit immediately
 *   MOCK_PARTIAL_VERSION  "1" print only the first half of the version block
 *   MOCK_EXIT_AFTER_VERSION "1" exit after the version block
 *   MOCK_HANDSHAKE_HANG   "1" never answer hw version
 *   MOCK_WRITE_MODE       ok (default), fail, silent, magic, emulator
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(unsigned ms)
{
    Sleep(ms);
}
#else
#include <unistd.h>
static void sleep_ms(unsigned ms)
{
    usleep(ms * 1000);
}
#endif

static int flag(const char *name)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

static const char *env_or(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0')
        return fallback;
    return value;
}

static unsigned env_ms(const char *name)
{
    const char *value = getenv(name);
    if (value == NULL)
        return 0;
    return (unsigned)strtoul(value, NULL, 10);
}

static void emit(const char *text)
{
    if (!flag("MOCK_SPLIT")) {
        fputs(text, stdout);
        fflush(stdout);
        return;
    }

    size_t len = strlen(text);
    size_t pos = 0;
    while (pos < len) {
        size_t chunk = len - pos;
        if (chunk > 7)
            chunk = 7;
        fwrite(text + pos, 1, chunk, stdout);
        fflush(stdout);
        pos += chunk;
        sleep_ms(5);
    }
}

static void emit_line(const char *line)
{
    const char *eol = flag("MOCK_CRLF") ? "\r\n" : "\n";
    char buffer[4096];
    snprintf(buffer, sizeof(buffer), "%s%s", line, eol);
    emit(buffer);
}

static void emit_field(const char *line)
{
    const char *eol = flag("MOCK_CRLF") ? "\r\n" : "\n";
    char buffer[4096];
    if (flag("MOCK_ANSI"))
        snprintf(buffer, sizeof(buffer), "\x1b[32m%s\x1b[0m%s", line, eol);
    else
        snprintf(buffer, sizeof(buffer), "%s%s", line, eol);
    emit(buffer);
}

static void print_prompt(void)
{
    emit("[offline|script] pm3 --> ");
}

static void print_version(const char *os)
{
    emit_field(" [ Client ]");
    emit_field("  Iceman/master/v4.23346-suspect 2026-09-18 00:22:18 9ce8a4b6c");
    emit_field("  QT GUI support............ present");

    if (flag("MOCK_PARTIAL_VERSION")) {
        if (flag("MOCK_EXIT_AFTER_VERSION"))
            exit(0);
        return;
    }

    if (os[0] != '\0') {
        emit_field(" [ Model ]");
        emit_field("    Target.... PM3 GENERIC");
        emit_field("    Client.... v4.23346-suspect");
        char line[256];
        snprintf(line, sizeof(line), "    OS........ %s", os);
        emit_field(line);
    }
}

static void print_write(const char *command)
{
    const char *mode = env_or("MOCK_WRITE_MODE", "ok");
    if (strstr(command, "esetblk") != NULL || strcmp(mode, "emulator") == 0)
        return; // emulator writes have no success marker
    if (strstr(command, "csetblk") != NULL || strcmp(mode, "magic") == 0) {
        emit_field("Writing block number: 1");
        return;
    }
    if (strcmp(mode, "silent") == 0)
        return;
    if (strcmp(mode, "magic") == 0) {
        emit_field("Writing block number: 1");
        return;
    }
    if (strcmp(mode, "fail") == 0) {
        emit_field("[-] Write ( fail )");
        return;
    }
    emit_field("Writing block no 1, key type:A - FFFFFFFFFFFF");
    emit_field("[+] Write ( ok )");
}

static void print_read(void)
{
    if (strcmp(env_or("MOCK_READ_MODE", "ok"), "silent") == 0)
        return;
    emit_field(" 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f");
}

int main(void)
{
    char line[1024];
    const int interactive = strcmp(env_or("MOCK_MODE", "pipe"), "interactive") == 0;
    const char *os = getenv("MOCK_OS_VERSION");
    if (os == NULL || strcmp(os, "offline") == 0)
        os = "";

    const char *pidFile = getenv("MOCK_PID_FILE");
    if (pidFile != NULL && pidFile[0] != '\0') {
        FILE *log = fopen(pidFile, "a");
        if (log != NULL) {
#ifdef _WIN32
            fprintf(log, "%lu\n", (unsigned long)GetCurrentProcessId());
#else
            fprintf(log, "%ld\n", (long)getpid());
#endif
            fclose(log);
        }
    }

    if (flag("MOCK_EXIT_AFTER_START"))
        return 0;

    sleep_ms(env_ms("MOCK_STARTUP_DELAY_MS"));

    emit_line("[=] Session log mock.log");
    emit_line("[=] OFFLINE mode.");
    if (interactive)
        print_prompt();

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        // Pipe mode: prompt and command are printed together before execution.
        if (!interactive) {
            print_prompt();
            unsigned echoSplit = env_ms("MOCK_ECHO_SPLIT_MS");
            if (echoSplit > 0)
                sleep_ms(echoSplit);
            emit_line(line);
        }

        if (flag("MOCK_HANDSHAKE_HANG")) {
            sleep_ms(50);
            continue;
        }

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            break;
        } else if (strcmp(line, "hw version") == 0) {
            if (!flag("MOCK_HANDSHAKE_HANG"))
                print_version(os);
        } else if (strncmp(line, "rem ", 4) == 0) {
            unsigned delay = env_ms("MOCK_REM_DELAY_MS");
            if (delay > 0)
                sleep_ms(delay);
            char marker[1100];
            snprintf(marker, sizeof(marker), "[+] 2026-11-10T11:53:37Z remark: %s", line + 4);
            emit_field(marker);
        } else if (strstr(line, "wrbl") != NULL || strstr(line, "csetblk") != NULL
                   || strstr(line, "esetblk") != NULL) {
            print_write(line);
        } else if (strstr(line, "rdbl") != NULL || strstr(line, "getblk") != NULL) {
            print_read();
        } else {
            emit_field("[-] unknown command");
        }

        if (interactive)
            print_prompt();
    }
    return 0;
}
