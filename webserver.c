#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>/* for the waitpid() system call */
#include <signal.h>/* signal name macros, and the kill() prototype */

typedef int bool;
#define true 1
#define false 0

const char* MSG_200 ="HTTP/1.1 200 OK\r\n\
Connection: Keep-Alive\r\n";

const char* MSG_404 = "HTTP/1.1 404 Not Found\r\n\
Content-type: text/html\r\n\
Content-length: 112\r\n\
Connection: close\r\n\
\r\n\
<html><head><title>Not Found</title></head><body>\r\n\
The requested URL was not found on this server\r\n\
</body><html>";

const char* TEXTHTML = "Content-type: text/html\r\n";
const char* JPEG = "Content-type: image/jpeg\r\n";
const char* GIF = "Content-type: image/gif\r\n";
const char* OTHER = "Content-type: application/octet-stream\r\n";

const int MAXBUFSIZE = 4096; //1 << 12
const int MAXFILENAMESIZE = 256;

void error(char *msg)
{
  perror(msg);
  exit(1);
}

void handle_request(int socket) {
  
    int n; 
    char request[MAXBUFSIZE];
    char file_size_str[30]; //cannot exceed size of 2^14 bytes
    
    memset(request, 0, MAXBUFSIZE); //reset memory
    memset(file_size_str, 0, 10);

    //read request message
    n = read(socket, request, MAXBUFSIZE - 1);
    if (n < 0)
      error("ERROR reading from socket");

    printf("%s", request);
    //Do some things.

    //Parse the filename from request message
    char filename[MAXFILENAMESIZE];
    int pos = 0;
    
    int i;
    bool first_blank = false; //tells us when filename starts
    for (i = 0; i != MAXBUFSIZE; i++) {
      if (!first_blank) {
	if (request[i] == ' ')
 	  first_blank = true;
	continue;
      }
      else {
	if (request[i] == ' ') //hit end of filename
	  break;
	filename[pos] = request[i];
      	pos++;
      }
    }
    
    filename[pos] = '\0';

    //Get rid of initial '/'
    for (i = 0; i != MAXFILENAMESIZE - 1; i++)
      filename[i] = filename[i+1];

    //Determine content type
    char* exten = strrchr (filename, '.');
    exten = exten + 1;              //do not include '.'

    //Determine if it is a valid filename
    int file_fd;
    file_fd = open(filename, O_RDONLY);
    if (file_fd == -1) //if file does not exist, print 404
      n = write(socket, MSG_404, strlen(MSG_404));
    
    //Find size of the file
    int beginning = lseek(file_fd, 0, SEEK_CUR);
    int file_length = lseek(file_fd, 0, SEEK_END);
    lseek(file_fd, beginning, SEEK_SET);    

    //Load requested file into memory to send to socket
    char* response_body = (char*) malloc(file_length*sizeof(char));
    
    n = read(file_fd, response_body, file_length);
    if (n < 0)
      error("ERROR reading file");
        
    //Create Content-length header line
    sprintf(file_size_str, "Content-length: %i\r\n\r\n", file_length);

    //Send the response message
    n = write(socket, MSG_200, strlen(MSG_200));
    if (n < 0)
      error("ERROR writing to socket");

    //Content-type
    if (strcmp(exten, "html") == 0 || strcmp(exten, "txt") == 0)
      n = write(socket, TEXTHTML, strlen(TEXTHTML));
    else if (strcmp(exten, "jpeg") == 0)
      n = write(socket, JPEG, strlen(JPEG)); 
    else if (strcmp(exten, "gif") == 0)
      n = write(socket, GIF, strlen(GIF));
    else
      n = write(socket, OTHER, strlen(OTHER)); //octet is default for all other file types
    if (n < 0)
      error("ERROR writing to socket");

    //Content-length
    n = write(socket, file_size_str, strlen(file_size_str));
    if (n < 0)
      error("ERROR writing to socket");
    
    //Response body
    n = write(socket, response_body, file_length);
    if (n < 0)
      error("ERROR writing to socket");
    
    close(file_fd);

    free(response_body);
    
    //Close the connection
    close(socket);
    fflush(stdout);
    exit(0);
}

//Handles SIGCHLD
static void sigchld_hdl (int sig)
{
  while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int main()
{
  int sockfd, newsockfd, serverPort;
  socklen_t clilen;
  struct sockaddr_in serv_addr, cli_addr;
  pid_t pid;

  serverPort = 1029;
  
  sockfd = socket(AF_INET, SOCK_STREAM, 0);//create socket
  if (sockfd < 0)
    error("ERROR opening socket");
  memset((char *) &serv_addr, 0, sizeof(serv_addr));//reset memory

  //fill in address info
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(serverPort);

  
  //Handle zombie child processes 
  struct sigaction act;
  memset (&act, 0, sizeof(act));
  act.sa_handler = sigchld_hdl;

  act.sa_flags = SA_RESTART;

  if (sigaction(SIGCHLD, &act, 0)) {
    perror ("sigaction");
    return 1;
    }

  
  //bind the socket to the port address
  if (bind(sockfd, (struct sockaddr *) &serv_addr,
	   sizeof(serv_addr)) < 0)
    error("ERROR on binding");

  listen(sockfd, 5);

  //always listen for requests
  while (1) {
  
  //create a new port for each new connection accepted
  clilen = sizeof(cli_addr);
  newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

  if (newsockfd < 0)
    error("ERROR on accept");

  
  pid = fork(); //fork a new child process to handle the connection
  if (pid < 0)
    error("ERROR on fork");
  else if (pid == 0) {            //process successfully created
    close(sockfd);   //child process does not need listening socket
    handle_request(newsockfd);  
  }
  else
    close(newsockfd); //close connection
  
  }
  
  return 0;
}


