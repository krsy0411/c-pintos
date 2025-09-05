// MLFQS(Multi-Level Feedback Queue Scheduler)
#include <stdio.h>
#include <stdlib.h>

#define QUEUE_NUMS 4
#define TIME_SLICE 1

typedef struct thread_t
{
    int id;
    int priority; // 0(최고) ~ 3(최저)
    int cpu_burst; // 남은 CPU 점유 시간
    int nice; // 우선순위 결정에 영향을 미치는 값
    struct thread_t* next;
} thread_t;

thread_t* queues[QUEUE_NUMS]; // 준비 큐 : 우선순위 별로 4개의 큐

void enqueue(thread_t** queue, thread_t* thread);
thread_t* dequeue(thread_t** queue);
thread_t* schedule();
void recalculate_priority(thread_t* thread);

// ---------------------------------------------------------------

void enqueue(thread_t** queue, thread_t* thread)
{
    thread->next = NULL;
    // 큐가 비어있는 경우
    if(*queue == NULL)
    {
        *queue = thread;
    }
    // 큐가 비어있지 않은 경우
    else
    {
        thread_t* temp = *queue;

        while(temp->next != NULL)
        {
            temp = temp->next;
        }
        
        // 큐의 마지막에 삽입
        temp->next = thread;
    }
}

thread_t* dequeue(thread_t** queue)
{
    if(*queue == NULL)
    {
        return NULL;
    }
    
    thread_t* t = *queue; // 제거할 스레드(헤드)
    *queue = (*queue)->next; // 다음 스레드를 앞으로 당기기(헤드로 설정)

    return t;
}

thread_t* schedule()
{
    for(int i=0; i<QUEUE_NUMS; i++)
    {
        if(queues[i] != NULL)
        {
            // 가장 높은 우선순위 큐에서 스레드 선택 : 해당 큐에서 빼낸 스레드 반환
            return dequeue(&queues[i]);
        }
    }

    // 모든 큐가 비어있는 경우
    return NULL;
}

// 단순화된 버전의 "우선순위 재계산" 함수
void recalculate_priority(thread_t* thread)
{
    /*
        <우선순위 재계산 공식 (예시)> : 공식은 자유롭게 변경 가능
        1. nice값이 클수록 우선순위 낮아짐
        2. cpu_burst가 클수록(CPU 집중 프로세스일수록 = CPU 사용량이 많을수록) 우선순위 낮아짐
    */
    int new_priority = 2 - (thread->nice) - (thread->cpu_burst / 4);

    if(new_priority < 0)
    {
        new_priority = 0;
    }
    else if(new_priority > 3)
    {
        new_priority = 3;
    }

    // 새로운 우선순위로 업데이트
    thread->priority = new_priority;
}

// ---------------------------------------------------------------

int main()
{
    // 스레드 3개 생성 : id, priority, cpu_burst, nice
    thread_t t1 = {1, 0, 5, 0, NULL};
    thread_t t2 = {2, 1, 3, 1, NULL};
    thread_t t3 = {3, 2, 8, 0, NULL};

    thread_t* threads[] = {&t1, &t2, &t3};

    // 초기 우선순위 계산 & 준비 큐에 삽입
    for(int i=0; i<3; i++)
    {
        recalculate_priority(threads[i]);
        enqueue(&queues[threads[i]->priority], threads[i]); // 우선순위에 맞는 큐에 삽입
    }

    printf("MLFQS Scheduling 시작!\n");

    thread_t* current_thread;
    while((current_thread = schedule()))
    {
        printf("스레드 %d 실행 (우선순위: %d, 남은 CPU 점유 시간: %d)\n", 
            current_thread->id, 
            current_thread->priority, 
            current_thread->cpu_burst
        );

        current_thread->cpu_burst -= TIME_SLICE; // CPU 점유 시간 감소

        if(current_thread->cpu_burst > 0)
        {
            recalculate_priority(current_thread); // 우선순위 재계산
            enqueue(&queues[current_thread->priority], current_thread); // 다시 준비 큐에
        }
    }

    printf("모든 스레드가 완료되었습니다!\n");
    return 0;
}