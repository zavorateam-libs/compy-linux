#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

extern char **environ;

int main(int argc, char *argv[]) {
    // We expect the arguments to be: [./compy-proxy, /bin/sh, arg1, arg2, ...]
    if (argc < 2) {
        fprintf(stderr, "Compy Proxy: Error: No application path provided.\n");
        return 1;
    }

    // argv[1] is the actual target application path (e.g., /bin/sh)
    char *const app_path = argv[1];
    
    // Create new arguments array for the target application
    char *new_argv[argc]; 
    new_argv[0] = app_path;

    // Copy the user's arguments (starting from original argv[2])
    for (int i = 2; i < argc; ++i) {
        new_argv[i - 1] = argv[i];
    }
    new_argv[argc - 1] = NULL; // Null-terminate

    // Execute the real application (e.g., /bin/sh)
    // The static nature of the proxy bypasses the dynamic linker conflict.
    if (execve(app_path, new_argv, environ) == -1) {
        perror("Compy Proxy: execve failed inside isolation");
        return 1;
    }

    return 0; // Unreachable
}
