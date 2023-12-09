#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#define ll long long

ll find_size(char *path){
	ll total_size = 0;
	struct stat sbuf;
	if(lstat(path, &sbuf) == -1){
		perror("Unable to execute\n");
		exit(-1);
	};
	// fprintf(stderr, "%s %d\n", path, sbuf.st_mode);
	if(S_ISREG(sbuf.st_mode)){
		total_size += sbuf.st_size;
		return total_size;
	}

	if(S_ISDIR(sbuf.st_mode)){
		// fprintf(stderr, "%s %d\n", path, sbuf.st_mode);
		// if(S_ISDIR(sbuf.st_mode)) total_size += sbuf.st_size;
		total_size += sbuf.st_size;
		DIR *dp = opendir(path);
		if(dp == NULL){
			perror("Unable to execute\n");
			exit(-1);
		}
		struct dirent *d;
		
		while((d = readdir(dp))){
			// ll size = strlen(path) + 1 + strlen(d->d_name);
			ll size = 4096;
			char *new = (char *)malloc(size + 1);
			if(new == NULL){
				perror("Unable to execute\n");
				exit(-1);
			}
			
			strcpy(new, path);
			if(new[strlen(new)-1] != '/') strcat(new, "/");
			strcat(new, d->d_name);
			new[size] = '\0';
			
			struct stat sbuf2;
			if(lstat(new, &sbuf2) == -1){
				perror("Unable to execute\n");
				exit(-1);
			};
			
			if(!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")){
				continue;
			}

			if(S_ISREG(sbuf2.st_mode)){
				total_size += sbuf2.st_size;
			}

			else if(S_ISDIR(sbuf2.st_mode)){
				// total_size += sbuf2.st_size;
				total_size += find_size(new);
			}

			else if(S_ISLNK(sbuf2.st_mode)){
				char newPath[4097];
				ssize_t len = 0;

				while((len = readlink(new, newPath, 4097)) != -1){
					// fprintf(stderr, "56 : %s		", new);
					// fprintf(stderr, "%s\n", newPath);
					for(int i=len;i<4097;i++){
						newPath[i] = '\0';
					}
					int index = 0;
					for(int i=0;i<strlen(new);i++){
						if(new[i] == '/') index = i;
					}
					for(int i=index+1;i<strlen(new);i++){
						new[i] = '\0';
					}
					new[index+1] = '\0';
					strcat(new, newPath);
					new[strlen(new)] = '\0';
				}
				total_size += find_size(new);
			}

		}
		
		closedir(dp);
	}

	return total_size;
}

int main(int argc, char *argv[]){
	ll total_size = 0;
	if(argc <= 1 || argc >= 3){
		perror("Unable to execute\n");
		exit(-1);
	}

	DIR *dp = opendir(argv[1]);
	if(dp == NULL){
		perror("Unable to execute\n");
		exit(-1);
	}
	
	struct dirent *d;
	struct stat sbuff;
	if(stat(argv[1], &sbuff) == -1){
		perror("Unable to execute\n");
		exit(-1);
	};
	total_size += sbuff.st_size;
	// fprintf(stderr, "%lld\n", total_size);

	while((d = readdir(dp))){
		// ll size = strlen(argv[argc-1]) + 1 + strlen(d->d_name);
		ll size = 4096;
		char *new = (char *)malloc(size + 1);
		if(new == NULL){
			perror("Unable to execute\n");
			exit(-1);
		}
		
		strcpy(new, argv[argc-1]);
		if(new[strlen(new)-1] != '/') strcat(new, "/");
		strcat(new, d->d_name);
		new[size] = '\0';
		
		struct stat sbuf;
		if(lstat(new, &sbuf) == -1){
			perror("Unable to execute\n");
			exit(-1);
		};
		// fprintf(stderr, "%s %d\n", new, sbuf.st_mode);
		if(!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")){
			continue;
		}
		// fprintf(stderr, "%s\n", new);
		if(S_ISREG(sbuf.st_mode)){
			total_size += sbuf.st_size;
			// fprintf(stderr, "%s : %lld\n", new, total_size);
		}
		if(S_ISDIR(sbuf.st_mode)){
			int fd[2];
			if(pipe(fd) < 0){
				perror("Unable to execute\n");
				exit(-1);
			}

			int pid = fork();
			if(pid < 0){
				perror("Unable to execute\n");
				exit(-1);
			}
			if(!pid){	// Child Process
				close(fd[0]);
				close(1);
				dup(fd[1]);
				strcpy(argv[1], new);
				if(execv(argv[0], argv))
					perror("Unable to execute\n");
				exit(-1);
			}
			// wait(NULL);
			close(fd[1]);
			close(0);
			dup(fd[0]);

			ll child_size = 0;
			char tmp[20] = {0x0};
			read(0, tmp, 20);
			child_size = strtoll(tmp, NULL, 10);
			total_size += child_size;
		}
		if(S_ISLNK(sbuf.st_mode)){
			// fprintf(stderr, "%s\n", new);

			char newPath[4097];
			ssize_t len = 0;

			while((len = readlink(new, newPath, 4097)) != -1){
				for(int i=len;i<4097;i++){
					newPath[i] = '\0';
				}
				// newPath[len] = '\0';
				// fprintf(stderr, "151 : %s		%s\n", new, newPath);
				int index = 0;
				for(int i=0;i<strlen(new);i++){
					if(new[i] == '/') index = i;
				}
				for(int i=index+1;i<strlen(new);i++){
					new[i] = '\0';
				}
				new[index+1] = '\0';
				strcat(new, newPath);
				new[strlen(new)] = '\0';
			}




			// if(readlink(new, newPath, 4097) == -1){
			// 	perror("Unable to execute\n");
			// 	exit(-1);
			// }
			// char new_path[4097];
			// strcpy(new_path, argv[argc-1]);
			// strcat(new_path, "/");
			// strcat(new_path, newPath);
			// new_path[strlen(new_path)] = '\0';
			// fprintf(stderr, "%s	%s	%s\n", new, newPath, new_path);
			total_size += find_size(new);
		}
		// fprintf(stderr, "%s : %lld\n", new, total_size);
	}

	closedir(dp);
	char tmp[20] = {0x0};
	sprintf(tmp, "%lld\n", total_size);
	int size = 0;
	for(int i=0;i<20;i++){
		if(tmp[i] != '\0') size++;
	}
	write(1, tmp, size);
	exit(0);
}
