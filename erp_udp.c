#include <pthread.h>    //pthread_create(), pthread_join(), pthread_attr_destroy(), pthread_cond_wait(), pthread_cond_signal(), pthread_mutex_lock(), pthread_mutex_unlock()
#include <stdio.h>      //printf(), perror(), fflush()
#include <stdlib.h>     //exit(), atoi()
#include <sys/types.h>  //socket(), bind(), accept(), recv(), connect(), fork(), open()
#include <sys/socket.h> //socket(), bind(), accept(), recv(), connect()
#include <netinet/in.h> //sockaddr_in, htons()
#include <netdb.h>      //gethostbyname() 
#include <string.h>     //strcpy(), memcpy()
#include <unistd.h>     //gethostname(), close(), write(), sleep(), fork()
#include <sys/stat.h>   //open()
#include <fcntl.h>      //open()
#include <time.h>       //gettimeofday()
#include <sys/time.h>   //gettimeofday()

#define MTU             1500
#define min(a,b)        (((a) < (b)) ? (a) : (b))

//solo para hacer pruebas/////
#define srv_PORTNUMBER 12346
#define cli_PORTNUMBER 12345
//////////////////////////////

//argv[0]: throttle_tcp
//argv[1]: puerto local
//argv[2]: [host remoto]
//argv[3]: puerto remoto
struct paquete{
       char buffer[MTU];
       int leng;
       struct timeval t_init;
       struct paquete *siguiente;
};
struct paquete *primero_up, *ultimo_up, *primero_down, *ultimo_down;

int bucketLevelUp = 0;                                          // Variable global con el espacio usado del balde de subida.
int bucketLevelDown = 0;                                        // Variable global con el espacio usado del balde de bajada.
int bucketSize = 15000;                                         // Variable global para controlar el tamano maximo de rafaga.
int leakingRate = 10000;                                        // Variable global para controlar la tasa de vaciado del balde.
int subida=0;                                                   // Variable global para almacenar la tasa de subida (graphics).
int bajada=0;                                                   // Variable global para almacenar la tasa de bajada (graphics).
int contador_up=0;
int retardo =2000000;
int contador_down=0;

    struct sockaddr_in name_srv;    // Caracteristicas del socket servidor.
    struct sockaddr_in name_cli;    // Caracteristicas del socket cliente.

pthread_mutex_t subidaLock = PTHREAD_MUTEX_INITIALIZER;         // Candado de exclusion mutua para la tasa de subida.
pthread_mutex_t bajadaLock = PTHREAD_MUTEX_INITIALIZER;         // Candado de exclusion mutua para la tasa de bajada.
pthread_mutex_t bucketLockUp = PTHREAD_MUTEX_INITIALIZER;       // Candado de exclusion mutua para el balde de subida.
pthread_mutex_t bucketLockDown = PTHREAD_MUTEX_INITIALIZER;     // Candado de exclusion mutua para el balde de bajada.
pthread_mutex_t bufupfirst = PTHREAD_MUTEX_INITIALIZER;         // Candado de exclusion mutua para el bufer de subida
pthread_mutex_t bufdownfirst = PTHREAD_MUTEX_INITIALIZER;         // Candado de exclusion mutua para el bufer de bajada
pthread_mutex_t bufuplast = PTHREAD_MUTEX_INITIALIZER;          // Candado de exclusion mutua para el bufer de subida.
pthread_mutex_t bufdownlast = PTHREAD_MUTEX_INITIALIZER;          // Candado de exclusion mutua para el bufer de bajada.
pthread_mutex_t uploadlock = PTHREAD_MUTEX_INITIALIZER;          // Candado de exclusion mutua para el hilo de subida.
pthread_mutex_t downloadlock = PTHREAD_MUTEX_INITIALIZER;          // Candado de exclusion mutua para el hilo de bajada.

pthread_cond_t bucketChangeUp = PTHREAD_COND_INITIALIZER;       // Variable de condicion de cambio para el balde de subida.
pthread_cond_t bucketChangeDown = PTHREAD_COND_INITIALIZER;     // Variable de condicion de cambio para el balde de bajada.
pthread_cond_t bufferespera = PTHREAD_COND_INITIALIZER;         // Variable de condicion de cambio para el hilo de subida.
pthread_cond_t bufferesperad = PTHREAD_COND_INITIALIZER;         // Variable de condicion de cambio para el hilo de .

pthread_rwlock_t retardolock = PTHREAD_RWLOCK_INITIALIZER;      // Candado de exclusion mutua para el retardo
pthread_rwlock_t contadorup = PTHREAD_RWLOCK_INITIALIZER;       // Candado de exclusion mutua para el contador de subida

pthread_rwlock_t contadordown = PTHREAD_RWLOCK_INITIALIZER;     // Candado de exclusion mutua para el contador de bajada
pthread_rwlock_t bucketSizeLock = PTHREAD_RWLOCK_INITIALIZER;   // Candado de exclusion mutua para modificacion de tamano del balde.
pthread_rwlock_t leakingRateLock = PTHREAD_RWLOCK_INITIALIZER;  // Candado de exclusion mutua para modificacion de tasa de vaciado.

void * leak(void * arg);
void * graphics(void * arg);
void graphicsCleanup(void * arg);

// Funcion que agrega paquete la buffer de subida.
void add_up(){
    struct paquete *nuevo;

    printf("ADD UP!!\n");


     nuevo = (struct paquete *) malloc (sizeof(struct paquete));
     gettimeofday(&nuevo->t_init, NULL);
     nuevo->siguiente = NULL;

     if (primero_up==NULL) {
         pthread_mutex_lock(&bufupfirst);
         primero_up = nuevo;
         pthread_mutex_unlock(&bufupfirst);
         pthread_mutex_lock(&bufuplast);
         ultimo_up = nuevo;
         pthread_mutex_unlock(&bufuplast);
         }
      else {
           // el que hasta ahora era el último tiene que apuntar al nuevo 
           pthread_mutex_lock(&bufuplast);
           ultimo_up->siguiente = nuevo;          //hacemos que el nuevo sea ahora el último
           ultimo_up = nuevo;
           pthread_mutex_unlock(&bufuplast);
      }
}


// Funcion que controla el buffer de subida
void *recibe_up(void *args){        //OK
    char buf[MTU];
    int n,i;

    printf("PARTE RECIBE UP\n");

    printf("s_srv RECIBIDO = %d\n", *((int *)(args)));

    while ((n = recv(*((int *)(args)), buf, sizeof(buf), 0)) > 0){
        printf("RECIBIDO (recibe_up)= %s \n", buf );


        add_up();

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
}


// Funcion que controla la direccion de subida.
void *upload(void *args) {
    struct paquete *auxiliar;
    char buf[MTU];                                                          // Buffer de transferencia.
    int n;                                                                  // Cantidad total de elementos a transferir.
    int i;                                                                  // Posicion del buffer donde comienzan los elementos que faltan por transmitir.
    int j;                                                                  // Cantidad parcial de elementos a transferir en un ciclo.
    int microsegundos;
    int dormir;
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

        n=primero_up->leng;
        for (i=0;i<=n;i++){
              buf[i]=primero_up->buffer[i];
        }

        auxiliar=primero_up;
        pthread_mutex_unlock(&bufupfirst);
        gettimeofday(&t_act,NULL);
        microsegundos = ((t_act.tv_usec - auxiliar->t_init.tv_usec)  + ((t_act.tv_sec - auxiliar->t_init.tv_sec) * 1000000.0f));
        

        pthread_rwlock_rdlock(&retardolock);
        if(microsegundos<retardo){
        dormir = retardo - microsegundos;
        pthread_rwlock_unlock(&retardolock);
           usleep(dormir);
        }

        for(i=0;i<n;i+=j)
        {

            printf("Se va a enviar: %s\n", buf+i );

            pthread_mutex_lock(&bucketLockUp);                              // Toma el candado de acceso al balde de subida.
            pthread_rwlock_rdlock(&bucketSizeLock);                         // Toma el candado para garantizar lectura valida del tamano del balde.



            while (bucketLevelUp >= bucketSize)                             // Mientra el balde esta lleno.
            {
                pthread_rwlock_unlock(&bucketSizeLock);                     // Libera candado de lectura de tamano de balde.
                pthread_cond_wait(&bucketChangeUp, &bucketLockUp);          // Se duerme en la variable de condicion del balde de subida. (Wait tipo Hoare)
                pthread_rwlock_rdlock(&bucketSizeLock);                     // Toma el candado para garantizar lectura valida del tamano del balde en la revision del while.
            }



            j = min(n-i,bucketSize-bucketLevelUp);                          // Cantidad permitida de elementos a transferir. (n-i): elementos restantes por enviar, (bucketSize-bucketLevelUp): maxima rafaga permitida

            pthread_rwlock_unlock(&bucketSizeLock);                         // Libera candado de lectura de tamano de balde.

            pthread_mutex_lock(&subidaLock);                                // Toma el candado de acceso a la tasa de subida para el grafico.



            subida+=j;                                                      // Suma la cantidad de bytes que transmitira al servidor.
            pthread_mutex_unlock(&subidaLock);                              // Libera el candado de acceso a la tasa de subida para el grafico.
            bucketLevelUp+=j;                                               // Aumenta el uso del balde de subida en el mayor posible.
            pthread_mutex_unlock(&bucketLockUp);                            // Libera el candado de acceso al balde de subida.


            printf("*((int *)(args) + 1) = %d, buf+i = %s, j = %d\n",*((int *)(args) + 1), buf+i, j );



            // if (send(*((int *)(args) + 1), buf+i, j, 0) < 0)                // Envia j bytes recibidos del cliente al servidor.
            if (sendto(*((int *)(args) + 1), buf+i, j, 0, (struct sockaddr*) &name_cli, sizeof(struct sockaddr_in)) < 0)                // Envia j bytes recibidos del cliente al servidor.
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


// Funcion que agrega paquete la buffer de bajada.
void add_down(){
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
           // el que hasta ahora era el último tiene que apuntar al nuevo 
           pthread_mutex_lock(&bufdownlast);
           ultimo_down->siguiente = nuevo;          //hacemos que el nuevo sea ahora el último
           ultimo_down = nuevo;
           pthread_mutex_unlock(&bufdownlast);
      }
}


// Funcion que controla el buffer de subida
void *recibe_down(void *args){
    char buf[MTU];
    int n,i;
    while ((n = recv(*((int *)(args)+1), buf, sizeof(buf), 0)) > 0){


        printf("RECIBIDO (recibe_down)= %s \n", buf );


        add_down();

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
}


// Funcion que controla la direccion de bajada.
void *download(void *args) {
    struct paquete *auxiliar;
    char buf[MTU];                                                          // Buffer de transferencia.
    int n;                                                                  // Cantidad de elementos a transferir.
    int i;                                                                  // Posicion del buffer donde comienzan los elementos que faltan por transmitir.
    int j;                                                                  // Cantidad parcial de elementos a transferir en un ciclo.
    int microsegundos;
    int dormir;
    struct timeval t_act;
    
    while(1)
    {
        pthread_mutex_lock(&downloadlock);
        pthread_rwlock_rdlock(&contadordown);
        while(contador_down == 0){                                    // mientras la lista este vacia

          pthread_rwlock_unlock(&contadordown);
          pthread_cond_wait(&bufferesperad, &downloadlock);
          pthread_rwlock_rdlock(&contadordown);
         }


        pthread_rwlock_unlock(&contadordown);

        pthread_mutex_lock(&bufdownfirst);

        n=primero_down->leng;
        for (i=0;i<=n;i++){
              buf[i]=primero_down->buffer[i];
        }
        auxiliar=primero_down;
        pthread_mutex_unlock(&bufdownfirst);
        gettimeofday(&t_act,NULL);
        microsegundos = ((t_act.tv_usec - auxiliar->t_init.tv_usec)  + ((t_act.tv_sec - auxiliar->t_init.tv_sec) * 1000000.0f));                                            // calcula diferencia en micro seg
        

        pthread_rwlock_rdlock(&retardolock);
        if(microsegundos<retardo){                                     //compara si el tiempo es suficiente
        dormir = retardo - microsegundos;                              // calcula cuanto falta
        pthread_rwlock_unlock(&retardolock);
           usleep(dormir);
        }


        for(i=0;i<n;i+=j)
        {
            pthread_mutex_lock(&bucketLockDown);                            // Toma el candado de acceso al balde de subida.
            pthread_rwlock_rdlock(&bucketSizeLock);                         // Toma el candado para garantizar lectura valida del tamano del balde.
            while (bucketLevelDown >= bucketSize)                           // Mientra el balde esta lleno.
            {
                pthread_rwlock_unlock(&bucketSizeLock);                     // Libera candado de lectura de tamano de balde.
                pthread_cond_wait(&bucketChangeDown, &bucketLockDown);      // Se duerme en la variable de condicion del balde de subida. (Wait tipo Hoare)      
                pthread_rwlock_rdlock(&bucketSizeLock);                     // Toma el candado para garantizar lectura valida del tamano del balde en la revision del while.
            }
            j = min(n-i,bucketSize-bucketLevelDown);                        // Cantidad permitida de elementos a transferir. (n-i): elementos restantes por enviar, (bucketSize-bucketLevelUp): maxima rafaga permitida
            pthread_rwlock_unlock(&bucketSizeLock);                         // Libera candado de lectura de tamano de balde.
            pthread_mutex_lock(&bajadaLock);                                // Toma el candado de acceso a la tasa de bajada para el grafico.
            bajada+=j;                                                      // Suma la cantidad de bytes que transmitira al cliente.
            pthread_mutex_unlock(&bajadaLock);                              // Libera el candado de acceso a la tasa de bajada para el grafico.
            bucketLevelDown+=j;                                             // Aumenta el uso del balde de subida en el mayor posible.
            pthread_mutex_unlock(&bucketLockDown);                          // Libera el candado de acceso al balde de subida.
            if (send(*((int *)(args)), buf+i, j, 0) < 0)                    // Envia los j bytes recibidos del servidor al cliente.
                {perror("send failed"); exit(1);}
        }

        pthread_rwlock_wrlock(&contadordown);
        contador_down=contador_down -1;                                    // decrementa contador de bajada
        pthread_rwlock_unlock(&contadordown);
        pthread_mutex_lock(&bufdownfirst);
        primero_down=auxiliar->siguiente;
        free(auxiliar);                                                    // libera memoria
        pthread_mutex_unlock(&bufdownfirst);
        pthread_mutex_unlock(&downloadlock);
    }
}                      

int main(int argc, char *argv[]) {
    int s_srv;                      // Descriptor referenciando al socket de la conexion con el programa cliente.
    int s_cli;                      // Descriptor referenciando al socket de la conexion con el programa servidor.
    int ns;                         // Descriptor de socket servidor abierto.
    int len_srv;                    // Tamano de la estructura sockaddr_in para el extremo servidor.
    int len_cli;                    // Tamano de la estructura sockaddr_in para el extremo cliente.
    char hostname[64];              // Nombre del host remoto.
    pthread_t up;                   // Hebra para control de upload.
    pthread_t down;                 // Hebra para control de download.
    pthread_t leakyB;               // Hebra para control de vaciamiento de los baldes.
    pthread_t recibeup;             // Hebra para la recepcion de paquetes de subida.
    pthread_t recibedown;             // Hebra para la recepcion de paquetes de bajada.
    // struct sockaddr_in name_srv;    // Caracteristicas del socket servidor.
    // struct sockaddr_in name_cli;    // Caracteristicas del socket cliente.
    struct hostent *hp;             // Puntero a la estructura con la informacion sobre la maquina remota.
    int args[2];                    // Arreglo para pasar mas de un argumento con pthread_create.

    // switch(argc) {
    //     case 3:
            if(gethostname(hostname, sizeof(hostname)) < 0)                     // Si se omite el host_remoto se entiende que es la misma maquina (localhost).
                {perror("gethostname failed"); exit(-1);}
    //         break;
    //     case 4:
    //         strcpy(hostname, argv[2]);                                          // Extrae nombre de host del argumento.
    //         break;
    //     default:
    //         printf("Uso: %s puerto [host_remoto] puerto_remoto \n",argv[0]);
    //         exit(-1);
    // }   // En hostname queda el nombre de la maquina destino.

    //////////////////////////////////////////////////////////
    //  Establecimiento de conexion cliente <-> servidor    //
    //////////////////////////////////////////////////////////
    // if ((s_srv = socket(AF_INET, SOCK_STREAM, 0)) < 0)                          // Crea socket IPv4, Conexion TCP.
    if ((s_srv = socket(AF_INET, SOCK_DGRAM, 0)) < 0)                          // UDP
        {perror("socket failed"); exit(-1);}
    name_srv.sin_family = AF_INET;                                              // Address Family Internet.
    
    // name_srv.sin_port = htons(atoi(argv[1]));                                   // Asigna la puerta de servicio de throttle_tcp, la cual es pasada en el primer argumento.
    name_srv.sin_port = htons(srv_PORTNUMBER);
    
    name_srv.sin_addr.s_addr = htonl(INADDR_ANY);                               // Se atienden requerimientos entrantes por cualquier interfaz.
    len_srv = sizeof(struct sockaddr_in);                                       // Tamano de la estructura sockaddr_in.
    if(bind(s_srv, (struct sockaddr *) &name_srv, len_srv))                     // Asocia el socket con su nombre (caracteristicas).
        {perror("bind failed"); exit(-1);}

    // listen(s_srv, 5);                                                           // Escucha por conexiones en el socket s_srv.
    // if ((ns = accept(s_srv, (struct sockaddr *) &name_srv, &len_srv)) < 0)      // Acepta conexiones en el socket s_srv de caracteristicas name_srv.
    //     {perror("accept failed"); exit(-1);}


    printf("SRV_PORTNUMBER = %d\n", ntohs(name_srv.sin_port));


    if((hp = gethostbyname(hostname)) == NULL)                                  // Obtiene informacion de la maquina remota.
        {perror("gethostbyname failed"); exit(-1);}
    if ((s_cli = socket(AF_INET, SOCK_DGRAM, 0)) < 0)                          // Crea socket IPv4, Conexion TCP.
        {perror("socket failed"); exit(-1);}
    name_cli.sin_family = AF_INET;                                              // Address Family Internet.

    // name_cli.sin_port = htons(atoi(argv[argc-1]));                              // Asigna la puerta de servicio del host remoto, la cual es pasada en el tercer argumento.
    name_cli.sin_port = htons(cli_PORTNUMBER);                              // Asigna la puerta de servicio del host remoto, la cual es pasada en el tercer argumento.

    memcpy(&name_cli.sin_addr, hp->h_addr_list[0], hp->h_length);               // Guarda la direccion del host remoto en name_cli.
    len_cli = sizeof(struct sockaddr_in);                                       // Tamano de la estructura sockaddr_in.

    // if((connect(s_cli, (struct sockaddr *) &name_cli, len_cli)) < 0 )           // Se conecta con el host remoto.
    //     {perror("connect failed"); exit(-1);}

    printf("CLI_PORTNUMBER = %d\n", ntohs(name_cli.sin_port));

    //////////////////////////////////////////////////////////
    //  Manejo de las conexiones                            //
    //////////////////////////////////////////////////////////


    printf("s_srv = %d, s_cli %d\n",s_srv, s_cli);

    // args[0]=ns;                                                 // Guarda en el arreglo de argumentos el descriptor del socket servidor abierto (conectado con el programa cliente).
    args[0]=s_srv;                                                 // Guarda en el arreglo de argumentos el descriptor del socket servidor abierto (conectado con el programa cliente).
    args[1]=s_cli;                                              // Guarda en el arreglo de argumentos el descriptor del socket cliente (conectado con el programa servidor).


    if(pthread_create(&recibedown, NULL, recibe_down,(void *) args))        // Crea la hebra que controlara el bufer de subida.
        {perror("pthread_create failed"); exit(-1);}
    if(pthread_create(&recibeup, NULL, recibe_up,(void *) args))        // Crea la hebra que controlara el bufer de subida.
        {perror("pthread_create failed"); exit(-1);}
    if(pthread_create(&leakyB, NULL, leak, NULL))               // Crea la hebra que controlara el vaciamiento de los baldes.
        {perror("pthread_create failed"); exit(-1);}
    if(pthread_create(&up, NULL, upload,(void *) args))         // Crea la hebra que manejara el upload.
        {perror("pthread_create failed"); exit(-1);}
    if(pthread_create(&down, NULL, download,(void *) args))     // Crea la hebra que manejara el download.
        {perror("pthread_create failed"); exit(-1);}


    printf("Para terminar fije un tamano invalido (p.ej. -1).\n");
    while(1)                                                    // Prompt de cambio de los parametros de leaky bucket.
    {
        int temp=0;
        int condicion=0;
        printf("1)  Nueva tasa en [Bps] \n2)  Nuevo retardo en [ms]\n3)Nuevo tamano del balde [B]:\n4) salir \n"); scanf("%d",&condicion);
        if(condicion==4)
        {
            printf(" cerrando...\n");
            break;
        }
        if(condicion==3)
        {
            printf("Nuevo tamano del balde [B]: "); scanf("%d",&temp);
            pthread_rwlock_wrlock(&bucketSizeLock);             // Toma candado de exclusion mutua para cambiar el tamano del balde.
            bucketSize = temp;                                  // Cambia el tamano del balde.
            pthread_rwlock_unlock(&bucketSizeLock);             // Libera candado de modificacion de tamano del balde.
            pthread_cond_signal(&bucketChangeUp);               // Senala a la variable de condicion del balde de subida que hubo un cambio.
            pthread_cond_signal(&bucketChangeDown);             // Senala a la variable de condicion del balde de bajada que hubo un cambio.
        }if(condicion==1){
        printf("Nueva tasa de vaciado [Bps]: "); scanf("%d",&temp);
        if((temp>bucketSize))
        {
            printf("Tamano invalido, cerrando...\n");
            break;
        }
        else
        {
            pthread_rwlock_wrlock(&leakingRateLock);            // Toma candado de exclusion mutua para cambiar tasa de vaciado.
            leakingRate = temp;                                 // Cambia la tasa de vaciado del balde.
            printf("la nueva tasa es= %d\n",leakingRate);
            pthread_rwlock_unlock(&leakingRateLock);            // Libera candado de modificacion de tasa de vaciado.
        }}
        if (condicion==2){
               printf("Ingrese nuevo retardo [ms]: "); scanf("%d",&temp);
               pthread_rwlock_wrlock(&retardolock);
               retardo= temp*1000;
               printf("El nuevo retardo es de %d ms\n",temp);
               pthread_rwlock_unlock(&retardolock);
             }

        
    }

    if(pthread_cancel(recibeup))                                // Cancela la hebra de subida.
        {perror("pthread_cancel failed"); exit(-1);}
    if(pthread_cancel(up))                                      // Cancela la hebra de subida.
        {perror("pthread_cancel failed"); exit(-1);}
    if(pthread_cancel(down))                                    // Cancela la hebra de bajada.
        {perror("pthread_cancel failed"); exit(-1);}
    if(pthread_cancel(leakyB))                                  // Cancela la hebra de actualizacion de tasa de vaciado.
        {perror("pthread_cancel failed"); exit(-1);}


    // Espera por el retorno de las hebras canceladas.     
    if(pthread_join(up, NULL))
        {perror("pthread_join failed"); exit(-1);}
    if(pthread_join(down, NULL))
        {perror("pthread_join failed"); exit(-1);}
    if(pthread_join(leakyB, NULL))
        {perror("pthread_join failed"); exit(-1);}

    // close(ns);      // Cierra descriptor de socket servidor abierto.
    close(s_srv);   // Cierra descriptor referenciando al socket de la conexion con el cliente.
    close(s_cli);   // Cierra descriptor referenciando al socket de la conexion con el servidor.
    return 0;
}

// Funcion que controla el vaciamiento de los baldes.
void * leak(void * arg){
    while (1)
    {
        sleep(1);
        pthread_mutex_lock(&bucketLockUp);              // Toma el candado de acceso al balde de subida.
        if (bucketLevelUp>0)                            // Si el balde de subida no esta vacio.
        {
            pthread_rwlock_rdlock(&leakingRateLock);    // Toma el candado de lectura de tasa de vaciado.
            bucketLevelUp-=leakingRate;                 // Libera una cantidad dada por la tasa de salida.
            pthread_rwlock_unlock(&leakingRateLock);    // Libera el candado de lectura de tasa de vaciado.
        }
        pthread_mutex_unlock(&bucketLockUp);            // Libera el candado de acceso al balde de subida.
        pthread_cond_signal(&bucketChangeUp);           // Senala a la variable de condicion del balde de subida que hubo un cambio.

        pthread_mutex_lock(&bucketLockDown);            // Toma el candado de acceso al balde de bajada.
        if (bucketLevelDown>0)                          // Si el balde de bajada no esta vacio.
        {
            pthread_rwlock_rdlock(&leakingRateLock);    // Toma el candado de lectura de tasa de vaciado.
            bucketLevelDown-=leakingRate;               // Libera una cantidad dada por la tasa de salida.
            pthread_rwlock_unlock(&leakingRateLock);    // Libera el candado de lectura de tasa de vaciado.
        }
        pthread_mutex_unlock(&bucketLockDown);          // Libera el candado de acceso al balde de bajada.
        pthread_cond_signal(&bucketChangeDown);         // Senala a la variable de condicion del balde de bajada que hubo un cambio.
    }
}