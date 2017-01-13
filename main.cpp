#include <iostream>
#include <set>
#include <algorithm>
#include <thread>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>


#define MAX_EVENTS 32 /*может быть любым*/


using namespace std;

int set_nonblock(int fd)
{
    int flags;
#if defined(O_NONBLOCK)
    if((flags=fcntl(fd, F_GETFL, 0)) == -1)
        flags=0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else 
    flags=1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

//char T_OK[]="HTTP/1.0 200 OK\r\nContent-Length:%d\r\nContent-Type:Text/html\r\nConnection: close\r\n\r\n";
char T_OK[]="HTTP/1.0 200 OK\r\n\r\n";
char T_ERR[]= "HTTP/1.0 %s\r\nContent-Length:%d\r\nContent-Type:Text/html\r\nConnection: close\r\n\r\n%s";
char E_400[]="HTTP/1.0 400 Bad Request";
char E_404[]="HTTP/1.0 404 Not Found";
const char bad_request[] =
  "<html>"
  "<head><title>Bad Request</title></head>"
  "<body><h1>400 Bad Request</h1></body>"
  "</html>";
const char not_found[] =
  "<html>"
  "<head><title>Not Found</title></head>"
  "<body><h1>404 Not Found</h1></body>"
  "</html>";




class SlaveWorker
{ 
    char Dir[256];
    int fd;
    int  RecvSize;
    int  iBuf;
    char rBuffer[1024];
    enum STATE
    {
        START,
        GET,
        URL,
        VER,
    }
     state;
     
     int iparam;
     char url[512];

public:   
    SlaveWorker(int _fd, char *_Dir)
    {
        fd=_fd;
        strcpy(Dir, _Dir);
        RecvSize=0;
        iBuf=0;
        state=START;
        iparam=0;
        std::thread th(&SlaveWorker::backround, this);
        th.detach();
    }
    
    int ReadByte()
    {
        if(RecvSize > iBuf)
          {
            return(0x00FF & rBuffer[iBuf++]);
          }
        
        RecvSize=recv(fd, rBuffer, sizeof rBuffer, MSG_NOSIGNAL);
        if((RecvSize<=0) && (errno != EAGAIN))
          {
            int er=errno;
            return-1;
          }
        
{
    int cfd=open("//home//box//cmdstrings.txt", O_RDWR | O_CREAT | O_APPEND, 0666);
    if(cfd>0)
    {
        int lb=strlen(rBuffer);
        write(cfd, rBuffer, lb);
        write(cfd, "\xD\xA", 2);
        close(cfd);
    }
}
        
        
        iBuf=1;
        return(0x00FF & rBuffer[0]);
    }
 
    void SendError(int fd, const char *err_header, const char *err_body)
    {
           char AnswBuf[1024];
           sprintf(AnswBuf, T_ERR, err_header, strlen(err_body), err_body);
           send(fd, AnswBuf, strlen(AnswBuf), MSG_NOSIGNAL);
    }
    
    void SendFile(int fd, int pfd, int size)
    {
           char *pAnswBuf=new char[size+256];
           //sprintf(pAnswBuf, T_OK, size);
           strcpy(pAnswBuf, T_OK);           
           read(pfd, pAnswBuf+strlen(pAnswBuf), size);
           send(fd, pAnswBuf, strlen(pAnswBuf), MSG_NOSIGNAL);
           //sendfile(fd, pfd, 0, size);
           delete []pAnswBuf;
    }
    
    void backround()
    {
       char bb=0;
       while((bb=ReadByte())>=0)
        {
          switch(state)
          {   
       case START:
            if((bb==' ') ||(bb=='\t'))
               continue;
            if( (bb | 0x20) == 'g')
            {
             if((bb=ReadByte())<0) goto ex;
             if( (bb | 0x20) != 'e')
               continue;
             if((bb=ReadByte())<0) goto ex;
             if( (bb | 0x20) != 't')
               continue;
             state=GET;
            }
            break;
       case GET:
           if((bb==' ') ||(bb=='\t'))
               continue;
           state=URL;
           url[0]=bb;
           iparam=1;
           url[iparam]=0;
           break;
       case URL:
           if((bb==' ') || (bb=='\t') || (bb=='?'))
           {
               state=VER;
               continue;
           }
           url[iparam++]=bb;
           url[iparam]=0;
           break;
       case VER:
           char *ref=0;
           if(url[0]=='\"')
           {
               ref=strchr(&url[1], '\"');
               if(ref)
                   *ref=0;
               strcpy(url, &url[1]);
           }
           char tt[]="http://";
           if(strncmp(url, tt, 7)==0)
           {
            ref=strchr(&url[8], '/');  
            if(ref)
                strcpy(url, ref);
           }
           
           
           if(strcmp(url, "/")==0)
             strcpy(url, "/index.html");
           RecvSize=0;
           iBuf=0;
           
           char Filename[512];
           strcat(strcpy(Filename, Dir), url);
           int pfd=open(Filename, O_RDONLY);
           struct stat st;
           if((pfd>0) && (fstat(pfd, &st)!=-1))
           {
              SendFile(fd, pfd, st.st_size);
              close(pfd);
           }
           else //нет такого файла
                SendError(fd, E_404, not_found);
           goto ex;
          }         
        }
 ex:       
        shutdown(fd, SHUT_RDWR);
        close(fd);
        delete this;       
    }
    
};

bool pExit=false;

int Server(char *addr, int port, char *dir)
{
    int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    //std::set<int> SlaveSockets;
    

    struct sockaddr_in SockAddr;
    inet_aton(addr, &SockAddr.sin_addr); 
    SockAddr.sin_addr.s_addr= htonl(SockAddr.sin_addr.s_addr);
    SockAddr.sin_family = AF_INET;
    SockAddr.sin_port= htons(port);

    bind(MasterSocket, (struct sockaddr*)&SockAddr, sizeof(struct sockaddr));
    
    set_nonblock(MasterSocket);
    listen(MasterSocket, SOMAXCONN);
    
    int Epoll= epoll_create1(0);
    struct epoll_event Event;
    Event.data.fd = MasterSocket;
    Event.events=EPOLLIN;
    epoll_ctl(Epoll, EPOLL_CTL_ADD, MasterSocket, &Event);
    
    while(true)
    {
        struct epoll_event Events[MAX_EVENTS];
        int N= epoll_wait(Epoll, Events, MAX_EVENTS, -1);  //ждем новых подключений (-1)бесконечно 
        //std::cout << "N=" << N << endl;
        //дождались
        for(int ii=0; ii<N; ii++)
        {
              if(Events[ii].data.fd==MasterSocket) 
              {//серверный socket принимает вызов
                   int SlaveSocket= accept(MasterSocket, 0, 0);
                   if (SlaveSocket>0)
                     {
                     //set_nonblock(SlaveSocket);
                     new SlaveWorker(SlaveSocket, dir);
                     /*
                     struct epoll_event Event;
                     Event.data.fd = SlaveSocket;
                     Event.events=EPOLLIN;
                     epoll_ctl(Epoll, EPOLL_CTL_ADD, SlaveSocket, &Event);
                      */
                     }
              }

         }
    }
    
}



int main(int argc, char** argv) 
{
    char addr[128]="0.0.0.0"; 
    int  port=80;
    char dir[256]="";
       
    //std::cout << "начали" << endl;
    int opt;
    while((opt=getopt(argc, argv, "h:p:d:")) != -1)
        switch(opt)
        {
            case 'h':
                if(optarg)
                  strcpy(addr, optarg);
                break;
                            
            case 'p':
                if(optarg)
                  port=atoi(optarg);
                break;
                
            case 'd':
                if(optarg)
                  strcpy(dir, optarg);
                int len=strlen(dir);
                if(dir[len-1]=='/')
                  dir[len-1]=0;
                 break;
        }
        
      
//   Server(addr, port, dir); exit(0);
        
    if(fork()==0)
    {
    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    Server(addr, port, dir);
    }

    return 0;
}
