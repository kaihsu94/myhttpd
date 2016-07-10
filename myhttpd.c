#define	BUF_LEN	8192
#define IN_BUF 2048
#define TIME_BUF 128
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include	<netdb.h>
#include	<netinet/in.h>
#include	<inttypes.h>

#include    <pthread.h>
#include    <semaphore.h>
#include    <fcntl.h>
#include    <time.h>
#include    <sys/sendfile.h>
#include    <unistd.h>
#include    <sys/stat.h>
#include    <stdbool.h>

char *progname;
char buf[BUF_LEN];

void help();
void usage();
int setup_client();
//int setup_server();
void setup_server();



int s, sock,newsock,ch, server, done, bytes, aflg;
socklen_t clientlen;
int soctype = SOCK_STREAM;
char *host = NULL;

extern char *optarg;
extern int optind;

char *logfile_name = NULL;
char *port = NULL;
char *dir = NULL;
int schedtime;
int threadnum;
char *schedtype = NULL;
int schedflag;
char workingdir[1024];
int debug;

sem_t sem;
//Helper functions/Vars for strings/characters
int findnextchar(char,char*,int);
char get_filename(char*);
void writeLog(char*);
void writeDebug(char*);
char invalid_getrequest[]="server:Invalid GET Request Example: Get 1.html HTTP/1.1 \n";
char invalid_headrequest[]="server:Invalid HEAD Request Example: Head 2.html HTTP/1.1 \n";
char invalid_request[]="server:Server only accepts Get and Head requests\n";
char invalid_file[] = "server:Unable to find file\n";
void GET_response();
void HEAD_response();
time_t t;
pid_t pid;

//Log File Variables
FILE *logfile;
char NOTSET[6] = "NOTSET";
int zero = 0;
int status_code = 0;


//Thread
pthread_t listen_t,scheduler_t,worker_t;
pthread_t *workers;
void *listener(void*);
void *worker();
void *scheduler(void*);
struct data msg_to_be_scheduled;


struct sockaddr_in serv, remote;
struct servent *se;
int newsock, len;

fd_set ready;
struct sockaddr_in msgfrom;
struct sockaddr_in serv;
int msgsize;

//Locks
pthread_mutex_t queLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t schedMutex=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t workercv = PTHREAD_COND_INITIALIZER;

struct data {
	/*
	Other bits of data to include here?
	time, ipaddress ... get from server or set up
	*/
  int id;
  int size;
  char time_arrived[TIME_BUF];
  char file_name[IN_BUF];
  char message[BUF_LEN]; //is this large enough
  char ip_address[BUF_LEN];
  struct data * next;
};


struct data *head = NULL;
struct data *curr = NULL;
struct data* get_head(){
    return head;
}
struct data* get_tail(){
    return curr;
}
void print_list(void){
    struct data *ptr = head;
    while(ptr != NULL){
        printf("%d, %d, %s, %s, %s %s\n", ptr->id, ptr->size, ptr->time_arrived, ptr->file_name, ptr->ip_address, ptr->message);
        ptr = ptr->next;
    }
    return;
}
struct data* create_list(int id,int size,char* time_arrived, char* file_name, char* message, char* ip){
    printf("\n Creating new list with head node message %s \n", message);

    struct data *ptr = (struct data*)malloc(sizeof(struct data));

    if (NULL == ptr){
        printf("\n List creation failed");
    }
    char timenow[TIME_BUF];
    char file[IN_BUF];
    char msg[IN_BUF];
    char ipa[BUF_LEN];
    strcpy(timenow,time_arrived);
    strcpy(file,file_name);
    strcpy(msg,message);
    strcpy(ipa, ip);
    // sets node data struct to these
    ptr->id= id;
    ptr->size =  size;
    strcpy(ptr->time_arrived,timenow);
    strcpy(ptr->file_name, file);
    strcpy(ptr->message, msg);
    strcpy(ptr->ip_address, ipa);
    ptr->next = NULL;

    head = curr = ptr;
    //queue = &head;

    return ptr;
    }
int find_shortest_job(struct data **prev){

    struct data *ptr = head;
    struct data *tmp = NULL;
    struct data *holder = head;
    int SJF_ID;
    int minsize;
    char* buf;

    if( ptr == NULL){
        printf("queue is empty! cannot find SJF");
        return 0;
    }
	if( head->next == NULL){
return head->id;
}
    SJF_ID = ptr->id; // sets shortest job id to head
    minsize = ptr->size; // sets minimun size to this
    //printf("Printlist function follows\n");

    ptr = ptr->next; // moves pointer
    if ( ptr->next == NULL){
        return ptr->id; //only job in queue
    }
    while (ptr != NULL) {
      //printf("IN SJF: minsize is: %d\n", minsize);

      if (ptr->size < minsize ) {
	minsize = ptr->size;
        SJF_ID =  ptr->id;
        tmp = ptr;
        ptr = ptr->next;
      }
      else {

	tmp = ptr;
	ptr = ptr->next;
      }
    }

    if (prev) {
      //*prev = tmp;
      *prev = holder;
      //print_list();
      return  SJF_ID;
    }
    else {
      return 0;
    }

}
struct data* add_node(int id,int size,char* time_arrived, char* file_name, char* message,char* ip) {
	if (head == NULL) {
		return(create_list(id,size,time_arrived,file_name,message, ip));
	}
    printf("\n adding new msgnode with msg [%s] \n ", message);
	struct data *ptr = (struct data*)malloc(sizeof(struct data));
	if (ptr == NULL) {
		printf("MsgNode build failure");
		return NULL;
	}
	char timenow[TIME_BUF];
    char file[IN_BUF];
    char msg[IN_BUF];
    char ipa[BUF_LEN];
    strcpy(file,file_name);
    strcpy(msg,message);
    strcpy(timenow,time_arrived);
    strcpy(ipa,ip);

    // sets node data struct to these
    ptr->id = id;
    ptr->size =  size;
    strcpy(ptr->time_arrived,timenow);
    strcpy(ptr->file_name, file);
    strcpy(ptr->message, msg);
    strcpy(ptr->ip_address, ipa);


	ptr->next = NULL;
    // assume we only add to end of queue
	curr->next = ptr;
	curr = ptr;
	return ptr;
}
struct data pop_msg (struct data **head ){ //**head

    struct data request_message; // container
    //struct data *tmp = *head;
    struct data *tmp = NULL;
    //if (tmp == NULL){ return;}
    if(*head == NULL){return;}
    tmp = (*head)->next;
    request_message.size = (*head)->size;
    request_message.id = (*head)->id;
    strcpy(request_message.time_arrived, (*head)->time_arrived);
    strcpy(request_message.file_name, (*head)->file_name);
    strcpy(request_message.message, (*head)->message);
    strcpy(request_message.ip_address, (*head)->ip_address);
    // copied all info into to be returned
    free(*head);
    *head = tmp;
    /*
    *head = tmp->next; // head is now head's next node
    free(tmp); // free memory space of old head.
    */
    return request_message;

}

struct data get_message(int val){//, struct data *head){
    struct data *prev, *curr;
    struct data request_message;
    curr = head;
    while(curr != NULL){
        if(curr->id == val){
        //if(curr->next == NULL){printf("BBBB"); break;}
            if(curr == head){
                head=curr->next;
                //if(head == NULL){printf("AAAAAA");}
                printf("Was head\n");
                request_message.id = curr->id;
                request_message.size = curr->size;
                strcpy(request_message.file_name, curr->file_name);
                strcpy(request_message.message, curr->message);
                strcpy(request_message.time_arrived, curr->time_arrived);
		strcpy(request_message.ip_address, curr->ip_address);
                free(curr);

                return request_message;
            }
            else if (curr->next == NULL){
               // free(curr);
                //curr=NULL;
                //printf("end case");
                //  prev->next = NULL;
                request_message.id = curr->id;
                request_message.size = curr->size;
                strcpy(request_message.file_name, curr->file_name);
                strcpy(request_message.message, curr->message);
                strcpy(request_message.time_arrived, curr->time_arrived);

                free(prev->next);
                prev->next = NULL;


                printf("ssss %d \n", curr->id);
                return request_message;
            }
            else if (curr->next != NULL){
                printf("middle case");
                request_message.id = curr->id;
                request_message.size = curr->size;
                strcpy(request_message.file_name, curr->file_name);
                strcpy(request_message.message, curr->message);
                strcpy(request_message.time_arrived, curr->time_arrived);
                prev->next = curr->next;

                free(curr);
                //printf("ssss %d \n", curr->id);
                return request_message;
            }
        }
        else{
           //printf("i got here");
            prev=curr;
            curr= curr->next;
        }
    }
}







int
main(int argc,char *argv[])
{
    server = 1;
        //fd_set = 1; EDIT--fd_set is type changed ready type of fd_set to 1 instead
        //ready = 1;
        struct sockaddr_in msgfrom;
        struct sockaddr_in serv;
        int msgsize;

	union {
		uint32_t addr;
		char bytes[4];
	} fromaddr;

	/*
        Set Program Defaults before getting arguments
        Notes: Mitcht
        I changed the original program parameters to what our server needs.
        So far this is functioning correctly
        I removed the client code mainly because we just don't need it
        since testing we need to use the following
        telnet timberlake.cse.buffalo.edu Port
        This does the same thing as the client did.
        The program will run without any parameters or any number of them.
        Compile the same way that was shown on piaza.
	*/

	port = "8080";
	schedtime = 60;
	schedtype = "FIFO";
	debug = 0;
	dir = "/"; //Sets dir to process current working directory.
	threadnum = 4;
	pthread_t workers[threadnum];


	if ((progname = rindex(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;
	while ((ch = getopt(argc, argv, "dhl:p:r:t:n:s:")) != -1)
		switch(ch) {
			case 'd':
			    debug = 1;
			    fprintf(stderr,"Debug Enabled \n");
				break;
			case 'h':
			    help();
				break;
			case 'l':
			    logfile_name = optarg;
			    fprintf(stderr,"Logging enabled in %s \n",logfile_name);
			    //FILE *log;
			    //log = fopen(file,)
				break;
			case 'p':
                port = optarg;
				break;
			case 'r':
			    dir = optarg;
			    if(chdir(dir) == -1){
                    fprintf(stderr,"Unable to change working directory to %s\n",dir);
                    fprintf(stderr,"Current working dir is %s\n",getcwd(workingdir,1024));
			    }else{
                    fprintf(stderr,"Current working dir is %s\n",getcwd(workingdir,1024));
			    }
                break;
            case 't':
                schedtime = atoi(optarg);
                break;
            case 'n':
			    threadnum = atoi(optarg);
			    break;
            case 's':
                schedtype = optarg;
                if(strcmp("FIFO", schedtype) == 0){
                    schedflag = 0;
                }else if(strcmp("SJF", schedtype) == 0){
                    schedflag = 1;
                }
                break;
			default:
				usage();
		}
	argc -= optind;
	if (argc != 0)
		usage();


		//Print statements to make confirm program config
		fprintf(stderr,"Schedule time is %i \n", schedtime);
		fprintf(stderr,"Number of threads %i \n", threadnum);
		fprintf(stderr,"Schedule Type is %s \n",schedtype);

		if(debug == 0){
		close(STDOUT_FILENO);
		}



	//Initialize Locks
    //pthread_mutex_init(&queLock,NULL);
    //pthread_mutex_init(&schedMutex,NULL);



/*
 * Create socket on Server.
 Notes: Mitcht - Im not sure if we need multithreading here.
 This is where we create a socket on the server and actually setup the server.
 */
 //Setup server listening on port and waiting for a connection
 //Once Connection is obtained fork process and child process will handle request
 //Main process continues to listen
    s = socket(AF_INET,soctype,0);
    if(s < 0){
        perror("socket");
        exit(1);
    }else{
        //setup_server();



	len = sizeof(remote);
	memset((void *)&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	if (port == NULL)
		serv.sin_port = htons(0);
	else if (isdigit(*port))
		serv.sin_port = htons(atoi(port));
	else {
		if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) {
			perror(port);
			exit(1);
		}
		serv.sin_port = se->s_port;
	}
	if (bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
		perror("bind");
		exit(1);
	}
	if (getsockname(s, (struct sockaddr *) &remote, &len) < 0) {
		perror("getsockname");
		exit(1);fprintf(stderr, "In For Loop\n");
	}
	fprintf(stderr, "Port number is %d\n", ntohs(remote.sin_port));

    }
    sem_init(&sem,0,threadnum);

    //pthread_create(&worker_t,NULL,worker,NULL);
    int i = 0;


    for(i; i<threadnum;i++){
        pthread_create(&workers[i],NULL,&worker,NULL);
        fprintf(stderr,"%i: New Worker Thread Added \n",i);
    }
    pthread_create(&listen_t,NULL,listener,&s);
    sleep(schedtime);
    pthread_create(&scheduler_t,NULL,scheduler,&schedflag);
    //Initialize worker thread array
    //pthread_t workers[threadnum];
    pthread_join(listen_t,NULL);
    pthread_join(scheduler_t,NULL);
    //int j = 0;
    //for(j;j<threadnum;j++){
    //    pthread_join(workers[i],NULL);
    //}

    pthread_exit(&listen_t);

	return(0);
}

/*
 * setup_client() - set up socket for the mode of soc running as a
 *		client connecting to a port on a remote machine.
 */

int
setup_client() {

	struct hostent *hp, *gethostbyname();
	struct sockaddr_in serv;
	struct servent *se;

/*
 * Look up name of remote machine, getting its address.
 */
	if ((hp = gethostbyname(host)) == NULL) {
		fprintf(stderr, "%s: %s unknown host\n", progname, host);
		exit(1);
	}
/*
 * Set up the information needed for the socket to be bound to a socket on
 * a remote host.  Needs address family to use, the address of the remote
 * host (obtained above), and the port on the remote host to connect to.
 */
	serv.sin_family = AF_INET;
	memcpy(&serv.sin_addr, hp->h_addr, hp->h_length);
	if (isdigit(*port))
		serv.sin_port = htons(atoi(port));
	else {
		if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) {
			perror(port);
			exit(1);
		}
		serv.sin_port = se->s_port;
	}
/*
 * Try to connect the sockets...
 */
	if (connect(s, (struct sockaddr *) &serv, sizeof(serv)) < 0) {
		perror("connect");
		exit(1);
	} else
		fprintf(stderr, "Connected...\n");
	return(s);
}

/*
 * setup_server() - set up socket for mode of soc running as a server.
 */

void
setup_server() {
	struct sockaddr_in serv, remote;
	struct servent *se;
	int newsock, len;

	len = sizeof(remote);
	memset((void *)&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	if (port == NULL)
		serv.sin_port = htons(0);
	else if (isdigit(*port))
		serv.sin_port = htons(atoi(port));
	else {
		if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) {
			perror(port);
			exit(1);
		}
		serv.sin_port = se->s_port;
	}
	if (bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
		perror("bind");
		exit(1);
	}
	if (getsockname(s, (struct sockaddr *) &remote, &len) < 0) {
		perror("getsockname");
		exit(1);fprintf(stderr, "In For Loop\n");
	}
	fprintf(stderr, "Port number is %d\n", ntohs(remote.sin_port));

	//Listener thread here

	/*if(debug == 0){
        //Debug is not on , run normally.
        listen(s, 1);
        newsock = s;
        if (soctype == SOCK_STREAM) {
            fprintf(stderr, "Entering accept() waiting for connection.\n");
            fprintf(stderr, "Waiting For Connection... \n");
            //Accept First connections
            //Daemonize
            //pid_t pid;
            //Set Current Working DIR for Daemon process to Root
            //pid = fork();
            //if(pid != 0){
            //chdir(dir);
            //}
            //Force Daemon Process to sit in while loop accepting connections
            //Forked process should proceed on to read messages
            //while(pid != 0){
            //#######################################
            //Instead of forking force listener thread to sit in loop here on accept??
            //#######################################
            newsock = accept(s, (struct sockaddr *) &remote, &len);
            //daemon(0,0);
            //fork();
            fprintf(stderr, "New Client Connected \n");
            //}
        }
	}else{
	    //Debug on
	    fprintf(stderr, "Debug Enabled\n");
	    listen(s, 1);
        newsock = s;
        if (soctype == SOCK_STREAM) {
            fprintf(stderr, "Entering accept() waiting for connection.\n");
            fprintf(stderr, "Waiting For Connection... \n");
            //Accept First connections
            newsock = accept(s, (struct sockaddr *) &remote, &len);
            fprintf(stderr, "New Client Connected \n");
        }
        //Run as foreground process
	}
	*/

	//Maybe Start forking here

	//return(newsock);
}

/*
 * usage - print usage string and exit
 */
void
GET_response(char msg[],int fd){
    int sock = fd;

    char *buf = msg;

    //file variables
    int line_count;
    FILE *f;
    char *buffer[255];
    char file_name[1024];
    time_t *last_modified;
    struct stat stat_info;



    int start = findnextchar(' ',buf,0);
    int end = findnextchar(' ',buf,start + 1);
    int i = start + 1;
    int j = 0;
    if(end == -1 || start == -1){
        //Not a valid format for Get Request
        write(sock,invalid_getrequest,sizeof(invalid_getrequest)-1);
        status_code = 400;
    }else{
        for(i;i<end;i++){
                file_name[j] = buf[i];
                j++;
        }
                //Open File for reading
                f = fopen(file_name,"r");
                if(f == NULL){
                    write(sock,invalid_file,sizeof(invalid_file));
                    status_code = 400;
                }else{
                    //Get Line Count
                    int line = fgetc(f);
                    int flag = 0;
                    while(flag = 0){
                        if(line == '\n'){
                            line_count++;
                        }
                        if(line == EOF){
                            flag = 1;
                        }
                        fprintf(stderr,"line_count = %i\n",line_count);
                    }
                    status_code = 200;
                    fclose(f);
                    //Get Last Modified Date

                    //Open File for Reading again
                    //Could just reset fd , not sure how though.
                    fopen(file_name,"r");

                    //Get Header response
                    write(sock,"\n",1);
                    write(sock,"Server: myhttpd Web Server v1.0\n",28);
                    write(sock,"HTTP/1.1 200 OK\n",16);
                    write(sock,"Last Modified: ",15);
                    //write(sock,gmtime(last_modified),sizeof(gmtime(last_modified)));
                    write(sock,"Content-Type: text/html\n",24);
                    write(sock,"\n",1);
                    write(sock,"Content-Length: ",16);
                    //write(sock,line_count,sizeof(line_count));
                    write(sock,"\n",1);
                    //Read Contents of File and send
                    sendfile(sock,fileno(f),NULL,sizeof(buffer));
                    write(sock,"\n",1);
                    fclose(f);

                }
        }
}
void
HEAD_response(char msg[],int fd){

    int sock = fd;

    char *buf = msg;
        //file variables
    int line_count;
    FILE *f;
    char *buffer[255];
    char file_name[1024];
    char stat_cmd[255] = "stat ";
    time_t *last_modified;
    struct stat stat_info;
    char *access_time = malloc(1024);
    char *modified_time = malloc(1024);
    char *change_time = malloc(1024);

    char stat_buf[2048];
    time_t atime,ctime, mtime;
    struct tm * atimeinfo, * ctimeinfo, * mtimeinfo;
    char tmpbufa[128],tmpbufc[128],tmpbufm[128];


    //Get File Name

    int start = findnextchar(' ',buf,0);
    int end = findnextchar(' ',buf,start + 1);
    int i = start + 1;
    int j = 0;
    if(end == -1 || start == -1){
        //Not a valid format for Get Request
        write(sock,invalid_headrequest,sizeof(invalid_headrequest)-1);
        status_code = 400;
    }else{
        for(i;i<end;i++){
                file_name[j] = buf[i];
                j++;
        }
                //Open File for reading
                f = fopen(file_name,"r");
                if(f == NULL){
                    write(sock,invalid_file,sizeof(invalid_file));
                    status_code = 400;
                }else{
                    //Get Line Count
                    int line = fgetc(f);
                    int flag = 0;
                    while(flag = 0){
                        if(line == '\n'){
                            line_count++;
                        }
                        if(line == EOF){
                            flag = 1;
                        }
                        fprintf(stderr,"line_count = %i\n",line_count);
                    }
                    fclose(f);


                    //strcat(stat_cmd,file_name);
                    //fprintf(stderr,"cmd = %s",stat_cmd);
                    //system(stat_cmd);


                    //Open File for Reading again
                    //Could just reset fd , not sure how though.
                    fopen(file_name,"r");




                    //Get Header response
                    write(sock,"\n",1);
                    write(sock,"Server: myhttpd Web Server v1.0\n",28);
                    write(sock,"HTTP/1.1 200 OK\n",16);
                    write(sock,"Last Modified: \n",15);
                    //write(sock,gmtime(last_modified),sizeof(gmtime(last_modified)));
                    write(sock,"Content-Type: text/html\n",24);
                    write(sock,"\n",1);
                    //write(sock,"Content-Length: ",16);
                    //write(sock,line_count,sizeof(line_count));
                    write(sock,"\n",1);

                    //Get File Data
                    stat(file_name,&stat_info);

                    //Convert Times from stat
                    atime = stat_info.st_atime;
                    ctime = stat_info.st_ctime;
                    mtime = stat_info.st_mtime;

                    atimeinfo = localtime(&atime);
                    ctimeinfo = localtime(&ctime);
                    mtimeinfo = localtime(&mtime);

                    strftime(tmpbufa,sizeof(tmpbufa),"%d/%b/%Y : %H:%M:%S %z",atimeinfo);
                    strftime(tmpbufc,sizeof(tmpbufc),"%d/%b/%Y : %H:%M:%S %z",ctimeinfo);
                    strftime(tmpbufm,sizeof(tmpbufm),"%d/%b/%Y : %H:%M:%S %z",mtimeinfo);

                    //Createa stat_buf var to send over socket
                    sprintf(stat_buf,"'%s' \n Size: %i\n Blocks: %i Block Size: %i \n Inode: %i Links: %i\n Access: %s\n Modify: %s\n Change: %s\n",
                    file_name,stat_info.st_size,stat_info.st_blocks,stat_info.st_blksize,stat_info.st_ino,stat_info.st_nlink,tmpbufa,tmpbufm,tmpbufc);

                    //Send to client
                    write(sock,stat_buf,strlen(stat_buf));

                    fclose(f);
                    status_code = 200;
                }
        }
}
void
usage()
{
	fprintf(stderr, "usage: %s -d -l filename -p port -r dir -t time -n threadnum -s sched \n",progname);
	fprintf(stderr, "%s -h for help\n",progname);
	exit(1);
}
void
help(){
    fprintf(stderr, "Usage information\n");
	fprintf(stderr, "-d               :Enter Debugging Mode. Will not Daemonize. One connection at a time. Enables logging to stdout.\n");
	fprintf(stderr, "-h               :Prints Usage Summary with all options and exit\n");
	fprintf(stderr, "-l filename      :Log all requests to the given file. \n");
	fprintf(stderr, "-p               :Listen on the given port. If not provided, myhttpd will listen on port 8080\n");
	fprintf(stderr, "-r dir           :Set the root directory for the http server to dir\n");
	fprintf(stderr, "-t time          :Set the queuing time to time seconds. The default will be 60 seconds\n");
	fprintf(stderr, "-n threadnumber  :Set number of threads waiting ready in thread pool. Default is 4");
	fprintf(stderr, "-s sched         :Set the scheduling policy. Either FCFS or SJF. Default is FCFS\n");
    exit(1);
}
void
debug_print(time_t time_rcvd,time_t time_assigned,char *buf,unsigned int size_response){

    if(debug){
        //Print Logging to screen
    }else{
        //Do Nothing
    }
}

/*
    ###############################################################
    THREAD FUNCTIONS
    ###############################################################
*/

void *listener(void *_sock){ //EDIT--modified listener declaration at top
    int s = *((int*)_sock);

    union {             //EDIT--pulled union fromaddr into listener function
		uint32_t addr;
		char bytes[4];
	} fromaddr;


    int newsock;
    char tmpbuf[128];

    char ip_buf[255];



    listen(s, 5);
    while(1){
       // printf("hi im here");
        if (soctype == SOCK_STREAM) {
            fprintf(stderr, "Entering accept() waiting for connection.\n");
            fprintf(stderr, "Waiting For Connection... \n");
            //Accept First connections


            newsock = accept(s, (struct sockaddr *) &remote, &len);
            //unsigned int ip = remote.sin_addr.s_addr;
            //fprintf(stderr,"%u \n",ip);
            if(newsock < 0){
                printf("error connecting to client");
            }
            else{
            fprintf(stderr, "New Client Connected \n");
            }

            char timebuf[128];
            time_t rawtime;
            struct tm * timeinfo;
            time ( &rawtime );
            timeinfo = localtime ( &rawtime );
            strftime(tmpbuf,sizeof(tmpbuf),"%d/%b/%Y : %H:%M:%S %z",timeinfo);
            printf("the time is %s ", tmpbuf);

        }


        sock = newsock;

        //
        //while (!done) {
       // printf("imstuck");
		FD_ZERO(&ready);
		FD_SET(sock, &ready);
		FD_SET(fileno(stdin), &ready);

		if (select((sock + 1), &ready, 0, 0, 0) < 0) {
			perror("select");
			exit(1);
		}
		//Sends information from server to client
		if (FD_ISSET(fileno(stdin), &ready)) {
			if ((bytes = read(fileno(stdin), buf, BUF_LEN)) <= 0)
				done++;
			send(sock, buf, bytes, 0);

		}



		msgsize = sizeof(msgfrom);
		if (FD_ISSET(sock, &ready)) {
			if ((bytes = recvfrom(sock, buf, BUF_LEN, 0, (struct sockaddr *)&msgfrom, &msgsize)) <= 0) {
				done++;
				writeLog("Receiving Message");
			} else if (aflg) {
				fromaddr.addr = ntohl(msgfrom.sin_addr.s_addr);
				fprintf(stderr, "%d.%d.%d.%d: ", 0xff & (unsigned int)fromaddr.bytes[0],
			    	0xff & (unsigned int)fromaddr.bytes[1],
			    	0xff & (unsigned int)fromaddr.bytes[2],
			    	0xff & (unsigned int)fromaddr.bytes[3]);
			}

        //Get File Name
        char file_name[1024];
        int start = findnextchar(' ',buf,0);
        int end = findnextchar(' ',buf,start + 1);
        int i = start + 1;
        int j = 0;
        for(i;i<end;i++){
                file_name[j] = buf[i];
                j++;
        }

        struct stat stat_info;
        stat(file_name,&stat_info);

        //Get File Size
        int fileSize = stat_info.st_size;

        //Get IP address
        fromaddr.addr = ntohl(remote.sin_addr.s_addr);
                    sprintf(ip_buf, "%d.%d.%d.%d: ", 0xff & (unsigned int)fromaddr.bytes[0],
			    	0xff & (unsigned int)fromaddr.bytes[1],
			    	0xff & (unsigned int)fromaddr.bytes[2],
			    	0xff & (unsigned int)fromaddr.bytes[3]);

       fprintf(stderr,"%s \n",ip_buf);
       add_node(sock,fileSize,tmpbuf,file_name,buf,ip_buf);
       print_list();







            //add_node (id buffer )

        }

    //}

}
}



void *worker(){

    while(1){

    int id;
    int sfj;
    char *buffer;
    struct data msg;

    char timebuf[128];
    time_t rawtime;
    struct tm * timeinfo;
    char tmpbuf[128];




    //printf("Worker Thread Entered  worker\n");
    pthread_mutex_lock(&schedMutex);
    pthread_cond_wait(&workercv,&schedMutex);
    //printf("worker is working");
        //msg = msg_to_be_scheduled;
    //Need to set msg to the current job, not sure how?
    pthread_mutex_lock(&queLock);
        if ( schedflag == 0){
            msg = pop_msg(&head);
        }
        else if (schedflag == 1){
            sfj = find_shortest_job(&head);
            msg = get_message(sfj);
        }
    pthread_mutex_unlock(&queLock);

    //Set Buffer = to actual message recvd
    buffer = msg.message;
    //Set execution time
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    strftime(tmpbuf,sizeof(tmpbuf),"%d/%b/%Y : %H:%M:%S %z",timeinfo);

    pthread_mutex_unlock(&schedMutex);
        sem_post(&sem);
    printf("my msg is %s \n", buffer);
    //do GET or HEAD
    id = msg.id;
    char msgType = buffer[0];

    switch(msgType){
        case'G':
        {
            GET_response(buffer,id);
            //Logging/Debug
            /*
            %a : The remote IP address.
            %t : The time the request was received by the queuing thread (in GMT).
            %t : The time the request was assigned to an execution thread by the scheduler (in GMT).
            %r : The (quoted) first line of the request.
            %>s : The status of the request.
            %b : Size of the response in bytes. i.e, "Content-Length".
            Example:
            127.0.0.1 - [19/Sep/2011:13:55:36 -0600] [19/Sep/2011:13:58:21 -0600]
            "GET /index.html HTTP/1.0" 200 326
            */

            char log_buffer[2048];
            sprintf(log_buffer,"%s - [%s] [%s] \"%s\" %i %i",
                    &msg.ip_address,&msg.time_arrived,tmpbuf,msg.message,status_code,msg.size);
            writeLog(log_buffer);
            writeDebug(log_buffer);

        }
        break;
        case'H':
        {
            HEAD_response(buffer,id);
            /*
            %a : The remote IP address.
            %t : The time the request was received by the queuing thread (in GMT).
            %t : The time the request was assigned to an execution thread by the scheduler (in GMT).
            %r : The (quoted) first line of the request.
            %>s : The status of the request.
            %b : Size of the response in bytes. i.e, "Content-Length".
            Example:
            127.0.0.1 - [19/Sep/2011:13:55:36 -0600] [19/Sep/2011:13:58:21 -0600]
            "GET /index.html HTTP/1.0" 200 326
            */


            char log_buffer[2048];
            sprintf(log_buffer,"%s - [%s] [%s] \"%s\" %i %i",
                    &msg.ip_address,&msg.time_arrived,tmpbuf,msg.message,status_code,msg.size);
            writeLog(log_buffer);
            writeDebug(log_buffer);
        }
        break;
        default:
        {
            write(id,invalid_request,sizeof(invalid_request)-1);
            status_code = 400;

            char log_buffer[2048];
            sprintf(log_buffer,"%s - [%s] [%s] \"%s\" %i %i",
                    &msg.ip_address,&msg.time_arrived,tmpbuf,msg.message,status_code,0);
            writeLog(log_buffer);
            writeDebug(log_buffer);
        }
          //  sem_post(&sem);
        }
    }
}
void *scheduler(void *sched_flag){

    int flag = *((int*)sched_flag);
    //flag =0;
    int sfj_id;
    int suc;
    //int flag = sched_flag;  // we need sched flag to know which algo to use, defualt FIFO is easy, just pop()! default = 0 =  FIFO
  //  int thread_avaiable ; // this should keep track of
    //threads avaible to work, we dont want to send work if no thread is open

    //move pthread lock above if statements
    while(1){

        sem_wait(&sem);
        pthread_mutex_lock(&schedMutex);
            if(head ==  NULL){ // we have empty queue
                printf("Scheduleing for FIFO failed, queue is empty!\n");
		printf("you have 15 seconds to enter a request\n");
		sleep(15);
            }

            // some mutex to lock popping the queue
           // pthread_mutex_lock(&queLock);
            //msg_to_be_scheduled =pop_msg(&head);
            // we aqcuired msg to be scheduled
          //  pthread_mutex_unlock(&queLock);
            // unlock mutex , we've popped queue
            // signal worker thread to do work on this message
            suc =pthread_cond_signal(&workercv);
            pthread_mutex_unlock(&schedMutex);
            sleep(1); // make sure that one worker only gets one job
            //printf("the code for signal was %d \n" , suc);
            printf("after FIFO , new queue is now \n");
            print_list();



    }
}
//
  //  ####################################################################
//*/

//HELPER FUNCTIONS
char
get_filename(char *buf){
    char *msg = buf;
    char filename[1024];

    int start = findnextchar(' ',buf,0);
    int end = findnextchar(' ',buf,start + 1);
    int i = start;
    int j = 0;
    fprintf(stderr,"msg = %s\n",msg);
    fprintf(stderr,"start = %i\n",start);
    fprintf(stderr,"end = %i\n",end);
    if(start == -1 || end == -1){

        return *invalid_getrequest;
    }
    for(i;i<end;i++){
        filename[j] = buf[i];
        j++;
    }
    fprintf(stderr,"filename = %s\n",filename);
    return *filename;
}

int
findnextchar(char c,char *string,int start){
    //Returns first index of c in string
    //Returns -1 if no space found
    char *s = string;
    int flag = 0;

    int i = start;
    int index = -1;

    if(i < 0 || i > sizeof(s)){
        return index;
    }else{
        while(flag == 0){
            if(i == strlen(s)){
                flag = 1;
            }
            if(s[i] == c){
                index = i;
                flag = 1;
            }
            i++;
        }
    }
    return index;
}

void
writeLog(char *s){
    if(logfile_name == NULL){
        //Do not log
    }else{
        logfile = fopen(logfile_name,"a");
        fprintf(logfile,s);
        fprintf(logfile,"\n");
        fclose(logfile);
    }
}

void
writeDebug(char *s){
    if(debug == 0){
        //printf("DEBUG");//debug off
    }else{
        //print debug log to screen
        fprintf(stderr,s);
        fprintf(stderr,"\n");
    }
}
