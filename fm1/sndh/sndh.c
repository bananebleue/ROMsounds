/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : lire un fichier SNDH (musique de l'Atari ST : un programme 68000 qui pilote le YM2149). Pour la page
 * (web/sndh.wasm, compilé par tools/build_sndh.sh) et pour les tests sur PC (SNDH_MAIN : tests/sndh_test.sh).
 *
 * Un Atari ST réduit au nécessaire autour du 68000 de Musashi (ext/musashi, MIT) : 1 Mo de mémoire, le YM2149
 * ($FF8800 : registre choisi, $FF8802 : valeur ; movep compris), le MFP 68901 ($FFFA01-$FFFA25 : minuteries A, B, D en
 * mode délai, interruptions de niveau 6 avec vecteur, fin d'interruption logicielle), quelques appels du système (XBIOS :
 * Xbtimer, Giaccess, Supexec, Jdisint, Jenabint ; BIOS : Setexc ; GEMDOS : Super) et les compteurs $466 et $4BA.
 * Le fichier est chargé en $10000 ; init (+0, d0 = numéro du morceau) une fois, puis play (+8) à la fréquence de
 * l'en-tête (TC50, TC200…). Chaque écriture dans le YM est notée avec son instant (échantillons à 44,1 kHz) : la page
 * en fait un VGM (commandes 0xA0) que le FM-1 joue avec son AY / YM2149.
 *
 *   sndh_buf()                                 où la page copie le fichier
 *   sndh_run(taille, morceau, hz, trames)      -> nombre d'écritures notées (négatif : erreur)
 *   sndh_ev_t(), sndh_ev_rv()                  instants (u32) et registre << 8 | valeur (u16) */
#include <stdint.h>
#include "m68k.h"

#define RAM_SIZE 0x100000u
#define LOAD_AT 0x10000u
#define IDLE 0x600u                             /* « bra.s * » : le 68000 attend ici entre deux appels */
#define RTE_AT 0x500u
#define SSP_TOP (LOAD_AT - 16u)
#define CPU_HZ 8000000u
#define MFP_HZ 2457600u
#define EV_MAX (1u << 21)

static uint8_t ram[RAM_SIZE];
static uint32_t ev_t[EV_MAX];
static uint16_t ev_rv[EV_MAX];
static uint32_t ev_n;
static uint64_t now;                            /* cycles du 68000 depuis le début */
static uint8_t ym_sel, ym_reg[16];

/* ---- MFP 68901 : minuteries A (canal 13), B (8), C (5), D (4) */
typedef struct { uint8_t ctrl, data, run; uint32_t rem; } mfp_timer_t;   /* rem : tics du MFP avant la prochaine fin */
static mfp_timer_t tim[4];
static uint8_t mfp[0x40];                       /* les registres (adresses impaires $FFFA01-$FFFA3F : mfp[a >> 1]) */
static uint64_t mfp_acc;                        /* (reste de la conversion cycles -> tics du MFP) */
static const uint16_t PRESC[8] = {0, 4, 10, 16, 50, 64, 100, 200};
static const uint8_t TCH[4] = {13, 8, 5, 4};    /* canal de chaque minuterie */
#define IERA mfp[0x07 >> 1]
#define IERB mfp[0x09 >> 1]
#define IPRA mfp[0x0B >> 1]
#define IPRB mfp[0x0D >> 1]
#define ISRA mfp[0x0F >> 1]
#define ISRB mfp[0x11 >> 1]
#define IMRA mfp[0x13 >> 1]
#define IMRB mfp[0x15 >> 1]
#define VR mfp[0x17 >> 1]

static uint32_t ch_bit(uint32_t ch, uint8_t **a, uint8_t **b, uint8_t **ip, uint8_t **is, uint8_t **im)
{
    int hi = ch >= 8u;
    *a = hi ? &IERA : &IERB; *ip = hi ? &IPRA : &IPRB; *is = hi ? &ISRA : &ISRB; *im = hi ? &IMRA : &IMRB; (void)b;
    return 1u << (ch & 7u);
}

static uint32_t tim_period(const mfp_timer_t *t) { return PRESC[t->ctrl & 7u] * (t->data ? t->data : 256u); }

static void mfp_fire(uint32_t k)
{
    uint8_t *ie, *ip, *is, *im;
    uint32_t m = ch_bit(TCH[k], &ie, 0, &ip, &is, &im);
    if (*ie & m) *ip |= (uint8_t)m;
}

static int mfp_irq_channel(void)                /* le canal à servir (priorité : le plus haut), -1 : aucun */
{
    int ch;
    for (ch = 15; ch >= 0; ch--) {
        uint8_t *ie, *ip, *is, *im;
        uint32_t m = ch_bit((uint32_t)ch, &ie, 0, &ip, &is, &im);
        if (*is & m) return -1;                 /* (en service : bloque celui-ci et les moins prioritaires) */
        if ((*ip & m) && (*im & m)) return ch;
    }
    return -1;
}

static void mfp_update_irq(void) { m68k_set_irq(mfp_irq_channel() >= 0 ? 6 : 0); }

static int int_ack(int level)
{
    int ch = mfp_irq_channel();
    uint8_t *ie, *ip, *is, *im;
    uint32_t m;
    (void)level;
    if (ch < 0) return M68K_INT_ACK_SPURIOUS;
    m = ch_bit((uint32_t)ch, &ie, 0, &ip, &is, &im);
    *ip &= (uint8_t)~m;
    if (VR & 8u) *is |= (uint8_t)m;             /* fin d'interruption logicielle : le programme efface le bit */
    m68k_set_irq(0);
    return (VR & 0xF0u) | ch;
}

static void mfp_advance(uint64_t cycles)        /* le temps passe pour les minuteries */
{
    uint64_t t = cycles * MFP_HZ + mfp_acc;
    uint32_t ticks = (uint32_t)(t / CPU_HZ), k;
    mfp_acc = t % CPU_HZ;
    for (k = 0; k < 4u; k++) {
        mfp_timer_t *tm = &tim[k];
        uint32_t left = ticks;
        if (!tm->run) continue;
        while (left >= tm->rem) { left -= tm->rem; tm->rem = tim_period(tm); mfp_fire(k); }
        tm->rem -= left;
    }
}

static uint64_t mfp_next(void)                  /* cycles jusqu'à la prochaine fin de minuterie */
{
    uint64_t best = ~0ull;
    uint32_t k;
    for (k = 0; k < 4u; k++) if (tim[k].run) {
        uint64_t c = ((uint64_t)tim[k].rem * CPU_HZ - mfp_acc + MFP_HZ - 1u) / MFP_HZ;
        if (c < best) best = c;
    }
    return best;
}

static void tim_ctrl(uint32_t k, uint32_t v)
{
    mfp_timer_t *t = &tim[k];
    uint32_t was = t->run;
    t->ctrl = (uint8_t)(v & 7u);
    t->run = (v & 7u) != 0u;                    /* (les modes « événement » et « impulsion » : traités comme arrêtés) */
    if ((v & 8u)) t->run = 0;
    if (t->run && !was) t->rem = tim_period(t);
}

static uint32_t mfp_read(uint32_t r)
{
    if (r >= 0x1Fu && r <= 0x25u && (r & 1u)) {   /* TxDR : le compte en cours */
        mfp_timer_t *t = &tim[(r - 0x1Fu) >> 1];
        if (t->run) { uint32_t p = PRESC[t->ctrl & 7u]; return ((t->rem + p - 1u) / p) & 255u; }
        return t->data;
    }
    if (r == 0x01u) return 0xFFu;               /* GPIP : rien d'actif */
    return mfp[r >> 1];
}

static void mfp_write(uint32_t r, uint32_t v)
{
    v &= 255u;
    switch (r) {
    case 0x0B: case 0x0D: mfp[r >> 1] &= (uint8_t)v; break;   /* IPR, ISR : on ne peut qu'effacer */
    case 0x0F: case 0x11: mfp[r >> 1] &= (uint8_t)v; break;
    case 0x07: mfp[r >> 1] = (uint8_t)v; IPRA &= (uint8_t)v; break;   /* (une source coupée perd sa demande) */
    case 0x09: mfp[r >> 1] = (uint8_t)v; IPRB &= (uint8_t)v; break;
    case 0x19: tim_ctrl(0, v); break;
    case 0x1B: tim_ctrl(1, v); break;
    case 0x1D: tim_ctrl(3, v & 7u); tim_ctrl(2, v >> 4 & 7u); break;   /* TCDCR : C (bits 4-6), D (bits 0-2) */
    case 0x1F: case 0x21: case 0x23: case 0x25: {
        mfp_timer_t *t = &tim[(r - 0x1Fu) >> 1];
        t->data = (uint8_t)v;
        if (!t->run) t->rem = tim_period(t);
        break;
    }
    default: mfp[r >> 1] = (uint8_t)v; break;
    }
    mfp_update_irq();
}

/* ---- le YM2149 */
static void ym_write(uint32_t a, uint32_t v)
{
    if (!(a & 2u)) { ym_sel = (uint8_t)(v & 15u); return; }
    if (ym_sel < 14u) {
        uint64_t s = (now + (uint64_t)m68k_cycles_run()) * 44100u / CPU_HZ;
        if (ym_sel == 13u || ym_reg[ym_sel] != (uint8_t)v) {
            if (ev_n < EV_MAX) { ev_t[ev_n] = (uint32_t)s; ev_rv[ev_n] = (uint16_t)(ym_sel << 8 | (v & 255u)); ev_n++; }
        }
    }
    ym_reg[ym_sel] = (uint8_t)v;
}

/* ---- la mémoire vue par le 68000 */
static uint32_t rd8(uint32_t a)
{
    a &= 0xFFFFFFu;
    if (a < RAM_SIZE) return ram[a];
    if ((a & 0xFFFF00u) == 0xFF8800u) return (a & 3u) ? 0xFFu : ym_reg[ym_sel];
    if ((a & 0xFFFFC0u) == 0xFFFA00u) return (a & 1u) ? mfp_read(a & 0x3Fu) : 0xFFu;
    return 0;
}
static void wr8(uint32_t a, uint32_t v)
{
    a &= 0xFFFFFFu;
    if (a < RAM_SIZE) { if (a >= 8u) ram[a] = (uint8_t)v; return; }
    if ((a & 0xFFFF00u) == 0xFF8800u) { if (!(a & 1u)) ym_write(a, v); return; }   /* (le YM est sur les octets pairs) */
    if ((a & 0xFFFFC0u) == 0xFFFA00u) { if (a & 1u) mfp_write(a & 0x3Fu, v); return; }
}
unsigned int m68k_read_memory_8(unsigned int a) { return rd8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd8(a) << 8 | rd8(a + 1u); }
unsigned int m68k_read_memory_32(unsigned int a) { return rd8(a) << 24 | rd8(a + 1u) << 16 | rd8(a + 2u) << 8 | rd8(a + 3u); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { wr8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { wr8(a, v >> 8 & 255u); wr8(a + 1u, v & 255u); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { wr8(a, v >> 24); wr8(a + 1u, v >> 16 & 255u); wr8(a + 2u, v >> 8 & 255u); wr8(a + 3u, v & 255u); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return rd8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }

static uint32_t r16(uint32_t a) { return m68k_read_memory_16(a); }
static uint32_t r32(uint32_t a) { return m68k_read_memory_32(a); }
static void w32(uint32_t a, uint32_t v) { m68k_write_memory_32(a, v); }

/* ---- les appels du système (TRAP) */
static void push32(uint32_t v) { uint32_t sp = m68k_get_reg(0, M68K_REG_A7) - 4u; w32(sp, v); m68k_set_reg(M68K_REG_A7, sp); }

static int trap(int n)
{
    uint32_t sp = m68k_get_reg(0, M68K_REG_A7), fn = r16(sp);
    uint32_t d0 = 0;
    if (n == 14) {                              /* XBIOS */
        if (fn == 31u) {                        /* Xbtimer(minuterie, contrôle, donnée, vecteur) */
            uint32_t k = r16(sp + 2u) & 3u, c = r16(sp + 4u), d = r16(sp + 6u), vec = r32(sp + 8u);
            uint8_t *ie, *ip, *is, *im;
            uint32_t m = ch_bit(TCH[k], &ie, 0, &ip, &is, &im);
            if (!(VR & 0xF0u)) VR = 0x48u;
            w32(((VR & 0xF0u) | TCH[k]) * 4u, vec);
            if (k == 2u) tim_ctrl(2, 0); else tim_ctrl(k == 3u ? 3u : k, 0);
            tim[k].data = (uint8_t)d; tim[k].rem = tim_period(&tim[k]);
            tim_ctrl(k, c);
            *ie |= (uint8_t)m; *im |= (uint8_t)m;
            mfp_update_irq();
        } else if (fn == 28u) {                 /* Giaccess(valeur, registre) */
            uint32_t v = r16(sp + 2u) & 255u, r = r16(sp + 4u);
            ym_sel = (uint8_t)(r & 15u);
            if (r & 0x80u) ym_write(0xFF8802u, v);
            d0 = ym_reg[ym_sel];
        } else if (fn == 38u) {                 /* Supexec(fonction) : appelée, elle revient après le TRAP */
            push32(m68k_get_reg(0, M68K_REG_PC));
            m68k_set_reg(M68K_REG_PC, r32(sp + 2u));
            return 1;
        } else if (fn == 26u || fn == 27u) {    /* Jdisint, Jenabint(numéro) */
            uint8_t *ie, *ip, *is, *im;
            uint32_t m = ch_bit(r16(sp + 2u) & 15u, &ie, 0, &ip, &is, &im);
            if (fn == 26u) { *ie &= (uint8_t)~m; *ip &= (uint8_t)~m; } else { *ie |= (uint8_t)m; *im |= (uint8_t)m; }
            mfp_update_irq();
        }
    } else if (n == 13) {                       /* BIOS : Setexc(numéro, vecteur) */
        if (fn == 5u) { uint32_t v = r16(sp + 2u) & 255u, a = r32(sp + 4u); d0 = r32(v * 4u); if (a != 0xFFFFFFFFu) w32(v * 4u, a); }
    }                                           /* GEMDOS (Super…) : rien à faire, on est déjà superviseur */
    m68k_set_reg(M68K_REG_D0, d0);
    return 1;
}

static void hook(unsigned int pc) { if (pc == IDLE) m68k_end_timeslice(); }

static int call(uint32_t pc, uint32_t d0)       /* appeler une routine du fichier ; elle revient en IDLE */
{
    m68k_set_reg(M68K_REG_A7, SSP_TOP - 4u); w32(SSP_TOP - 4u, IDLE);
    m68k_set_reg(M68K_REG_SR, 0x2300u);
    m68k_set_reg(M68K_REG_D0, d0);
    m68k_set_reg(M68K_REG_PC, pc);
    return 0;
}

static void run_until(uint64_t end, int idle_stop)   /* exécuter jusqu'à l'instant end (cycles), minuteries comprises */
{
    while (now < end) {
        if (idle_stop && m68k_get_reg(0, M68K_REG_PC) == IDLE) return;
        uint64_t slice = end - now, nx = mfp_next();
        int used;
        if (nx < slice) slice = nx ? nx : 1u;
        if (slice > 1000000u) slice = 1000000u;
        if (m68k_get_reg(0, M68K_REG_PC) == IDLE && mfp_irq_channel() < 0) used = (int)slice;   /* au repos : on saute */
        else {
            used = m68k_execute((int)slice);
            if (used <= 0) used = (int)slice;
        }
        now += (uint64_t)used;
        mfp_advance((uint64_t)used);
        mfp_update_irq();
    }
}

#ifdef SNDH_WASM
#define EXPORT(n) __attribute__((export_name(n)))
#else
#define EXPORT(n)
#endif
EXPORT("sndh_buf") uint8_t *sndh_buf(void) { return ram + LOAD_AT; }
EXPORT("sndh_ev_t") uint32_t *sndh_ev_t(void) { return ev_t; }
EXPORT("sndh_ev_rv") uint16_t *sndh_ev_rv(void) { return ev_rv; }

EXPORT("sndh_run") int sndh_run(uint32_t len, uint32_t tune, uint32_t hz, uint32_t frames)
{
    uint32_t i, f;
    uint64_t per, t;
    if (len < 16u || len > RAM_SIZE - LOAD_AT - 0x10000u || !hz || hz > 1000u) return -1;
    for (i = 0; i < LOAD_AT; i++) ram[i] = 0;
    for (i = LOAD_AT + len; i < RAM_SIZE; i++) ram[i] = 0;
    for (i = 0; i < 4u; i++) { tim[i].ctrl = tim[i].data = tim[i].run = 0; tim[i].rem = 0; }
    for (i = 0; i < sizeof mfp; i++) mfp[i] = 0;
    VR = 0x48u;                                 /* (comme le TOS : vecteurs $40, fin d'interruption logicielle) */
    for (i = 0; i < 16u; i++) ym_reg[i] = 0;
    ym_reg[7] = 0xFFu;
    ym_sel = 0; ev_n = 0; now = 0; mfp_acc = 0;
    for (i = 2; i < 256u; i++) w32(i * 4u, RTE_AT);
    ram[RTE_AT] = 0x4E; ram[RTE_AT + 1u] = 0x73;   /* rte */
    ram[IDLE] = 0x60; ram[IDLE + 1u] = 0xFE;       /* bra.s * */
    ram[0x424] = 0x0A;                              /* memcntlr : 1 Mo */
    w32(0x42E, RAM_SIZE);                           /* phystop */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(int_ack);
    m68k_set_trap_instr_callback(trap);
    m68k_set_instr_hook_callback(hook);
    ram[0] = ram[1] = 0; ram[2] = (uint8_t)(SSP_TOP >> 8); ram[3] = (uint8_t)SSP_TOP; ram[4] = ram[5] = 0; ram[6] = IDLE >> 8; ram[7] = IDLE & 255u;
    m68k_pulse_reset();
    call(LOAD_AT, tune ? tune : 1u);                /* init */
    run_until(now + (uint64_t)CPU_HZ * 2u, 1);      /* (2 s au plus) */
    if (m68k_get_reg(0, M68K_REG_PC) != IDLE) return -2;
    per = CPU_HZ / hz;
    t = now;
    for (f = 0; f < frames; f++) {
        if (m68k_get_reg(0, M68K_REG_PC) == IDLE) call(LOAD_AT + 8u, 0);
        w32(0x466, r32(0x466) + 1u);                /* _frclock */
        t += per;
        run_until(t, 0);
        w32(0x4BA, (uint32_t)(now / (CPU_HZ / 200u)));   /* _hz_200 */
    }
    return (int)ev_n;
}

#ifdef SNDH_MAIN
#include <stdio.h>
#include <stdlib.h>
/* sndh_test FICHIER.sndh HZ TRAMES [morceau] : les écritures notées, en texte (instant registre valeur) */
int main(int argc, char **argv)
{
    FILE *fp;
    long n;
    int k, i;
    if (argc < 4) { fprintf(stderr, "sndh_test FICHIER HZ TRAMES [morceau]\n"); return 2; }
    fp = fopen(argv[1], "rb");
    if (!fp) return 2;
    n = (long)fread(sndh_buf(), 1, RAM_SIZE - LOAD_AT - 0x10000u, fp);
    fclose(fp);
    k = sndh_run((uint32_t)n, argc > 4 ? (uint32_t)atoi(argv[4]) : 1u, (uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]));
    if (k < 0) { fprintf(stderr, "erreur %d\n", k); return 1; }
    for (i = 0; i < k; i++) printf("%u %u %u\n", ev_t[i], ev_rv[i] >> 8, ev_rv[i] & 255u);
    fprintf(stderr, "%d écritures\n", k);
    return 0;
}
#endif
