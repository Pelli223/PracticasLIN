El programa de prueba para la parte del programa multihebra se trata, de una prueba para el módulo de productor consumidor
en el que se crean 3 hebras, la primera va generando numeros aleatorios y escribiendolos en el fichero de caracteres, la segunda
va leyendo de este al principio se realizan 5 lecturas y una 6 que bloquea la hebra en el read, despues de esto, la hebra que escribe
lo hace más rápido debido a un sleep menor que el de la hebra lectora, esto para comprobar si al llenar el buffer esta se bloque como
debería. Por último tenemos una hebra que escribe patata en el dispositivo de caracteres para comprobar que al intentar escribir 
valores no válidos, no se produce ningún error en el funcionamiento.
