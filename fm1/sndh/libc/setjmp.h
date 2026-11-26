/* (Musashi ne s'en sert que pour les erreurs de bus et d'adresse, non émulées ici) */
typedef int jmp_buf[1];
#define setjmp(b) 0
#define longjmp(b, v) ((void)0)
