#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

typedef unsigned long long int ull;

ull square_func(ull x)
{
	return x * x;
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		perror("Unable to execute\n");
		exit(1);
	}
	char *string_to_be_processed = *(argv + argc - 1);
	ull num_to_be_processed = atoll(string_to_be_processed);
	argc -= 1;
	ull ans_of_the_func = square_func(num_to_be_processed);
	ull pid = fork();
	if (pid < 0)
	{
		perror("Unable to execute\n");
		exit(1);
	}
	if (argc == 1)
	{
		if (pid > 0)
		{
			printf("%llu\n", ans_of_the_func);
			return 0;
		}
	}
	else if (argc > 1)
	{
		if (pid == 0LL)
		{
			char *next_operator = argv[1];
			char new_command[strlen(next_operator) + 3];
			char *executable_sign = "./";
			strcpy(new_command, executable_sign);
			strcat(new_command, next_operator);
			*(argv++) = new_command;
			char ans_in_string[200];
			sprintf(ans_in_string, "%llu", ans_of_the_func);
			*(argv + argc - 1) = ans_in_string;
			execv(new_command, argv);
		}
	}
	return 0;
}
