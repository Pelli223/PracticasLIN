#include<errno.h>
#include<sys/syscall.h>
#include<unistd.h>
#include<stdio.h>
#include<errno.h>
#include<stdlib.h>
#define __NR_LEDCT1	442

char usage[] = "Usage: ./ledct1_invoke <ledmask>\n";

int main(int argc, char **argv){
	int leds;
	if(argc != 2){
		printf("%s", usage);
		return -EINVAL;
	}
	if(sscanf(argv[1], "%i", &leds) != 1){
		fprintf(stderr, "Argumento %s invalido, no es numero del 0 al 7\n", argv[1]);
		return -EINVAL;
	}
	if(syscall(__NR_LEDCT1,leds) == -1){
		perror("Error");
      		return -EINVAL;
	}
	return 0;
}	

