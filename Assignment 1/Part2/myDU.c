#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <dirent.h>

typedef unsigned long long int ull;

ull calculate_symbolic_target_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) >= 0)
    {
        if (S_ISLNK(st.st_mode))
        {
            char target[4096];
            ull tmplen = 0;
            if ((tmplen = readlink(path, target, sizeof(target) - 1)) < 0)
            {
                perror("Unable to execute\n");
                exit(EXIT_FAILURE);
            }
            char finaltarget[4099];
            snprintf(finaltarget, sizeof(finaltarget), "%s/%s", path, target);
            return calculate_symbolic_target_size(finaltarget);
        }
        else if (S_ISREG(st.st_mode))
        {
            return st.st_size;
        }
        ull total_size = (ull)(st.st_size);
        DIR *dir = opendir(path);
        if (dir == NULL)
        {
            perror("Unable to execute\n");
            exit(EXIT_FAILURE);
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                char fullpath[40960];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
                if (entry->d_type == DT_REG)
                {
                    struct stat file_struct;
                    if (stat(fullpath, &file_struct) >= 0)
                    {
                        total_size += (ull)(file_struct.st_size);
                    }
                    else
                    {
                        perror("Unable to execute\n");
                        exit(1);
                    }
                }
                else if (entry->d_type == DT_DIR)
                {
                    total_size += calculate_symbolic_target_size(fullpath);
                }
                else if (entry->d_type == DT_LNK)
                {
                    char target[4096];
                    ull tmplen = 0;
                    if ((tmplen = readlink(fullpath, target, sizeof(target) - 1)) < 0)
                    {
                        perror("Unable to execute\n");
                        exit(EXIT_FAILURE);
                    }
                    char finaltarget[4099];
                    snprintf(finaltarget, sizeof(finaltarget), "%s/%s", path, target);
                    struct stat checknewfile;
                    if (stat(finaltarget, &checknewfile) >= 0)
                    {
                        if (S_ISREG(checknewfile.st_mode))
                        {
                            total_size += checknewfile.st_size;
                        }
                        else
                        {
                            total_size += calculate_symbolic_target_size(finaltarget);
                        }
                    }
                    else
                    {
                        perror("Unable to execute\n");
                        exit(1);
                    }
                }
            }
        }
        return total_size;
    }
    else
    {
        perror("Unable to execute\n");
        exit(EXIT_FAILURE);
    }
    return 0LL;
}

ull calculate_directory_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) >= 0)
    {
        ull total_size = (ull)(st.st_size);
        DIR *dir = opendir(path);
        if (dir == NULL)
        {
            perror("Unable to execute\n");
            exit(EXIT_FAILURE);
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                char fullpath[40960];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
                if (entry->d_type == DT_REG)
                {
                    struct stat file_struct;
                    if (stat(fullpath, &file_struct) >= 0)
                    {
                        total_size += (ull)(file_struct.st_size);
                    }
                    else
                    {
                        perror("Unable to execute\n");
                        exit(1);
                    }
                }
                else if (entry->d_type == DT_DIR)
                {
                    // printf("%s\n", entry->d_name);
                    int fd[2];
                    if (pipe(fd) < 0)
                    {
                        perror("Unable to execute\n");
                        exit(1);
                    }
                    ull pid = fork();
                    if (pid < 0)
                    {
                        perror("Unable to execute\n");
                        exit(EXIT_FAILURE);
                    }
                    else if (pid == 0)
                    {
                        close(fd[0]);
                        dup2(fd[1], STDOUT_FILENO);
                        close(fd[1]);
                        execl("./myDU", "myDU", fullpath, (char *)(NULL));
                        perror("Unable to execute\n");
                        exit(EXIT_FAILURE);
                    }
                    else
                    {
                        close(fd[1]);
                        waitpid(pid, NULL, 0);
                        char read_from_pipe[64];
                        ull bytes_read = 0;
                        if ((bytes_read = read(fd[0], read_from_pipe, sizeof(read_from_pipe) - 1)) < 0)
                        {
                            perror("Unable to execute\n");
                            exit(EXIT_FAILURE);
                        }
                        read_from_pipe[bytes_read] = '\0';
                        total_size += atoll(read_from_pipe);
                        close(fd[0]);
                    }
                }
                else if (entry->d_type == DT_LNK)
                {
                    char target[4096];
                    ull tmplen = 0;
                    if ((tmplen = readlink(fullpath, target, sizeof(target) - 1)) < 0)
                    {
                        perror("Unable to execute\n");
                        exit(EXIT_FAILURE);
                    }
                    char finaltarget[4099];
                    snprintf(finaltarget, sizeof(finaltarget), "%s/%s", path, target);
                    struct stat checknewfile;
                    if (stat(finaltarget, &checknewfile) >= 0)
                    {
                        if (S_ISREG(checknewfile.st_mode))
                        {
                            total_size += checknewfile.st_size;
                        }
                        else
                        {
                            total_size += calculate_symbolic_target_size(finaltarget);
                        }
                    }
                    else
                    {
                        perror("Unable to execute\n");
                        exit(1);
                    }
                }
            }
        }
        return total_size;
    }
    else
    {
        perror("Unable to execute\n");
        exit(EXIT_FAILURE);
    }
    return 0LL;
}

int main(int argc, char *argv[])
{
    const char *directory_path = argv[1];
    ull size = calculate_directory_size(directory_path);
    printf("%llu", size);
    return 0;
}