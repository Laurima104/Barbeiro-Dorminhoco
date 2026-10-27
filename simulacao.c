#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#define NUM_CADEIRAS 4   // Número de cadeiras na sala de espera
#define NUM_CLIENTES 200  // Total de clientes simulados

sem_t mutex;     // Exclusão mútua para acessar a variável 'esperando'
sem_t clientes;  // Semáforo para contar clientes dormindo (acordar o barbeiro)
sem_t barbeiro;  // Semáforo para o barbeiro indicar que está pronto para atender

int esperando = 0; // Quantidade de clientes na sala de espera

void* funcao_barbeiro(void* arg) {
    while (1) {
        // Barbeiro dorme até que chegue um cliente
        printf("Barbeiro: Dormindo...\n");
        sem_wait(&clientes);

        // Entra na região crítica
        sem_wait(&mutex);
        esperando--;
        
        // Libera o cliente para sentar na cadeira de atendimento
        sem_post(&barbeiro);
        sem_post(&mutex);

        // Corta o cabelo (fora da região crítica)
        printf("Barbeiro: Cortando o cabelo de um cliente.\n");
        sleep(2);
    }
    return NULL;
}

void* funcao_cliente(void* id_ptr) {
    int id = *((int*) id_ptr);
    free(id_ptr);

    // Cliente chega na barbearia
    printf("Cliente %d chegou na barbearia.\n", id);

    sem_wait(&mutex);
    if (esperando < NUM_CADEIRAS) {
        esperando++;
        printf("Cliente %d sentou na sala de espera. (Esperando: %d)\n", id, esperando);
        
        // Acorda o barbeiro
        sem_post(&clientes);
        sem_post(&mutex);

        // Espera o barbeiro ficar disponível
        sem_wait(&barbeiro);
        printf("Cliente %d está cortando o cabelo.\n", id);
    } else {
        // Sem cadeiras livres, vai embora
        sem_post(&mutex);
        printf("Cliente %d foi embora (sem cadeiras livres).\n", id);
    }

    return NULL;
}

int main() {
    pthread_t thread_barbeiro;
    pthread_t threads_clientes[NUM_CLIENTES];

    // Inicializa os semáforos
    sem_init(&mutex, 0, 1);
    sem_init(&clientes, 0, 0);
    sem_init(&barbeiro, 0, 0);

    // Cria a thread do barbeiro
    pthread_create(&thread_barbeiro, NULL, funcao_barbeiro, NULL);

    // Cria as threads dos clientes em tempos diferentes
    for (int i = 0; i < NUM_CLIENTES; i++) {
        int* id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&threads_clientes[i], NULL, funcao_cliente, id);
        sleep(1); // Intervalo de chegada entre os clientes
    }

    // Aguarda o término dos clientes (na simulação real fica rodando)
    for (int i = 0; i < NUM_CLIENTES; i++) {
        pthread_join(threads_clientes[i], NULL);
    }

    return 0;
}
