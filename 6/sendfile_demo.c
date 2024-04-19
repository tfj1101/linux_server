#include<sys/sendfile.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<assert.h>
#include<libgen.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<netinet/in.h>
#include<stdlib.h>
int main(int argc,char* argv[])
{   
    if (argc<=3)
    {
        printf("usage :%s ip_address port_number filename\n",basename(argv[0]));
    }
    const char*ip = argv[1];
    int port = atoi(argv[2]);
    const char*file_name = argv[3];
    int filefd = open(file_name,O_RDONLY);
    assert(filefd);

    struct stat stat_buf;
    fstat(filefd,&stat_buf);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port = htons(port);
    int sock = socket(AF_INET,SOCK_STREAM,0);
    bind(sock,(struct sockaddr*)&address,sizeof(address));
    listen(sock,5);
    struct sockaddr_in client;
    socklen_t client_len= sizeof(client);
    int connfd  = accept(sock,(struct sockaddr*)&client,&client_len);
    sendfile(filefd,connfd,NULL,stat_buf.st_size);
    close(connfd);
    close(sock);
    return 0;
}
