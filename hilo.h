#ifndef HILO_H
#define HILO_H

#include "sistema.h"

//TIPOS--------------------------------------------------------------------
/**
 * @brief the parametro_t structure, stores the initial information of a thread, before the search
 *
 *
 */
typedef struct
{
    long int position; /*!< parametro queva a determinar la posicion inicial y final de búsqueda de un hilo*/
    int n_threads;     /*!< numero de hilos que se le ha ordenado crear al minero*/
    long int taget;    /*!< y de la función f(x)=y que tiene que encontrar*/
    info_sistema * sistema;/*!< Segmento de memoria compartida del sistema*/
} parametro_t;

/**
 * @brief the return_t structure, stores the return information of a thread, after the search
 *
 *
 */

typedef struct
{
    long int taget1;    /*!< Y que va a tener que buscar en la actual ronda de  minado*/
    long int taget2;    /*!< Y que va a tener que buscar en la segunda ronda de minado*/
    Boolean encontrado; /*!< Comprueba si el hilo ha encontrado la solucion */

} return_t;

#endif