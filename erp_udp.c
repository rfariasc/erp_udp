#include <pthread.h>    
#include <stdio.h>      
#include <stdlib.h>     
#include <sys/types.h>  
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <netdb.h>      
#include <string.h>     
#include <unistd.h>     
#include <sys/stat.h>   
#include <fcntl.h>      
#include <time.h>       
#include <sys/time.h>   

//#define TEST 1

#define MTU             1500

//solo para hacer pruebas/////
#define srv_PORTNUMBER 12346  //12346
#define cli_PORTNUMBER 12345    //12345
#define PORCENTAJE_PERDIDA 0
#define RETARDOUS 0                 //en ms
#define RETARDOUS_VARIATION 0     //en ms
//////////////////////////////


struct paquete{
       char buffer[MTU];
       int leng;
       struct timeval t_init;
       struct paquete *siguiente;
};
struct paquete *primero_up, *ultimo_up, *primero_down, *ultimo_down;
/*
    Los punteros primero_up y ultimo_up, son los punteros al ultimo y primero de la lista
    que contiene los paquetes recibidos desde el cliente.
    Mismo caso para primero_down y ultimo_down
*/

int subida=0;    
int bajada=0;    
int contador_up=0;

int retardo=RETARDOUS*1000, variacion_retardo=RETARDOUS_VARIATION*1000;  

int contador_down=0;
int PROB_PERDIDA=PORCENTAJE_PERDIDA;

struct sockaddr_in name_srv;   
struct sockaddr_in name_cli;   
struct sockaddr_in cli_side;

pthread_mutex_t subidaLock = PTHREAD_MUTEX_INITIALIZER;         
pthread_mutex_t bajadaLock = PTHREAD_MUTEX_INITIALIZER;         
pthread_mutex_t bufupfirst = PTHREAD_MUTEX_INITIALIZER;         
pthread_mutex_t bufdownfirst = PTHREAD_MUTEX_INITIALIZER;       
pthread_mutex_t bufuplast = PTHREAD_MUTEX_INITIALIZER;          
pthread_mutex_t bufdownlast = PTHREAD_MUTEX_INITIALIZER;        
pthread_mutex_t uploadlock = PTHREAD_MUTEX_INITIALIZER;         
pthread_mutex_t downloadlock = PTHREAD_MUTEX_INITIALIZER;       

pthread_cond_t bufferespera = PTHREAD_COND_INITIALIZER;         
pthread_cond_t bufferesperad = PTHREAD_COND_INITIALIZER;        

pthread_rwlock_t retardolock = PTHREAD_RWLOCK_INITIALIZER;      
pthread_rwlock_t prob_perdida_lock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t contadorup = PTHREAD_RWLOCK_INITIALIZER;       
pthread_rwlock_t contadordown = PTHREAD_RWLOCK_INITIALIZER;     


//////////////////Funciones que intervienen en el flujo desde el Cliente hacia el Servidor//////////////////

void toServer_list(void);               //Crea paquetes, dependiendo de cuantos sean mandandos por el cliente hacia el servidor
void *toServer_receiver(void *args);    //Recibe y almacena los paquetes del cliente (los que serán enviados al servidor)
void *toServer_postman(void *args);     //Envia los paquetes (que recibio toServer_receiver) al Servidor

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////Funciones que intervienen en el flujo desde el Servidor hacia el Cliente//////////////////

void toClient_list();                   //Crea paquetes, dependiendo de cuantos sean mandandos por el servidor hacia el cliente
void *toClient_receiver(void *args);    //Recibe y almacena los paquetes del servidor (los que serán enviados al cliente)
void *toClient_postman(void *args);     //Envia los paquetes (que recibio toClient_receiver) al Cliente

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    int s_srv;                     
    int s_cli;                     
    int len_srv;                   
    char hostname[64];             
    pthread_t up;                  
    pthread_t down;                
    pthread_t recibeup;            
    pthread_t recibedown;          
    struct hostent *hp;            
    int args[2];                   

    srand(time(NULL));             

    #ifdef TEST
        if(gethostname(hostname, sizeof(hostname)) < 0)            
                {perror("gethostname failed"); exit(-1);}
    #else
    switch(argc){
        case 6:
            if(gethostname(hostname, sizeof(hostname)) < 0)        
               {perror("gethostname failed"); exit(-1);}
            break;
        case 7:
            strcpy(hostname, argv[5]);                             
            break;
        default:
            printf("Uso $%s (retardo_promedio) (variación_retardo) (porcentaje_pérdida) (puerto_local) [host_remoto] (puerto_remoto)\n", argv[0]);
            return -1;
    }

    retardo = atoi(argv[1])*1000; 
    variacion_retardo = atoi(argv[2])*1000;
    PROB_PERDIDA= atoi(argv[3]);
    #endif

    if(PROB_PERDIDA<0 || PROB_PERDIDA>100){
        printf("La probabilidad de perdida va entre 0 y 100%%\n");
        return -1;
    }


    /////////////////////////////////////////////////
    if ((s_srv = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        {perror("socket failed"); exit(-1);}
    name_srv.sin_family = AF_INET; 
    
    #ifdef TEST
        name_srv.sin_port = htons(srv_PORTNUMBER);
    #else
        name_srv.sin_port = htons(atoi(argv[4]));     
    #endif    
    
    name_srv.sin_addr.s_addr = htonl(INADDR_ANY);     
    len_srv = sizeof(struct sockaddr_in);             
    if(bind(s_srv, (struct sockaddr *) &name_srv, len_srv))                 
        {perror("bind failed"); exit(-1);}


    #ifdef TEST
    printf("SRV_PORTNUMBER_SOCKET = %d\n", ntohs(name_srv.sin_port));
    #endif

    if((hp = gethostbyname(hostname)) == NULL)                        
        {perror("gethostbyname failed"); exit(-1);}
    if ((s_cli = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        {perror("socket failed"); exit(-1);}
    name_cli.sin_family = AF_INET;                         

    #ifdef TEST
        name_cli.sin_port = htons(cli_PORTNUMBER);   
    #else            
        name_cli.sin_port = htons(atoi(argv[argc-1])); 
    #endif

    memcpy(&name_cli.sin_addr, hp->h_addr_list[0], hp->h_length);     


    #ifdef TEST
    printf("CLI_PORTNUMBER_SOCKET = %d\n", ntohs(name_cli.sin_port));
    #endif

    ////////////////////////////////////////

    args[0]=s_srv;    
    args[1]=s_cli;          

    if(pthread_create(&recibedown, NULL, toClient_receiver,(void *) args))    
        {perror("pthread_create failed"); exit(-1);}
    if(pthread_create(&recibeup, NULL, toServer_receiver,(void *) args))  
        {perror("pthread_create failed"); exit(-1);}
    if(pthread_create(&up, NULL, toServer_postman,(void *) args))  
        {perror("pthread_create failed"); exit(-1);}
    if(pthread_create(&down, NULL, toClient_postman,(void *) args))    
        {perror("pthread_create failed"); exit(-1);}

    while(1){

        int temp=0;
        int condicion=0;
        printf("===================================\n");
        printf("1)  Nuevo retardo promedio [ms]\n2)  Nueva variacion retardo en [ms]\n3)  Nueva probabilidad de perdida [0 a 100]:\n4)  Salir \n"); 
        printf("===================================\n");
        scanf("%d",&condicion);
        if (condicion==1){
            printf("Ingrese nuevo retardo promedio [ms]: "); scanf("%d",&temp);

            pthread_rwlock_wrlock(&retardolock);
            retardo= temp*1000;
            pthread_rwlock_unlock(&retardolock);

            printf("El nuevo retardo es de %d ms\n",temp);

        }        
        if (condicion==2){
            printf("Ingrese nueva variacion retardo [ms]: "); scanf("%d",&temp);

            pthread_rwlock_wrlock(&retardolock);
            variacion_retardo= temp*1000;
            pthread_rwlock_unlock(&retardolock);

            printf("La nueva variacion de retardo es de %d ms\n",temp);
        }        
        if (condicion==3){

            printf("Ingrese nueva probabilidad de perdida [0 a 100]: "); scanf("%d",&temp);
            
            if(temp>=0 && temp<=100){

                pthread_rwlock_wrlock(&prob_perdida_lock);
                PROB_PERDIDA=  temp;
                pthread_rwlock_unlock(&prob_perdida_lock);
                printf("La nueva probabilidad de perdida es de %d%%\n",temp);               

            }else{
                printf("La probabilidad de perdida va entre 0 y 100%%\n");
            }

        }        
        if(condicion==4){
            printf(" Cerrando...\n");
            break;
        }
    }

    if(pthread_cancel(recibeup))                               
        {perror("pthread_cancel failed"); exit(-1);}
    if(pthread_cancel(up))                                      
        {perror("pthread_cancel failed"); exit(-1);}
    if(pthread_cancel(down))                                    
        {perror("pthread_cancel failed"); exit(-1);}

    
    if(pthread_join(up, NULL))
        {perror("pthread_join failed"); exit(-1);}
    if(pthread_join(down, NULL))
        {perror("pthread_join failed"); exit(-1);}

    close(s_srv);  
    close(s_cli);   
    return 0;
}

void toServer_list(){
    struct paquete *nuevo;

    nuevo = (struct paquete *) malloc (sizeof(struct paquete));
    gettimeofday(&nuevo->t_init, NULL);
    nuevo->siguiente = NULL;

    if (primero_up==NULL){
        pthread_mutex_lock(&bufupfirst);
        primero_up = nuevo;
        pthread_mutex_unlock(&bufupfirst);

        pthread_mutex_lock(&bufuplast);
        ultimo_up = nuevo;
        pthread_mutex_unlock(&bufuplast);
    }
    else{
        pthread_mutex_lock(&bufuplast);
        ultimo_up->siguiente = nuevo;          
        ultimo_up = nuevo;
        pthread_mutex_unlock(&bufuplast);
    }
}

void *toServer_receiver(void *args){ 
    char buf[MTU];

    int i;
    int n;
    struct sockaddr* temp = (struct sockaddr *)&cli_side;
    int addr_len = sizeof(cli_side);
    while ((n = recvfrom(*((int *)(args)), buf, sizeof(buf), 0, temp, &addr_len)) > 0){

        toServer_list();

        pthread_mutex_lock(&bufuplast);
        for (i=0;i<=n;i++){ 
            ultimo_up->buffer[i]=buf[i];
        }
        ultimo_up->leng=n;
        pthread_mutex_unlock(&bufuplast);

        pthread_rwlock_wrlock(&contadorup);
        contador_up=contador_up +1;
        pthread_rwlock_unlock(&contadorup);

        pthread_cond_signal(&bufferespera);
    }
    return 0;
}

void *toServer_postman(void *args) {
    struct paquete *auxiliar;
    int microsegundos,dormir,numero, agregado; 

    struct timeval t_act;
    while(1){

        pthread_mutex_lock(&uploadlock);

        pthread_rwlock_rdlock(&contadorup);
        while(contador_up == 0){

          pthread_rwlock_unlock(&contadorup);
          pthread_cond_wait(&bufferespera, &uploadlock);
          pthread_rwlock_rdlock(&contadorup);
         }
        pthread_rwlock_unlock(&contadorup);

        pthread_mutex_lock(&bufupfirst);
        auxiliar=primero_up;
        pthread_mutex_unlock(&bufupfirst);

        gettimeofday(&t_act,NULL);
        microsegundos = ((t_act.tv_usec - auxiliar->t_init.tv_usec)  + ((t_act.tv_sec - auxiliar->t_init.tv_sec) * 1000000.0f));
        
        pthread_rwlock_rdlock(&retardolock);

        (variacion_retardo != 0) ?  (agregado = ( (rand()% (2*variacion_retardo)) - variacion_retardo )) :  (agregado = 0);

        if(microsegundos<retardo + agregado ){
            dormir = (retardo - microsegundos) + agregado;
            pthread_rwlock_unlock(&retardolock);
            usleep(dormir);
        }

        pthread_rwlock_wrlock(&prob_perdida_lock);
        if ((numero = rand()%101) < (100-PROB_PERDIDA)){            //Si hay suerte se manda
            pthread_rwlock_unlock(&prob_perdida_lock);

            if (sendto(*((int *)(args) + 1), auxiliar->buffer, auxiliar->leng, 0, (struct sockaddr*) &name_cli, sizeof(struct sockaddr_in)) < 0)                // Envia j bytes recibidos del cliente al servidor.
               {perror("send failed"); exit(1);}
        }

        pthread_rwlock_wrlock(&contadorup);
        contador_up=contador_up -1;
        pthread_rwlock_unlock(&contadorup);

        pthread_mutex_lock(&bufupfirst);
        primero_up=auxiliar->siguiente;
        free(auxiliar);
        pthread_mutex_unlock(&bufupfirst);

        pthread_mutex_unlock(&uploadlock);
    }
}

void toClient_list(){
     struct paquete *nuevo;

     nuevo = (struct paquete *) malloc (sizeof(struct paquete));
     gettimeofday(&nuevo->t_init, NULL);
     nuevo->siguiente = NULL;

     if (primero_down==NULL) {
         pthread_mutex_lock(&bufdownfirst);
         primero_down = nuevo;
         pthread_mutex_unlock(&bufdownfirst);

         pthread_mutex_lock(&bufdownlast);
         ultimo_down = nuevo;
         pthread_mutex_unlock(&bufdownlast);
         }
      else {
           pthread_mutex_lock(&bufdownlast);
           ultimo_down->siguiente = nuevo;        
           ultimo_down = nuevo;
           pthread_mutex_unlock(&bufdownlast);
      }
}

void *toClient_receiver(void *args){
    char buf[MTU];
    int n,i;
    while ((n = recv(*((int *)(args)+1), buf, sizeof(buf), 0)) > 0){

        toClient_list();

        pthread_mutex_lock(&bufdownlast);
        for (i=0;i<=n;i++){
            ultimo_down->buffer[i]=buf[i];
        }
        ultimo_down->leng=n;
        pthread_mutex_unlock(&bufdownlast);

        pthread_rwlock_wrlock(&contadordown);
        contador_down=contador_down +1;
        pthread_rwlock_unlock(&contadordown);

        pthread_cond_signal(&bufferesperad);
    }
    return 0;
}

void *toClient_postman(void *args) {
    struct paquete *auxiliar;
    int agregado, microsegundos, numero, dormir;
    struct timeval t_act;

    while(1)
    {
        pthread_mutex_lock(&downloadlock);

        pthread_rwlock_rdlock(&contadordown);
        while(contador_down == 0){           

          pthread_rwlock_unlock(&contadordown);
          pthread_cond_wait(&bufferesperad, &downloadlock);
          pthread_rwlock_rdlock(&contadordown);
         }
        pthread_rwlock_unlock(&contadordown);

        pthread_mutex_lock(&bufdownfirst);
        auxiliar=primero_down;
        pthread_mutex_unlock(&bufdownfirst);

        gettimeofday(&t_act,NULL);
        microsegundos = ((t_act.tv_usec - auxiliar->t_init.tv_usec)  + ((t_act.tv_sec - auxiliar->t_init.tv_sec) * 1000000.0f));                                            // calcula diferencia en micro seg        

        pthread_rwlock_rdlock(&retardolock);

        (variacion_retardo != 0) ?  (agregado = ( (rand()% (2*variacion_retardo)) - variacion_retardo )) :  (agregado = 0);

        if(microsegundos<retardo + agregado ){
            dormir = (retardo - microsegundos) + agregado;
            pthread_rwlock_unlock(&retardolock);
            usleep(dormir);
        }

        pthread_rwlock_wrlock(&prob_perdida_lock);
        if ((numero = rand()%101) < (100-PROB_PERDIDA)){
            pthread_rwlock_unlock(&prob_perdida_lock);

            if (sendto(*((int *)(args)), auxiliar->buffer, auxiliar->leng, 0, (struct sockaddr*) &cli_side, sizeof(struct sockaddr_in)) < 0)                    // Envia los j bytes recibidos del servidor al cliente.
                {perror("send failed"); exit(1);}

        }

        pthread_rwlock_wrlock(&contadordown);
        contador_down=contador_down - 1;                      
        pthread_rwlock_unlock(&contadordown);

        pthread_mutex_lock(&bufdownfirst);
        primero_down=auxiliar->siguiente;
        free(auxiliar);                                       
        pthread_mutex_unlock(&bufdownfirst);
        
        pthread_mutex_unlock(&downloadlock);
    }
}                      
