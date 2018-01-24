#define _GNU_SOURCE
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include "utilities.h"


#define MAX 256 /* max number of bytes we can get at once */
#define BACKLOG 10


#define MAX_HANGMEN_WORDS 288
#define ERROR -1
#define DEFAULT_PORT 12345



struct auth_user *auth_user_first = NULL;
struct auth_user *auth_user_last = NULL;

struct Hang *Hang_first = NULL;
struct Hang *Hang_last = NULL;

struct Leaderboard *Leaderboard_first =  NULL;
struct Leaderboard *Leaderboard_last = NULL;

typedef enum {
    _,
    PLAY_HANGMAN,
    SHOW_LEADERBOARD,
    QUIT
} Menu;

char **users;
char **passwords;

char **word1;

int won = 0;
int played = 0;
int local = 1;
//listen socket
int sockfd;

//running the server side game
int hangman(int socket_id, char* username){


	uint16_t converter;
  //gets random word from linked list
  char *results = printRandom(Hang_first);

  char str[200];
  char* word = results;
  char *ptr;

  //splits the word where the , is
  strcpy (str, word);
  strtok_r (str, ",", &ptr);

  //the two words will try and make it better later
	char* word1 = str;
	char* word2 = ptr;

  //prints it out for debugging
	printf("Word1 is %s and word2 is %s \n", word1, word2);
	printf("Word1 length is %zu and word2 length is %zu \n", strlen(word1), strlen(word2));

	//so many error
	char* client_guessed_letters = calloc(MAX, sizeof(char));

	//number of guesses left
	int num_guesses = guesses_min(strlen(word1) + strlen(word2) + 10);

	char cword1[MAX];
	char cword2[MAX];

	for(int i = 0; i < strlen(word1); i++){
		cword1[i] = 95;
	}
	cword1[strlen(word1)] = '\0';

	for(int i = 0; i < strlen(word2); i++){
		cword2[i] = 95;
	}
	cword2[strlen(word2)] = '\0';

	//initialize client guessed single letter
	char recv_letter;

	int GameOver = 0;
	int winner = 0;


	while(1){

		//gameover man
		if(num_guesses <= 0){
			GameOver = 1;
		}

		//checking if words are the same will fix later bad bad code
		if(strcmp(word1, cword1) == 0 && strcmp(word2, cword2) == 0){
			winner = 1;
		}

		//Game to server
		converter = htons(GameOver);
		if(send(socket_id, &converter, sizeof(uint16_t), 0) <= 0){
			return ERROR;
		}
    //winner to server
		converter = htons(winner);
		if(send(socket_id, &converter, sizeof(uint16_t), 0) <= 0){
			return ERROR;
		}

		//send guessed letterd
		if(send(socket_id, client_guessed_letters, MAX, 0) <= 0){
			return ERROR;
		}

		//how many
		converter = htons(num_guesses);
		if(send(socket_id, &converter, sizeof(uint16_t), 0) <= 0){
			return ERROR;
		}

		//send words need to get rid of double loop
		if(send(socket_id, cword1, MAX, 0) <= 0){
			return ERROR;
		}
		if(send(socket_id, cword2, MAX, 0) <= 0){
			return ERROR;
		}

		//winning adds score to scoreboard and breaks
		if(winner){
			printf("\nPlayer %s win the game!\n", username);
      add_board(username, 1, 1);
			break;
		}

		//game over add score to board and break
		if(GameOver){
			printf("\nPlayer %s lose the game!\n", username);
      add_board(username, 0, 1);
			break;
		}

		//looping thru
		if(recv(socket_id, &recv_letter, sizeof(char), 0) <= 0){
			return ERROR;
		}


		num_guesses--;

		//add in
		if(strchr(client_guessed_letters, recv_letter) == NULL){
			strcat(client_guessed_letters, &recv_letter);
		}

		//put in guesses need to get rid of this double loop
		for(int i = 0; i < strlen(word1); i++){
			if (word1[i] == recv_letter){
				cword1[i] = word1[i];
			}
		}

		for(int i = 0; i < strlen(word2); i++){
			if(word2[i] == recv_letter){
				cword2[i] = word2[i];
			}
		}

	}
	return 0;
}


void Game_play(int socket_id, char* username) {
    int input;
    do { //the game menu - after the login screen
        switch ((input = menu_input(socket_id,username))) {
            case PLAY_HANGMAN:
                //plays the game
                hangman(socket_id,username);
                //for displaying nothing on client side
                local = 0;
                break;
            case SHOW_LEADERBOARD:
                //sends score
                SendScore(socket_id);
                //prints linked list
                print_list_board();
                break;
        }
    } while (input != QUIT); //quit if 3 is pressed
}

//gets client input
int menu_input(int socket_id, char* username) {
		uint16_t converter;
		int input = 0;
		if (recv(socket_id, &converter, sizeof(uint16_t), 0) <= 0){
			return;
		}
		input = ntohs(converter);
    //debugging
		printf("User menu option : %d \n", input);

    return input;
}

void SendScore(int socket_id){
  uint16_t stuff;
  //if nothing in linked list show nothing
  if(local == 1){
    printf("nothing\n");
  } else{
  //send the how many game played over
  stuff = htons(Leaderboard_last->played);
  if(send(socket_id, &stuff, sizeof(uint16_t), 0) <= 0){
    return;
  }
  //send how many games won over
  stuff = htons(Leaderboard_last->won);
  if(send(socket_id, &stuff, sizeof(uint16_t), 0) <= 0){
    return;
  }
  }
}

int guesses_min(int words_length){
	if (words_length <= 26){
		return words_length;
	} else {
		return 26;
	}
}
int main(int argc, char *argv[]) {


	int new_fd;  /* listen on sock_fd, new connection on new_fd */
	struct sockaddr_in my_addr;    /* my address information */
	struct sockaddr_in their_addr; /* connector's address information */
	socklen_t sin_size;

	/* Get port number for server to listen on */
	if (argc > 2) {
		fprintf(stderr,"usage: client port_number\n");
		exit(1);
	}

  //default port
	int port_num;
	if (argc == 1){
		port_num = DEFAULT_PORT;
	} else {
		port_num = atoi(argv[1]);
	}


	/* generate the socket */
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}


	int reuseaddr=1;
	if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&reuseaddr,sizeof(reuseaddr))==-1) {
	    perror("setsockopt");
	}

	/* generate the end point */
	my_addr.sin_family = AF_INET;         /* host byte order */
	my_addr.sin_port = htons(port_num);     /* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY; /* auto-fill with my IP */
		/* bzero(&(my_addr.sin_zero), 8);   ZJL*/     /* zero the rest of the struct */

	/* bind the socket to the end point */
	if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) \
	== -1) {
		perror("bind");
		exit(1);
	}

	/* start listnening */
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}


	printf("Server starts listnening ...\n");

	/* repeat: accept, send, close the connection */
	/* for every accepted connection, use a sepetate process or thread to serve it */
	while(1) {  /* main accept() loop */
		sin_size = sizeof(struct sockaddr_in);
		if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, \
		&sin_size)) == -1) {
			perror("accept");
			continue;
		}
		printf("server: got connection from %s\n", \
			inet_ntoa(their_addr.sin_addr));
      //read the auth users in
			read_file();
      //read all the hangman words in
			read_file_word();

      //login stuff
			char username[20];
			char password[20];
			char buffer[MAX];
			memset(buffer,0,MAX);
      //get client input
			int8_t un_len = recv(new_fd,username, 20,0);
			int8_t pin_len = recv(new_fd,password, 20,0);

      //checks if found or not
			int8_t ind = searchn(auth_user_first,username,password);
			printf("%d\n",ind );
			bool logged_on = false;

			if (ind != 0){
				logged_on = true;
			}
			if (logged_on){
				if (send(new_fd, "Auth", 20, 0) == -1) perror("send");
					while(1){
          //game
					Game_play(new_fd, username);
		}
	}
		else {
      if (send(new_fd, "UnAuth,",20,0) == -1) perror("send");
    }

	}
}
