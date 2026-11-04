# Merge Sort experimental para inteiros de 64 bits

O arquivo novo e `ordenacao_mergesort_experimental.c`. Nenhum arquivo original
foi alterado. `ordenacao_mergesort_max.c` nao estava no checkout (commit
`899c247`), e a consulta ao GitHub falhou na conexao com o proxy. Por isso, a
analise e os benchmarks usam **`ordem.c` com `BLOCO_INICIAL=64`** como baseline.
Isso nao estabelece uma comparacao com o arquivo ausente nem com os 282 ms
informados para o Ryzen 7 5700U.

O alvo principal, confirmado pelo usuario, e **10.000.000 de inteiros que
precisam de 64 bits**. Os benchmarks finais usam valores assinados de 64 bits,
nao uma substituicao por chaves de 32 bits. O programa tambem preserva o suporte
a entradas de 32 bits do baseline.

## Analise do baseline disponivel

`ordem.c` ja usa Merge Sort iterativo, buffers alternados, `restrict`, `memcpy`
para sobras e mesclagens dispensaveis, tratamento de entrada crescente/decrescente
e comparacao escalar sem um `if` por elemento. Essas caracteristicas foram
preservadas; nao sao novidades da variante.

Mesmo sem o desvio na comparacao, atualizar os indices e carregar as proximas
cabecas cria uma cadeia de dependencias por elemento. Alem disso, o baseline
conclui cada nivel sobre o vetor inteiro antes de iniciar o proximo: os niveis
pequenos tambem percorrem os 80 MB de dados. Insertion sort em cada folha de
64 valores faz deslocamentos de elementos; reduzi-lo tem um custo em niveis
adicionais de merge, que precisa ser medido.

## Mudancas na variante

1. **Folhas de quatro `int64_t` com AVX2.** Redes de comparacao mesclam
   subsequencias de 1 para 2 e de 2 para 4 elementos. Substituem o insertion sort
   das folhas de 64 no caminho vetorial. Ha quatro niveis adicionais de merge
   em relacao a folhas de 64, portanto folhas menores nao sao, por si so,
   garantia de ganho. Para `int32_t`, a folha vetorial tem oito elementos.

2. **Merge vetorial de dois fluxos ordenados.** Reverte-se um dos vetores para
   formar uma sequencia bitonica. Comparacoes e permutacoes produzem os quatro
   menores e os quatro maiores valores, ambos ordenados. Os menores seguem
   para a saida; os maiores ficam em registradores para o proximo passo.
   AVX2 nao possui min/max assinados de 64 bits: usamos `cmpgt_epi64` e
   `blendv_epi8`. Nenhuma chave e truncada, convertida para float ou subtraida
   para comparar. `INT64_MIN` e `INT64_MAX` sao valores normais, sem sentinelas.

3. **Dois fluxos de merge independentes pelas extremidades.** Para metades
   suficientemente grandes, uma rede produz os menores pela frente e outra
   produz os maiores por tras. Isso permite sobrepor parte da latencia das
   redes de comparacao. Cada metade precisa ter pelo menos oito elementos
   restantes antes de uma iteracao, para que as leituras nao se cruzem.
   Na finalizacao, os valores retidos nas duas extremidades sao mesclados e
   reinseridos por um merge escalar de tres fluxos. Presumir que todo valor
   retido na frente e menor que todo valor retido atras e incorreto. A suite
   inclui repetidos e metades desiguais para verificar essa fronteira.

4. **Blocos de cache de 16.384 elementos.** As folhas e todos os merges locais
   de um bloco terminam antes de passar ao proximo. Em 64 bits, entrada e
   auxiliar desse bloco ocupam 256 KiB no total. A intencao e reutilizar o L2
   nas etapas pequenas; os merges maiores continuam sequenciais sobre o vetor
   inteiro. Nao foram medidos contadores de cache, portanto nao atribuimos uma
   reducao numerica de misses a essa mudanca. Uma subdivisao adicional para L1
   nao apresentou ganho consistente nas rodadas exploratorias e foi removida.

5. **Selecao condicional de ponteiros e stores normais.** O caminho vetorial
   seleciona qual vetor carregar pela cabeca de cada fluxo, evitando expressar
   um desvio imprevisivel por numero. A geracao de instrucoes depende do GCC e
   do alvo. Usamos loads/stores sem exigencia de alinhamento, sem ultrapassar
   as extremidades dos buffers. Stores nao temporais nao foram adotados: os
   resultados locais sao relidos nas proximas mesclagens.

6. **Resultado no buffer em que a ultima passagem terminou.** `mergeSort32/64`
   retornam `v` ou `aux`. O chamador usa esse ponteiro e libera ambos os buffers,
   sem copiar o resultado inteiro de volta. Isso remove uma copia quando o
   numero de passagens e impar. Para 10 milhoes de valores e folhas de 64 no
   baseline, sao 18 passagens, pares: **nao atribuimos ganho dessa copia a esse
   caso**. A variante de 64 bits com folhas de quatro faz 22 passagens, tambem
   pares.

7. **Fallback escalar.** Compilacoes sem AVX2 e caudas que nao formam vetores
   completos usam merge escalar pelas duas extremidades, com quatro pares por
   iteracao e verificacao dos limites antes do grupo. Insertion sort permanece
   apenas nas folhas escalares. Desativar AVX2 seleciona folhas de 64 por padrao
   no programa, quando os tamanhos nao sao sobrescritos por macros.

O algoritmo continua sendo **Merge Sort sequencial**, com redes bitonicas nas
mesclagens vetoriais. Nao usa radix sort, quicksort, OpenMP ou ordenacao de
referencia no caminho de producao. `qsort` existe somente no verificador.
A complexidade continua O(n log n), com O(n) de auxiliar. Para 10 milhoes de
`int64_t`, dados e auxiliar somam 160 MB, alem da capacidade excedente do leitor
herdado. O caminho SIMD nao promete estabilidade para registros com payload;
a interface ordena somente inteiros.

## Compilar e executar no Ryzen

Na raiz do repositorio, com GCC:

```sh
gcc -std=c11 -O3 -march=native -flto -Wall -Wextra -Wpedantic \
    ordenacao_mergesort_experimental.c -o /tmp/merge_exp
/tmp/merge_exp dados.txt
```

Compile no proprio 5700U ao usar `-march=native`; um executavel compilado assim
em outro processador pode conter instrucoes incompativeis. Para gerar o conjunto
de instrucoes Zen 2 explicitamente, use `-march=znver2`. Medir esse executavel
no EPYC ainda nao reproduz o cache, a frequencia ou a memoria do Ryzen.

O leitor, a promocao de 32 para 64 bits, a alocacao e o checksum inicial ficam
**antes** de `clock()`. A validacao final fica **depois**. Deteccao de entrada
ordenada, ordenacao das folhas, todos os merges e movimentacoes necessarias
ficam dentro do cronometro. A CLI informa o tempo de `clock()`, como o baseline.

Os ajustes de compilacao sao independentes:

| Macro | Padrao com AVX2 | Funcao |
| --- | --- | --- |
| `BLOCO_INICIAL` | 8 | Folha para 32 bits |
| `BLOCO_INICIAL_64` | 4 | Folha para 64 bits |
| `TILE_ELEMENTOS` | 16384 | Bloco de cache, multiplo das folhas por potencia de dois |
| `MESCLA_AVX2` | 1 | Zero desativa os caminhos vetoriais |
| `AVX2_DUAS_PONTAS` | 1 | Zero seleciona um fluxo vetorial para 64 bits |
| `AVX2_SEM_DESVIO` | 1 | Selecao condicional de ponteiros no fluxo vetorial simples |
| `MESCLA_DUPLA` | 1 | Dois extremos no fallback escalar |

Os dois fluxos vetoriais de 64 bits sempre usam selecao condicional. Sem AVX2,
os tamanhos de folha padrao da CLI sao 64. O script permite sobrescrever cada
parametro; para reproduzir esse fallback, especifique ambos os blocos de 64
conforme o exemplo abaixo. `BLOCO_INICIAL=64` sozinho nao muda a folha de
64 bits da experimental: para ela, use `BLOCO_INICIAL_64=64`.

## Corretude e reproducao

O driver e o script requerem Linux/WSL, Python 3 e GCC, sem pacotes Python extras:

```sh
python3 tools/benchmark_mergesort.py --test
python3 tools/benchmark_mergesort.py --test --sanitize
python3 tools/benchmark_mergesort.py --bits 64 --n 10000000 \
    --repeats 10 --seeds 41 2026 5700 --output /tmp/merge64.csv
```

Nos testes exatos, ambos os tipos e algoritmos sao comparados com `qsort` e
`memcmp`, verificando o vetor inteiro e nao apenas um checksum. A suite cobre
0 a 130 elementos, fronteiras de blocos/cache, tamanhos nao potencias de dois,
aleatorios, repetidos, sinais, extremos, entradas ordenadas e invertidas.
Inclui todas as 8! permutacoes de uma folha de oito, as 3^8 combinacoes de
minimo/zero/maximo e as C(16,8) intercalacoes de duas sequencias. Esses casos sao
repetidos com chaves genuinamente de 64 bits. Ha tambem 9.747 testes diretos de
merge de 64 bits, incluindo metades bastante desiguais.

Foram executadas **257.247 comparacoes exatas por configuracao**: padrao AVX2,
ASan+UBSan, fallback sem AVX2 e AVX2 com um fluxo. Todas passaram. Neste ambiente,
LeakSanitizer falhou por incompatibilidade com o rastreamento do processo;
usamos `ASAN_OPTIONS=detect_leaks=0` apenas na execucao instrumentada. ASan de
acessos a memoria e UBSan permaneceram ativos. Nao foi validada a deteccao de
vazamentos por LeakSanitizer aqui.

Para repetir o fallback e isolar o caminho com um fluxo:

```sh
python3 tools/benchmark_mergesort.py --test --avx2 0 \
    --experimental-block 64 --experimental-block64 64 \
    --flags '-O3 -march=x86-64 -flto'
python3 tools/benchmark_mergesort.py --test --simd-two-ended 0
```

O benchmark compila o original e o experimental em unidades separadas, sem
editar nenhum deles. Fixa ambos na mesma CPU logica quando o SO permite.
Para cada semente, gera uma entrada unica de 10 milhoes de inteiros, aquece
ambos e alterna AB/BA em dez rodadas medidas. Cada chamada recebe novamente
a entrada nao ordenada. Geracao/leitura, reset dos dados, first touch dos
buffers, `qsort` e validacao ficam fora do tempo. Cada saida medida e comparada
integralmente com a referencia. O scratch e previamente tocado para ambos;
assim, este benchmark de kernel difere da CLI, cujo auxiliar pode causar
page faults durante a ordenacao.

O CSV registra tempo de CPU e tempo monotonic de parede. O JSON registra
compilador, flags, parametros, afinidade e SHA-256 das fontes. O default e
64 bits, distribuicao pseudoaleatoria uniforme (SplitMix64), sementes
41, 2026 e 5700. Esses dados nao foram apresentados como sendo o arquivo usado
nos 282 ms do usuario. Para outras distribuicoes, `--pattern` aceita:

| Valor | Distribuicao |
| --- | --- |
| 0 | Pseudoaleatorios em toda a largura escolhida |
| 1 | Todos iguais |
| 2 | Crescente |
| 3 | Decrescente |
| 4 | Apenas 17 valores distintos |
| 5 | Minimo e maximo alternados |
| 6 | Dente de serra de periodo 97 |
| 7 | Blocos invertidos de 64 elementos |
| 8 | Quase ordenada, com pequenos trechos invertidos |

## Medicoes finais

Os resultados e metadados finais sao registrados em `benchmarks/mergesort/`.
A maquina de medicao e um AMD EPYC 9V74 virtualizado, com GCC 14.2.0, uma thread
e afinidade na CPU logica 0. Cada linha abaixo agrega 30 medicoes do algoritmo
(tres sementes vezes dez rodadas), com 10.000.000 de `int64_t` por chamada.

| Compilacao | Variante | Media CPU (ms) | Mediana (ms) | Melhor (ms) | Pior (ms) | Desvio (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `-O3 -march=native -flto` | `ordem.c`, bloco 64 | 631,580 | 629,162 | 568,814 | 703,154 | 31,304 |
| `-O3 -march=native -flto` | Experimental | 236,858 | 235,798 | 211,943 | 264,294 | 13,603 |
| `-O3 -march=znver2 -flto` | `ordem.c`, bloco 64 | 599,416 | 595,348 | 570,941 | 657,371 | 22,576 |
| `-O3 -march=znver2 -flto` | Experimental | 221,929 | 218,465 | 208,181 | 257,875 | 11,674 |

As medias de parede foram, respectivamente, 631,737 / 236,886 ms e
599,471 / 221,957 ms. Nao foram excluidas amostras lentas. Ha chamadas acima
de 250 ms, embora as medias locais tenham ficado abaixo desse valor.
Cada compilacao foi medida em sua propria serie A/B; diferencas entre as duas
series nao isolam o efeito de `-march`, pois o host tambem pode variar.

As fontes originais e experimentais de cada serie sao identificadas nos JSONs
pelos hashes; CSVs contem as 60 amostras medidas de cada serie e a verificacao
de corretude de cada chamada. Todos os 120 resultados medidos passaram na
comparacao exata. Arquivos:

- [Amostras native](benchmarks/mergesort/epyc-native-64.csv) e
  [metadados](benchmarks/mergesort/epyc-native-64.csv.json).
- [Amostras znver2](benchmarks/mergesort/epyc-znver2-64.csv) e
  [metadados](benchmarks/mergesort/epyc-znver2-64.csv.json).

A CLI tambem foi compilada sem avisos e executada sobre um arquivo texto com
10.000.000 de valores genuinamente de 64 bits (semente 41), tanto no baseline
quanto na experimental: ambas informaram a quantidade correta e ordenacao
CORRETA. Essa execucao funcional unica nao foi misturada as medias do driver.
Outras sete verificacoes da CLI cobriram promocao tardia para 64 bits, troca
de buffer com numero impar de passagens e rejeicao de entrada vazia/invalida,
arquivo inexistente e falta de argumentos. Todos os arquivos originais
continuaram sem alteracoes no Git.

Os numeros do Ryzen informados pelo usuario nao foram
misturados a essas amostras. A media abaixo de 250 ms no 5700U so pode ser
confirmada executando a variante e o baseline exato naquele computador com
a mesma entrada e condicoes de energia/temperatura.

Para usar outro baseline no driver, `--baseline caminho.c` pressupoe que ele
tenha `mergeSort32/64(v, aux, n)` com resultado em `v`. Se a interface do arquivo
`max` for diferente, ajuste somente o adaptador de testes, mantendo o original
intacto. A CLI experimental pode ser executada diretamente sobre o mesmo
arquivo de entrada usado no benchmark original do usuario.
