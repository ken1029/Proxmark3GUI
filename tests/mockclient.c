/*
 * Minimal mock Proxmark3 client for the GUI process tests.
 *
 * Speaks just enough of the v4.23346 console format for PM3Process/Util to
 * start it, detect the prompt and parse the "hw version" block.
 * Windows cannot exec a #! shell script through QProcess, so the process
 * tests use this executable there (and on Unix it is built but unused).
 *
 * MOCK_OS_VERSION: firmware version reported by "hw version".
 *   unset -> v4.23346-suspect, "offline" -> no device in the model block.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void prompt(void)
{
    fputs("\n[offline|script] pm3 --> ", stdout);
    fflush(stdout);
}

int main(void)
{
    char line[512];
    const char *os = getenv("MOCK_OS_VERSION");
    if (os == NULL)
        os = "v4.23346-suspect";
    else if (strcmp(os, "offline") == 0)
        os = "";

    fputs("[=] Session log mock.log\n[=] OFFLINE mode.", stdout);
    prompt();

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (strcmp(line, "hw version") == 0) {
            fputs("\n [ Client ]\n  Iceman/master/v4.23346-suspect 2026-09-18\n"
                  "  QT GUI support............ present\n", stdout);
            if (os[0] != '\0') {
                printf("\n [ Model ]\n    Target.... PM3 GENERIC\n"
                       "    Client.... v4.23346-suspect\n    OS........ %s\n", os);
            }
            prompt();
        } else if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            break;
        }
    }
    return 0;
}
