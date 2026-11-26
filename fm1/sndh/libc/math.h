/* (l'unité de calcul flottant du 68881 : jamais utilisée par un 68000) */
static inline double pow(double a, double b) { (void)a; (void)b; return 0; }
static inline double exp(double a) { (void)a; return 0; }
static inline double log(double a) { (void)a; return 0; }
static inline double log10(double a) { (void)a; return 0; }
static inline double sin(double a) { (void)a; return 0; }
static inline double cos(double a) { (void)a; return 0; }
static inline double tan(double a) { (void)a; return 0; }
static inline double atan(double a) { (void)a; return 0; }
static inline double asin(double a) { (void)a; return 0; }
static inline double acos(double a) { (void)a; return 0; }
static inline double sqrt(double a) { (void)a; return 0; }
static inline double fabs(double a) { return a < 0 ? -a : a; }
static inline double floor(double a) { return (double)(long long)a; }
static inline double ceil(double a) { return (double)(long long)a; }
static inline double fmod(double a, double b) { (void)b; return a; }
static inline double ldexp(double a, int e) { (void)e; return a; }
static inline double frexp(double a, int *e) { *e = 0; return a; }
static inline double tanh(double a) { (void)a; return 0; }
static inline double sinh(double a) { (void)a; return 0; }
static inline double cosh(double a) { (void)a; return 0; }
static inline double atanh(double a) { (void)a; return 0; }
static inline double log1p(double a) { (void)a; return 0; }
static inline double expm1(double a) { (void)a; return 0; }
static inline double log2(double a) { (void)a; return 0; }
static inline double exp2(double a) { (void)a; return 0; }
static inline double round(double a) { return (double)(long long)(a + (a < 0 ? -0.5 : 0.5)); }
