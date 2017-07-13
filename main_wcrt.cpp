/**
 * mbed program to evaluate RTA and HET schedubility methods.
 *
 * The wcrt-test-mbed.py will compile this program and upload the binary to 
 * the lpc1768 or frdm-k64f board.
 */
#include "mbed.h"

#define forever while (1)

/* ceil/floor operations */
#define CEIL_TYPE 1 
#if CEIL_TYPE == 1
#define U_CEIL( x, y )    ( ( x / y ) + ( x % y != 0 ) )
#define U_FLOOR( x, y )   ( x / y )
#endif
#if CEIL_TYPE == 2
#define U_CEIL( x, y )    (int) ceil((double) x / (double) y)
#define U_FLOOR( x, y )   (int) floor((double) x / (double) y)
#endif

#ifndef BAUD
#define BAUD 115200
#endif

#ifndef NUM_TASKS
#define NUM_TASKS 100
#endif

#ifndef USE_LCD
#define USE_LCD 1
#endif

#ifndef CHECK_FOR_DWT
#define CHECK_FOR_DWT 0
#endif

/* 
 * Which schedulability methods to include in the program. 
 * The gcc -D option flag is used to set up these #defines values.
 */
/*
#define TEST_RTA     1
#define TEST_RTA2    1
#define TEST_RTA3    1
#define TEST_RTA4    1 // <-- THIS IS THE PUBLISHED METHOD
#define TEST_HET     0
#define TEST_HET2    1
*/

/*
 * 20/04/2017
 * Removes the difference computation inside the ceil. Used to measure the cost of this
 * minus operation, against RTA3.
 */
#define REMOVE_SUBTRACTION_RTA4 0
/*
 * 21/04/2017
 * Remove the if clause that perform the verification tr > min_i. Used to evaluate the
 * algorithm cost with task-sets with a high utilization factor and 50 or more tasks. In
 * this form, RTA4 is basically RTA3 + the improvement in the ceil calculation made by 
 * Mariano.
 */
#define DONT_USE_MIN_B_RTA4 0
/*
 * 21/04/2017
 * If equal to 1, the algorithm performs the schedulability check of the current task 
 * (tr <= d) after the for loop that performs the summation is done. If it is equal to
 * 0, this check is donde inside the for, each time that the tr is updated.
 */
#define CHECK_DEADLINE_AFTER_FOR 0
/*
 * 02/05/2017
 * If equal to 0 writes into the UART the wcrt, number of ceil/floor operations
 * and for/while loops for each task. If equal to 1 write to the UART the 
 * summation of the ceil/floor count and for/while loop count measured for all 
 * the tasks.
 */
#define SUM_CC_LOOPS 1

/*
 * Count cpu cycles.
 */ 
#define COUNTER_ENABLE_BIT  ( 0x01UL )        // enable CYCCNT
#define LSUEVTENA           ( 0x01UL << 20 )  // enable LSU count event
#define DWT_LAR_KEY         ( 0xC5ACCE55 )
#define CPU_CYCLES          DWT->CYCCNT
#define STOPWATCH_RESET()                           \
{                                                   \
    /* Enable Trace System (TRCENA) */              \
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; \
    /* Reset counter. */                            \
    DWT->CYCCNT = 0;                                \
    /* Enable counter. */                           \
    DWT->CTRL |= COUNTER_ENABLE_BIT;                \
}

//#define PRINT_TASK_RESULTS       1
#define PRINT_TASK_RESULTS_FOR   1
#define PRINT_TASK_RESULTS_CC    1
#define PRINT_TASK_RESULTS_WHILE 1

/*
 * Type of measurements performed.
 * 1: usecs.
 * 2: cpu cycles.
 * 4: both usecs and cpu cycles.
 * 5: number of ceil operations, and total cpu cycles used for this operations.
 * 6: cpu cycles used to perform the summation.
 * 7: maximum cost in cpu cycles of the summation.
 */
#ifndef TEST_TYPE
#define TEST_TYPE 1
#endif

#if ( TEST_TYPE == 1 )
Timer t;
#define timing(A, B, C) C = 0; t.reset(); t.start(); A; t.stop(); B = t.read_us()
#endif

#if ( TEST_TYPE == 2 )
uint32_t cycles_start;
#define timing(A, B, C) B = 0; STOPWATCH_RESET(); cycles_start = CPU_CYCLES; A; C = CPU_CYCLES - cycles_start;
#endif

#if ( TEST_TYPE == 4 )
Timer t;
uint32_t cycles_start;
#define timing(A, B, C) t.reset(); t.start(); STOPWATCH_RESET(); cycles_start = CPU_CYCLES; A; C = CPU_CYCLES - cycles_start; t.stop(); B = t.read_us();
#endif

#if ( TEST_TYPE == 5 )
uint32_t cycles_start;
int ceil_count;
#define timing(A, B, C)   ceil_count = 0; C = 0; A; B = ceil_count;
#define timing_ceil(A, B) STOPWATCH_RESET(); cycles_start = CPU_CYCLES; A; B += (CPU_CYCLES - cycles_start); ceil_count = ceil_count + 1;
#endif

/* 
 * To perform this test, set PRINT_TASK_RESULTS=1, set PRINT_TASK_RESULTS_{FOR,CC,WHILE}=0 and SUM_CC_LOOPS=1.
 */
#if ( TEST_TYPE == 6 )
#undef PRINT_TASK_RESULTS
#define PRINT_TASK_RESULTS 1
#undef PRINT_TASK_RESULTS_CC
#define PRINT_TASK_RESULTS_CC 0
#undef PRINT_TASK_RESULTS_FOR
#define PRINT_TASK_RESULTS_FOR 0
#undef PRINT_TASK_RESULTS_WHILE
#define PRINT_TASK_RESULTS_WHILE 0
#undef SUM_CC_LOOPS
#define SUM_CC_LOOPS 1
uint32_t cycles_start;
uint32_t cycles_start_for;
uint32_t sum_for_count;
#define timing(A, B, C)         sum_for_count = 0; STOPWATCH_RESET(); cycles_start = CPU_CYCLES; A; C = CPU_CYCLES - cycles_start; B = sum_for_count;
#define timing_sumfor_start()   cycles_start_for = CPU_CYCLES;
#define timing_sumfor_end()     sum_for_count += (CPU_CYCLES - cycles_start_for)
#endif

/*
 * Record the maximum cycle count required to execute one inner summation.
 */
#if ( TEST_TYPE == 7 )
uint32_t cycles_start;
uint32_t cycles_start_for;
uint32_t max_for_count;
uint32_t sum_for_count;
#define timing(A, B, C)         max_for_count = 0; sum_for_count = 0; STOPWATCH_RESET(); cycles_start = CPU_CYCLES; A; C = CPU_CYCLES - cycles_start; B = max_for_count;
#define timing_sumfor_start()   cycles_start_for = CPU_CYCLES;
#define timing_sumfor_end()     sum_for_count = (CPU_CYCLES - cycles_start_for); if ( sum_for_count > max_for_count ) { max_for_count = sum_for_count; }
#endif 

void het_wcrt();
void het2_wcrt();
void rta4_wcrt();
void rta3_wcrt();
void rta2_wcrt();
void rta_wcrt();
int het_workload(int i, int b, int n);
int het2_workload(int i, int b, int n);

DigitalOut leds[] = { DigitalOut(LED1) = 0,
                      DigitalOut(LED2) = 0,
                      DigitalOut(LED3) = 0,
                      DigitalOut(LED4) = 0 };

struct method_t {
    int wcrt;                  // worst case response time (wcrt)
    int cc;                    // number of ceil/floor operations
    int loops;                 // number of for/while loops
    int loops_for;             // number of for loops
    int last_psi;              // het
    int last_workload;         // het
};

// Tarea de TR
struct task_t {
    int id;                    // task id
    int c;                     // worst case execution time (wcet)
    int t;                     // period
    int d;                     // relative deadline
    int a;
    int b;
    int tmc;                   // period - wcet
    struct method_t methods[6]; // index by METHOD_ID    
};

// RTS
task_t str[NUM_TASKS];

typedef void(*fmethod)();

union int_union {
    char c[4];
    int i;
} int_u;

int num_task = 0;
int num_rts = 0;
int rta2_sched, rta3_sched, rta4_sched, rta_sched, het_sched, het2_sched;
int rta2_usecs, rta3_usecs, rta4_usecs, rta_usecs, het_usecs, het2_usecs;
int rta2_cycles, rta3_cycles, rta4_cycles, rta_cycles, het_cycles, het2_cycles;
int rta_lsu, rta2_lsu, rta3_lsu, rta4_lsu;

// Serial port
Serial pc(USBTX, USBRX);

// LEDs used for visual feedback
DigitalOut myled(LED1);

static void putc(int i)
{
    int_u.i = i;
    pc.putc(int_u.c[3]);
    pc.putc(int_u.c[2]);
    pc.putc(int_u.c[1]);
    pc.putc(int_u.c[0]);    
}

static int getc()
{
    int_u.c[3] = pc.getc();
    int_u.c[2] = pc.getc();
    int_u.c[1] = pc.getc();
    int_u.c[0] = pc.getc();
    return int_u.i;
}

static void test_method(fmethod method, int led, int method_id, int *usecs, int *cycles )
{
    leds[led] = 1;
    for (int j = 0; j < num_task; j++) {
        struct method_t *method = &(str[j].methods[method_id]);
        method->wcrt = 0;
        method->cc = 0;
        method->loops = 0;
        method->loops_for = 0;
        method->last_psi = 0;
        method->last_workload = 0;
        str[j].a = str[j].c;
        str[j].b = str[j].t;
    }
    timing( method(), *usecs, *cycles);
    leds[led] = 0;
}

static void send_results(int method_id, int sched, int usecs, int cycles)
{
    putc(method_id);
    putc(sched);
    putc(usecs);
    putc(cycles);
    #if PRINT_TASK_RESULTS == 1
    #if SUM_CC_LOOPS == 0
    for (int j = 0; j < num_task; j++) {
        putc(str[j].methods[method_id].wcrt);
        putc(str[j].methods[method_id].cc);
        putc(str[j].methods[method_id].loops);
    }
    #else
    int cc_total = 0;
    int loops_total = 0;
    for (int j = 0; j < num_task; j++) {
        cc_total += str[j].methods[method_id].cc;
        loops_total += str[j].methods[method_id].loops;
    }
    putc(cc_total);
    putc(loops_total);
    #endif
    #endif
}

int main() {
    pc.baud(BAUD);
    
    // Verify if DWT is available
    #if CHECK_FOR_DWT == 1
    int flag = 1 << 25;    
    if (DWT->CTRL & flag) {
        forever {
            myled = 0;
            wait(0.5);
            myled = 1;
            wait(0.5);
        }
    }
    #endif
 
    forever {
        while (pc.readable() == 0) {
            ;
        }

        // read the number of tasks
        num_task = getc();

        // read task-set from serial
        for (int j = 0; j < num_task; j++) {
            str[j].id = j + 1;
            
            // read C
            str[j].c = getc();
            
            // read T
            str[j].t = getc();
            
            // read D
            str[j].d = getc();
            
            str[j].tmc = str[j].t - str[j].c;
        }

        // === Sjodin ===
        #ifdef TEST_RTA
        test_method(rta_wcrt, 0, RTA_ID, &rta_usecs, &rta_cycles);
        #endif

        // === RTA2 ===
        #ifdef TEST_RTA2
        test_method(rta2_wcrt, 1, RTA2_ID, &rta2_usecs, &rta2_cycles);
        #endif
        
        // === RTA3 ===
        #ifdef TEST_RTA3
        test_method(rta3_wcrt, 2, RTA3_ID, &rta3_usecs, &rta3_cycles);
        #endif

        // === RTA4 ===
        #ifdef TEST_RTA4
        test_method(rta4_wcrt, 3, RTA4_ID, &rta4_usecs, &rta4_cycles);
        #endif
                
        // === HET ===
        #ifdef TEST_HET
        test_method(het_wcrt, 0, HET_ID, &het_usecs, &het_cycles);
        #endif
        
        // === HET2 ===
        #ifdef TEST_HET2
        test_method(het2_wcrt, 1, HET2_ID, &het2_usecs, &het2_cycles);
        #endif
                
        // write result into serial
        #ifdef TEST_HET
        send_results(HET_ID, het_sched, het_usecs, het_cycles);
        #endif
        
        #ifdef TEST_HET2
        send_results(HET2_ID, het2_sched, het2_usecs, het2_cycles);
        #endif
        
        #ifdef TEST_RTA
        send_results(RTA_ID, rta_sched, rta_usecs, rta_cycles);
        #endif
        
        #ifdef TEST_RTA2
        send_results(RTA2_ID, rta2_sched, rta2_usecs, rta2_cycles);
        #endif
        
        #ifdef TEST_RTA3
        send_results(RTA3_ID, rta3_sched, rta3_usecs, rta3_cycles);
        #endif
                
        #ifdef TEST_RTA4
        send_results(RTA4_ID, rta4_sched, rta4_usecs, rta4_cycles);
        #endif

        // send magic key
        putc(0xABBA);
    }
}

/*
 * ============================================================================================
 *                              Schedulability Analysis Methods
 * ============================================================================================
 */
 
/*
 * HET: workload utility function
 */
int het_workload(int i, int b, int n)
{
    str[n].methods[HET_ID].loops += 1;
    
    if (i < 0) {        
        return 0;
    }
    
    int ci = str[i].c;
    int ti = str[i].t;

    if (b <= str[i].methods[HET_ID].last_psi) {
        return str[i].methods[HET_ID].last_workload;
    }

    int f = U_FLOOR( b, ti );
    int c = U_CEIL( b, ti );
    str[n].methods[HET_ID].cc = str[n].methods[HET_ID].cc + 2;
    
    int branch0 = b - f * (ti - ci) + het_workload(i - 1, f * ti, n);
    int branch1 = c * ci + het_workload(i - 1, b, n);

    str[i].methods[HET_ID].last_psi = b;
    if (branch0 <= branch1) {
        str[i].methods[HET_ID].last_workload = branch0;
    } else {
        str[i].methods[HET_ID].last_workload = branch1;
    }

    return str[i].methods[HET_ID].last_workload;
}

/*
 * HET
 */
void het_wcrt()
{
    int i;
    for (i = 0; i < num_task; i++) {
        int c = str[i].c;
        int d = str[i].d;
        int w = het_workload(i - 1, d, i);
        if ((w + c) > d) {
            het_sched = 0;
            return;
        }
        str[i].methods[HET_ID].wcrt = w + c;
    }
    het_sched = 1;
}
/* ------------------------------------------------------------------------- */

/*
 * HET2: workload utility function
 */
int het2_workload(int i, int b, int n)
{
    #if PRINT_TASK_RESULTS == 1
    str[n].methods[HET2_ID].loops += 1;
    #endif
    
    int ci = str[i].c;
    int ti = str[i].t;

    int f = U_FLOOR( b, ti );
    int c = U_CEIL( b, ti );
    
    #if PRINT_TASK_RESULTS == 1
    str[n].methods[HET2_ID].cc = str[n].methods[HET2_ID].cc + 2;
    #endif

    int branch0 = b - f * (ti - ci);
    int branch1 = c * ci;
    
    if (i > 0)
    {
        int l_w = str[i - 1].methods[HET2_ID].last_workload;
        int tmp = f * ti;
        if (tmp > str[i - 1].methods[HET2_ID].last_psi) {
            l_w = het2_workload(i - 1, tmp, n);
        }
    
        branch0 = branch0 + l_w;
        branch1 = branch1 + het2_workload(i - 1, b, n);
    }

    str[i].methods[HET2_ID].last_psi = b;
    if (branch0 <= branch1) {
        str[i].methods[HET2_ID].last_workload = branch0;
    } else {
        str[i].methods[HET2_ID].last_workload = branch1;
    }

    return str[i].methods[HET2_ID].last_workload;
}

/*
 * HET -- improved.
 */
void het2_wcrt()
{
    int i;
    str[0].methods[HET2_ID].wcrt = str[0].d - str[0].c;
    
    for (i = 1; i < num_task; i++) {
        int c = str[i].c;
        int d = str[i].d;
        int w = het2_workload(i - 1, d, i);
        if ((w + c) > d) {
            het2_sched = 0;
            return;
        }
        str[i].methods[HET2_ID].wcrt = w + c;
    }
    het2_sched = 1;
}
/* ------------------------------------------------------------------------- */

/*
 * RTA ( Sjodin )
 */
void rta_wcrt()
{
    int w = 0;
    int tr = 0;
    int t = str[0].c;
    str[0].methods[RTA_ID].wcrt = str[0].c;

    int i, j;
    for (i = 1; i < num_task; i++) {
        tr = t + str[i].c;
        
        #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1
        str[i].methods[RTA_ID].loops_for += 1;
        #endif
        
        do {
            #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_WHILE == 1
            str[i].methods[RTA_ID].loops += 1;
            #endif
            
            t = tr;
            w = str[i].c;
            
            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_start();
            #endif

            for (j = 0; j < i; j++) {
                #if ( PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1) || ( TEST_TYPE == 6 || TEST_TYPE == 7 )
                str[i].methods[RTA_ID].loops_for += 1;
                #endif
                
                int a = U_CEIL( tr, str[j].t );
                
                #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_CC == 1
                str[i].methods[RTA_ID].cc = str[i].methods[RTA_ID].cc + 1;
                #endif
                
                w = w + (a * str[j].c);
                
                if (w > str[i].d) {
                    rta_sched = 0;
                    return;
                }
            }
            
            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_end();
            #endif
            
            tr = w;
        
        } while (t != tr);

        str[i].methods[RTA_ID].wcrt = t;
    }
    
    rta_sched = 1;
}
/* ------------------------------------------------------------------------- */

/*
 * RTA2 ( Urriza et. al.)
 */
void rta2_wcrt()
{
    int tr = 0;
    int t = str[0].c;
    str[0].methods[RTA2_ID].wcrt = str[0].c;

    int i, j;
    for (i = 1; i < num_task; i++) {
        tr = t + str[i].c;
        
        #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1
        str[i].methods[RTA2_ID].loops_for += 1;
        #endif

        do {
            #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_WHILE == 1
            str[i].methods[RTA2_ID].loops += 1;
            #endif
            
            t = tr;
            
            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_start();
            #endif

            for (j = 0; j < i; j++) {
                
                #if ( PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1 ) || ( TEST_TYPE == 6 || TEST_TYPE == 7 )
                str[i].methods[RTA2_ID].loops_for += 1;
                #endif
                
                int a = U_CEIL( tr, str[j].t );
                
                #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_CC == 1
                str[i].methods[RTA2_ID].cc = str[i].methods[RTA2_ID].cc + 1;
                #endif
                
                a = a * str[j].c;
                
                if (a > str[j].a) {
                    tr = tr + a - str[j].a;
                    str[j].a = a;
                    
                    if (tr > str[i].d) {
                        rta2_sched = 0;
                        return;
                    }
                }
            }
            
            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_end();
            #endif
            
        } while (t != tr);

        str[i].methods[RTA2_ID].wcrt = t;
    }
    
    rta2_sched = 1;
}
/* ------------------------------------------------------------------------- */

/*
 * RTA3 ( Urriza et. al. )
 */
void rta3_wcrt()
{
    int tr = 0;
    int t = str[0].c;
    str[0].methods[RTA3_ID].wcrt = str[0].c;

    int i, j;
    for (i = 1; i < num_task; i++) {
        tr = t + str[i].c;
        
        #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1
        str[i].methods[RTA3_ID].loops_for += 1;
        #endif

        do {
            #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_WHILE == 1
            str[i].methods[RTA3_ID].loops += 1;
            #endif
            
            t = tr;
            
            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_start();
            #endif
            
            for (j = i - 1; j >= 0; j--) {
                
                #if ( PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1 ) || ( TEST_TYPE == 6 || TEST_TYPE == 7 )
                str[i].methods[RTA3_ID].loops_for += 1;
                #endif
            
                if (tr > str[j].b) {
                    #if TEST_TYPE == 5
                    timing_ceil(int a_t = U_CEIL( tr, str[j].t ), rta3_cycles)                    
                    #else
                    int a_t = U_CEIL( tr, str[j].t );
                    #endif
                    
                    #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_CC == 1
                    str[i].methods[RTA3_ID].cc = str[i].methods[RTA3_ID].cc + 1;
                    #endif

                    int a = a_t * str[j].c;
                    tr = tr + a - str[j].a;

                    str[j].a = a;
                    str[j].b = a_t * str[j].t;
                    
                    // verifica vencimiento
                    if (tr > str[i].d) {
                        rta3_sched = 0;
                        return;
                    }
                }
            }

            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_end();
            #endif
            
        } while (t != tr);

        str[i].methods[RTA3_ID].wcrt = t;
    }
    
    rta3_sched = 1;
}
/* ------------------------------------------------------------------------- */

/*
 * ===================> THIS IS THE METHOD PUBLISHED <=======================
 */
void rta4_wcrt()
{
    int tr = str[0].c;
    str[0].methods[RTA4_ID].wcrt = str[0].c;
    
    int min_i = str[0].b;
	
	int i;
    for (i = 1; i < num_task; i++) {
        tr += str[i].c;
        
        #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1
        str[i].methods[RTA4_ID].loops_for += 1;
        #endif
        
        while (tr > min_i) {             
            min_i = str[i].b;
            
            #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_WHILE == 1
            str[i].methods[RTA4_ID].loops += 1;
            #endif
            
            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_start();
            #endif
            
            int j;
            for (j = i - 1; j >= 0; j--) {
            
                #if ( PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_FOR == 1 ) || ( TEST_TYPE == 6 || TEST_TYPE == 7 )
                str[i].methods[RTA4_ID].loops_for += 1;
                #endif

                if (tr > str[j].b) {
                    #if REMOVE_SUBTRACTION_RTA4 == 1
                    int a_t = U_CEIL( tr, str[j].t );
                    #else
                    int a_dif = tr - str[j].a;
                    #if TEST_TYPE == 5
                    timing_ceil(int a_t = U_CEIL( a_dif, str[j].tmc ), rta4_cycles)
                    #else
                    int a_t = U_CEIL( a_dif, str[j].tmc );
                    #endif
                    #endif
                    
                    #if PRINT_TASK_RESULTS == 1 && PRINT_TASK_RESULTS_CC == 1
                    str[i].methods[RTA4_ID].cc = str[i].methods[RTA4_ID].cc + 1;
                    #endif

                    #if REMOVE_SUBTRACTION_RTA4 == 1
                    int a = a_t * str[j].c;
                    tr = tr + a - str[j].a;
                    str[j].a = a;
                    str[j].b = a_t * str[j].t;                        
                    #else
                    str[j].a = a_t * str[j].c;
                    str[j].b = a_t * str[j].t;
                    tr = str[j].a + a_dif;
                    #endif
                    
                    #if CHECK_DEADLINE_AFTER_FOR == 0
                    // check deadline
                    if (tr > str[i].d) {
                        rta4_sched = 0;
                        return;
                    }
                    #endif                        
                }

                if (min_i > str[j].b) {
                    min_i = str[j].b;
                }
            }            
            
            #if TEST_TYPE == 6 || TEST_TYPE == 7
            timing_sumfor_end();
            #endif
            
            #if CHECK_DEADLINE_AFTER_FOR == 1
            // check deadline
            if (tr > str[i].d) {
                rta4_sched = 0;
                return;
            }
            #endif            
        }

        str[i].methods[RTA4_ID].wcrt = tr;
    }
	
    rta4_sched = 1;
}
/* ------------------------------------------------------------------------- */
