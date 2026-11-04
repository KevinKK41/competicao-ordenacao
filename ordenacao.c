
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

int carregarDados(const char *nomeArquivo, long long **vetor, size_t *quantidade) {
    FILE *arquivo = fopen(nomeArquivo, "r");

    if (arquivo == NULL) {
        perror("Erro ao abrir o arquivo");
        return 0;
    }

    size_t capacidade = 1024;
    size_t total = 0;

    long long *dados = malloc(capacidade * sizeof(*dados));

    if (dados == NULL) {
        fclose(arquivo);
        return 0;
    }

    long long valor;
    int resultado;

    while ((resultado = fscanf(arquivo, "%lld", &valor)) == 1) {
        if (total == capacidade) {
            if (capacidade > SIZE_MAX / 2 / sizeof(*dados)) {
                free(dados);
                fclose(arquivo);
                return 0;
            }

            size_t novaCapacidade = capacidade * 2;

            long long *temporario = realloc(
                dados, novaCapacidade * sizeof(*dados)
            );

            if (temporario == NULL) {
                free(dados);
                fclose(arquivo);
                return 0;
            }

            dados = temporario;
            capacidade = novaCapacidade;
        }

        dados[total] = valor;
        total++;
    }

    if (resultado != EOF || ferror(arquivo)) {
        fprintf(stderr, "Erro na leitura dos dados.\n");
        free(dados);
        fclose(arquivo);
        return 0;
    }

    fclose(arquivo);

    *vetor = dados;
    *quantidade = total;

    return 1;
}

void mergeSort(long long vetor[], long long auxiliar[],
               size_t inicio, size_t fim) {

    if (fim - inicio <= 1) {
        return;
    }

    size_t meio = inicio + (fim - inicio) / 2;

    mergeSort(vetor, auxiliar, inicio, meio);
    mergeSort(vetor, auxiliar, meio, fim);

    size_t i = inicio;
    size_t j = meio;
    size_t k = inicio;

    while (i < meio && j < fim) {
        if (vetor[i] <= vetor[j]) {
            auxiliar[k] = vetor[i];
            i++;
        } else {
            auxiliar[k] = vetor[j];
            j++;
        }

        k++;
    }

    while (i < meio) {
        auxiliar[k] = vetor[i];
        i++;
        k++;
    }

    while (j < fim) {
        auxiliar[k] = vetor[j];
        j++;
        k++;
    }

    for (k = inicio; k < fim; k++) {
        vetor[k] = auxiliar[k];
    }
}

int estaOrdenado(const long long vetor[], size_t quantidade) {
    for (size_t i = 1; i < quantidade; i++) {
        if (vetor[i - 1] > vetor[i]) {
            return 0;
        }
    }

    return 1;
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Uso: %s arquivo_de_dados.txt\n", argv[0]);
        return 1;
    }

    long long *vetor = NULL;
    size_t quantidade = 0;

    if (!carregarDados(argv[1], &vetor, &quantidade)) {
        return 1;
    }

    if (quantidade == 0) {
        printf("O arquivo nao possui numeros para ordenar.\n");
        free(vetor);
        return 1;
    }

    long long *auxiliar = malloc(quantidade * sizeof(*auxiliar));

    if (auxiliar == NULL) {
        printf("Erro ao alocar memoria auxiliar.\n");
        free(vetor);
        return 1;
    }

    clock_t inicio = clock();

    if (inicio == (clock_t)-1) {
        printf("Erro ao iniciar o cronometro.\n");
        free(auxiliar);
        free(vetor);
        return 1;
    }

    mergeSort(vetor, auxiliar, 0, quantidade);

    clock_t fim = clock();

    if (fim == (clock_t)-1) {
        printf("Erro ao finalizar o cronometro.\n");
        free(auxiliar);
        free(vetor);
        return 1;
    }

    double tempo = (double)(fim - inicio) / CLOCKS_PER_SEC;

    int correto = estaOrdenado(vetor, quantidade);

    printf("Quantidade de elementos: %zu\n", quantidade);
    printf("Verificacao da ordenacao: %s\n",
           correto ? "CORRETA" : "INCORRETA");

    printf("Tempo de ordenacao: %.6f segundos (%.3f ms)\n",
           tempo, tempo * 1000.0);

    free(auxiliar);
    free(vetor);

    return correto ? 0 : 1;
}
