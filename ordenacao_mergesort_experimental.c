#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <limits.h>

/* Variante experimental de ordem.c; o arquivo max nao estava no checkout.
 * GCC: -std=c11 -O3 -march=native -flto
 * Merge Sort sequencial: folhas AVX2 de 8 int32_t / 4 int64_t;
 * insertion sort no fallback. Comparacoes AVX2 sao sempre assinadas.
 * Veja MERGESORT_EXPERIMENTAL.md para limites e protocolo de medicao.
 */
#ifndef BLOCO_INICIAL
#if defined(__AVX2__) && (!defined(MESCLA_AVX2) || MESCLA_AVX2)
#define BLOCO_INICIAL 8u
#else
#define BLOCO_INICIAL 64u
#endif
#endif

#if BLOCO_INICIAL < 2
#error "BLOCO_INICIAL deve ser >= 2"
#endif

/* Folha de 64 bits com ajuste independente (quatro chaves por AVX2). */
#ifndef BLOCO_INICIAL_64
#if defined(__AVX2__) && (!defined(MESCLA_AVX2) || MESCLA_AVX2)
#define BLOCO_INICIAL_64 4u
#else
#define BLOCO_INICIAL_64 64u
#endif
#endif
#if BLOCO_INICIAL_64 < 2
#error "BLOCO_INICIAL_64 deve ser >= 2"
#endif

/* Potencia de dois vezes o tamanho das folhas. Entrada + auxiliar no L2.
 * 16384 elementos ocupam 128 KiB (32 bits) ou 256 KiB (64 bits), com auxiliar.
 */
#ifndef TILE_ELEMENTOS
#define TILE_ELEMENTOS 16384u
#endif
#if TILE_ELEMENTOS < BLOCO_INICIAL || TILE_ELEMENTOS % BLOCO_INICIAL != 0
#error "TILE_ELEMENTOS deve ser BLOCO_INICIAL vezes uma potencia de dois"
#endif
#if ((TILE_ELEMENTOS / BLOCO_INICIAL) & ((TILE_ELEMENTOS / BLOCO_INICIAL) - 1)) != 0
#error "TILE_ELEMENTOS / BLOCO_INICIAL deve ser potencia de dois"
#endif
#if TILE_ELEMENTOS < BLOCO_INICIAL_64 || TILE_ELEMENTOS % BLOCO_INICIAL_64 != 0
#error "TILE_ELEMENTOS deve ser BLOCO_INICIAL_64 vezes uma potencia de dois"
#endif
#if ((TILE_ELEMENTOS / BLOCO_INICIAL_64) & ((TILE_ELEMENTOS / BLOCO_INICIAL_64) - 1)) != 0
#error "TILE_ELEMENTOS / BLOCO_INICIAL_64 deve ser potencia de dois"
#endif

#ifndef MESCLA_DUPLA
#define MESCLA_DUPLA 1
#endif

#ifndef MESCLA_AVX2
#define MESCLA_AVX2 1
#endif
#ifndef AVX2_SEM_DESVIO
#define AVX2_SEM_DESVIO 1
#endif
#ifndef AVX2_DUAS_PONTAS
#define AVX2_DUAS_PONTAS 1
#endif

#if MESCLA_AVX2 && defined(__AVX2__)
#include <immintrin.h>

/* Ordena um vetor bitonico de oito int32_t. Cada etapa compara pares a
 * distancia 4, 2 e 1. min/max sao assinados; INT32_MIN/MAX sao dados normais.
 */
static inline __m256i ordenarBitonico8(__m256i v) {
    __m256i w = _mm256_permute2x128_si256(v, v, 1);
    v = _mm256_blend_epi32(_mm256_min_epi32(v, w),
                           _mm256_max_epi32(v, w), 0xf0);
    w = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
    v = _mm256_blend_epi32(_mm256_min_epi32(v, w),
                           _mm256_max_epi32(v, w), 0xcc);
    w = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1));
    return _mm256_blend_epi32(_mm256_min_epi32(v, w),
                              _mm256_max_epi32(v, w), 0xaa);
}

/* Folha de oito valores: merges bitonicos de 1->2->4->8 elementos.
 * Nao ha deslocamentos quadráticos do insertion sort nesta folha.
 */
static inline __m256i ordenarFolha8(__m256i v) {
    __m256i w = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1));
    v = _mm256_blend_epi32(_mm256_min_epi32(v, w),
                           _mm256_max_epi32(v, w), 0x66);
    w = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
    v = _mm256_blend_epi32(_mm256_min_epi32(v, w),
                           _mm256_max_epi32(v, w), 0x3c);
    w = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1));
    v = _mm256_blend_epi32(_mm256_min_epi32(v, w),
                           _mm256_max_epi32(v, w), 0x5a);
    return ordenarBitonico8(v);
}

/* Rede de MERGE: recebe duas sequencias ja ordenadas, produz as oito
 * menores em ordem e guarda as oito maiores, tambem em ordem, no carry.
 * Nao e radix sort, quicksort nem uma ordenacao por chave alternativa.
 */
static inline __m256i mesclarVetores8(__m256i a, __m256i b, __m256i *carry) {
    const __m256i reverso = _mm256_setr_epi32(7, 6, 5, 4, 3, 2, 1, 0);
    b = _mm256_permutevar8x32_epi32(b, reverso);
    *carry = ordenarBitonico8(_mm256_max_epi32(a, b));
    return ordenarBitonico8(_mm256_min_epi32(a, b));
}

static inline void mesclarAVX2(const int32_t *restrict src,
                               int32_t *restrict dst,
                               size_t inicio, size_t meio, size_t fim) {
    const int32_t *a = src + inicio, *ae = src + meio;
    const int32_t *b = ae, *be = src + fim;
    int32_t *d = dst + inicio;
    __m256i carry = _mm256_loadu_si256((const __m256i *)a);
    __m256i proximo = _mm256_loadu_si256((const __m256i *)b);
    a += 8; b += 8;
    __m256i menor = mesclarVetores8(carry, proximo, &carry);
    _mm256_storeu_si256((__m256i *)d, menor);
    d += 8;
    while (a < ae && b < be) {
        /* Um desvio por vetor, em vez de um por numero. Escolher o bloco
         * com menor cabeca garante que nenhuma saida ultrapasse uma
         * cabeca ainda nao consumida. O carry e o outro fluxo ordenado.
         */
#if AVX2_SEM_DESVIO
        size_t esquerda = (size_t)(*a <= *b);
        const int32_t *escolhido = esquerda ? a : b;
        proximo = _mm256_loadu_si256((const __m256i *)escolhido);
        a += 8 * esquerda;
        b += 8 * (1u - esquerda);
#else
        if (*a <= *b) {
            proximo = _mm256_loadu_si256((const __m256i *)a);
            a += 8;
        } else {
            proximo = _mm256_loadu_si256((const __m256i *)b);
            b += 8;
        }
#endif
        menor = mesclarVetores8(carry, proximo, &carry);
        _mm256_storeu_si256((__m256i *)d, menor);
        d += 8;
    }
    while (a < ae) {
        proximo = _mm256_loadu_si256((const __m256i *)a); a += 8;
        menor = mesclarVetores8(carry, proximo, &carry);
        _mm256_storeu_si256((__m256i *)d, menor); d += 8;
    }
    while (b < be) {
        proximo = _mm256_loadu_si256((const __m256i *)b); b += 8;
        menor = mesclarVetores8(carry, proximo, &carry);
        _mm256_storeu_si256((__m256i *)d, menor); d += 8;
    }
    _mm256_storeu_si256((__m256i *)d, carry);
}

/* AVX2 nao tem min/max assinados de int64_t. A mascara de cmpgt_epi64
 * seleciona o menor/maior com blendv, sem truncar as chaves a 32 bits.
 * alto marca as posicoes que devem receber o maior de cada par.
 */
static inline __m256i compararTrocar64(__m256i v, __m256i w, __m256i alto) {
    __m256i trocar = _mm256_xor_si256(_mm256_cmpgt_epi64(v, w), alto);
    return _mm256_blendv_epi8(v, w, trocar);
}

static inline __m256i ordenarBitonico4(__m256i v) {
    __m256i w = _mm256_permute2x128_si256(v, v, 1);
    v = compararTrocar64(v, w, _mm256_setr_epi64x(0, 0, -1, -1));
    w = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
    return compararTrocar64(v, w, _mm256_setr_epi64x(0, -1, 0, -1));
}

static inline __m256i ordenarFolha4(__m256i v) {
    __m256i w = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
    v = compararTrocar64(v, w, _mm256_setr_epi64x(0, -1, -1, 0));
    return ordenarBitonico4(v);
}

static inline __m256i mesclarVetores4(__m256i a, __m256i b, __m256i *carry) {
    b = _mm256_permute4x64_epi64(b, _MM_SHUFFLE(0, 1, 2, 3));
    __m256i maiorA = _mm256_cmpgt_epi64(a, b);
    __m256i menor = _mm256_blendv_epi8(a, b, maiorA);
    __m256i maior = _mm256_blendv_epi8(b, a, maiorA);
    *carry = ordenarBitonico4(maior);
    return ordenarBitonico4(menor);
}

static inline void mesclarAVX2_64(const int64_t *restrict src,
                                  int64_t *restrict dst,
                                  size_t inicio, size_t meio, size_t fim) {
    const int64_t *a = src + inicio, *ae = src + meio;
    const int64_t *b = ae, *be = src + fim;
    int64_t *d = dst + inicio;
    __m256i carry = _mm256_loadu_si256((const __m256i *)a);
    __m256i proximo = _mm256_loadu_si256((const __m256i *)b);
    a += 4; b += 4;
    __m256i menor = mesclarVetores4(carry, proximo, &carry);
    _mm256_storeu_si256((__m256i *)d, menor); d += 4;
    while (a < ae && b < be) {
#if AVX2_SEM_DESVIO
        size_t esquerda = (size_t)(*a <= *b);
        const int64_t *escolhido = esquerda ? a : b;
        proximo = _mm256_loadu_si256((const __m256i *)escolhido);
        a += 4 * esquerda;
        b += 4 * (1u - esquerda);
#else
        if (*a <= *b) {
            proximo = _mm256_loadu_si256((const __m256i *)a); a += 4;
        } else {
            proximo = _mm256_loadu_si256((const __m256i *)b); b += 4;
        }
#endif
        menor = mesclarVetores4(carry, proximo, &carry);
        _mm256_storeu_si256((__m256i *)d, menor); d += 4;
    }
    while (a < ae) {
        proximo = _mm256_loadu_si256((const __m256i *)a); a += 4;
        menor = mesclarVetores4(carry, proximo, &carry);
        _mm256_storeu_si256((__m256i *)d, menor); d += 4;
    }
    while (b < be) {
        proximo = _mm256_loadu_si256((const __m256i *)b); b += 4;
        menor = mesclarVetores4(carry, proximo, &carry);
        _mm256_storeu_si256((__m256i *)d, menor); d += 4;
    }
    _mm256_storeu_si256((__m256i *)d, carry);
}

static inline void mesclarAVX2_64_dupla(const int64_t *restrict src,
                                        int64_t *restrict dst,
                                        size_t inicio, size_t meio, size_t fim) {
    const int64_t *a = src + inicio, *ae = src + meio;
    const int64_t *b = ae, *be = src + fim;
    int64_t *d = dst + inicio, *de = dst + fim;
    __m256i frente = _mm256_loadu_si256((const __m256i *)a);
    __m256i proximo = _mm256_loadu_si256((const __m256i *)b);
    a += 4; b += 4;
    __m256i menor = mesclarVetores4(frente, proximo, &frente);
    _mm256_storeu_si256((__m256i *)d, menor); d += 4;

    ae -= 4; be -= 4;
    __m256i fundo = _mm256_loadu_si256((const __m256i *)ae);
    proximo = _mm256_loadu_si256((const __m256i *)be);
    __m256i maior;
    fundo = mesclarVetores4(fundo, proximo, &maior);
    de -= 4; _mm256_storeu_si256((__m256i *)de, maior);
    /* Dois carries independentes escondem parte da latencia das redes
     * cmpgt/blend de 64 bits. As duas metades precisam ter >=8 restantes:
     * uma iteracao pode consumir os dois vetores do mesmo lado.
     */
    while ((size_t)(ae - a) >= 8 && (size_t)(be - b) >= 8) {
        size_t esquerda = (size_t)(*a <= *b);
        const int64_t *p = esquerda ? a : b;
        __m256i vetorFrente = _mm256_loadu_si256((const __m256i *)p);
        a += 4 * esquerda; b += 4 * (1u - esquerda);
        esquerda = (size_t)(ae[-1] > be[-1]);
        p = esquerda ? ae : be;
        __m256i vetorFundo = _mm256_loadu_si256((const __m256i *)(p - 4));
        ae -= 4 * esquerda; be -= 4 * (1u - esquerda);

        menor = mesclarVetores4(frente, vetorFrente, &frente);
        fundo = mesclarVetores4(fundo, vetorFundo, &maior);
        _mm256_storeu_si256((__m256i *)d, menor); d += 4;
        de -= 4; _mm256_storeu_si256((__m256i *)de, maior);
    }
    /* Ao esgotar um lado, os dois carries podem se intercalar. Nunca
     * presumimos frente <= fundo. Ordenamos os oito valores pendentes e
     * terminamos por merge escalar de TRES fluxos: a, b e reservado.
     * Selecionar vetores inteiros de tres fluxos so pelas cabecas nao e
     * correto em geral (especialmente com repetidos). O acabamento escalar
     * preserva cada valor e tambem trata metades de tamanhos desiguais.
     */
    int64_t reservado[8];
    menor = mesclarVetores4(frente, fundo, &maior);
    _mm256_storeu_si256((__m256i *)reservado, menor);
    _mm256_storeu_si256((__m256i *)(reservado + 4), maior);
    const int64_t *c = reservado, *ce = reservado + 8;
    while (a < ae && b < be && c < ce) {
        if (*a <= *b) {
            if (*a <= *c) *d++ = *a++;
            else *d++ = *c++;
        } else {
            if (*b <= *c) *d++ = *b++;
            else *d++ = *c++;
        }
    }
    while (a < ae && b < be) {
        if (*a <= *b) *d++ = *a++;
        else *d++ = *b++;
    }
    while (a < ae && c < ce) {
        if (*a <= *c) *d++ = *a++;
        else *d++ = *c++;
    }
    while (b < be && c < ce) {
        if (*b <= *c) *d++ = *b++;
        else *d++ = *c++;
    }
    if (a < ae) memcpy(d, a, (size_t)(ae - a) * sizeof(*a));
    else if (b < be) memcpy(d, b, (size_t)(be - b) * sizeof(*b));
    else if (c < ce) memcpy(d, c, (size_t)(ce - c) * sizeof(*c));
}
#endif

typedef struct {
    void *valores;
    size_t quantidade;
    size_t capacidade;
    int usa64;
} Dados;

static void liberarDados(Dados *dados) {
    free(dados->valores);
    dados->valores = NULL;
    dados->quantidade = 0;
    dados->capacidade = 0;
    dados->usa64 = 0;
}

static int carregarDados(const char *nomeArquivo, Dados *dados) {
    FILE *arquivo = fopen(nomeArquivo, "r");
    if (!arquivo) {
        perror("Erro ao abrir o arquivo");
        return 0;
    }

    dados->capacidade = 4096;
    dados->quantidade = 0;
    dados->usa64 = 0;
    dados->valores = malloc(dados->capacidade * sizeof(int32_t));
    if (!dados->valores) {
        fprintf(stderr, "Erro ao alocar memoria.\n");
        fclose(arquivo);
        return 0;
    }

    int64_t valor;
    int resultado;

    while ((resultado = fscanf(arquivo, "%" SCNd64, &valor)) == 1) {
        if (dados->quantidade == dados->capacidade) {
            if (dados->capacidade > SIZE_MAX / (2 * sizeof(int64_t))) {
                fprintf(stderr, "Quantidade de dados excede o limite.\n");
                goto erro;
            }
            size_t novaCapacidade = dados->capacidade * 2;
            size_t tamanho = dados->usa64 ? sizeof(int64_t) : sizeof(int32_t);
            void *novo = realloc(dados->valores, novaCapacidade * tamanho);
            if (!novo) {
                fprintf(stderr, "Memoria insuficiente para os dados.\n");
                goto erro;
            }
            dados->valores = novo;
            dados->capacidade = novaCapacidade;
        }

        if (!dados->usa64 && (valor < INT32_MIN || valor > INT32_MAX)) {
            int64_t *novo = malloc(dados->capacidade * sizeof(int64_t));
            if (!novo) {
                fprintf(stderr, "Memoria insuficiente para valores de 64 bits.\n");
                goto erro;
            }
            int32_t *antigo = (int32_t *)dados->valores;
            for (size_t i = 0; i < dados->quantidade; ++i) {
                novo[i] = antigo[i];
            }
            free(dados->valores);
            dados->valores = novo;
            dados->usa64 = 1;
        }

        if (dados->usa64) {
            ((int64_t *)dados->valores)[dados->quantidade++] = valor;
        } else {
            ((int32_t *)dados->valores)[dados->quantidade++] = (int32_t)valor;
        }
    }

    if (resultado != EOF || ferror(arquivo)) {
        fprintf(stderr, "Arquivo contem valores invalidos ou houve erro de leitura.\n");
        goto erro;
    }
    if (fclose(arquivo) != 0) {
        fprintf(stderr, "Erro ao fechar o arquivo de entrada.\n");
        liberarDados(dados);
        return 0;
    }
    return 1;

erro:
    fclose(arquivo);
    liberarDados(dados);
    return 0;
}

/* Merge Sort de 32 bits, sem sentinelas numericas. AVX2 nao promete
 * estabilidade para registros com payload; so ordenamos inteiros.
 */
static inline void insertion32(int32_t *v, size_t n) {
#if MESCLA_AVX2 && defined(__AVX2__)
    if (n == 8) {
        __m256i valores = _mm256_loadu_si256((const __m256i *)v);
        _mm256_storeu_si256((__m256i *)v, ordenarFolha8(valores));
        return;
    }
#endif
    for (size_t i = 1; i < n; ++i) {
        int32_t atual = v[i];
        size_t j = i;
        while (j && v[j - 1] > atual) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = atual;
    }
}

static inline void mesclar32(const int32_t *restrict src,
                              int32_t *restrict dst,
                              size_t inicio, size_t meio, size_t fim) {
    if (meio == fim || src[meio - 1] <= src[meio]) {
        memcpy(dst + inicio, src + inicio, (fim - inicio) * sizeof(*src));
        return;
    }
#if MESCLA_AVX2 && defined(__AVX2__)
    /* So vetores completos; caudas arbitrarias usam o merge escalar.
     * O caminho AVX2 ordena inteiros, mas nao promete estabilidade para
     * registros com payload. O fallback escalar e estavel na mesclagem.
     */
    if ((meio - inicio) % 8 == 0 && (fim - meio) % 8 == 0) {
        mesclarAVX2(src, dst, inicio, meio, fim);
        return;
    }
#endif
    const int32_t *a = src + inicio, *ae = src + meio;
    const int32_t *b = ae, *be = src + fim;
    int32_t *d = dst + inicio;
#if MESCLA_DUPLA
    int32_t *de = dst + fim;
    /* Duas cadeias independentes: minimo na frente, maximo no fundo.
     * Oito elementos por lado garantem que os 4 pares nunca se cruzem,
     * mesmo se todos os oito escolhidos vierem de uma unica metade.
     * Testamos os limites uma vez por oito saidas; nao lemos fora do vetor.
     */
    while ((size_t)(ae - a) >= 8 && (size_t)(be - b) >= 8) {
        for (unsigned passo = 0; passo < 4; ++passo) {
            int32_t x = *a, y = *b;
            size_t esquerda = (size_t)(x <= y);
            *d++ = esquerda ? x : y;
            a += esquerda;
            b += 1u - esquerda;

            x = ae[-1]; y = be[-1];
            esquerda = (size_t)(x > y);
            *--de = esquerda ? x : y;
            ae -= esquerda;
            be -= 1u - esquerda;
        }
    }
#endif
    while (a < ae && b < be) {
        int32_t x = *a, y = *b;
        size_t esquerda = (size_t)(x <= y);
        *d++ = esquerda ? x : y;
        a += esquerda;
        b += 1u - esquerda;
    }
    if (a < ae) memcpy(d, a, (size_t)(ae - a) * sizeof(*a));
    else if (b < be) memcpy(d, b, (size_t)(be - b) * sizeof(*b));
}

/* Retorna o buffer que contem o resultado. v e aux sao disjuntos, com n
 * elementos cada; ambos continuam pertencendo ao chamador. Nao copia de
 * volta: o chamador deve usar o ponteiro retornado (pode ser aux).
 */
static int32_t *mergeSort32(int32_t *v, int32_t *aux, size_t n) {
    if (n < 2) return v;
    int crescente = 1, decrescente = 1;
    for (size_t i = 1; i < n; ++i) {
        if (v[i - 1] > v[i]) crescente = 0;
        if (v[i - 1] < v[i]) decrescente = 0;
        if (!crescente && !decrescente) break;
    }
    if (crescente) return v;
    if (decrescente) {
        for (size_t i = 0, j = n - 1; i < j; ++i, --j) {
            int32_t tmp = v[i]; v[i] = v[j]; v[j] = tmp;
        }
        return v;
    }

    size_t tile = BLOCO_INICIAL;
    unsigned etapas = 0;
    while (tile < TILE_ELEMENTOS && tile < n) {
        tile *= 2;
        ++etapas;
    }
    /* Termina os niveis locais de um tile antes de percorrer o seguinte.
     * Mantem entrada + auxiliar quentes no cache, em vez de fazer cada
     * nivel pequeno percorrer os 40/80 MB de entrada inteira.
     */
    for (size_t base = 0; base < n;) {
        size_t total = n - base;
        if (total > tile) total = tile;
        int32_t *local = v + base, *temporario = aux + base;
        for (size_t i = 0; i < total;) {
            size_t tamanho = total - i;
            if (tamanho > BLOCO_INICIAL) tamanho = BLOCO_INICIAL;
            insertion32(local + i, tamanho);
            i += tamanho;
        }
        for (size_t largura = BLOCO_INICIAL; largura < tile; largura *= 2) {
            for (size_t i = 0; i < total;) {
                size_t meio = i + (largura < total - i ? largura : total - i);
                size_t fim = meio + (largura < total - meio ? largura : total - meio);
                mesclar32(local, temporario, i, meio, fim);
                i = fim;
            }
            int32_t *tmp = local; local = temporario; temporario = tmp;
        }
        base += total;
    }
    int32_t *origem = (etapas & 1u) ? aux : v;
    int32_t *destino = (etapas & 1u) ? v : aux;
    for (size_t largura = tile; largura < n;) {
        for (size_t i = 0; i < n;) {
            size_t meio = i + (largura < n - i ? largura : n - i);
            size_t fim = meio + (largura < n - meio ? largura : n - meio);
            mesclar32(origem, destino, i, meio, fim);
            i = fim;
        }
        int32_t *tmp = origem; origem = destino; destino = tmp;
        if (largura >= n - largura) break;
        largura *= 2;
    }
    return origem;
}

/* Merge Sort de 64 bits, sem sentinelas numericas ou estreitamento. */
static inline void insertion64(int64_t *v, size_t n) {
#if MESCLA_AVX2 && defined(__AVX2__)
    if (n == 4) {
        __m256i valores = _mm256_loadu_si256((const __m256i *)v);
        _mm256_storeu_si256((__m256i *)v, ordenarFolha4(valores));
        return;
    }
#endif
    for (size_t i = 1; i < n; ++i) {
        int64_t atual = v[i];
        size_t j = i;
        while (j && v[j - 1] > atual) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = atual;
    }
}

static inline void mesclar64(const int64_t *restrict src,
                              int64_t *restrict dst,
                              size_t inicio, size_t meio, size_t fim) {
    if (meio == fim || src[meio - 1] <= src[meio]) {
        memcpy(dst + inicio, src + inicio, (fim - inicio) * sizeof(*src));
        return;
    }
#if MESCLA_AVX2 && defined(__AVX2__)
    if ((meio - inicio) % 4 == 0 && (fim - meio) % 4 == 0) {
#if AVX2_DUAS_PONTAS
        if (meio - inicio >= 8 && fim - meio >= 8) {
            mesclarAVX2_64_dupla(src, dst, inicio, meio, fim);
            return;
        }
#endif
        mesclarAVX2_64(src, dst, inicio, meio, fim);
        return;
    }
#endif
    const int64_t *a = src + inicio, *ae = src + meio;
    const int64_t *b = ae, *be = src + fim;
    int64_t *d = dst + inicio;
#if MESCLA_DUPLA
    int64_t *de = dst + fim;
    /* Duas cadeias independentes: minimo na frente, maximo no fundo.
     * Oito elementos por lado garantem que os 4 pares nunca se cruzem,
     * mesmo se todos os oito escolhidos vierem de uma unica metade.
     * Testamos os limites uma vez por oito saidas; nao lemos fora do vetor.
     */
    while ((size_t)(ae - a) >= 8 && (size_t)(be - b) >= 8) {
        for (unsigned passo = 0; passo < 4; ++passo) {
            int64_t x = *a, y = *b;
            size_t esquerda = (size_t)(x <= y);
            *d++ = esquerda ? x : y;
            a += esquerda;
            b += 1u - esquerda;

            x = ae[-1]; y = be[-1];
            esquerda = (size_t)(x > y);
            *--de = esquerda ? x : y;
            ae -= esquerda;
            be -= 1u - esquerda;
        }
    }
#endif
    while (a < ae && b < be) {
        int64_t x = *a, y = *b;
        size_t esquerda = (size_t)(x <= y);
        *d++ = esquerda ? x : y;
        a += esquerda;
        b += 1u - esquerda;
    }
    if (a < ae) memcpy(d, a, (size_t)(ae - a) * sizeof(*a));
    else if (b < be) memcpy(d, b, (size_t)(be - b) * sizeof(*b));
}

/* Retorna o buffer que contem o resultado. v e aux sao disjuntos, com n
 * elementos cada; ambos continuam pertencendo ao chamador. Nao copia de
 * volta: o chamador deve usar o ponteiro retornado (pode ser aux).
 */
static int64_t *mergeSort64(int64_t *v, int64_t *aux, size_t n) {
    if (n < 2) return v;
    int crescente = 1, decrescente = 1;
    for (size_t i = 1; i < n; ++i) {
        if (v[i - 1] > v[i]) crescente = 0;
        if (v[i - 1] < v[i]) decrescente = 0;
        if (!crescente && !decrescente) break;
    }
    if (crescente) return v;
    if (decrescente) {
        for (size_t i = 0, j = n - 1; i < j; ++i, --j) {
            int64_t tmp = v[i]; v[i] = v[j]; v[j] = tmp;
        }
        return v;
    }

    size_t tile = BLOCO_INICIAL_64;
    unsigned etapas = 0;
    while (tile < TILE_ELEMENTOS && tile < n) {
        tile *= 2;
        ++etapas;
    }
    /* Termina os niveis locais de um tile antes de percorrer o seguinte.
     * Mantem entrada + auxiliar quentes no cache, em vez de fazer cada
     * nivel pequeno percorrer os 40/80 MB de entrada inteira.
     */
    for (size_t base = 0; base < n;) {
        size_t total = n - base;
        if (total > tile) total = tile;
        int64_t *local = v + base, *temporario = aux + base;
        for (size_t i = 0; i < total;) {
            size_t tamanho = total - i;
            if (tamanho > BLOCO_INICIAL_64) tamanho = BLOCO_INICIAL_64;
            insertion64(local + i, tamanho);
            i += tamanho;
        }
        for (size_t largura = BLOCO_INICIAL_64; largura < tile; largura *= 2) {
            for (size_t i = 0; i < total;) {
                size_t meio = i + (largura < total - i ? largura : total - i);
                size_t fim = meio + (largura < total - meio ? largura : total - meio);
                mesclar64(local, temporario, i, meio, fim);
                i = fim;
            }
            int64_t *tmp = local; local = temporario; temporario = tmp;
        }
        base += total;
    }
    int64_t *origem = (etapas & 1u) ? aux : v;
    int64_t *destino = (etapas & 1u) ? v : aux;
    for (size_t largura = tile; largura < n;) {
        for (size_t i = 0; i < n;) {
            size_t meio = i + (largura < n - i ? largura : n - i);
            size_t fim = meio + (largura < n - meio ? largura : n - meio);
            mesclar64(origem, destino, i, meio, fim);
            i = fim;
        }
        int64_t *tmp = origem; origem = destino; destino = tmp;
        if (largura >= n - largura) break;
        largura *= 2;
    }
    return origem;
}

/* Checksums independentes da ordem detectam perdas e alteracoes acidentais. */
typedef struct {
    uint64_t soma;
    uint64_t xorval;
} ImpressaoDigital;

static uint64_t misturar64(uint64_t valor) {
    valor += UINT64_C(0x9e3779b97f4a7c15);
    valor = (valor ^ (valor >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    valor = (valor ^ (valor >> 27)) * UINT64_C(0x94d049bb133111eb);
    return valor ^ (valor >> 31);
}

static ImpressaoDigital verificarDados(const Dados *dados, int *ordenado) {
    ImpressaoDigital h = {0, 0};
    *ordenado = 1;
    size_t n = dados->quantidade;

    if (dados->usa64) {
        const int64_t *v = (const int64_t *)dados->valores;
        for (size_t i = 0; i < n; ++i) {
            uint64_t mistura = misturar64((uint64_t)v[i]);
            h.soma += mistura;
            h.xorval ^= mistura;
            if (i && v[i - 1] > v[i]) *ordenado = 0;
        }
    } else {
        const int32_t *v = (const int32_t *)dados->valores;
        for (size_t i = 0; i < n; ++i) {
            uint64_t mistura = misturar64((uint64_t)(int64_t)v[i]);
            h.soma += mistura;
            h.xorval ^= mistura;
            if (i && v[i - 1] > v[i]) *ordenado = 0;
        }
    }
    return h;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s dados.txt\n", argv[0]);
        return EXIT_FAILURE;
    }

    Dados dados = {0};
    if (!carregarDados(argv[1], &dados)) return EXIT_FAILURE;
    if (!dados.quantidade) {
        fprintf(stderr, "Arquivo de dados vazio.\n");
        liberarDados(&dados);
        return EXIT_FAILURE;
    }

    size_t bytesElemento = dados.usa64 ? sizeof(int64_t) : sizeof(int32_t);
    if (dados.quantidade > SIZE_MAX / bytesElemento) {
        fprintf(stderr, "Quantidade grande demais para o sistema.\n");
        liberarDados(&dados);
        return EXIT_FAILURE;
    }

    void *auxiliar = malloc(dados.quantidade * bytesElemento);
    if (!auxiliar) {
        fprintf(stderr, "Nao foi possivel alocar o vetor auxiliar.\n");
        liberarDados(&dados);
        return EXIT_FAILURE;
    }

    int ordenadoAntes;
    ImpressaoDigital antes = verificarDados(&dados, &ordenadoAntes);

    clock_t inicio = clock();
    if (inicio == (clock_t)-1) {
        fprintf(stderr, "Erro ao iniciar o cronometro.\n");
        free(auxiliar);
        liberarDados(&dados);
        return EXIT_FAILURE;
    }

    void *resultado;
    if (dados.usa64) {
        resultado = mergeSort64(dados.valores, auxiliar, dados.quantidade);
    } else {
        resultado = mergeSort32(dados.valores, auxiliar, dados.quantidade);
    }
    if (resultado == auxiliar) {
        auxiliar = dados.valores;
        dados.valores = resultado;
    }

    clock_t fim = clock();
    if (fim == (clock_t)-1) {
        fprintf(stderr, "Erro ao finalizar o cronometro.\n");
        free(auxiliar);
        liberarDados(&dados);
        return EXIT_FAILURE;
    }

    int ordenadoDepois;
    ImpressaoDigital depois = verificarDados(&dados, &ordenadoDepois);
    int correto = ordenadoDepois && antes.soma == depois.soma &&
                  antes.xorval == depois.xorval;

    double segundos = (double)(fim - inicio) / CLOCKS_PER_SEC;
    printf("Quantidade de elementos: %zu\n", dados.quantidade);
    printf("Verificacao da ordenacao: %s\n", correto ? "CORRETA" : "INCORRETA");
    printf("Tempo de ordenacao: %.6f segundos (%.3f ms)\n",
           segundos, 1000.0 * segundos);

    free(auxiliar);
    liberarDados(&dados);
    return correto ? EXIT_SUCCESS : EXIT_FAILURE;
}
