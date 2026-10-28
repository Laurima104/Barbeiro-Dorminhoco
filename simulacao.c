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
#ifndef TEMPO_CORTE_MS
#define TEMPO_CORTE_MS 2.0
#endif
#define MAX_EVENTOS (NUM_CLIENTES * 4 + 4)

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
static pthread_cond_t chegadas_concluidas = PTHREAD_COND_INITIALIZER;
static Cliente *fila[NUM_CLIENTES];
static Cliente *proximo;
static size_t cabeca, cauda, na_fila, cadeiras, prontos;
static int barbeiro_ocupado;
static size_t clientes_planejados = NUM_CLIENTES;
static size_t tentativas_concluidas;
static int encerrando, liberar_inicio, abortar_inicio;
static Cliente clientes[NUM_CLIENTES];
static struct timespec inicio_total;
static double atendimento_ms;
static int atendidos, clientes_aceitos, rejeitados;
static atomic_int falhas;
static int barbeiro_atendendo;
typedef struct {
    double tempo_ms;
    int cliente_id;
    size_t fila;
    int ocupado;
    char evento[32];
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

/* Nunca deixa timestamps não inicializados contaminarem as métricas. */
static int obter_tempo(struct timespec *ts) {
    if (clock_gettime(CLOCK_MONOTONIC, ts) == 0) return 0;
    perror("clock_gettime(CLOCK_MONOTONIC)");
    *ts = (struct timespec){0, 0};
    atomic_store(&falhas, 1);
    return -1;
}

/* Chamado com o mutex da simulação adquirido: salva um instantâneo do estado. */
static void registrar_evento(const char *tipo, const Cliente *c, struct timespec instante) {
    if (total_eventos >= MAX_EVENTOS) { atomic_store(&falhas, 1); return; }
    Evento *e = &eventos[total_eventos++];
    e->tempo_ms = ms_entre(inicio_total, instante);
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

static void destruir_recursos(size_t conds_clientes) {
    for (size_t i = 0; i < conds_clientes; ++i)
        erro_pthread(pthread_cond_destroy(&clientes[i].mudou), "pthread_cond_destroy cliente");
    erro_pthread(pthread_cond_destroy(&trabalho), "pthread_cond_destroy");
    erro_pthread(pthread_cond_destroy(&inicio_simulacao), "pthread_cond_destroy");
    erro_pthread(pthread_cond_destroy(&todos_prontos), "pthread_cond_destroy");
    erro_pthread(pthread_cond_destroy(&chegadas_concluidas), "pthread_cond_destroy");
    erro_pthread(pthread_mutex_destroy(&mutex), "pthread_mutex_destroy");
}

static void abortar_threads_iniciais(pthread_t barbeiro, pthread_t threads[], size_t criadas) {
    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    abortar_inicio = 1;
    liberar_inicio = 1;
    erro_pthread(pthread_cond_broadcast(&inicio_simulacao), "pthread_cond_broadcast");
    erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    for (size_t i = 0; i < criadas; ++i)
        erro_pthread(pthread_join(threads[i], NULL), "pthread_join cliente após falha de criação");
    erro_pthread(pthread_join(barbeiro, NULL), "pthread_join barbeiro após falha de criação");
}

static void *funcao_barbeiro(void *arg) {
    (void)arg;
    aguardar_barreira();
    if (abortar_inicio) return NULL;
    for (;;) {
        erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
        while (proximo == NULL && na_fila == 0 && !encerrando)
            erro_pthread(pthread_cond_wait(&trabalho, &mutex), "pthread_cond_wait");
        if (proximo == NULL && na_fila == 0 && encerrando) {
            struct timespec agora;
            obter_tempo(&agora);
            registrar_evento("BARBEIRO_ENCERRADO", NULL, agora);
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
        obter_tempo(&c->inicio);
        c->inicio_registrado = 1;
        barbeiro_atendendo = 1;
        registrar_evento("INICIO", c, c->inicio);
        erro_pthread(pthread_cond_signal(&c->mudou), "pthread_cond_signal");
        erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");

        if (esperar_ms(TEMPO_CORTE_MS) != 0) { perror("nanosleep"); atomic_store(&falhas, 1); }

        erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
        obter_tempo(&c->fim);
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
        registrar_evento("FIM", c, c->fim);
        erro_pthread(pthread_cond_signal(&c->mudou), "pthread_cond_signal");
        erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    }
}

typedef struct { Cliente *cliente; double intervalo_ms; } ArgsCliente;
static ArgsCliente args_clientes[NUM_CLIENTES];

static void registrar_tentativa_concluida(void) {
    ++tentativas_concluidas;
    if (tentativas_concluidas == clientes_planejados)
        erro_pthread(pthread_cond_signal(&chegadas_concluidas), "pthread_cond_signal chegadas");
}

static void *funcao_cliente(void *arg) {
    ArgsCliente *args = arg;
    Cliente *c = args->cliente;
    aguardar_barreira();
    if (abortar_inicio) return NULL;
    if (esperar_ms((c->id - 1) * args->intervalo_ms) != 0) { perror("nanosleep"); atomic_store(&falhas, 1); }
    obter_tempo(&c->chegada);
    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    registrar_evento("CHEGADA", c, c->chegada);
    if (!barbeiro_ocupado) {
        barbeiro_ocupado = 1;
        proximo = c;
        obter_tempo(&c->entrada_fila);
        c->estado = AGUARDANDO;
        ++clientes_aceitos;
        registrar_evento("CHEGADA_ACEITA", c, c->entrada_fila);
        registrar_tentativa_concluida();
        erro_pthread(pthread_cond_signal(&trabalho), "pthread_cond_signal");
        while (!c->inicio_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
        while (!c->fim_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
    } else if (na_fila < cadeiras) {
        obter_tempo(&c->entrada_fila);
        c->estado = AGUARDANDO;
        fila[cauda] = c;
        cauda = (cauda + 1) % NUM_CLIENTES;
        ++na_fila;
        ++clientes_aceitos;
        registrar_evento("CHEGADA_ACEITA", c, c->entrada_fila);
        registrar_tentativa_concluida();
        erro_pthread(pthread_cond_signal(&trabalho), "pthread_cond_signal");
        while (!c->inicio_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
        while (!c->fim_registrado)
            erro_pthread(pthread_cond_wait(&c->mudou, &mutex), "pthread_cond_wait");
    } else {
        c->estado = REJEITADO;
        ++rejeitados;
        registrar_evento("CHEGADA_REJEITADA", c, c->chegada);
        registrar_tentativa_concluida();
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

static int parse_clientes_teste(const char *s, size_t *v) {
    char *fim; errno = 0; unsigned long n = strtoul(s, &fim, 10);
    if (s[0] == '\0' || *fim != '\0' || errno == ERANGE || n > NUM_CLIENTES) return 0;
    *v = (size_t)n;
    return 1;
}

static int comparar_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static int gravar_csv(const char *path, const char *exp, const char *config, int repeticao,
                      double intervalo, size_t assentos, double total, double vazao,
                      size_t planejados, int n_aceitos, int n_atendidos, int n_rejeitados,
                      size_t amostras_espera,
                      double media, double p95, double maximo, double rejeicao_pct,
                      double utilizacao, size_t participantes_encerrados,
                      int violacoes, const char *status, const char *limite_timeout) {
    FILE *f = fopen(path, "a+");
    if (!f) { perror("fopen CSV"); return -1; }
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) fprintf(f, "experimento,configuracao,repeticao,semente,aleatoriedade,clientes_planejados,clientes_aceitos,clientes_atendidos,clientes_rejeitados,cadeiras,intervalo_chegada_ms,atendimento_configurado_ms,tempo_total_ms,vazao_clientes_s,amostras_espera,espera_media_ms,espera_p95_ms,espera_maxima_ms,taxa_rejeicao_pct,utilizacao_pct,participantes_encerrados,violacoes,status,timeout,limite_timeout_s\n");
    fprintf(f, "%s,%s,%d,N/A,nao,", exp, config, repeticao);
    fprintf(f, "%zu,%d,%d,%d,%zu,%.9g,%.3f,%.9f,", planejados, n_aceitos, n_atendidos, n_rejeitados, assentos, intervalo, TEMPO_CORTE_MS, total);
    if (total > 0.0) fprintf(f, "%.9f,", vazao); else fprintf(f, "N/A,");
    fprintf(f, "%zu,", amostras_espera);
    if (n_atendidos) fprintf(f, "%.9f,%.9f,%.9f,", media, p95, maximo); else fprintf(f, "N/A,N/A,N/A,");
    if (planejados) fprintf(f, "%.9f,", rejeicao_pct); else fprintf(f, "N/A,");
    fprintf(f, "%.9f,%zu,%d,%s,0,%s\n", utilizacao, participantes_encerrados,
            violacoes, status, limite_timeout && *limite_timeout ? limite_timeout : "N/A");
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
    const char *clientes_env = getenv("BARBEIRO_CLIENTES");
    if (clientes_env && !parse_clientes_teste(clientes_env, &clientes_planejados)) {
        fprintf(stderr, "BARBEIRO_CLIENTES deve ser um inteiro entre 0 e %d.\n", NUM_CLIENTES);
        return EXIT_FAILURE;
    }
    nome_experimento = exp;
    nome_configuracao = config;
    numero_repeticao = repeticao;
    pthread_t barbeiro, threads[NUM_CLIENTES];
    size_t cond_inicializadas = 0;
    for (; cond_inicializadas < clientes_planejados; ++cond_inicializadas) {
        clientes[cond_inicializadas].id = (int)cond_inicializadas + 1;
        clientes[cond_inicializadas].estado = REJEITADO;
        int init_rc = pthread_cond_init(&clientes[cond_inicializadas].mudou, NULL);
        if (init_rc) {
            fprintf(stderr, "pthread_cond_init cliente: %s\n", strerror(init_rc));
            destruir_recursos(cond_inicializadas);
            return EXIT_FAILURE;
        }
    }
    int rc = pthread_create(&barbeiro, NULL, funcao_barbeiro, NULL);
    if (rc) {
        fprintf(stderr, "pthread_create barbeiro: %s\n", strerror(rc));
        destruir_recursos(cond_inicializadas);
        return EXIT_FAILURE;
    }
    size_t criadas = 0;
#ifdef BARBEIRO_TESTING
    const char *falhar_criacao = getenv("BARBEIRO_TEST_FALHAR_CRIACAO_APOS");
    long falhar_apos = falhar_criacao ? strtol(falhar_criacao, NULL, 10) : -1;
#endif
    for (; criadas < clientes_planejados; ++criadas) {
        args_clientes[criadas] = (ArgsCliente){ &clientes[criadas], intervalo };
#ifdef BARBEIRO_TESTING
        rc = ((long)criadas == falhar_apos) ? EAGAIN : pthread_create(&threads[criadas], NULL, funcao_cliente, &args_clientes[criadas]);
#else
        rc = pthread_create(&threads[criadas], NULL, funcao_cliente, &args_clientes[criadas]);
#endif
        if (rc) {
            fprintf(stderr, "pthread_create cliente %zu: %s\n", criadas + 1, strerror(rc));
            abortar_threads_iniciais(barbeiro, threads, criadas);
            destruir_recursos(cond_inicializadas);
            return EXIT_FAILURE;
        }
    }

    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    while (prontos < clientes_planejados + 1)
        erro_pthread(pthread_cond_wait(&todos_prontos, &mutex), "pthread_cond_wait");
    obter_tempo(&inicio_total);
    registrar_evento("INICIO_SIMULACAO", NULL, inicio_total);
    liberar_inicio = 1;
    erro_pthread(pthread_cond_broadcast(&inicio_simulacao), "pthread_cond_broadcast");
    erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");

    erro_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    while (tentativas_concluidas < clientes_planejados)
        erro_pthread(pthread_cond_wait(&chegadas_concluidas, &mutex), "pthread_cond_wait chegadas");
    struct timespec instante_fechamento;
    obter_tempo(&instante_fechamento);
    registrar_evento("ENCERRAMENTO_SOLICITADO", NULL, instante_fechamento);
    encerrando = 1;
    erro_pthread(pthread_cond_signal(&trabalho), "pthread_cond_signal");
    erro_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");

    size_t participantes_encerrados = 0;
    for (size_t i = 0; i < criadas; ++i) {
        erro_pthread(pthread_join(threads[i], NULL), "pthread_join cliente");
        ++participantes_encerrados;
    }
    erro_pthread(pthread_join(barbeiro, NULL), "pthread_join barbeiro");
    ++participantes_encerrados;
    struct timespec fim_total;
    obter_tempo(&fim_total);

    double esperas[NUM_CLIENTES]; size_t n_esperas = 0;
    int registros_validos = 1;
    for (size_t i = 0; i < clientes_planejados; ++i) {
        Cliente *c = &clientes[i];
        if (c->estado == ATENDIDO) {
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
    double rejeicao_pct = clientes_planejados ? (double)rejeitados * 100.0 / clientes_planejados : 0.0;
    double utilizacao = total > 0.0 ? atendimento_ms * 100.0 / total : 0.0;
    int violacoes = (clientes_aceitos != atendidos)
                  + ((size_t)(clientes_aceitos + rejeitados) != clientes_planejados)
                  + (n_esperas != (size_t)atendidos) + (na_fila != 0)
                  + (!registros_validos) + (tentativas_concluidas != clientes_planejados)
                  + (participantes_encerrados != clientes_planejados + 1)
                  + (atomic_load(&falhas) != 0);
    const char *status = violacoes == 0 ? "OK" : "VIOLACAO";
    const char *limite_timeout = getenv("SIM_TIMEOUT_S");
    printf("Configuração: %.9g ms entre chegadas; %zu cadeiras de espera; atendimento %.3f ms\n", intervalo, cadeiras, TEMPO_CORTE_MS);
    printf("Planejados: %zu | Aceitos: %d | Atendidos: %d | Rejeitados: %d\n", clientes_planejados, clientes_aceitos, atendidos, rejeitados);
    printf("Participantes encerrados: %zu/%zu | Violações: %d | Status: %s\n", participantes_encerrados, clientes_planejados + 1, violacoes, status);
    printf("Tempo total: %.6f ms | Vazão: %.6f clientes/s\n", total, vazao);
    if (n_esperas) printf("Amostras de espera: %zu | Média: %.6f ms | p95: %.6f ms | máxima: %.6f ms\n", n_esperas, media, p95, maximo);
    else printf("Amostras de espera: 0 | Média: N/A | p95: N/A | máxima: N/A\n");
    if (clientes_planejados) printf("Taxa de rejeição: %.3f%% | Utilização do barbeiro: %.3f%%\n", rejeicao_pct, utilizacao);
    else printf("Taxa de rejeição: N/A | Utilização do barbeiro: %.3f%%\n", utilizacao);
    printf("Fila ao final: %zu | Thread do barbeiro encerrada\n", na_fila);
    if (csv && gravar_csv(csv, exp, config, repeticao, intervalo, cadeiras, total, vazao,
                          clientes_planejados, clientes_aceitos, atendidos, rejeitados, n_esperas, media, p95, maximo,
                          rejeicao_pct, utilizacao, participantes_encerrados, violacoes,
                          status, limite_timeout) != 0) violacoes++;
    if (gravar_eventos(getenv("TRACE_CSV")) != 0) violacoes++;
    destruir_recursos(cond_inicializadas);
    return violacoes == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
