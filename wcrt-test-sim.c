/*
 * Evaluation of the scheduling algorithms HET, RTA, RTA2 and RTA3.
 * v1.0 -- 12/01/2017 -- initial version.
 * v2.0 -- 16/05/2017 -- second version.
 * v3.0 -- 04/07/2017 -- revision for publication.
 */
#include <libxml/xmlreader.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <gsl/gsl_statistics.h>

/*
 * Ceil and floor operations without using the library math, when period
 * and wcet values use an integer data type.
 */
#define CEIL_TYPE 1 
#if CEIL_TYPE == 1
#define U_CEIL( x, y )    ( ( x / y ) + ( x % y != 0 ) )
#define U_FLOOR( x, y )   ( x / y )
#endif
#if CEIL_TYPE == 2
#define U_CEIL( x, y )    (int) ceil((double) x / (double) y)
#define U_FLOOR( x, y )   (int) floor((double) x / (double) y)
#endif

/*
 * XML tags.
 */
#define ELEMENT             1                               // Tag end
#define END_ELEMENT         15                              // Tag start
#define SET_TAG             (const xmlChar*) "Set"          // <Set> tag -- set
#define S_TAG               (const xmlChar*) "S"            // <S> tag -- rts
#define I_TAG               (const xmlChar*) "i"            // <i> tag -- task
#define SET_SIZE_ATTR       (const xmlChar*) "size"         // "size" attribute in <Set> tag
#define SET_RTS_SIZE_ATTR   (const xmlChar*) "n"            // "n" attribute in <Set> tag
#define SET_UF_ATTR         (const xmlChar*) "u"            // "u" attribute in <Set> tag
#define RTS_ID_ATTR         (const xmlChar*) "count"        // "count" attribute in <S> tag
#define RTS_UF_ATTR         (const xmlChar*) "U"            // "U" attribute in <S> tag
#define ID_ATTR             (const xmlChar*) "nro"          // "nro" attribute in <i> tag
#define WCET_ATTR           (const xmlChar*) "C"            // "C" attribute in <i> tag
#define T_ATTR              (const xmlChar*) "T"            // "T" attribute in <i> tag
#define D_ATTR              (const xmlChar*) "D"            // "D" attribute in <i> tag

/*
 * Number of schedulability methods to test.
 */
#define NUM_SCHED_METHODS 5

/*
 * Name of the schedulability methods to evaluate.
 */
#define HET    "het"
#define RTA    "rta"
#define RTA2   "rta2"
#define RTA3   "rta3"
#define RTA4   "rta4"

/*
 * Position of the method in the schedulabilty methods array.
 */
#define HET_ID    0
#define RTA_ID    1
#define RTA2_ID   2
#define RTA3_ID   3
#define RTA4_ID   4

/*
 * Return value for the schedulability methods.
 */
#define SCHED     1
#define NON_SCHED 0

/*
 * Global variables.
 */
int rts_founded = 0;        // Number of RTS in the XML file evaluated.
int verbose = 0;            // Print addtional info to stderr
FILE* out_file;             // Result file

// Tarea
struct task_t {
    int id;                         // task id
    int c;                          // wcet
    int t;                          // period
    int d;                          // deadline
    int tmc;                        // period - deadline
    int wcrt[NUM_SCHED_METHODS];    // wcrt
    int cc[NUM_SCHED_METHODS];      // cc
    int loops_w[NUM_SCHED_METHODS]; // number of while loops
    int loops_f[NUM_SCHED_METHODS]; // number of for loops
    int a_rta2;
    int b_rta2;    
    int a_rta3;
    int b_rta3;
    int a_rta4;
    int b_rta4;
    int last_psi;                   // used by het -- last time instant evaluated
    int last_workload;              // used by het -- last workload   
};

// rts
struct rts_t {
    int rts_id;
    int rts_uf;
    int rts_ntask;
    int *schedulable;
    struct task_t **tasks;    
};

// set of rts
struct set_t {
    int set_size;
    int set_uf;   
    int set_rts_ntask;
    struct rts_t **rts_list;
};

// prototipe for scheduling analysis methods
typedef int (*sched_test_method) (struct rts_t*);

// test method result
struct result_t {
    double *cc;
    double *loops;
    double cc_mean;
    double cc_std;
    double loops_mean;
    double loops_std;
};

struct method_t {
    char* method_name;
    int method_id;
    sched_test_method method;
    struct result_t *result;
};

/*
 * Prototipes
 */
int rta_wcrt(struct rts_t*);
int rta2_wcrt(struct rts_t*);
int rta3_wcrt(struct rts_t*);
int het_workload(int i, int b, int n, struct task_t**);
int het_wcrt(struct rts_t*);

int het_workload(int i, int b, int n, struct task_t **tasks)
{
    tasks[n]->loops_w[HET_ID] += 1;

    int f = (int) U_FLOOR(b, tasks[i]->t);
    int c = (int) U_CEIL(b, tasks[i]->t);

    tasks[n]->cc[HET_ID] += 2;

    int branch0 = b - f * (tasks[i]->t - tasks[i]->c);
    int branch1 = c * tasks[i]->c;

    if (i > 0) {
        int l_w = tasks[i - 1]->last_workload;
        int tmp = f * tasks[i]->t;
        if (tmp > tasks[i - 1]->last_psi) {
            l_w = het_workload(i - 1, tmp, n, tasks);
        }

        branch0 += l_w;
        branch1 += het_workload(i - 1, b, n, tasks);
    }

    tasks[i]->last_psi = b;

    if (branch0 <= branch1) {
        tasks[i]->last_workload = branch0;
    } else {
        tasks[i]->last_workload = branch1;
    }

    return tasks[i]->last_workload;
}

/*
 * HET
 * "Schedulability Analysis of Periodic Fixed Priority Systems"
 * http://ieeexplore.ieee.org/document/1336766/
 * --
 * See also:
 * "Efficient Exact Schedulability Tests for Fixed Priority Real-Time Systems"
 * http://ieeexplore.ieee.org/document/4487061/
 */
int het_wcrt(struct rts_t *rts)
{
    struct task_t **tasks = rts->tasks;
    
    int i;
    for (i = 1; i < rts->rts_ntask; i++) {
        tasks[i]->loops_f[HET_ID] += 1;

        int w = het_workload(i - 1, tasks[i]->d, i, tasks);

        if ((w + tasks[i]->c) > tasks[i]->d) {
            rts->schedulable[HET_ID] = NON_SCHED;
            return NON_SCHED;
        }
        
        tasks[i]->wcrt[HET_ID] = w + tasks[i]->c;
    } 
    
    rts->schedulable[HET_ID] = SCHED;
    return SCHED;
}

/*
 * RTA
 * "Improved Response-Time Analysis Calculations"
 * http://doi.ieeecomputersociety.org/10.1109/REAL.1998.739773
 */
int rta_wcrt(struct rts_t *rts)
{
    struct task_t **tasks = rts->tasks;
    
    int w = 0;
    int tr = 0;
    int t = tasks[0]->c;
    tasks[0]->wcrt[RTA_ID] = tasks[0]->c;

    int i, j;
    for (i = 1; i < rts->rts_ntask; i++) {
        tr = t + tasks[i]->c;
        tasks[i]->loops_f[RTA_ID] += 1;

        do {
            tasks[i]->loops_w[RTA_ID] += 1;
            t = tr;
            w = tasks[i]->c;

            for (j = 0; j < i; j++) {
                tasks[i]->loops_f[RTA_ID] += 1;
                
                int c_j = tasks[j]->c;
                int t_j = tasks[j]->t;
                int a = U_CEIL(tr, t_j);
                tasks[i]->cc[RTA_ID] += 1;
                
                w = w + (a * c_j);
                
                if (w > tasks[i]->d) {
                    rts->schedulable[RTA_ID] = NON_SCHED;
                    return NON_SCHED;
                }
            }
            
            tr = w;
        
        } while (t != tr);

        tasks[i]->wcrt[RTA_ID] = t;
    }
    
    rts->schedulable[RTA_ID] = SCHED;
    return SCHED;
}

/*
 * RTA2
 * "Reduced computational cost in the calculation of worst case response time for real time systems"
 * http://sedici.unlp.edu.ar/handle/10915/9654
 */
int rta2_wcrt(struct rts_t *rts)
{
    struct task_t **tasks = rts->tasks;

    int tr = 0;
    int t = tasks[0]->c;
    tasks[0]->wcrt[RTA2_ID] = tasks[0]->c;

    int i, j;
    for (i = 1; i < rts->rts_ntask; i++) {
        tr = t + tasks[i]->c;
        tasks[i]->loops_f[RTA2_ID] += 1;

        do {
            tasks[i]->loops_w[RTA2_ID] += 1;
            t = tr;

            for (j = 0; j < i; j++) {
                tasks[i]->loops_f[RTA2_ID] += 1;
                
                int a = U_CEIL(tr, tasks[j]->t);
                tasks[i]->cc[RTA2_ID] += 1;
                a = a * tasks[j]->c;
                
                if (a > tasks[j]->a_rta2) {
                    tr = tr + a - tasks[j]->a_rta2;
                    tasks[j]->a_rta2 = a;
                    
                    if (tr > tasks[i]->d) {
                        rts->schedulable[RTA2_ID] = NON_SCHED;
                        return NON_SCHED;
                    }
                }
            }
        } while (t != tr);
     
        tasks[i]->wcrt[RTA2_ID] = t;
    }
    
    rts->schedulable[RTA2_ID] = SCHED;
    return SCHED;
}

/*
 * RTA3
 * "Computational Cost Reduction for Real-Time Schedulability Tests Algorithms"
 * http://ieeexplore.ieee.org/document/7404899/
 */
int rta3_wcrt(struct rts_t *rts)
{
    struct task_t **tasks = rts->tasks;

    int tr = 0;
    int t = tasks[0]->c;
    tasks[0]->wcrt[RTA3_ID] = tasks[0]->c;

    int i, j;
    for (i = 1; i < rts->rts_ntask; i++) {
        tr = t + tasks[i]->c;
        tasks[i]->loops_f[RTA3_ID] += 1;

        do {
            tasks[i]->loops_w[RTA3_ID] += 1;
            t = tr;
            
            for (j = i - 1; j >= 0; j--) {
                tasks[i]->loops_f[RTA3_ID] += 1;
            
                if (tr > tasks[j]->b_rta3) {
                    int a_t = U_CEIL(tr, tasks[j]->t);
                    tasks[i]->cc[RTA3_ID] += 1;

                    int a = a_t * tasks[j]->c;
                    tr = tr + a - tasks[j]->a_rta3;

                    tasks[j]->a_rta3 = a;
                    tasks[j]->b_rta3 = a_t * tasks[j]->t;
                    
                    // verifica vencimiento
                    if (tr > tasks[i]->d) {
                        rts->schedulable[RTA3_ID] = NON_SCHED;
                        return NON_SCHED;
                    }
                }
            }            
        } while (t != tr);

        tasks[i]->wcrt[RTA3_ID] = t;
    }
    
    rts->schedulable[RTA3_ID] = SCHED;
    return SCHED;
}

/*
 * RTA4
 */
int rta4_wcrt(struct rts_t *rts)
{
    struct task_t **tasks = rts->tasks;

    int tr = tasks[0]->c;
    tasks[0]->wcrt[RTA4_ID] = tasks[0]->c;
    
    int min_i = tasks[0]->b_rta4;
	
	int i;
    for (i = 1; i < rts->rts_ntask; i++) {
        tr += tasks[i]->c;
        tasks[i]->loops_f[RTA4_ID] += 1;
        
        while (tr > min_i) { 
            min_i = tasks[i]->b_rta4;
                        
            tasks[i]->loops_w[RTA4_ID] += 1;
            
            int j;
            for (j = i - 1; j >= 0; j--) {
            
                tasks[i]->loops_f[RTA4_ID] += 1;

                if (tr > tasks[j]->b_rta4) {
                    int a_dif = tr - tasks[j]->a_rta4;
                    int a_t = U_CEIL( a_dif, tasks[j]->tmc );
                    
                    tasks[i]->cc[RTA4_ID] += 1;

                    tasks[j]->a_rta4 = a_t * tasks[j]->c;
                    tasks[j]->b_rta4 = a_t * tasks[j]->t;
                    tr = tasks[j]->a_rta4 + a_dif;
                    
                    // verifica vencimiento
                    if (tr > tasks[i]->d) {
                        rts->schedulable[RTA4_ID] = NON_SCHED;
                        return NON_SCHED;
                    }
                }

                if (min_i > tasks[j]->b_rta4) {
                    min_i = tasks[j]->b_rta4;
                }
            }
        }

        tasks[i]->wcrt[RTA4_ID] = tr;
    }
	
    rts->schedulable[RTA4_ID] = SCHED;
    return SCHED;
}

void reset_rts(struct rts_t *rts)
{
    int i, j;
    for (i = 0; i < rts->rts_ntask; i++) {
        struct task_t *task = rts->tasks[i];

        task->a_rta2 = task->c;
        task->b_rta2 = task->t;
        task->a_rta3 = task->c;
        task->b_rta3 = task->t;
        task->a_rta4 = task->c;
        task->b_rta4 = task->t;
        task->last_psi = 0;
        task->last_workload = 0;
                       
        for (j = 0; j < NUM_SCHED_METHODS; j++) {
            task->cc[j] = 0;
            task->loops_w[j] = 0;
            task->loops_f[j] = 0;
            task->wcrt[j] = 0;
        }
    }
}

/*
 * Parse the XML file. If a new RTS is found, it is evalutad with the methods in method array.
 */
void processXmlFile(xmlTextReaderPtr reader, struct set_t *rts_set, struct method_t *methods)
{
    xmlChar *name = xmlTextReaderLocalName(reader);

    // Tag <Set> -- initial tag
    if (xmlStrcasecmp(name, SET_TAG) == 0) {
        if (xmlTextReaderNodeType(reader) == ELEMENT) {            
            xmlChar *c_set_rts_uf = xmlTextReaderGetAttribute(reader, SET_UF_ATTR);
            xmlChar *c_set_rts_size = xmlTextReaderGetAttribute(reader, SET_SIZE_ATTR);
            xmlChar *c_set_rts_ntask = xmlTextReaderGetAttribute(reader, SET_RTS_SIZE_ATTR);

            rts_set->set_uf = atoi((char*) c_set_rts_uf);
            rts_set->set_size = atoi((char*) c_set_rts_size);
            rts_set->set_rts_ntask = atoi((char*) c_set_rts_ntask);

            xmlFree(c_set_rts_uf);
            xmlFree(c_set_rts_size);
            xmlFree(c_set_rts_ntask);
        }
    }

    // Tag <S> -- RTS
    if (xmlStrcasecmp(name, S_TAG) == 0) {
        if (xmlTextReaderNodeType(reader) == ELEMENT) {
            xmlChar *c_rts_id = xmlTextReaderGetAttribute(reader, RTS_ID_ATTR);
            xmlChar *c_rts_uf = xmlTextReaderGetAttribute(reader, RTS_UF_ATTR);

            // reserve memory for the rts and add it to rts_list
            struct rts_t *new_rts = malloc(sizeof(struct rts_t));            

            // reserve memory for the methods results
            new_rts->schedulable = malloc(sizeof(int) * NUM_SCHED_METHODS);
            // reserve memory for the rts tasks
            new_rts->tasks = malloc(sizeof(struct task_t*) * rts_set->set_rts_ntask);

            // complete data about this rts
            new_rts->rts_id = atoi((char*) c_rts_id);
            new_rts->rts_uf = atoi((char*) c_rts_uf);
            new_rts->rts_ntask = rts_set->set_rts_ntask;

            // add rts to set
            rts_set->rts_list[rts_founded] = new_rts;

            // free memory
            xmlFree(c_rts_id);
            xmlFree(c_rts_uf);
        }

        if (xmlTextReaderNodeType(reader) == END_ELEMENT) {            
            struct rts_t *rts = rts_set->rts_list[rts_founded];
            reset_rts(rts);
            
            // evaluate the methods
            int i, j;
            for (i = 0; i < NUM_SCHED_METHODS; i++) {
                int method_id = methods[i].method_id;
                rts->schedulable[method_id] = (*methods[i].method)(rts);

                // store totals
                for (j = 0; j < rts->rts_ntask; j++) {
                    struct task_t *task = rts->tasks[j];
                    methods[i].result->cc[rts_founded] += task->cc[method_id];
                    methods[i].result->loops[rts_founded] += task->loops_w[method_id] + task->loops_f[method_id];
                }
            }

            rts_founded = rts_founded + 1;
        }
    }

    // Tag <i> -- a real-time task
    if (xmlStrcasecmp(name, I_TAG) == 0) {
        xmlChar *c_id = xmlTextReaderGetAttribute(reader, ID_ATTR);
        xmlChar *wcet = xmlTextReaderGetAttribute(reader, WCET_ATTR);
        xmlChar *t = xmlTextReaderGetAttribute(reader, T_ATTR);
        xmlChar *d = xmlTextReaderGetAttribute(reader, D_ATTR);

        int id = atoi((char*) c_id) - 1;

        struct rts_t *rts = rts_set->rts_list[rts_founded];
        
        // reserve memory for the task
        struct task_t *task = malloc(sizeof(struct task_t));

        // complete the basic task data
        task->id = id + 1;
        task->c = atoi((char*) wcet);
        task->t = atoi((char*) t);
        task->d = atoi((char*) d);
        task->tmc = task->t - task->c;

        // add the task to rts
        rts->tasks[id] = task;

        xmlFree(c_id);
        xmlFree(wcet);
        xmlFree(t);
        xmlFree(d);       
    }

    xmlFree(name);
}

/*
 * Evaluate the schedulability of the rts in the specified xml file.
 */ 
void testRtsInXml(char *file, struct set_t* rts_set, struct method_t *methods, int limit)
{    
    // get read pointer
    xmlTextReaderPtr reader = xmlNewTextReaderFilename(file);
    if (reader == NULL) {
        fprintf(stderr, "Unable to open %s\n", file);
        exit(EXIT_FAILURE);
    }

    // parse xml file and evaluate schedulability methods
    int ret = xmlTextReaderRead(reader);
    while (ret == 1) {
        if (limit > 0 && rts_founded == limit) {
            break;
        }
        processXmlFile(reader, rts_set, methods);
        ret = xmlTextReaderRead(reader);
    }

    if (rts_set->set_size < limit) {
        fprintf(stderr, "Warning: %d str in file according to XML info, but %d to be tested.\n", rts_set->set_size, limit);
    }

    xmlFreeTextReader(reader);
}

/*
 * Print method results to out_file.
 */
void save_result(char* method, struct result_t* result, int use_csv, char *csv_sep)
{
    if (use_csv == 0) {
        fprintf(out_file, "%10s%15f%15f%15f%15f\n", method, result->cc_mean, result->cc_std, 
                                                    result->loops_mean, result->loops_std);
    } else {
        fprintf(out_file, "%2$s%1$s%3$f%1$s%4$f%1$s%5$f%1$s%6$f\n", csv_sep, method, result->cc_mean, result->cc_std, 
                                                                                     result->loops_mean, result->loops_std);
    }    
}

/*
 * Print help and usage information.
 */
void printUsage(char* progName, int exitCode)
{
    fprintf(stderr, "Usage: %s [options] file\n", progName);
    fprintf(stderr,
            "\t-v  --verbose\tDisplay additional information about the clock used.\n"
            "\t-h  --help\tDisplay this information.\n"
            "\t-l  --limit\tTest first n RTS in file.\n"
            "\t-c  --csv\tCSV output with specified line separator.\n");
    exit(exitCode);
}

int main(int argc, char **argv)
{
    int i, j, k;

    if (argc <= 1) {
        printUsage(argv[0], EXIT_FAILURE);
    }

    // options -- short format
    const char *shortOpts = "hvl:c:";
    // options -- long format
    const struct option longOpts[] = {
        {"help",    no_argument,        NULL, 'h'},
        {"verbose", no_argument,        NULL, 'v'},
        {"limit",   required_argument,  NULL, 'l'},
        {"csv",     required_argument,  NULL, 'c'},
        {0, 0, 0, 0}
    };

    verbose = 0;
    rts_founded = 0;
    int limit = 0;
    
    int use_csv = 0;
    char* csv_sep;

    int nextOption;

    do {
        nextOption = getopt_long(argc, argv, shortOpts, longOpts, NULL);
        switch (nextOption) {
            case 'h': // -h or --help
                printUsage(argv[0], EXIT_SUCCESS);
            case 'v': // -v or --verbose
                verbose = 1;
                break;
            case 'l': // -l or --limit
                limit = atoi(optarg);
                break;            
            case 'c': // -c or --csv
                use_csv = 1;
                csv_sep = optarg;
                break;
            case '?': // invalid option
                printUsage(argv[0], EXIT_FAILURE);
            case -1: // no more options
                break;
            default:
                abort();
        }
    } while (nextOption != -1);
    
    // print info to stderr if requested
    if (verbose == 1) {
        fprintf(stderr, "Testing %s...\n", argv[optind]);
    }

    // stdout as default output file
    out_file = stdout;

    // reserve memory for the set
    struct set_t *rts_set = malloc(sizeof(struct set_t));
    rts_set->set_uf = 0;
    rts_set->set_size = 0;
    rts_set->set_rts_ntask = 0;    
    rts_set->rts_list = malloc(sizeof(struct rts_t*) * limit);

    // reserve memory for test results
    struct result_t* het_results = malloc(sizeof(struct result_t));
    struct result_t* rta_results = malloc(sizeof(struct result_t));
    struct result_t* rta2_results = malloc(sizeof(struct result_t));
    struct result_t* rta3_results = malloc(sizeof(struct result_t));
    struct result_t* rta4_results = malloc(sizeof(struct result_t));

    // methods to test
    struct method_t methods[] = {[RTA_ID]  {RTA,  RTA_ID,  rta_wcrt,  rta_results},
                                 [RTA2_ID] {RTA2, RTA2_ID, rta2_wcrt, rta2_results},
                                 [RTA3_ID] {RTA3, RTA3_ID, rta3_wcrt, rta3_results},
                                 [RTA4_ID] {RTA4, RTA4_ID, rta4_wcrt, rta4_results},
                                 [HET_ID]  {HET,  HET_ID,  het_wcrt,  het_results}
                                 };

    for (i = 0; i < NUM_SCHED_METHODS; i++) {
        methods[i].result->cc = calloc(limit, sizeof(double));
        methods[i].result->loops = calloc(limit, sizeof(double));
    }

    if (verbose == 1) {
        fprintf(stderr, "Testing %d rts.\n", limit);
    }

    // read rts from xml file into rts_set
    char *filename = argv[optind];
    testRtsInXml(filename, rts_set, methods, limit);

    // compute means and stdev
    for (i = 0; i < NUM_SCHED_METHODS; i++) {
        methods[i].result->cc_mean = gsl_stats_mean(methods[i].result->cc, 1, rts_founded);
        methods[i].result->cc_std = gsl_stats_sd_m(methods[i].result->cc, 1, rts_founded, methods[i].result->cc_mean);
        methods[i].result->loops_mean = gsl_stats_mean(methods[i].result->loops, 1, rts_founded);
        methods[i].result->loops_std = gsl_stats_sd_m(methods[i].result->loops, 1, rts_founded, methods[i].result->loops_mean);
    }
        
    int rts_sched_cnt = 0; 
    int rts_nonsched_cnt = 0;

    // verify that all methods give the same result
    for (i = 0; i < rts_founded; i++) {
        struct rts_t *rts = rts_set->rts_list[i];        

        int sum = 0;
        for (j = 0; j < NUM_SCHED_METHODS; j++) {
            sum += rts->schedulable[j];
        }

        if (sum > 0 && sum < NUM_SCHED_METHODS) {
            fprintf(stderr, "Error! Method results are not the same. RTS %d\n", i);
            for (j = 0; j < NUM_SCHED_METHODS; j++) {
                fprintf(stderr, "%s: %d\n", methods[j].method_name, rts->schedulable[j]);
            }
            break;
        }

        if (rts->schedulable[RTA_ID] == SCHED) {
            rts_sched_cnt += 1;
        } else {
            rts_nonsched_cnt += 1; 
        }
    }
    
    // verify that all wcrt are the same (only for RTA methods)
    for (j = 0; j < rts_founded; j++) {
        struct rts_t *rts = rts_set->rts_list[j];
        for (k = 0; k < rts->rts_ntask; k++) {
            struct task_t *task = rts->tasks[k];
            int ref_wcrt = task->wcrt[RTA_ID];
            if (ref_wcrt != task->wcrt[RTA2_ID] || ref_wcrt != task->wcrt[RTA3_ID] || ref_wcrt != task->wcrt[RTA4_ID]) 
            {
                fprintf(stderr, "Error! WCRT are not the same. RTS %d, task %d\n", j, k);
                
                int i = 0;                
                fprintf(stderr, "%13s%10s%10s%10s%10s%10s\n", "RTA", "RTA2", "RTA3", "RTA4", "C_i", "D_i"); 
                for (i = 0; i < rts->rts_ntask; i++) {
                    fprintf(stderr, "%3d%10d%10d%10d%10d%10d%10d\n", i, rts->tasks[i]->wcrt[RTA_ID], rts->tasks[i]->wcrt[RTA2_ID], 
                                                                        rts->tasks[i]->wcrt[RTA3_ID], rts->tasks[i]->wcrt[RTA4_ID],
                                                                        rts->tasks[i]->c, rts->tasks[i]->d );
                }
            
                return(EXIT_FAILURE);
            }
        }
    }                
    
    // get timestamp for the test
    char test_date[50];
    time_t current_time = time(NULL);            
    strftime(test_date, 50, "%R %d/%m/%Y", localtime(&current_time));
    
    // print header
    fprintf(out_file, "%s\n", filename);
    fprintf(out_file, "%s\n", test_date);
    fprintf(out_file, "Total: %d\n", rts_founded);
    fprintf(out_file, "Sched: %d\n", rts_sched_cnt);
    fprintf(out_file, "Non sched: %d\n", rts_nonsched_cnt);

    // print column names
    if (use_csv == 0) {
        fprintf(out_file, "%10s%15s%15s%15s%15s\n", "method", "cc_mean", "cc_mean_std", "loops_mean", "loops_mean_std");
    } else {
        fprintf(out_file, "method%1$scc_mean%1$scc_mean_std%1$sloops_mean%1$sloops_mean_std\n", csv_sep);
    }    
                      
    // print the results
    for (i = 0; i < NUM_SCHED_METHODS; i++) {
        save_result(methods[i].method_name, methods[i].result, use_csv, csv_sep);
    }
    
    return(EXIT_SUCCESS);
}

