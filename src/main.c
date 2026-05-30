#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <readline/readline.h>
#include <readline/history.h>

char *find_in_path(const char *cmd)
{
    char *path_env = getenv("PATH");
    if (path_env == NULL) return NULL;

    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");

    while (dir != NULL)
    {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);

        if (access(full_path, X_OK) == 0)
        {
            free(path_copy);
            return strdup(full_path);
        }

        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL;
}

int parse_args(char *input, char **args, int max_args)
{
    int argc = 0;
    char *p = input;
    char buf[1024];

    while (*p != '\0' && argc < max_args - 1)
    {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        int buf_len = 0;

        while (*p != '\0')
        {
            if (*p == '\'')
            {
                p++;
                while (*p != '\0' && *p != '\'')
                    buf[buf_len++] = *p++;
                if (*p == '\'') p++;
            }
            else if (*p == '"')
            {
                p++;
                while (*p != '\0' && *p != '"')
                {
                    if (*p == '\\' && (*(p+1) == '"' || *(p+1) == '\\'))
                    {
                        p++;
                        buf[buf_len++] = *p++;
                    }
                    else
                        buf[buf_len++] = *p++;
                }
                if (*p == '"') p++;
            }
            else if (*p == '\\')
            {
                p++;
                if (*p != '\0')
                    buf[buf_len++] = *p++;
            }
            else if (*p == ' ' || *p == '\t')
            {
                break;
            }
            else
            {
                buf[buf_len++] = *p++;
            }
        }

        buf[buf_len] = '\0';
        args[argc++] = strdup(buf);
    }

    args[argc] = NULL;
    return argc;
}

void free_args(char **args, int argc)
{
    for (int i = 0; i < argc; i++)
        free(args[i]);
}

char *extract_redirect(char **args, int *nargs, int *is_stderr, int *is_append)
{
    for (int i = 0; i < *nargs; i++)
    {
        if (strcmp(args[i], "2>>") == 0)
        {
            *is_stderr = 1;
            *is_append = 1;
        }
        else if (strcmp(args[i], "2>") == 0)
        {
            *is_stderr = 1;
            *is_append = 0;
        }
        else if (strcmp(args[i], "1>>") == 0 || strcmp(args[i], ">>") == 0)
        {
            *is_stderr = 0;
            *is_append = 1;
        }
        else if (strcmp(args[i], "1>") == 0 || strcmp(args[i], ">") == 0)
        {
            *is_stderr = 0;
            *is_append = 0;
        }
        else
            continue;

        if (i + 1 < *nargs)
        {
            char *file = args[i + 1];
            free(args[i]);
            for (int j = i; j < *nargs - 2; j++)
                args[j] = args[j + 2];
            *nargs -= 2;
            args[*nargs] = NULL;
            return file;
        }
    }
    return NULL;
}

// Support code for complete builtin
#define MAX_COMPLETIONS 256
typedef struct {
    char *command;
    char *script;
} Completion;

Completion completions[MAX_COMPLETIONS];
int completion_count = 0;

// Background job tracking
#define MAX_JOBS 256
typedef struct {
    int job_num;
    pid_t pid;
    char *cmd;
    char *status; // "Running" or "Done"
} Job;

Job jobs_list[MAX_JOBS];
int jobs_count = 0;
int job_counter = 0;

// Builtins for TAB completion
char *builtin_list[] = {"echo", "exit", "type", "pwd", "cd", "complete", "jobs", NULL};
char **matches = NULL;
int match_count = 0;

char *builtin_generator(const char *text, int state)
{
    static int index;
    static int len;

    if (state == 0)
    {
        index = 0;
        len = strlen(text);
        match_count = 0;

        if (matches != NULL)
        {
            for (int i = 0; matches[i] != NULL; i++)
                free(matches[i]);
            free(matches);
            matches = NULL;
        }

        matches = malloc(sizeof(char *) * 1024);
        matches[0] = NULL;

        for (int i = 0; builtin_list[i] != NULL; i++)
        {
            if (strncmp(builtin_list[i], text, len) == 0)
                matches[match_count++] = strdup(builtin_list[i]);
        }

        char *path_env = getenv("PATH");
        if (path_env != NULL)
        {
            char *path_copy = strdup(path_env);
            char *dir = strtok(path_copy, ":");

            while (dir != NULL)
            {
                DIR *dp = opendir(dir);
                if (dp != NULL)
                {
                    struct dirent *entry;
                    while ((entry = readdir(dp)) != NULL)
                    {
                        if (strncmp(entry->d_name, text, len) == 0)
                        {
                            char full_path[1024];
                            snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
                            if (access(full_path, X_OK) == 0)
                            {
                                int dup = 0;
                                for (int i = 0; i < match_count; i++)
                                {
                                    if (strcmp(matches[i], entry->d_name) == 0)
                                    {
                                        dup = 1;
                                        break;
                                    }
                                }
                                if (!dup && match_count < 1023)
                                    matches[match_count++] = strdup(entry->d_name);
                            }
                        }
                    }
                    closedir(dp);
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
        matches[match_count] = NULL;
    }

    if (index < match_count)
        return strdup(matches[index++]);
    return NULL;
}

char **run_completer_multi(const char *script, const char *cmd, const char *word, const char *prev, const char *comp_line, int comp_point)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return NULL;

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        setenv("COMP_LINE", comp_line, 1);
        char comp_point_str[32];
        snprintf(comp_point_str, sizeof(comp_point_str), "%d", comp_point);
        setenv("COMP_POINT", comp_point_str, 1);

        execlp(script, script, cmd, word, prev, NULL);
        exit(1);
    }

    close(pipefd[1]);
    waitpid(pid, NULL, 0);

    char buf[4096];
    int n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    if (n <= 0) return NULL;
    buf[n] = '\0';

    char **results = malloc(sizeof(char *) * 256);
    int count = 0;
    char *line = strtok(buf, "\n");
    while (line != NULL && count < 255)
    {
        results[count++] = strdup(line);
        line = strtok(NULL, "\n");
    }
    results[count] = NULL;

    return results;
}

static char **completer_results = NULL;
static int completer_results_index = 0;

char *completer_results_generator(const char *text, int state)
{
    (void)text;
    if (state == 0) completer_results_index = 0;
    if (completer_results != NULL && completer_results[completer_results_index] != NULL)
        return strdup(completer_results[completer_results_index++]);
    return NULL;
}

void free_completer_results(void)
{
    if (completer_results != NULL)
    {
        for (int i = 0; completer_results[i] != NULL; i++)
            free(completer_results[i]);
        free(completer_results);
        completer_results = NULL;
    }
    completer_results_index = 0;
}

int compare_strings(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

char **shell_completion(const char *text, int start, int end)
{
    (void)end;

    if (start == 0)
    {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, builtin_generator);
    }

    char line_copy[1024];
    strncpy(line_copy, rl_line_buffer, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    char *tokens[64];
    int ntokens = 0;
    char *t = strtok(line_copy, " \t");
    while (t != NULL && ntokens < 63)
    {
        tokens[ntokens++] = t;
        t = strtok(NULL, " \t");
    }

    if (ntokens == 0) return NULL;

    char *cmd = tokens[0];
    char *word = (char *)text;
    char *prev = ntokens >= 2 ? tokens[ntokens - 2] : "";
    if (ntokens == 1) prev = "";

    const char *comp_line = rl_line_buffer;
    int comp_point = rl_point;

    for (int i = 0; i < completion_count; i++)
    {
        if (strcmp(completions[i].command, cmd) == 0)
        {
            free_completer_results();
            completer_results = run_completer_multi(completions[i].script, cmd, word, prev, comp_line, comp_point);

            if (completer_results == NULL) return NULL;

            int count = 0;
            while (completer_results[count] != NULL) count++;

            if (count == 0) return NULL;

            qsort(completer_results, count, sizeof(char *), compare_strings);

            rl_attempted_completion_over = 1;
            return rl_completion_matches(text, completer_results_generator);
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    rl_attempted_completion_function = shell_completion;

    while (1)
    {
        char *command = readline("$ ");
        if (command == NULL) break;

        if (command[0] == '\0')
        {
            free(command);
            continue;
        }

        char *args[1024];
        int nargs = parse_args(command, args, 1024);
        free(command);

        if (nargs == 0) continue;

        // Check for background operator &
        int is_background = 0;
        if (nargs > 0 && strcmp(args[nargs - 1], "&") == 0)
        {
            is_background = 1;
            free(args[nargs - 1]);
            args[nargs - 1] = NULL;
            nargs--;
        }

        if (nargs == 0)
        {
            free_args(args, 0);
            continue;
        }

        char *builtin = args[0];
        int is_stderr = 0;
        int is_append = 0;
        char *outfile = extract_redirect(args, &nargs, &is_stderr, &is_append);

        int saved_fd = -1;
        int target_fd = is_stderr ? STDERR_FILENO : STDOUT_FILENO;

        if (outfile != NULL)
        {
            saved_fd = dup(target_fd);
            int flags = O_WRONLY | O_CREAT | (is_append ? O_APPEND : O_TRUNC);
            int fd = open(outfile, flags, 0644);
            if (fd < 0)
            {
                perror("open");
                free(outfile);
                free_args(args, nargs);
                continue;
            }
            dup2(fd, target_fd);
            close(fd);
        }

        if (strcmp(builtin, "exit") == 0)
        {
            if (outfile)
            {
                dup2(saved_fd, target_fd);
                close(saved_fd);
                free(outfile);
            }
            free_args(args, nargs);
            break;
        }
        else if (strcmp(builtin, "echo") == 0)
        {
            for (int i = 1; i < nargs; i++)
            {
                if (i > 1) printf(" ");
                printf("%s", args[i]);
            }
            printf("\n");
        }
        else if (strcmp(builtin, "pwd") == 0)
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
                printf("%s\n", cwd);
            else
                perror("pwd");
        }
        else if (strcmp(builtin, "cd") == 0)
        {
            if (nargs < 2)
            {
                printf("cd: missing argument\n");
            }
            else
            {
                char *path = args[1];
                if (strcmp(path, "~") == 0)
                {
                    path = getenv("HOME");
                    if (path == NULL)
                    {
                        printf("cd: HOME not set\n");
                        if (outfile)
                        {
                            dup2(saved_fd, target_fd);
                            close(saved_fd);
                            free(outfile);
                        }
                        free_args(args, nargs);
                        continue;
                    }
                }
                if (chdir(path) != 0)
                    printf("cd: %s: No such file or directory\n", args[1]);
            }
        }
        else if (strcmp(builtin, "complete") == 0)
        {
            if (nargs >= 2 && strcmp(args[1], "-p") == 0)
            {
                if (nargs < 3)
                {
                    printf("complete: missing argument\n");
                }
                else
                {
                    int found = 0;
                    for (int i = 0; i < completion_count; i++)
                    {
                        if (strcmp(completions[i].command, args[2]) == 0)
                        {
                            printf("complete -C '%s' %s\n", completions[i].script, completions[i].command);
                            found = 1;
                            break;
                        }
                    }
                    if (!found)
                        printf("complete: %s: no completion specification\n", args[2]);
                }
            }
            else if (nargs >= 3 && strcmp(args[1], "-r") == 0)
            {
                char *target = args[2];
                for (int i = 0; i < completion_count; i++)
                {
                    if (strcmp(completions[i].command, target) == 0)
                    {
                        free(completions[i].command);
                        free(completions[i].script);
                        for (int j = i; j < completion_count - 1; j++)
                            completions[j] = completions[j + 1];
                        completion_count--;
                        break;
                    }
                }
            }
            else if (nargs >= 4 && strcmp(args[1], "-C") == 0)
            {
                int found = 0;
                for (int i = 0; i < completion_count; i++)
                {
                    if (strcmp(completions[i].command, args[3]) == 0)
                    {
                        free(completions[i].script);
                        completions[i].script = strdup(args[2]);
                        found = 1;
                        break;
                    }
                }
                if (!found && completion_count < MAX_COMPLETIONS)
                {
                    completions[completion_count].command = strdup(args[3]);
                    completions[completion_count].script = strdup(args[2]);
                    completion_count++;
                }
            }
        }
        // jobs builtin:
        else if (strcmp(builtin, "jobs") == 0)
        {
            // Step 1 — check each job for exit using WNOHANG
            for (int i = 0; i < jobs_count; i++)
            {
                int wstatus = 0;
                pid_t result = waitpid(jobs_list[i].pid, &wstatus, WNOHANG);
                if (result > 0 && WIFEXITED(wstatus))
                {
                    free(jobs_list[i].status);
                    jobs_list[i].status = strdup("Done");
                }
            }

            // Step 2 — determine the + and - markers based on highest job_num
            int max_job_num = -1;
            int second_max_job_num = -1;
            
            for (int i = 0; i < jobs_count; i++) {
                if (jobs_list[i].job_num > max_job_num) {
                    second_max_job_num = max_job_num;
                    max_job_num = jobs_list[i].job_num;
                } else if (jobs_list[i].job_num > second_max_job_num) {
                    second_max_job_num = jobs_list[i].job_num;
                }
            }

            // Print all jobs with the correct markers
            for (int i = 0; i < jobs_count; i++) {
                char marker = ' ';
                if (jobs_list[i].job_num == max_job_num) {
                    marker = '+';
                } else if (jobs_list[i].job_num == second_max_job_num) {
                    marker = '-';
                }

                int is_done = strcmp(jobs_list[i].status, "Done") == 0;
                if (is_done) {
                    printf("[%d]%c %-24s%s\n", jobs_list[i].job_num, marker, jobs_list[i].status, jobs_list[i].cmd);
                } else {
                    printf("[%d]%c %-24s%s &\n", jobs_list[i].job_num, marker, jobs_list[i].status, jobs_list[i].cmd);
                }
            }

            // Step 3 — remove Done jobs from the list
            int new_count = 0;
            for (int i = 0; i < jobs_count; i++)
            {
                if (strcmp(jobs_list[i].status, "Done") == 0)
                {
                    free(jobs_list[i].cmd);
                    free(jobs_list[i].status);
                }
                else
                {
                    jobs_list[new_count++] = jobs_list[i];
                }
            }
            jobs_count = new_count;
        }
        else if (strcmp(builtin, "type") == 0)
        {
            if (nargs < 2)
            {
                printf("type: missing argument\n");
            }
            else
            {
                char *name = args[1];
                if (!strcmp(name, "exit") || !strcmp(name, "echo") || !strcmp(name, "type") ||
                    !strcmp(name, "pwd") || !strcmp(name, "cd") || !strcmp(name, "complete") ||
                    !strcmp(name, "jobs"))
                {
                    printf("%s is a shell builtin\n", name);
                }
                else
                {
                    char *full_path = find_in_path(name);
                    if (full_path != NULL)
                    {
                        printf("%s is %s\n", name, full_path);
                        free(full_path);
                    }
                    else
                    {
                        printf("%s: not found\n", name);
                    }
                }
            }
        }
        else
        {
            char *full_path = find_in_path(builtin);
            if (full_path == NULL)
            {
                printf("%s: command not found\n", builtin);
            }
            else
            {
                pid_t pid = fork();
                if (pid == 0)
                {
                    execv(full_path, args);
                    perror("execv");
                    exit(1);
                }
                else if (pid > 0)
                {
                    if (is_background)
                    {
                        job_counter++;
                        char cmd_str[1024] = "";
                        for (int i = 0; i < nargs; i++)
                        {
                            if (i > 0) strcat(cmd_str, " ");
                            strcat(cmd_str, args[i]);
                        }
                        jobs_list[jobs_count].job_num = job_counter;
                        jobs_list[jobs_count].pid = pid;
                        jobs_list[jobs_count].cmd = strdup(cmd_str);
                        jobs_list[jobs_count].status = strdup("Running");
                        jobs_count++;
                        printf("[%d] %d\n", job_counter, pid);
                    }
                    else
                    {
                        waitpid(pid, NULL, 0);
                    }
                }
                else
                {
                    perror("fork");
                }
                free(full_path);
            }
        }

        if (outfile != NULL)
        {
            fflush(stdout);
            dup2(saved_fd, target_fd);
            close(saved_fd);
            free(outfile);
        }
        free_args(args, nargs);
    }

    return 0;
}