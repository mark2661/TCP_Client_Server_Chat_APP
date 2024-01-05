#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "3490"
#define BACKLOG 10
#define MAXBUFFERSIZE 1024


void sigchild_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
    
}

void* get_in_addr(struct sockaddr *sa)
{
    // if ipv4 return ipv4 address
    if(sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    // else return ipv6 address
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
    int sockfd, new_fdA, new_fdB; 
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address info
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my ip

    rv = getaddrinfo(NULL, PORT, &hints, &servinfo);
    if(rv != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
        {
            perror("server: socket");
            continue;
        }

        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        // if you reach here the socket has been initialised correctly (or ran out of addresses to try)
        break;
    }

    freeaddrinfo(servinfo); // this structure is not needed anymore

    if (p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if(listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchild_handler; // kill all zombie child processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    char temp[MAXBUFFERSIZE];
    int conn;
    sin_size = sizeof their_addr;
    while((conn = accept(sockfd, (struct sockaddr*)&their_addr, &sin_size)) != -1) // wrap assignment statment in () since != has higher precedence than =
    {
        
        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        // fork returns 0 for child processes so only child processes will enter this conditional
        if(!fork())
        {
            close(sockfd); // child doesn't need listener anymore
            int r = recv(conn, temp, MAXBUFFERSIZE-1, 0); 
            while(r > 0)
            {
                // Add null terminator to end of string
                temp[r] = 0;
                printf("Server recieved: %s", temp);
                r = recv(conn, temp, MAXBUFFERSIZE - 1, 0);
            }
            if(r == -1)
            {
                perror("recv");
                exit(1);
            }
            close(conn);
            exit(0);
            // close(new_fd); // this will be used by parent
        }
    }
    if(conn != -1) { close(conn); }
    close(sockfd);

    return 0;
}