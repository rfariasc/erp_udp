# erp_upd : Emulador de Retardo y Pérdida de paquetes en transferencias UDP.

#Sintaxis:
    $ erp_udp <retardo_promedio> <variación_retardo> <porcentaje_pérdida> <puerto_local> [host_remoto] <puerto_remoto> 

#Descripción
   En ocasiones nos vemos enfrentados a la necesidad de correr una aplicación que establece conexiones a través de Internet y nos interesaría averiguar cuál sería su comportamiento si las transferencias extremo a extremo poseen retardo y pérdidas de paquetes mayores a las de una red local. Por ejemplo, cuando deseamos controlar un robot remoto a través de Internet. Generalmente los desarrollos se hacen en un laboratorio donde la red de área local ofrece 100Mbps de tasas en nivel físico. Surge la pregunta ¿Cómo podemos correr un proyecto haciéndolo creer que tiene mayor retardo y mayor tasa de pérdida de paquetes?.
    A través de esta tarea usted creará una aplicación proxy para aumentar el retardo y las pérdidas en transferencias UDP. La idea es poner un proceso intermedio entre cliente y servidor y luego incorporar retardo y una tasa de pérdida según valores a elección, ver Figura 1.

    Figura 1: Intercepción de tráfico y emulación de retardo y pérdida de paquetes

      puerto corresponde al puerto local donde erp_upd espera por clientes UDP. Tan pronto llega un cliente a este puerto, erp_udp lo almacena en una cola FIFO y lo reenvía al puerto_remoto del host_remoto indicado luego de un retardo variable o eventualmente descarta el paquete para emular pérdida. Si se omite el host_remoto se entiende que es la máquina local (localhost).
        erp_udp maneja hebras en cada sentido destinadas a controlar el tiempo de residencia en cola y la posibilidad de péridada del paquete. El manejo de cada sentido del tráfico (subida o bajada) se puede manejar en forma equivalente. 

         La figura 2 muestra una forma de manejar el flujo de datos.  Por simplicidad en esta tarea los argumentos ingresados al programa deben ser usados en ambas direcciones de flujo de datos.
          La figura 3 muestra una forma sugerida para modelar el retardo variable de los paquetes. El tiempo de re-envío de un paquete depende de su tiempo de llegada, del tiempo de salida del anterior.
          tri = Random entre {el máximo entre (tai+retardo_promedio-variación_retardo) y tri-1} y {tai+ retardo_promedio + variación_retardo}
          Esta expresión permite cumplir orden FIFO de los paquetes, puesto que el límite inferior para el tiempo de re-envío será a lo más igual (en realidad inmediatamente después) al tiempo de re-envío del paquete anterior.

          Figura 2: Modelo para emular retardo y pérdida de paquetes en un sentido

          Figura 3: Modelo para controlar tasa y retardo en una dirección

#Compilación
    
  Para compilar el programa usar:

    $ make 

  Para limpiar la compilación:

    $ make clean

#Ejemplos de uso

  Para poder probar esta aplicación es necesario contar con un programa que emita datos por conexión UDP (Cliente) y otro que lo reciba (Servidor).

  Por ejemplo ejecutamos el servidor UDP de la siguiente página configurado para el puerto 12346

    http://profesores.elo.utfsm.cl/~agv/elo330/programs/ipc_withSockets/common/serverUDPs.c

  Y el siguiente cliente en el puerto 12345

    http://profesores.elo.utfsm.cl/~agv/elo330/programs/ipc_withSockets/common/clientUDPs.c

  Pero antes que esto ejecutamos nuestro programa de la siguiente manera

    $ ./erp_udp 5000 100 1 12345 localhost 12346 

  Esto retrasará el envío de paquetes desde el cliente en 5 seg, con una taza de pérdidas del 1%.
