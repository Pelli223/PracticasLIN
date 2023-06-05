#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define COLOR1 	"ff00b2"
#define COLOR2	"00d8ff"

char usage[] = "./blink_user [l](for lantern mode)";


void linterna(int fd){
	char *lint = malloc(sizeof(char) * 88);
	char *ledMsg = malloc(sizeof(char) * 11);
	for(int i = 0; i < 7; i++){
		sprintf(ledMsg, "%i:0xffffff,", i);
		strcat(lint, ledMsg);
	}
	strcat(lint, "7:0xffffff\n");
	if(write(fd, lint, strlen(lint))==-1){
		free(lint);
		free(ledMsg);
		perror("Error");
	}
}

int patron (int fd){
	int pos = 3;
	int colorFirst = 1;
	char *msg = malloc (sizeof(char)*22);
	char *msgAc = malloc(sizeof(char)*88);

	for(int i = 0; i < 7; i++){
		if(i < 4){
			sprintf(msg, "%i:0x%s,", i, COLOR1);
			strcat(msgAc, msg);
		}
		else{
			sprintf(msg, "%i:0x%s,", i, COLOR2);
			strcat(msgAc, msg);
		}
	}
	sprintf(msg, "7:0x%s\n", COLOR2);
	strcat(msgAc, msg);
	if(write(fd, msgAc, strlen(msgAc))==-1){
		free(msgAc);
		free(msg);
		perror("Error");
		return -1;
	}

	free(msgAc);

	sleep(3);
	while(1){
		sleep(1);
		if(pos < 0){
			pos = 3;
			colorFirst = colorFirst == 1 ? 2:1;
		}
		if(colorFirst == 1)	
			sprintf(msg, "%i:0x%s,%i:0x%s\n", pos, COLOR2,7 - pos, COLOR1);
		else
			sprintf(msg, "%i:0x%s,%i:0x%s\n", pos, COLOR1,7 - pos, COLOR2);

		write(fd, msg, strlen(msg));
		
		pos--;
	}

	return 0;
}

int main (int argc, char **argv) {
	int opt, fd;
	if(argc > 2){
		printf("%s\n", usage);
		return -1;
	}
	
	if((fd = open("/dev/usb/blinkstick0", O_WRONLY)) == -1){
		perror("Error");
		close(fd);
		return -1;
	}
	
	if(argc == 2){
		if(*argv[1] != 'l') {
			printf("%s\n", usage);
			close(fd);
			return -1;
		}	
		linterna(fd);
	}
	
	else	
		patron(fd);

	close(fd);
				
	return 0;
}
