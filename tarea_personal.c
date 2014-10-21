#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>

#define PORTNUMBER  12346

int main(void){

    char buf[50];
    int s, n, len;
    struct sockaddr_in name;

    /* Create the socket. "We need a place to receive postcards" */
    s = socket(AF_INET, SOCK_DGRAM, 0);

    /* Create the address of the server. "We need an address so other can write us" */
    name.sin_family = AF_INET;
    name.sin_port = htons(PORTNUMBER);
    name.sin_addr.s_addr = htonl(INADDR_ANY); /* Use the wildcard address.*/
    len = sizeof(struct sockaddr_in);

    /* Bind the socket to the address. "We need to place
     * the number in our house, so the mailman can gives us our mail" */
    bind(s, (struct sockaddr *) &name, len);

    /* Read from the socket until end-of-file and
     * print what we get on the standard output.
     * "We receive mail, open it, and show it to everyone" */
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0)
        write(1, buf, n);

    close(s);
    exit(0);
}

