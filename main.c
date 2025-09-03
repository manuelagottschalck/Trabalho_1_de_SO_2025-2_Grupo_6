#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// ---------------------- CONSTANTES -------------------------
#define MAX_PROCESSOS      8     // Número máximo de processos simulados
#define QUANTUM            3     // Quantum de tempo (fatias de CPU)
#define SEED               0     // Semente do gerador randômico (0 = usa time)

// Faixa de tempo de CPU necessário por processo
#define CPU_MIN            8       
#define CPU_MAX           25       

// Chance (em %) de um processo pedir I/O durante execução
#define IO_CHANCE_PCT     25    

// Duração mínima e máxima de cada tipo de I/O
#define DUR_DISCO_MIN      3
#define DUR_DISCO_MAX      7
#define DUR_FITA_MIN       4
#define DUR_FITA_MAX       9
#define DUR_IMPR_MIN       5
#define DUR_IMPR_MAX      10

// ---------------------- ENUMERAÇÕES -------------------------
enum { PRIORIDADE_BAIXA = 0, PRIORIDADE_ALTA = 1 };
enum { STATUS_PRONTO = 0, STATUS_EXEC = 1, STATUS_BLOQ = 2, STATUS_FIM = 3 };

enum { IO_NENHUM = 0, IO_DISCO = 1, IO_FITA = 2, IO_IMPRESSORA = 3 };

// ---------------------- ESTRUTURAS -------------------------

// PCB (Process Control Block)
typedef struct {
    int pid;            // ID do processo
    int ppid;           // ID do processo pai
    int prioridade;     // Alta ou baixa prioridade
    int status;         // Estado do processo (Pronto, Executando, Bloqueado, Finalizado)

    int cpu_total;      // Tempo total de CPU requerido
    int cpu_restante;   // Tempo de CPU ainda necessário

    int tipo_io;        // Tipo de I/O requisitado
    int io_restante;    // Tempo restante da operação de I/O
} PCB;

// Estrutura de Fila circular para gerenciar processos
typedef struct {
    int idx[MAX_PROCESSOS]; // IDs dos processos
    int inicio, fim, qtd;   // Controle da fila
} Fila;

// ---------------------- VARIÁVEIS GLOBAIS -------------------------

static PCB proc[MAX_PROCESSOS];     // Vetor com todos os processos
static int N = 0;                   // Número de processos ativos

// Filas de escalonamento
static Fila fila_alta;              // Fila de alta prioridade
static Fila fila_baixa;             // Fila de baixa prioridade
static Fila fila_io_disco;          // Fila de processos aguardando I/O de disco
static Fila fila_io_fita;           // Fila de processos aguardando I/O de fita
static Fila fila_io_impr;           // Fila de processos aguardando I/O de impressora

static int tempo = 0;               // Relógio global da simulação

// ---------------------- OPERAÇÕES DE FILA -------------------------

// Inicializa uma fila (zera índices e contador)
static void fila_init(Fila *fila_atual){ fila_atual->inicio = fila_atual->fim = fila_atual->qtd = 0; }

// Verifica se a fila está vazia
static int  fila_vazia(Fila *fila_atual){ return fila_atual->qtd==0; }

// Verifica se a fila está cheia
static int  fila_cheia(Fila *fila_atual){ return fila_atual->qtd == MAX_PROCESSOS; }

// Insere um processo na fila
static int  fila_push(Fila *fila_atual, int pid){
    if (fila_cheia(fila_atual)) return 0;
    fila_atual->idx[fila_atual->fim] = pid;
    fila_atual->fim = (fila_atual->fim + 1) % MAX_PROCESSOS;
    fila_atual->qtd++;
    return 1;
}

// Remove um processo da fila
static int  fila_pop(Fila *fila_atual, int *pid_out){
    if (fila_vazia(fila_atual)) return 0;
    *pid_out = fila_atual->idx[fila_atual->inicio];
    fila_atual->inicio = (fila_atual->inicio + 1) % MAX_PROCESSOS;
    fila_atual->qtd--;
    return 1;
}

// ---------------------- FUNÇÕES DE SUPORTE -------------------------

// Gera um valor aleatório entre [min, max]
static int rnd_range(int duracao_min, int duracao_max){ 
    return duracao_min + rand() % (duracao_max - duracao_min + 1);
}

// Define a duração da operação de I/O de acordo com o tipo
static int duracao_io(int tipo_io){
    switch(tipo_io){
        case IO_DISCO:      return rnd_range(DUR_DISCO_MIN, DUR_DISCO_MAX);
        case IO_FITA:       return rnd_range(DUR_FITA_MIN,  DUR_FITA_MAX);
        case IO_IMPRESSORA: return rnd_range(DUR_IMPR_MIN,  DUR_IMPR_MAX);
        default:            return 0;
    }
}

// ---------------------- CRIAÇÃO DE PROCESSOS -------------------------

static void gerar_processos(void){
    // Semente do gerador de números aleatórios
    if (SEED==0) srand((unsigned)time(NULL));
    else         srand(SEED);

    // Limpa todas as filas
    fila_init(&fila_alta);
    fila_init(&fila_baixa);
    fila_init(&fila_io_disco);
    fila_init(&fila_io_fita);
    fila_init(&fila_io_impr);

    N = MAX_PROCESSOS;

    // Cria cada processo
    for (int i = 0; i < MAX_PROCESSOS; i++){
        PCB processo;
        processo.pid = i;                    
        processo.ppid = (i==0) ? -1 : 0;     // Processo 0 não tem pai
        processo.prioridade = PRIORIDADE_ALTA; // Todos começam na fila de alta prioridade
        processo.status = STATUS_PRONTO;

        processo.cpu_total    = rnd_range(CPU_MIN, CPU_MAX);   // CPU aleatório
        processo.cpu_restante = processo.cpu_total;

        processo.tipo_io   = IO_NENHUM;   // Nenhum I/O no início
        processo.io_restante = 0;

        proc[i] = processo;

        // Insere na fila de alta prioridade
        fila_push(&fila_alta, processo.pid); 
        printf("[t=%02d] Criado P%d (PPID=%d, CPU=%d) → FILA ALTA\n",
               tempo, processo.pid, processo.ppid, processo.cpu_total);
    }
}

// ---------------------- GERENCIAMENTO DE I/O -------------------------

// Atualiza os processos em uma fila de I/O
static void tick_io_de_fila(Fila *fila_atual, int tipo){
    int quantidade_de_processos = fila_atual->qtd;
    for (int i=0; i<quantidade_de_processos; i++){
        int pid; fila_pop(fila_atual, &pid);
        PCB *processo = &proc[pid];

        if (processo->status != STATUS_BLOQ){
            fila_push(fila_atual, pid);
            continue;
        }

        if (processo->io_restante > 0) processo->io_restante--;

        // Se terminou o I/O
        if (processo->io_restante == 0){
            processo->tipo_io = IO_NENHUM;
            processo->status  = STATUS_PRONTO;
            if (tipo == IO_DISCO){
                // Disco → volta para FILA BAIXA
                processo->prioridade = PRIORIDADE_BAIXA;
                fila_push(&fila_baixa, pid);
                printf("[t=%02d] P%d concluiu I/O (Disco) → FILA BAIXA\n", tempo, pid);
            } else {
                // Outros I/Os → volta para FILA ALTA
                processo->prioridade = PRIORIDADE_ALTA;
                fila_push(&fila_alta, pid);
                const char* lab = (tipo==IO_FITA)?"Fita":"Impressora";
                printf("[t=%02d] P%d concluiu I/O (%s) → FILA ALTA\n", tempo, pid, lab);
            }
        } else {
            fila_push(fila_atual, pid); // Ainda está em I/O
        }
    }
}

// Atualiza todos os I/Os (tick do relógio)
static void atualizar_ios(void){
    tempo++;
    tick_io_de_fila(&fila_io_disco, IO_DISCO);
    tick_io_de_fila(&fila_io_fita,  IO_FITA);
    tick_io_de_fila(&fila_io_impr,  IO_IMPRESSORA);
}

// Chance de processo pedir I/O durante execução
static int processo_pede_io(void){
    return (rand()%100) < IO_CHANCE_PCT;
}

// ---------------------- EXECUÇÃO DE PROCESSOS -------------------------

// Executa um processo por até QUANTUM
static int executar_quantum(PCB *processo){
    int executado = 0;

    processo->status = STATUS_EXEC;
    printf("[t=%02d] EXECUTANDO P%d (restante = %d, prioridade = %s)\n",
           tempo, processo->pid, processo->cpu_restante, processo->prioridade? "ALTA":"BAIXA");

    // Loop do quantum
    while (executado < QUANTUM && processo->cpu_restante > 0){
        tempo++;
        processo->cpu_restante--;
        executado++;

        // Atualiza dispositivos de I/O enquanto processo executa
        tick_io_de_fila(&fila_io_disco, IO_DISCO);
        tick_io_de_fila(&fila_io_fita,  IO_FITA);
        tick_io_de_fila(&fila_io_impr,  IO_IMPRESSORA);

        // Verifica se pediu I/O
        if (processo->cpu_restante > 0 && processo_pede_io()){
            int tipo = rnd_range(IO_DISCO, IO_IMPRESSORA);
            processo->status = STATUS_BLOQ;
            processo->tipo_io = tipo;
            processo->io_restante = duracao_io(tipo);

            // Vai para a fila correta de I/O
            if (tipo == IO_DISCO)      fila_push(&fila_io_disco, processo->pid);
            else if (tipo == IO_FITA)  fila_push(&fila_io_fita,  processo->pid);
            else                       fila_push(&fila_io_impr,  processo->pid);

            const char* lab = (tipo==IO_DISCO)?"Disco":(tipo==IO_FITA)?"Fita":"Impressora";
            printf("[t=%02d] P%d requisitou I/O (%s) por %d\n",
                   tempo, processo->pid, lab, processo->io_restante);
            return 2; // Saiu para I/O
        }
    }

    // Terminou execução
    if (processo->cpu_restante == 0){
        processo->status = STATUS_FIM;
        printf("[t=%02d] P%d FINALIZADO\n", tempo, processo->pid);
        return 0; 
    }

    // Foi preemptado (não terminou no quantum)
    processo->status = STATUS_PRONTO;
    printf("[t=%02d] P%d foi preemptado (rest=%d)\n", tempo, processo->pid, processo->cpu_restante);
    return 1;
}

// ---------------------- ESCALONADOR -------------------------

static void escalonador_rr_feedback(void){
    int finalizados = 0;

    while (finalizados < N){
        int pid = -1;

        // Busca processo na fila de alta prioridade, se não tiver pega da baixa
        if (!fila_vazia(&fila_alta))      fila_pop(&fila_alta, &pid);
        else if (!fila_vazia(&fila_baixa)) fila_pop(&fila_baixa, &pid);
        else {
            // Nenhum pronto → só atualiza I/Os
            atualizar_ios();
            continue;
        }

        PCB *processo = &proc[pid];

        int restante = executar_quantum(processo);

        if (restante == 0){ 
            finalizados++; // Processo terminou
            continue;
        }
        else if (restante == 2){
            continue; // Saiu para I/O, já foi tratado
        }
        else { 
            // Caso preemptado → vai para fila BAIXA
            processo->prioridade = PRIORIDADE_BAIXA;
            fila_push(&fila_baixa, pid);
        }
    }

    printf("\n=== FIM: todos os %d processos concluídos em t=%d ===\n", N, tempo);
}

// ---------------------- MAIN -------------------------

int main(void){
    printf("- MAX_PROCESSOS=%d, QUANTUM=%d, IO_CHANCE=%d%%\n\n", MAX_PROCESSOS, QUANTUM, IO_CHANCE_PCT);
    gerar_processos();            // Cria os processos
    escalonador_rr_feedback();    // Inicia escalonador
}
