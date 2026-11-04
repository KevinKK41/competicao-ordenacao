#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <limits.h>

/* Altere para 16 ou 64 somente se quiser comparar benchmarks. */
#ifndef BLOCO_INICIAL
#define BLOCO_INICIAL 32u
#endif

#if BLOCO_INICIAL < 2
#error "BLOCO_INICIAL deve ser >= 2"
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

/* Merge Sort especializado para inteiros de 32 bits. */
static inline void insertion32(int32_t *v, size_t quantidade) {
    for (size_t i = 1; i < quantidade; ++i) {
        int32_t atual = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > atual) {
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

    size_t i = inicio, j = meio, k = inicio;

    while (i < meio && j < fim) {
        int32_t a = src[i];
        int32_t b = src[j];
        size_t esquerda = (size_t)(a <= b);
        dst[k++] = esquerda ? a : b;
        i += esquerda;
        j += 1u - esquerda;
    }

    if (i < meio) {
        memcpy(dst + k, src + i, (meio - i) * sizeof(*src));
    } else if (j < fim) {
        memcpy(dst + k, src + j, (fim - j) * sizeof(*src));
    }
}

static void mergeSort32(int32_t *v, int32_t *aux, size_t n) {
    if (n < 2) return;

    /* Casos especiais continuam dentro do cronometro da ordenacao. */
    int crescente = 1, decrescente = 1;
    for (size_t i = 1; i < n; ++i) {
        if (v[i - 1] > v[i]) crescente = 0;
        if (v[i - 1] < v[i]) decrescente = 0;
        if (!crescente && !decrescente) break;
    }
    if (crescente) return;

    if (decrescente) {
        for (size_t i = 0, j = n - 1; i < j; ++i, --j) {
            int32_t tmp = v[i];
            v[i] = v[j];
            v[j] = tmp;
        }
        return;
    }

    for (size_t inicio = 0; inicio < n;) {
        size_t tamanho = n - inicio;
        if (tamanho > BLOCO_INICIAL) tamanho = BLOCO_INICIAL;
        insertion32(v + inicio, tamanho);
        inicio += tamanho;
    }

    int32_t *origem = v;
    int32_t *destino = aux;

    for (size_t largura = BLOCO_INICIAL; largura < n;) {
        size_t inicio = 0;
        while (inicio < n) {
            size_t esquerdo = n - inicio;
            size_t meio = inicio + (largura < esquerdo ? largura : esquerdo);
            size_t direito = n - meio;
            size_t fim = meio + (largura < direito ? largura : direito);
            mesclar32(origem, destino, inicio, meio, fim);
            inicio = fim;
        }

        int32_t *tmp = origem;
        origem = destino;
        destino = tmp;

        if (largura >= n - largura) break;
        largura *= 2;
    }

    if (origem != v) {
        memcpy(v, origem, n * sizeof(*v));
    }
}

/* Merge Sort especializado para inteiros de 64 bits. */
static inline void insertion64(int64_t *v, size_t quantidade) {
    for (size_t i = 1; i < quantidade; ++i) {
        int64_t atual = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > atual) {
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

    size_t i = inicio, j = meio, k = inicio;

    while (i < meio && j < fim) {
        int64_t a = src[i];
        int64_t b = src[j];
        size_t esquerda = (size_t)(a <= b);
        dst[k++] = esquerda ? a : b;
        i += esquerda;
        j += 1u - esquerda;
    }

    if (i < meio) {
        memcpy(dst + k, src + i, (meio - i) * sizeof(*src));
    } else if (j < fim) {
        memcpy(dst + k, src + j, (fim - j) * sizeof(*src));
    }
}

static void mergeSort64(int64_t *v, int64_t *aux, size_t n) {
    if (n < 2) return;

    /* Casos especiais continuam dentro do cronometro da ordenacao. */
    int crescente = 1, decrescente = 1;
    for (size_t i = 1; i < n; ++i) {
        if (v[i - 1] > v[i]) crescente = 0;
        if (v[i - 1] < v[i]) decrescente = 0;
        if (!crescente && !decrescente) break;
    }
    if (crescente) return;

    if (decrescente) {
        for (size_t i = 0, j = n - 1; i < j; ++i, --j) {
            int64_t tmp = v[i];
            v[i] = v[j];
            v[j] = tmp;
        }
        return;
    }

    for (size_t inicio = 0; inicio < n;) {
        size_t tamanho = n - inicio;
        if (tamanho > BLOCO_INICIAL) tamanho = BLOCO_INICIAL;
        insertion64(v + inicio, tamanho);
        inicio += tamanho;
    }

    int64_t *origem = v;
    int64_t *destino = aux;

    for (size_t largura = BLOCO_INICIAL; largura < n;) {
        size_t inicio = 0;
        while (inicio < n) {
            size_t esquerdo = n - inicio;
            size_t meio = inicio + (largura < esquerdo ? largura : esquerdo);
            size_t direito = n - meio;
            size_t fim = meio + (largura < direito ? largura : direito);
            mesclar64(origem, destino, inicio, meio, fim);
            inicio = fim;
        }

        int64_t *tmp = origem;
        origem = destino;
        destino = tmp;

        if (largura >= n - largura) break;
        largura *= 2;
    }

    if (origem != v) {
        memcpy(v, origem, n * sizeof(*v));
    }
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

    if (dados.usa64) {
        mergeSort64((int64_t *)dados.valores, (int64_t *)auxiliar,
                    dados.quantidade);
    } else {
        mergeSort32((int32_t *)dados.valores, (int32_t *)auxiliar,
                    dados.quantidade);
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
