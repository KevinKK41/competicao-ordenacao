/* Compilado separadamente para nao alterar nem os simbolos do original. */
#ifndef MERGE_SOURCE
#error "Defina MERGE_SOURCE com o caminho do arquivo C"
#endif
#ifdef EXPERIMENTAL_ADAPTER
#define main programa_experimental_main
#else
#define main programa_baseline_main
#endif
#include MERGE_SOURCE
#undef main

#ifdef EXPERIMENTAL_ADAPTER
int32_t *experimental32(int32_t *v, int32_t *aux, size_t n) {
    return mergeSort32(v, aux, n);
}
int64_t *experimental64(int64_t *v, int64_t *aux, size_t n) {
    return mergeSort64(v, aux, n);
}
void experimental_merge64(const int64_t *src, int64_t *dst, size_t meio, size_t n) {
    mesclar64(src, dst, 0, meio, n);
}
#else
int32_t *baseline32(int32_t *v, int32_t *aux, size_t n) {
    mergeSort32(v, aux, n);
    return v;
}
int64_t *baseline64(int64_t *v, int64_t *aux, size_t n) {
    mergeSort64(v, aux, n);
    return v;
}
#endif
