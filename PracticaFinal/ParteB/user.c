#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int fd;

void *pruebaProdCons(void *arg){
	int i;
	int num = *((int*) arg);
	int r;
	char *c = malloc(4);
	char *s = malloc(4);

	if(num == 1){
		for(i = 0;i < 5; i++){
			sprintf(c, "%i", i); 
			if(write(fd, c, strlen(c)) < 0)
				perror("write: ");
		}
		sleep(3);
		while(1){
			r = rand() % 11;
			sprintf(c, "%i", r);
			write(fd, c, strlen(c));
			sleep(1);
		}
	}
	else if(num == 2){
		memset(s, 0, 4);
		sleep(1);
		for(i = 0; i < 5; i++){
			read(fd, s, 3);
			printf("%s", s);
		}
		while(1){
			read(fd, s, 3);
			printf("%s", s);
			sleep(3);
		}
	}
	else {
		while(1){
			write(fd, "patata", sizeof(char)*6);
			sleep(3);
		}
	}

	pthread_exit(0);

	return NULL;
}

int main(void){
	pthread_t th[3];
	int num;
	int arg[3];

	fd = open("/dev/prodcons", O_RDWR);


	if(fd < 0)
		printf("No se pudo abrir\n");

	for(num = 0; num < 3; num++){
		arg[num] = num;
		pthread_create(&th[num], NULL, pruebaProdCons, (void*)&arg[num]);
	}

	write(fd, "10", 2);

	pthread_join(th[0], NULL);
	pthread_join(th[1], NULL);
	pthread_join(th[2], NULL);

	close(fd);

	return 0;
}
