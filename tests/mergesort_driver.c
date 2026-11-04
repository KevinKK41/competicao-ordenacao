#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int32_t *baseline32(int32_t *, int32_t *, size_t);
int64_t *baseline64(int64_t *, int64_t *, size_t);
int32_t *experimental32(int32_t *, int32_t *, size_t);
int64_t *experimental64(int64_t *, int64_t *, size_t);
void experimental_merge64(const int64_t *, int64_t *, size_t, size_t);

static uint64_t estado;
static uint64_t aleatorio(void) {
    uint64_t z = (estado += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}
static int comparar32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}
static int comparar64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}
static void *alocar(size_t bytes) {
    void *p = malloc(bytes ? bytes : 1);
    if (!p) { perror("malloc"); exit(2); }
    return p;
}

/* As distribuicoes sao iguais para os dois algoritmos em cada rodada.
 * Conversoes unsigned->signed usam a representacao de complemento de dois
 * do GCC nos alvos x86-64 aqui considerados. Nao ha subtracao com overflow.
 */
static void preencher(void *dados, size_t n, int bits, unsigned padrao,
                      uint64_t seed) {
    estado = seed;
    for (size_t i = 0; i < n; ++i) {
        uint64_t r = aleatorio();
        int64_t valor;
        switch (padrao) {
            case 0: valor = bits == 32 ? (int32_t)r : (int64_t)r; break;
            case 1: valor = 7; break;
            case 2: valor = (int64_t)i - (int64_t)(n / 2); break;
            case 3: valor = (int64_t)(n - i) - (int64_t)(n / 2); break;
            case 4: valor = (int64_t)(r % 17) - 8; break;
            case 5: valor = i % 2 ? INT64_MAX : INT64_MIN; break;
            case 6: valor = (int64_t)(i % 97) - 48; break;
            case 7: valor = (int64_t)(i / 64) * 64 + (int64_t)(63 - i % 64); break;
            default: /* Quase ordenado: pequenos segmentos invertidos. */
                valor = (int64_t)i;
                if (i % 1024 < 16) valor += 15 - 2 * (int64_t)(i % 16);
                break;
        }
        if (bits == 32) {
            ((int32_t *)dados)[i] = padrao == 5
                ? (i % 2 ? INT32_MAX : INT32_MIN) : (int32_t)valor;
        } else {
            ((int64_t *)dados)[i] = valor;
        }
    }
}

static void *ordenar(int experimental, int bits, void *v, void *aux, size_t n) {
    if (bits == 32) return experimental ? experimental32(v, aux, n) : baseline32(v, aux, n);
    return experimental ? experimental64(v, aux, n) : baseline64(v, aux, n);
}
static void referencia(void *v, size_t n, int bits) {
    qsort(v, n, (size_t)bits / 8, bits == 32 ? comparar32 : comparar64);
}
static void verificar(void *resultado, void *v, void *aux, const void *ref,
                      size_t bytes, size_t n, int bits, unsigned padrao, int exp) {
    if ((resultado != v && resultado != aux) || memcmp(resultado, ref, bytes)) {
        fprintf(stderr, "FALHA: n=%zu bits=%d padrao=%u variante=%s\n",
                n, bits, padrao, exp ? "experimental" : "baseline");
        exit(1);
    }
}
static size_t testar_tamanho(size_t n, uint64_t seed) {
    size_t testes = 0;
    for (int bits = 32; bits <= 64; bits += 32) {
        size_t bytes = n * ((size_t)bits / 8);
        void *entrada = alocar(bytes), *ref = alocar(bytes);
        void *v = alocar(bytes), *aux = alocar(bytes);
        for (unsigned padrao = 0; padrao < 9; ++padrao) {
            preencher(entrada, n, bits, padrao, seed + n);
            memcpy(ref, entrada, bytes);
            referencia(ref, n, bits);
            for (int exp = 0; exp <= 1; ++exp) {
                memcpy(v, entrada, bytes);
                memset(aux, 0xa5, bytes);
                void *resultado = ordenar(exp, bits, v, aux, n);
                verificar(resultado, v, aux, ref, bytes, n, bits, padrao, exp);
                ++testes;
            }
        }
        free(aux); free(v); free(ref); free(entrada);
    }
    return testes;
}

static size_t testar_vetor32(const int32_t *entrada, size_t n) {
    int32_t ref[16], v[16], aux[16];
    size_t bytes = n * sizeof(*v);
    memcpy(ref, entrada, bytes);
    referencia(ref, n, 32);
    for (int exp = 0; exp <= 1; ++exp) {
        memcpy(v, entrada, bytes);
        memset(aux, 0xa5, sizeof(aux));
        void *resultado = ordenar(exp, 32, v, aux, n);
        verificar(resultado, v, aux, ref, bytes, n, 32, 99, exp);
    }
    /* Repete a mesma ordem relativa com chaves genuinamente de 64 bits.
     * A multiplicacao cabe em int64_t inclusive para INT32_MIN.
     */
    int64_t entrada64[16], ref64[16], v64[16], aux64[16];
    for (size_t i = 0; i < n; ++i) {
        entrada64[i] = entrada[i] == INT32_MAX ? INT64_MAX
            : (int64_t)entrada[i] * INT64_C(4294967296);
    }
    size_t bytes64 = n * sizeof(*v64);
    memcpy(ref64, entrada64, bytes64);
    referencia(ref64, n, 64);
    for (int exp = 0; exp <= 1; ++exp) {
        memcpy(v64, entrada64, bytes64);
        memset(aux64, 0xa5, sizeof(aux64));
        void *resultado = ordenar(exp, 64, v64, aux64, n);
        verificar(resultado, v64, aux64, ref64, bytes64, n, 64, 99, exp);
    }
    return 4;
}

static size_t testar_redes(void) {
    size_t total = 0;
    int32_t v[16];
    for (int i = 0; i < 8; ++i) v[i] = i;
    /* Todas as 8! permutacoes da folha vetorial. */
    for (;;) {
        total += testar_vetor32(v, 8);
        int i = 6;
        while (i >= 0 && v[i] >= v[i + 1]) --i;
        if (i < 0) break;
        int j = 7;
        while (v[j] <= v[i]) --j;
        int32_t tmp = v[i]; v[i] = v[j]; v[j] = tmp;
        for (int a = i + 1, b = 7; a < b; ++a, --b) {
            tmp = v[a]; v[a] = v[b]; v[b] = tmp;
        }
    }
    /* Todas as 3^8 combinacoes de minimo, zero e maximo: empates/extremos. */
    for (unsigned codigo = 0; codigo < 6561; ++codigo) {
        unsigned x = codigo;
        for (int i = 0; i < 8; ++i) {
            v[i] = x % 3 == 0 ? INT32_MIN : x % 3 == 1 ? 0 : INT32_MAX;
            x /= 3;
        }
        total += testar_vetor32(v, 8);
    }
    /* Todas as C(16,8) intercalacoes de duas sequencias ordenadas de 8. */
    for (unsigned mascara = 0; mascara < 65536; ++mascara) {
        unsigned count = 0;
        for (unsigned m = mascara; m; m &= m - 1) ++count;
        if (count != 8) continue;
        size_t a = 0, b = 8;
        for (int i = 0; i < 16; ++i) {
            if (mascara & (1u << i)) v[a++] = i;
            else v[b++] = i;
        }
        total += testar_vetor32(v, 16);
    }
    return total;
}

static size_t testar_mesclas64(void) {
    const size_t tamanhos[] = {
        1, 3, 4, 5, 7, 8, 9, 12, 16, 20, 24, 32, 60, 64, 65, 128, 256, 512, 4096
    };
    size_t testes = 0;
    for (size_t i = 0; i < sizeof(tamanhos) / sizeof(*tamanhos); ++i) {
        for (size_t j = 0; j < sizeof(tamanhos) / sizeof(*tamanhos); ++j) {
            size_t meio = tamanhos[i], n = meio + tamanhos[j];
            size_t bytes = n * sizeof(int64_t);
            int64_t *src = alocar(bytes), *dst = alocar(bytes), *ref = alocar(bytes);
            for (unsigned padrao = 0; padrao < 9; ++padrao) {
                for (uint64_t seed = 0; seed < 3; ++seed) {
                    preencher(src, n, 64, padrao, seed * 5700 + n);
                    referencia(src, meio, 64);
                    referencia(src + meio, n - meio, 64);
                    memcpy(ref, src, bytes);
                    referencia(ref, n, 64);
                    memset(dst, 0xa5, bytes);
                    experimental_merge64(src, dst, meio, n);
                    verificar(dst, src, dst, ref, bytes, n, 64, padrao, 1);
                    ++testes;
                }
            }
            free(ref); free(dst); free(src);
        }
    }
    return testes;
}

static double agora(clockid_t relogio) {
    struct timespec ts;
    if (clock_gettime(relogio, &ts)) { perror("clock_gettime"); exit(2); }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static size_t numero(const char *texto) {
    char *fim;
    errno = 0;
    unsigned long long valor = strtoull(texto, &fim, 10);
    if (errno || *fim || fim == texto || *texto == '-' || valor > SIZE_MAX) {
        fprintf(stderr, "Numero invalido: %s\n", texto); exit(2);
    }
    return (size_t)valor;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "test")) {
        size_t total = testar_redes() + testar_mesclas64();
        for (size_t n = 0; n <= 130; ++n) total += testar_tamanho(n, 41);
        const size_t limites[] = {
            255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
            4095, 4096, 4097, 8191, 8192, 8193, 16383, 16384, 16385,
            32767, 32768, 32769, 65535, 65536, 65537, 100003
        };
        for (size_t i = 0; i < sizeof(limites) / sizeof(*limites); ++i)
            total += testar_tamanho(limites[i], 41);
        /* Mais entradas aleatorias, com diversos tamanhos e seeds. */
        estado = 123;
        for (unsigned i = 0; i < 80; ++i) {
            size_t n = (size_t)(aleatorio() % 20001);
            total += testar_tamanho(n, (uint64_t)i * 997);
        }
        printf("%zu comparacoes exatas com qsort passaram (32/64 bits).\n", total);
        return 0;
    }
    if (argc != 7 || strcmp(argv[1], "bench")) {
        fprintf(stderr, "Uso: %s test | bench n repeticoes seed bits padrao\n", argv[0]);
        return 2;
    }
    size_t n = numero(argv[2]), repeticoes = numero(argv[3]);
    uint64_t seed = numero(argv[4]);
    size_t bits_arg = numero(argv[5]), padrao_arg = numero(argv[6]);
    if ((bits_arg != 32 && bits_arg != 64) || !repeticoes || repeticoes > 10000 ||
        padrao_arg > 8 || n > SIZE_MAX / 8 || n > (size_t)INT64_MAX / 2) return 2;
    int bits = (int)bits_arg;
    unsigned padrao = (unsigned)padrao_arg;
    size_t bytes = n * ((size_t)bits / 8);
    void *entrada = alocar(bytes), *ref = alocar(bytes);
    void *v = alocar(bytes), *aux = alocar(bytes);
    preencher(entrada, n, bits, padrao, seed);
    memcpy(ref, entrada, bytes);
    referencia(ref, n, bits);
    puts("bits,n,seed,padrao,rodada,ordem,variante,cpu_ms,wall_ms,correto");
    /* Rodada 0 aquece ambos e nao entra no CSV. AB/BA alterna a ordem.
     * Reset dos dados, first touch, qsort e verificacao ficam FORA do tempo.
     * Os dois algoritmos recebem a mesma entrada ainda nao ordenada.
     */
    for (size_t rodada = 0; rodada <= repeticoes; ++rodada) {
        for (int ordem = 0; ordem < 2; ++ordem) {
            int exp = ordem ^ (int)(rodada & 1);
            memcpy(v, entrada, bytes);
            memset(aux, 0xa5, bytes);
            double wall_inicio = agora(CLOCK_MONOTONIC);
            double cpu_inicio = agora(CLOCK_PROCESS_CPUTIME_ID);
            void *resultado = ordenar(exp, bits, v, aux, n);
            double cpu_ms = 1000 * (agora(CLOCK_PROCESS_CPUTIME_ID) - cpu_inicio);
            double wall_ms = 1000 * (agora(CLOCK_MONOTONIC) - wall_inicio);
            verificar(resultado, v, aux, ref, bytes, n, bits, padrao, exp);
            if (rodada) {
                printf("%d,%zu,%" PRIu64 ",%u,%zu,%d,%s,%.6f,%.6f,1\n",
                       bits, n, seed, padrao, rodada, ordem,
                       exp ? "experimental" : "baseline", cpu_ms, wall_ms);
                fflush(stdout);
            }
        }
    }
    free(aux); free(v); free(ref); free(entrada);
    return 0;
}
