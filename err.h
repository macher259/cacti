/**
 * Program z przykładowych programów współbieżnych w języku C, które
 * omawiane były na laboratoriach. Nie jestem autorem.
 * Używam kodu zgodnie z odpowiedzią na forum zadania.
 */

#ifndef CACTI_ERR_H
#define CACTI_ERR_H

#ifndef _ERR_
#define _ERR_

/* wypisuje informacje o błędnym zakończeniu funkcji systemowej
i kończy działanie */
extern void syserr(const char *fmt, ...);

/* wypisuje informacje o błędzie i kończy działanie */
extern void fatal(const char *fmt, ...);

#endif



#endif //CACTI_ERR_H
