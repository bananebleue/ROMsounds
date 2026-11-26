/* (journal de Musashi : désactivé) */
typedef struct FILE FILE;
#define fprintf(...) 0
#define printf(...) 0
#define sprintf(...) 0
#define stderr ((FILE *)0)
#define vfprintf(...) 0
#define sscanf(...) 0
