#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_CLIENTES 200
#define TEMPO_CORTE_MS 2.0
#define MAX_EVENTOS (NUM_CLIENTES * 4)

typedef enum { AGUARDANDO, ATENDIDO, REJEITADO } Estado;
typedef struct {
    int id;
    Estado estado;
    int inicio_registrado, fim_registrado;
    struct timespec chegada, entrada_fila, inicio, fim;
    pthread_cond_t mudou;
} Cliente;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t trabalho = PTHREAD_COND_INITIALIZER;
static pthread_cond_t inicio_simulacao = PTHREAD_COND_INITIALIZER;
static pthread_cond_t todos_prontos = PTHREAD_COND_INITIALIZER;
static Cliente *fila[NUM_CLIENTES];
static Cliente *proximo;
static size_t cabeca, cauda, na_fila, cadeiras, prontos;
static int barbeiro_ocupado;
static int encerrando, liberar_inicio;
static Cliente clientes[NUM_CLIENTES];
static struct timespec inicio_total;
static double atendimento_ms;
static int atendidos, rejeitados;
static atomic_int falhas;
static int barbeiro_atendendo;
typedef struct {
    double tempo_ms;
    int cliente_id;
    size_t fila;
    int ocupado;
    char evento[20];
} Evento;
static Evento eventos[MAX_EVENTOS];
static size_t total_eventos;
static const char *nome_experimento, *nome_configuracao;
static int numero_repeticao;

static void erro_pthread(int codigo, const char *operacao) {
    if (codigo != 0) { fprintf(stderr, "%s: %s\n", operacao, strerror(codigo)); exit(EXIT_FAILURE); }
}

static double ms_entre(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

/* Chamado com o mutex da simulação adquirido: salva um instantâneo do estado. */
static void registrar_evento(const char *tipo, const Cliente *c) {
    if (total_eventos >= MAX_EVENTOS) { atomic_store(&falhas, 1); return; }
    struct timespec agora;
    clock_gettime(CLOCK_MONOTONIC, &agora);
    Evento *e = &eventos[total_eventos++];
    e->tempo_ms = ms_entre(inicio_total, agora);
    e->cliente_id = c ? c->id : 0;
    e->fila = na_fila;
    e->ocupado = barbeiro_atendendo;
    snprintf(e->evento, sizeof e->evento, "%s", tipo);
}

/* nanosleep repetido preserva o prazo caso um sinal o interrompa. */
static int esperar_ms(double ms) {
    long double ns_total = (long double)ms * 1000000.0L;
    if (!isfinite(ms) || ms < 0.0 || ns_total > (long double)LLONG_MAX) return -1;
    long long ns = (long long)llroundl(ns_total);
    struct timespec restante = { (time_t)(ns / 1000000000LL), (long)(ns % 1000000000LL) };
    while (nanosleep(&restante, &restante) == -1) if (errno != EINTR) return -1;
    return 0;
}

static void aguardar_barreira(void) {
    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    ++prontos;
    erro_pthread(pthread_cond_signal(&todos_prontos), "pthread_cond_signal");
    while (!liberar_inicio) erro_pthread(pthread_cond_wait(&inicio_simulacao, &mutex), "pthread_cond_wait");
    erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
}

static void *funcao_barbeiro(void *arg) {
    (void)arg;
    aguardar_barreira();
    for (;;) {
        erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
        while (proximo == NULL && na_fila == 0 && !encerrando)
            erro_pthread(pthread_cond_wait(&trabalho, &mutex), "pthread_cond_wait");
        if (proximo == NULL && na_fila == 0 && encerrando) {
            erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
            return NULL;
        }
        Cliente *c;
        if (proximo != NULL) {
            c = proximo;
            proximo = NULL;
        } else {
            c = fila[cabeca];
            cabeca = (cabeca + 1) % NUM_CLIENTES;
            --na_fila;
        }
        c->inicio = (struct timespec){0};
        clock_gettime(CLOCK_MONOTONIC, &c->inicio);
        c->inicio_registrado = 1;
        barbeiro_atendendo = 1;
        registrar_evento("INICIO", c);
        erro_pthread(pthread_cond_signal(&c->mudou), "pthread_cond_signal");
        erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");

        if (esperar_ms(TEMPO_CORTE_MS) != 0) { perror("nanosleep"); atomic_store(&falhas, 1); }

        erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
        clock_gettime(CLOCK_MONOTONIC, &c->fim);
        c->fim_registrado = 1;
        c->estado = ATENDIDO;
        ++atendidos;
        atendimento_ms += ms_entre(c->inicio, c->fim);
        barbeiro_atendendo = 0;
        if (na_fila > 0) {
            proximo = fila[cabeca];
            cabeca = (cabeca + 1) % NUM_CLIENTES;
            --na_fila;
        } else {
            barbeiro_ocupado = 0;
        }
        registrar_evento("FIM", c);
        erro_pthread(pthread_cond_signal(&c->mudou), "pthread_cond_signal");
        erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    }
}

typedef struct { Cliente *cliente; double intervalo_ms; } ArgsCliente;
static ArgsCliente args_clientes[NUM_CLIENTES];

static void *funcao_cliente(void *arg) {
    ArgsCliente *args = arg;
    Cliente *c = args->cliente;
    aguardar_barreira();
    if (esperar_ms((c->id - 1) * args->intervalo_ms) != 0) { perror("nanosleep"); atomic_store(&falhas, 1); }
    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    clock_gettime(CLOCK_MONOTONIC, &c->chegada);
    if (!barbeiro_ocupado) {
        barbeiro_ocupado = 1;
        proximo = c;
        clock_gettime(CLOCK_MONOTONIC, &c->entrada_fila);
        c->estado = AGUARDANDO;
        registrar_evento("CHEGADA_ACEITA", c);
        erro_pthread(pthread_cond_signal(&trabalho), "pthread_cond_signal");
        while (!c->inicio_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
        while (!c->fim_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
    } else if (na_fila < cadeiras) {
        clock_gettime(CLOCK_MONOTONIC, &c->entrada_fila);
        c->estado = AGUARDANDO;
        fila[cauda] = c;
        cauda = (cauda + 1) % NUM_CLIENTES;
        ++na_fila;
        registrar_evento("CHEGADA_ACEITA", c);
        erro_pthread(pthread_cond_signal(&trabalho), "pthread_cond_signal");
        while (!c->inicio_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
        while (!c->fim_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
    } else {
        c->estado = REJEITADO;
        ++rejeitados;
        registrar_evento("CHEGADA_REJEITADA", c);
    }
    erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    return NULL;
}

static int parse_intervalo(const char *s, double *v) {
    char *fim; errno = 0; *v = strtod(s, &fim);
    return s[0] != '\0' && *fim == '\0' && errno != ERANGE && isfinite(*v) && *v >= 0.0 && *v <= 1.0e9;
}
static int parse_cadeiras(const char *s, size_t *v) {
    char *fim; errno = 0; unsigned long n = strtoul(s, &fim, 10);
    if (s[0] == '\0' || *fim != '\0' || errno == ERANGE || n == 0 || n > NUM_CLIENTES) return 0;
    *v = (size_t)n; return 1;
}

static int comparar_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static int gravar_csv(const char *path, const char *exp, const char *config, int repeticao,
                      double intervalo, size_t assentos, double total, double vazao,
                      int n_atendidos, int n_rejeitados, double media, double p95,
                      double maximo, double rejeicao_pct, double utilizacao) {
    FILE *f = fopen(path, "a+");
    if (!f) { perror("fopen CSV"); return -1; }
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) fprintf(f, "experimento,configuracao,repeticao,clientes_planejados,clientes_atendidos,clientes_rejeitados,cadeiras,intervalo_chegada_ms,atendimento_configurado_ms,tempo_total_ms,vazao_clientes_s,espera_media_ms,espera_p95_ms,espera_maxima_ms,taxa_rejeicao_pct,utilizacao_pct\n");
    fprintf(f, "%s,%s,%d,%d,%d,%d,%zu,%.9g,%.3f,%.9f,", exp, config, repeticao, NUM_CLIENTES, n_atendidos, n_rejeitados, assentos, intervalo, TEMPO_CORTE_MS, total);
    if (total > 0.0) fprintf(f, "%.9f,", vazao); else fprintf(f, "N/A,");
    if (n_atendidos) fprintf(f, "%.9f,%.9f,%.9f,", media, p95, maximo); else fprintf(f, "N/A,N/A,N/A,");
    fprintf(f, "%.9f,%.9f\n", rejeicao_pct, utilizacao);
    if (fclose(f) != 0) { perror("fclose CSV"); return -1; }
    return 0;
}

static int gravar_eventos(const char *path) {
    if (!path || !*path) return 0;
    FILE *f = fopen(path, "a+");
    if (!f) { perror("fopen CSV de eventos"); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { perror("fseek CSV de eventos"); fclose(f); return -1; }
    if (ftell(f) == 0)
        fprintf(f, "experimento,configuracao,repeticao,tempo_ms,cliente_id,event,fila,barbeiro_ocupado\n");
    for (size_t i = 0; i < total_eventos; ++i) {
        const Evento *e = &eventos[i];
        fprintf(f, "%s,%s,%d,%.9f,%d,%s,%zu,%d\n", nome_experimento,
                nome_configuracao, numero_repeticao, e->tempo_ms, e->cliente_id,
                e->evento, e->fila, e->ocupado);
    }
    if (fclose(f) != 0) { perror("fclose CSV de eventos"); return -1; }
    return 0;
}

int main(int argc, char **argv) {
    double intervalo;
    size_t cadeiras_arg;
    char entrada_intervalo[128], entrada_cadeiras[128];
    const char *arg_intervalo = NULL, *arg_cadeiras = NULL;
    if (argc == 1) {
        printf("Intervalo entre chegadas (ms; pode ser decimal, por exemplo 0.5): ");
        fflush(stdout);
        if (!fgets(entrada_intervalo, sizeof entrada_intervalo, stdin)) {
            fprintf(stderr, "Não foi possível ler o intervalo.\n");
            return EXIT_FAILURE;
        }
        entrada_intervalo[strcspn(entrada_intervalo, "\r\n")] = '\0';
        printf("Quantidade de cadeiras de espera: ");
        fflush(stdout);
        if (!fgets(entrada_cadeiras, sizeof entrada_cadeiras, stdin)) {
            fprintf(stderr, "Não foi possível ler a quantidade de cadeiras.\n");
            return EXIT_FAILURE;
        }
        entrada_cadeiras[strcspn(entrada_cadeiras, "\r\n")] = '\0';
        arg_intervalo = entrada_intervalo;
        arg_cadeiras = entrada_cadeiras;
    } else if (argc >= 3 && argc <= 7) {
        arg_intervalo = argv[1];
        arg_cadeiras = argv[2];
    } else {
        fprintf(stderr, "Uso: %s [intervalo_ms cadeiras_espera [csv [experimento [configuracao [repeticao]]]]]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!parse_intervalo(arg_intervalo, &intervalo) || !parse_cadeiras(arg_cadeiras, &cadeiras_arg)) {
        fprintf(stderr, "Entrada inválida. O intervalo deve ser finito e >= 0; as cadeiras devem ser um inteiro entre 1 e %d.\n", NUM_CLIENTES);
        return EXIT_FAILURE;
    }
    cadeiras = cadeiras_arg;
    const char *csv = argc >= 4 ? argv[3] : NULL;
    const char *exp = argc >= 5 ? argv[4] : "manual";
    const char *config = argc >= 6 ? argv[5] : "manual";
    int repeticao = argc >= 7 ? atoi(argv[6]) : 1;
    nome_experimento = exp;
    nome_configuracao = config;
    numero_repeticao = repeticao;
    pthread_t barbeiro, threads[NUM_CLIENTES];
    size_t cond_inicializadas = 0;
    for (; cond_inicializadas < NUM_CLIENTES; ++cond_inicializadas) {
        clientes[cond_inicializadas].id = (int)cond_inicializadas + 1;
        clientes[cond_inicializadas].estado = REJEITADO;
        int rc = pthread_cond_init(&clientes[cond_inicializadas].mudou, NULL);
        if (rc) { fprintf(stderr, "pthread_cond_init: %s\n", strerror(rc)); return EXIT_FAILURE; }
    }
    int rc = pthread_create(&barbeiro, NULL, funcao_barbeiro, NULL);
    if (rc) { fprintf(stderr, "pthread_create barbeiro: %s\n", strerror(rc)); return EXIT_FAILURE; }
    size_t criadas = 0;
    for (; criadas < NUM_CLIENTES; ++criadas) {
        args_clientes[criadas] = (ArgsCliente){ &clientes[criadas], intervalo };
        rc = pthread_create(&threads[criadas], NULL, funcao_cliente, &args_clientes[criadas]);
        if (rc) { fprintf(stderr, "pthread_create cliente: %s\n", strerror(rc)); return EXIT_FAILURE; }
    }
    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    while (prontos < NUM_CLIENTES + 1) erro_pthread(pthread_cond_wait(&todos_prontos, &mutex), "pthread_cond_wait");
    clock_gettime(CLOCK_MONOTONIC, &inicio_total);
    liberar_inicio = 1;
    erro_pthread(pthread_cond_broadcast(&inicio_simulacao), "pthread_cond_broadcast");
    erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");

    for (size_t i = 0; i < criadas; ++i) erro_pthread(pthread_join(threads[i], NULL), "pthread_join cliente");
    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    encerrando = 1;
    erro_pthread(pthread_cond_signal(&trabalho), "pthread_cond_signal");
    erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    erro_pthread(pthread_join(barbeiro, NULL), "pthread_join barbeiro");
    struct timespec fim_total; clock_gettime(CLOCK_MONOTONIC, &fim_total);

    double esperas[NUM_CLIENTES]; size_t n_esperas = 0;
    int aceitos = 0, registros_validos = 1;
    for (size_t i = 0; i < NUM_CLIENTES; ++i) {
        Cliente *c = &clientes[i];
        if (c->estado == ATENDIDO) {
            ++aceitos;
            if (!c->inicio_registrado || !c->fim_registrado) registros_validos = 0;
            esperas[n_esperas++] = ms_entre(c->entrada_fila, c->inicio);
        } else if (c->estado == AGUARDANDO) registros_validos = 0;
    }
    double total = ms_entre(inicio_total, fim_total), media = 0.0, p95 = 0.0, maximo = 0.0;
    if (n_esperas) {
        for (size_t i = 0; i < n_esperas; ++i) { media += esperas[i]; if (esperas[i] > maximo) maximo = esperas[i]; }
        media /= n_esperas;
        qsort(esperas, n_esperas, sizeof(double), comparar_double);
        size_t posicao = (size_t)ceil(0.95 * n_esperas); if (posicao == 0) posicao = 1;
        p95 = esperas[posicao - 1];
    }
    double vazao = total > 0.0 ? (double)atendidos / (total / 1000.0) : 0.0;
    double rejeicao_pct = (double)rejeitados * 100.0 / NUM_CLIENTES;
    double utilizacao = total > 0.0 ? atendimento_ms * 100.0 / total : 0.0;
    printf("Configuração: %.9g ms entre chegadas; %zu cadeiras de espera; atendimento %.3f ms\n", intervalo, cadeiras, TEMPO_CORTE_MS);
    printf("Planejados: %d | Atendidos: %d | Rejeitados: %d\n", NUM_CLIENTES, atendidos, rejeitados);
    printf("Tempo total: %.6f ms | Vazão: %.6f clientes/s\n", total, vazao);
    if (n_esperas) printf("Espera média: %.6f ms | p95: %.6f ms | máxima: %.6f ms\n", media, p95, maximo);
    else printf("Espera média: N/A | p95: N/A | máxima: N/A\n");
    printf("Taxa de rejeição: %.3f%% | Utilização do barbeiro: %.3f%%\n", rejeicao_pct, utilizacao);
    int consistente = aceitos == atendidos && atendidos + rejeitados == NUM_CLIENTES && n_esperas == (size_t)atendidos && na_fila == 0 && registros_validos && !atomic_load(&falhas);
    printf("Consistência: %s (fila=%zu, thread do barbeiro encerrada)\n", consistente ? "OK" : "ERRO", na_fila);
    if (csv && gravar_csv(csv, exp, config, repeticao, intervalo, cadeiras, total, vazao, atendidos, rejeitados, media, p95, maximo, rejeicao_pct, utilizacao) != 0) consistente = 0;
    if (gravar_eventos(getenv("TRACE_CSV")) != 0) consistente = 0;
    for (size_t i = 0; i < cond_inicializadas; ++i) erro_pthread(pthread_cond_destroy(&clientes[i].mudou), "pthread_cond_destroy");
    erro_pthread(pthread_cond_destroy(&trabalho), "pthread_cond_destroy");
    erro_pthread(pthread_cond_destroy(&inicio_simulacao), "pthread_cond_destroy");
    erro_pthread(pthread_cond_destroy(&todos_prontos), "pthread_cond_destroy");
    erro_pthread(pthread_mutex_destroy(&mutex), "pthread_mutex_destroy");
    return consistente ? EXIT_SUCCESS : EXIT_FAILURE;
}
