#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#define ll long long

int main(int argc, char *argv[])
{	
	ll num = strtoll(argv[argc-1], NULL, 10);
	num *= num;
	
	if(argc <= 1){
		perror("Unable to execute\n");
		exit(-1);
	}
	else if(argc == 2){
		printf("%lld\n", num);
		exit(0);
	}

	sprintf(argv[argc-1], "%lld", num);
	if(execv(argv[1], argv+1)){
		perror("Unable to execute\n");
		exit(-1);
	}

	exit(0);
}
