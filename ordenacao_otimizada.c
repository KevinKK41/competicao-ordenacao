
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

static int carregarDados(const char *nomeArquivo, long long **vetor, size_t *quantidade) {
    FILE *arquivo = fopen(nomeArquivo, "r");

    if (arquivo == NULL) {
        perror("Erro ao abrir o arquivo");
        return 0;
    }

    size_t capacidade = 1024;
    size_t total = 0;

    long long *dados = malloc(capacidade * sizeof(*dados));

    if (dados == NULL) {
        fprintf(stderr, "Erro ao alocar memoria para os dados.\n");
        fclose(arquivo);
        return 0;
    }

    long long valor;
    int resultado;

    while ((resultado = fscanf(arquivo, "%lld", &valor)) == 1) {
        if (total == capacidade) {
            if (capacidade > SIZE_MAX / 2 / sizeof(*dados)) {
                fprintf(stderr, "Quantidade de dados excede o limite de memoria.\n");
                free(dados);
                fclose(arquivo);
                return 0;
            }

            size_t novaCapacidade = capacidade * 2;

            long long *temporario = realloc(
                dados, novaCapacidade * sizeof(*dados)
            );

            if (temporario == NULL) {
                fprintf(stderr, "Erro ao aumentar a memoria dos dados.\n");
                free(dados);
                fclose(arquivo);
                return 0;
            }

            dados = temporario;
            capacidade = novaCapacidade;
        }

        dados[total++] = valor;
    }

    if (resultado != EOF || ferror(arquivo)) {
        fprintf(stderr, "Erro: dado invalido ou falha de leitura.\n");
        free(dados);
        fclose(arquivo);
        return 0;
    }

    fclose(arquivo);

    *vetor = dados;
    *quantidade = total;

    return 1;
}

static void mergeSortIterativo(long long vetor[], long long auxiliar[], size_t n) {
    long long *origem = vetor;
    long long *destino = auxiliar;

    for (size_t largura = 1; largura < n;) {
        size_t inicio = 0;

        while (inicio < n) {
            size_t restante = n - inicio;

            size_t meio = inicio +
                (largura < restante ? largura : restante);

            restante = n - meio;

            size_t fim = meio +
                (largura < restante ? largura : restante);

            if (meio == fim) {
                memcpy(destino + inicio, origem + inicio,
                       (fim - inicio) * sizeof(*origem));

                inicio = fim;
                continue;
            }

            size_t i = inicio;
            size_t j = meio;
            size_t k = inicio;

            while (i < meio && j < fim) {
                if (origem[i] <= origem[j]) {
                    destino[k++] = origem[i++];
                } else {
                    destino[k++] = origem[j++];
                }
            }

            if (i < meio) {
                size_t restanteEsquerda = meio - i;

                memcpy(destino + k, origem + i,
                       restanteEsquerda * sizeof(*origem));

            } else if (j < fim) {
                size_t restanteDireita = fim - j;

                memcpy(destino + k, origem + j,
                       restanteDireita * sizeof(*origem));
            }

            inicio = fim;
        }

        long long *temporario = origem;
        origem = destino;
        destino = temporario;

        if (largura > n / 2) {
            break;
        }

        largura *= 2;
    }

    if (origem != vetor) {
        memcpy(vetor, origem, n * sizeof(*vetor));
    }
}

static int estaOrdenado(const long long vetor[], size_t quantidade) {
    for (size_t i = 1; i < quantidade; i++) {
        if (vetor[i - 1] > vetor[i]) {
            return 0;
        }
    }

    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s arquivo_de_dados.txt\n", argv[0]);
        return 1;
    }

    long long *vetor = NULL;
    size_t quantidade = 0;

    if (!carregarDados(argv[1], &vetor, &quantidade)) {
        return 1;
    }

    if (quantidade == 0) {
        fprintf(stderr, "O arquivo nao possui numeros para ordenar.\n");
        free(vetor);
        return 1;
    }

    if (quantidade > SIZE_MAX / sizeof(long long)) {
        fprintf(stderr, "Quantidade de dados excede o limite de memoria auxiliar.\n");
        free(vetor);
        return 1;
    }

    long long *auxiliar = malloc(quantidade * sizeof(*auxiliar));

    if (auxiliar == NULL) {
        fprintf(stderr, "Erro ao alocar memoria auxiliar.\n");
        free(vetor);
        return 1;
    }

    clock_t inicio = clock();

    if (inicio == (clock_t)-1) {
        fprintf(stderr, "Erro ao iniciar o cronometro.\n");
        free(auxiliar);
        free(vetor);
        return 1;
    }

    mergeSortIterativo(vetor, auxiliar, quantidade);

    clock_t fim = clock();

    if (fim == (clock_t)-1) {
        fprintf(stderr, "Erro ao finalizar o cronometro.\n");
        free(auxiliar);
        free(vetor);
        return 1;
    }

    double tempoSegundos =
        (double)(fim - inicio) / CLOCKS_PER_SEC;

    int correto = estaOrdenado(vetor, quantidade);

    printf("Quantidade de elementos: %zu\n", quantidade);

    printf("Verificacao da ordenacao: %s\n",
           correto ? "CORRETA" : "INCORRETA");

    printf("Tempo de ordenacao: %.6f segundos (%.3f ms)\n",
           tempoSegundos, tempoSegundos * 1000.0);

    free(auxiliar);
    free(vetor);

    return correto ? 0 : 1;
}
