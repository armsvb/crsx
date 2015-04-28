// Copyright (c) 2014 IBM Corporation.
#include "prof.h"
#include "crsx.h"

#include <sys/resource.h>
#include <stdio.h>

// Mac does not have clock_gettime
#ifdef __MACH__
#include <mach/mach_time.h>
int clock_gettime(int clk_id, struct timespec *t)
{
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t time;
    time = mach_absolute_time();
    double nseconds = ((double) time * (double) timebase.numer)
            / ((double) timebase.denom);
    double seconds = ((double) time * (double) timebase.numer)
            / ((double) timebase.denom * 1e9);
    t->tv_sec = seconds;
    t->tv_nsec = nseconds;
    return 0;
}
#endif

#ifndef OMIT_TIMESPEC
struct timespec diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0)
    {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    } else
    {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}
#endif

#ifdef CRSX_ENABLE_PROFILING

typedef struct _CountTimeEntry *CountTimeEntry;

struct _CountTimeEntry {
    long count; // number of time function has been called
    long time; // Accumulated time
};

// Global profiler state.

long profMemuseMetaSubstitutes; // in KB
long pMetaSubstituteCount; // Count total number of meta substitutions
long pAccuMetaTime; // Time spent doing meta application
long pAccuMergeTime; // Time spent doing meta application
long pAccuPropagateTime; // Time spent propagating free vars
long pMergeCount;
long pCallCount; // Count total number of simple meta substitutions
long pFVTotalCount; // Count total number of free variable sets
long pFVCount; // Current number of free variable sets
size_t pFVMaxSize; // Maximum free variable sets size
long pFVUsedCount; // Count total number of free variable set actually used
long pFVRehashCount; // Count total number of time FV set has been rehashed
long pVarCount; // Current number of live variables
long pPeakVarCount; // Peak number of live variables
long pTotalConsCount; // Total number of construction
long pConsCount; // Current number of construction
long pAccuStepTime;

long pPeakTermSize, pNSPeakTermSize;
long pPeakTermMemuse, pNSPeakTermMemuse; // in Bytes
long pDuplicateMemuse;
size_t pMarker = 0;
long pStepCount;
time_t pTime; // Keep track of time
int pSampleRate; // Random term size sample rate
struct timespec pStartTime;
struct timespec pNanoTime;
struct timespec pMergeClock;
struct timespec pPropagateClock;
struct timespec pStepClock;
size_t pKeyPoolSize;

typedef char* Charp;
SETUP_STACK_TYPE(Charp)

CharpStack pStepName; // Current step function
Hashset2 pSteps; // Track time/count in step

// Measure memory use after computation.
struct rusage crsxpComputeUse;

// Measure memory use during substitution.
struct rusage crsxpSubstitutionUse;

// Count number of meta-substitutes and time per symbol
Hashset2 crsxpMetaCount;

// File where to dump events generated by instrumented code
FILE* pDumpFile;

// The beginning!
long pBegin;

// Map Variable to id
Hashset2 pVariableIds;
long pIdCounter;

static void crsxpPrintStats(Context context, Term term);

static void unlinkVarValue(Context context, const void* key, void* value)
{
    unlinkVariable(context, (Variable) key);
    FREE(context, value);
}

void crsxpInit(Context context)
{
    if (context->profiling)
    {
        getrusage(RUSAGE_SELF, &crsxpComputeUse);
        profMemuseMetaSubstitutes = 0l;
        pPeakTermSize = pNSPeakTermSize = 0l;
        pPeakTermMemuse = pNSPeakTermMemuse = 0l;
        pDuplicateMemuse = 0l;
        pStepCount = 0l;
        pMetaSubstituteCount = 0l;
        pCallCount = 0l;
        pFVTotalCount = 0l;
        pFVCount = 0l;
        pFVUsedCount = 0l;
        pFVRehashCount = 0l;
        pFVMaxSize = 0;
        pVarCount = 0l;
        pPeakVarCount = 0l;
        pTotalConsCount = 0l;
        pConsCount = 0l;
        time(&pTime);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &pStartTime);
        pSampleRate = (rand() % 500) + 500;
        crsxpMetaCount = makeHS2(context, 8, NULL, equalsChars, hashChars);
        pSteps = makeHS2(context, 16, NULL, equalsChars, hashChars);
        pStepName = makeCharpStack(context);
        pAccuMetaTime = 0l;
        pAccuMergeTime = 0l;
        pAccuPropagateTime = 0l;
        pMergeCount = 0;
        pAccuStepTime = 0;

        pDumpFile = NULL;
        pVariableIds = makeHS2(context, 8, unlinkVarValue, equalsPtr, hashPtr);
        pIdCounter = 0;

        struct timespec time;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &time);
        pBegin = time.tv_sec * 1000000000 + time.tv_nsec;

    }
}

static long nano2ms(long nano)
{
    return nano / 1000000;
}

void crsxpDestroy(Context context)
{
    if (context->profiling)
    {
        if (context->internal)
        {
            printProfiling(context);

            struct rusage usageafter;
            getrusage(RUSAGE_SELF, &usageafter);
            long use = (usageafter.ru_maxrss - crsxpComputeUse.ru_maxrss)
                    / 1024.0;
            PRINTF(context, "\nmemory use: %ldM\n", use);
        }

        if (pDumpFile)
        {
            fclose(pDumpFile);
            pDumpFile = NULL;
        }

        if (pStepName)
        {
        	freeCharpStack(pStepName);
        	pStepName = NULL;
        }
    }
}

void crsxpBeforeStep(Context context, Term term)
{
    if (context->profiling)
    {
        if (context->internal)
        {
            pushCharp(pStepName, SYMBOL(term));
            crsxpPrintStats(context, term);

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &pStepClock);
        }

        ++pStepCount;
    }
}

void crsxpAfterStep(Context context)
{
    if (context->profiling && context->internal)
    {
        struct timespec nanoTime;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &nanoTime);
        struct timespec d = diff(pStepClock, nanoTime);
        long dl = d.tv_sec * 1000000000 + d.tv_nsec;
        pAccuStepTime += dl;

        assert (!emptyCharpStack(pStepName));
        const void* name = (const void*) *topCharp(pStepName);
        CountTimeEntry entry = getValueHS2(pSteps, name);
        if (!entry)
        {
            entry = ALLOCATE(context, sizeof(struct _CountTimeEntry));
            entry->count = 0;
            entry->time = 0;
            addValueHS2(context, pSteps, name, (void*) entry);
        }
        entry->count++;
        entry->time += dl;
        popCharp(pStepName);
    }
}

void crsxpBeforeSubstitution(Context context, Term term)
{
    if (context->profiling &&  context->internal)
    {
        getrusage(RUSAGE_SELF, &crsxpSubstitutionUse);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &pNanoTime);
    }
}
void crsxpAfterSubstitution(Context context)
{
    if (context->profiling && context->internal)
    {
        pMetaSubstituteCount++;

        struct rusage uafter;
        getrusage(RUSAGE_SELF, &uafter);

        long memuse = uafter.ru_maxrss - crsxpSubstitutionUse.ru_maxrss;
        if (memuse > 300)
            PRINTF(context, "\n%-8ld# large meta substitution size: %ldKb",
                    pStepCount, memuse);

        struct timespec nanoTime;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &nanoTime);
        struct timespec d = diff(pNanoTime, nanoTime);
        long dl = d.tv_sec * 1000000000 + d.tv_nsec;

        const char* symbol = (const char*) *topCharp(pStepName);
        if (symbol)
        {
        	CountTimeEntry entry = (CountTimeEntry) getValueHS2(crsxpMetaCount,  (const void*) symbol);
            if (!entry)
            {
                entry = (CountTimeEntry) malloc(sizeof(struct _CountTimeEntry));
                entry->time = dl;
                entry->count = 1;
                addValueHS2(context, crsxpMetaCount, (const void*) symbol, (void*) entry);
            }
            else
            {
                entry->time += dl;
                entry->count ++;
            }
        }

        pAccuMetaTime += dl;
    }
}

void crsxpBeforePropagateFV(Context context)
{
    if (context->profiling && context->internal)
    {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &pPropagateClock);
    }
}

void crsxpAfterPropagateFV(Context context)
{
    if (context->profiling && context->internal)
    {
        struct timespec nanoTime;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &nanoTime);
        struct timespec d = diff(pPropagateClock, nanoTime);
        long dl = d.tv_sec * 1000000000 + d.tv_nsec;

        pAccuPropagateTime += dl;
    }
}

void crsxpBeforeCall(Context context)
{
    pCallCount++;
}

void crsxpAfterCall(Context context)
{
//    if (context->profiling)
//    {
//    }
}

void crsxpVSCreated(Context context)
{
    pFVCount++;
    pFVTotalCount++;
}

void crsxpVSFreed(Context context)
{
    pFVCount--;
}

void crsxpVSAdded(Context context, Hashset set)
{
    if (context->profiling && context->internal)
    {
        if (set->nitems > pFVMaxSize)
            pFVMaxSize = set->nitems;
    }
}

void crsxpVSContains(Hashset set)
{
    if (set && !set->marker)
    {
        pFVUsedCount++;
        set->marker = 1;
    }

}

void crsxpVSRehashed(Context context)
{
    pFVRehashCount++;
}

void crsxpReleasePools(Context context)
{
    if (context->profiling && context->internal)
    {
        pKeyPoolSize = 0;
        if (context->keyPool)
            pKeyPoolSize = context->keyPool->size;
    }
}

void crsxpBeforeMergeProperties(Context context)
{
    if (context->profiling && context->internal)
    {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &pMergeClock);
    }
}

void crsxpAfterMergeProperties(Context context)
{
    if (context->profiling && context->internal)
    {
        struct timespec nanoTime;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &nanoTime);
        struct timespec d = diff(pMergeClock, nanoTime);
        long dl = d.tv_sec * 1000000000 + d.tv_nsec;

        pAccuMergeTime += dl;
    }
}
void crsxpNamedPropertiesMerged(Context context, int count)
{
    if (context->profiling && context->internal)
    {
        pMergeCount += count;
    }
}

void crsxpMakeVariable(Context context)
{
    if (context->profiling && context->internal)
    {
        pVarCount++;
        if (pVarCount > pPeakVarCount)
            pPeakVarCount = pVarCount;
    }
}
void crsxpFreeVariable(Context context)
{
    if (context->profiling && context->internal)
    {
        pVarCount--;
    }
}

void crsxpMakeConstruction(Context context)
{
    if (context->profiling && context->internal)
    {
        pConsCount++;
        pTotalConsCount++;
    }
}

void crsxpFreeConstruction(Context context)
{
    if (context->profiling && context->internal)
    {
        pConsCount--;
    }

}

static inline FILE* dumpFile()
{
    if (!pDumpFile)
    {
        pDumpFile = fopen("crsxprof.csv", "w");
    }
    return pDumpFile;
}

void crsxpInstrumentEnter(Context context, Variable id, char* name)
{
    if (context->profiling)
    {
        FILE* out = dumpFile();
        if (out)
        {
            // Format: id,name,timestamp in nano,step
            struct timespec time;
            clock_gettime(CLOCK_MONOTONIC_COARSE, &time);
            long nano = (time.tv_sec * 1000000000 + time.tv_nsec) - pBegin;

            fprintf(out, "%ld,%s,%ld,%ld\n", pIdCounter, name, nano,
                    pStepCount);

            long* p = (long *) ALLOCATE(context, sizeof(long));
            (*p) = pIdCounter;
            linkVariable(context, id);
            pVariableIds = addValueHS2(context, pVariableIds, (const void*) id,
                    (void*) p);

            pIdCounter++;
        }
    }
}

void crsxpInstrumentExit(Context context, Variable id)
{
    if (context->profiling)
    {
        FILE* out = dumpFile();
        if (out)
        {
            // Format: id,,timestamp in nano,step
            struct timespec time;
            clock_gettime(CLOCK_MONOTONIC_COARSE, &time);
            long nano = (time.tv_sec * 1000000000 + time.tv_nsec) - pBegin;

            long* p = (long *) getValueHS2(pVariableIds, (const void*) id);
            long v = p ? *p : -1;

            fprintf(out, "%ld, ,%ld,%ld\n", v, nano, pStepCount);

            pVariableIds = removeHS2(context, pVariableIds, (const void*) id);
        }
    }
}

typedef struct _ProfCSVEntry *ProfCSVEntry;
struct _ProfCSVEntry {
    long id;
    char* name;
    long time;
    long step;
};
SETUP_STACK_TYPE(ProfCSVEntry)

typedef struct _ProfReport *ProfReport;
struct _ProfReport {
    char* name; // name
    long count;
    long accutime;
    ProfReport firstChild;
    ProfReport nextSibling;
};
SETUP_STACK_TYPE(ProfReport)

static void printChildReport(Context context, ProfReport report, int maxLength,
        int indent, long totalTime)
{
    double percent = (report->accutime / (double) totalTime) * 100.0;
    if (percent > 0.1)
    {
        printf("%.*s", indent,
                "                                                                                                                                                                           ");
        printf("%-*s%10ld%10ld%10.2f\n", maxLength - indent, report->name,
                report->count, (long) (report->accutime / 1000000.0),
                (report->accutime / (double) totalTime) * 100.0);

        ProfReport child = report->firstChild;
        while (child)
        {
            printChildReport(context, child, maxLength, indent + 1, totalTime);
            child = child->nextSibling;
        }
    }
}

static void printReport(Context context, ProfReport report, int maxLength,
        long totalTime)
{
    printf("%-*s%10s%10s%10s\n", maxLength, "name", "count", "time ms", "% time");
    printf("%.*s\n", maxLength + 30,
            "--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------");

    if (totalTime <= 0)
        totalTime = 1;

    ProfReport child = report->firstChild;
    while (child)
    {
        printChildReport(context, child, maxLength, 0, totalTime);
        child = child->nextSibling;
    }
}

static inline ProfCSVEntry makeCSVEntry(long id, char* name, long time,
        long step)
{
    ProfCSVEntry entry = (ProfCSVEntry) malloc(sizeof(struct _ProfCSVEntry));
    entry->id = id;
    entry->name = strdup(name);
    entry->time = time;
    entry->step = step;
    return entry;
}

static inline ProfReport makeReport(char* name)
{
    ProfReport report = (ProfReport) malloc(sizeof(struct _ProfReport));
    report->name = strdup(name);
    report->count = 0;
    report->accutime = 0;
    report->firstChild = NULL;
    report->nextSibling = NULL;
    return report;
}

static ProfReport findSiblingReport(ProfReport report, char* name)
{
    if (report->nextSibling)
    {
        if (!strcmp(report->nextSibling->name, name))
            return report->nextSibling;
        return findSiblingReport(report->nextSibling, name);
    }
    return NULL;
}

static ProfReport findChildReport(ProfReport report, char* name)
{
    if (report->firstChild)
    {
        if (!strcmp(report->firstChild->name, name))
            return report->firstChild;
        return findSiblingReport(report->firstChild, name);
    }
    return NULL;
}

static void addChildReport(ProfReport parent, ProfReport child)
{
    child->nextSibling = parent->firstChild;
    parent->firstChild = child;
}

void crsxpMergeBacktrace(Context context, FILE* file)
{
    long id;
    long time;
    long step;
    char name[512];
    size_t maxLength = 0;
    int maxNesting = 0;
    int nesting = 0;
    long start = -1;

    ProfCSVEntryStack entries = makeProfCSVEntryStack(context);
    ProfReportStack report = makeProfReportStack(context);
    ProfReport root = makeReport((char *) "Root");
    pushProfReport(report, root);

    while (fscanf(file, "%li,%512[^,],%li,%li\n", &id, name, &time, &step)
            != EOF)
    {
        if (strlen(name) > maxLength)
            maxLength = strlen(name);

        if (start == -1)
            start = time;

        ProfCSVEntry top = makeCSVEntry(id, name, time, step);

        if (emptyProfCSVEntryStack(entries)
                || (*topProfCSVEntry(entries))->id != id)
        {
            // Start event
            pushProfCSVEntry(entries, top);

            // Is event name already in report?
            ProfReport current = *topProfReport(report);
            ProfReport child = findChildReport(current, name);
            if (!child)
            {
                child = makeReport(name);
                addChildReport(current, child);
            }
            pushProfReport(report, child);
            nesting++;
            if (nesting > maxNesting)
                maxNesting = nesting;
        } else
        {
            // End event
            ProfCSVEntry start = *topProfCSVEntry(entries);
            popProfCSVEntry(entries);

            assert(!emptyProfReportStack(report));
            ProfReport current = *topProfReport(report);
            popProfReport(report);

            current->count++;
            current->accutime += (time - start->time);
            nesting--;
        }
    }

    if (!emptyProfCSVEntryStack(entries))
    {
        printf(
                "Error: invalid profiling information. Missing data corresponding to:\n");
        while (!emptyProfCSVEntryStack(entries))
        {
            ProfCSVEntry top = *topProfCSVEntry(entries);
            popProfCSVEntry(entries);
            printf("%ld,%s,%ld,%ld\n", top->id, top->name, top->time,
                    top->step);
        }
    }


    freeProfCSVEntryStack(entries);
    freeProfReportStack(report);

    printReport(context, root, maxLength + 1 + maxNesting, time - start);
}
//
//void pIncMetaCountFunction(Context context, char* name, long memuse)
//{
//    if (context->profiling)
//    {
//        // Search for existing entry
//        // function name are of the form R4a-Form-let2$XX
//
//        //    char* d = strchr(name, '$');
//        //    char* stem = name;
//        //    if (d)
//        //    {
//        //        int len = strlen(name) - strlen(d);
//        //        stem = ALLOCATE(context, len + 1);
//        //        memcpy(stem, name, len);
//        //        stem[len] = '\0';
//        //    }
//        char * stem = name;
//
//        int i = 0;
//        while (i < profFunctionsCount)
//        {
//
//            if (strcmp(profFunctions[i]->name, stem) == 0)
//            {
//                break;
//            }
//            i++;
//        }
//
//        if (i < profFunctionsCount)
//        {
//            profFunctions[i]->metaCount++;
//            profFunctions[i]->metaMemuse += memuse;
//            //        if (d)
//            //           FREE(context, stem);
//        }
//    }
//}
//
//static int profEntryCmp(const void* p1, const void* p2)
//{
//
//    CountTimeEntry e1 = *(CountTimeEntry*) p1;
//    CountTimeEntry e2 = *(CountTimeEntry*) p2;
//
//    if (e1->count == e2->count)
//        return 0;
//    if (e1->count > e2->count)
//        return -1;
////        if (e1->metaCount == e2->metaCount)
////            return 0;
////        if (e1->metaCount > e2->metaCount)
////            return -1;
////        if (e1->metaMemuse == e2->metaMemuse)
////               return 0;
////           if (e1->metaMemuse > e2->metaMemuse)
////               return -1;
//
//    return 1;
//}

static int stepPairEntryCmpTime(const void* p1, const void* p2)
{
    Pair pair1 = *(Pair*) p1;
    Pair pair2 = *(Pair*) p2;

    CountTimeEntry value1 = (CountTimeEntry) pair1->value;
    CountTimeEntry value2 = (CountTimeEntry) pair2->value;

    if (value1->time == value2->time)
        return 0;
    if (value1->time > value2->time)
        return -1;
    return 1;
}

static int stepPairEntryCmp(const void* p1, const void* p2)
{
    Pair pair1 = *(Pair*) p1;
    Pair pair2 = *(Pair*) p2;

    CountTimeEntry value1 = (CountTimeEntry) pair1->value;
    CountTimeEntry value2 = (CountTimeEntry) pair2->value;

    if (value1->count == value2->count)
        return 0;
    if (value1->count > value2->count)
        return -1;
    return 1;
}

void printProfiling(Context context)
{
    if (context->internal)
    {
        struct timespec endTime;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &endTime);
        struct timespec d = diff(pStartTime, endTime);
        long dl = d.tv_sec * 1000000000l + d.tv_nsec;

        PRINTF(context,
                "\n\nStep Function Report (step name) (call count) (time ms) (percent time)\n");
        PRINTF(context,
                "========================================================");

        Pair* steps = toArrayHS2(context, pSteps);
        if (steps)
        {
            qsort(steps, pSteps->size, sizeof(Pair), stepPairEntryCmp);

            size_t i;
            for (i = 0; i < pSteps->size && i < 100; i++)
            {
                long time = ((CountTimeEntry) steps[i]->value)->time;
                double percent = (time / (double) dl) * 100.0;
                PRINTF(context, "\n%-50s : %8ld %10ld %2.2f%%",
                        (const char* ) steps[i]->key,
                        ((CountTimeEntry ) steps[i]->value)->count,
                        nano2ms(time), percent);
            }
        }

        PRINTF(context,
                "\n\nMeta Substitution Report  (step name) (count) (time ms) (percent time) ...\n");
        PRINTF(context,
                "==================================================================");

        Pair* counts = toArrayHS2(context, crsxpMetaCount);
        if (counts)
        {
            qsort(counts, crsxpMetaCount->size, sizeof(Pair), stepPairEntryCmpTime);

            size_t i;
            for (i = 0; i < crsxpMetaCount->size && i < 50; i++)
            {
                CountTimeEntry entry = (CountTimeEntry) counts[i]->value;
                long time = entry->time;
                double percent = (time / (double) dl) * 100.0;
                PRINTF(context, "\n%-50s : %10ld %10ld %2.2f%%",
                        (const char* ) counts[i]->key,
						entry->count,
                        (long ) (time / 1000000.0), percent);
            }
        }

        PRINTF(context, "\n===\n\n%-50s : %ldM",
                "Total memory used by meta substitution",
                (profMemuseMetaSubstitutes / 1024));
        PRINTF(context, "\n%-50s : %ldms(%2.2f%%)",
                "Total time spent in meta substitution", nano2ms(pAccuMetaTime),
                (pAccuMetaTime / (double ) dl) * 100.0);

//        PRINTF(context, "\nPeak term size (sample)          : %ld nodes",
//                pPeakTermSize);
        //PRINTF(context, "\nPeak term memory use (sample)    : %ldM\n",
        //       (pPeakTermMemuse / 1024 / 1024));
        //PRINTF(context, "\nMemory use due to Duplicate      : %ldM\n", (pDuplicateMemuse / 1024));
        PRINTF(context, "\n%-50s : %ld", "Full meta substitution count",
                pMetaSubstituteCount);
        PRINTF(context, "\n%-50s : %ld", "Shallow meta substitution count",
                pCallCount);
        PRINTF(context, "\n%-50s : %ldms (%2.2f%%)",
                "Total time spent merging environments",
                nano2ms(pAccuMergeTime),
                (pAccuMergeTime / (double ) dl) * 100.0);
        PRINTF(context, "\n%-50s : %ld (%2.2f%%)",
                "Total time spent propagating free vars",
                nano2ms(pAccuPropagateTime),
                (pAccuPropagateTime / (double ) dl) * 100.0);
        PRINTF(context, "\n%-50s : %ld", "Total number of properties merged",
                pMergeCount);
        PRINTF(context, "\n%-50s : %ld", "Free variable sets total count",
                pFVTotalCount);
        PRINTF(context, "\n%-50s : %ld", "Free variable sets rehash count",
                pFVRehashCount);
        PRINTF(context, "\n%-50s : %ld", "Current free variable set count",
                pFVCount);
        PRINTF(context, "\n%-50s : %ld (%2.2f%%)",
                "Free variable set usage count", pFVUsedCount,
                (pFVUsedCount / (double ) pFVTotalCount) * 100.0);
        PRINTF(context, "\n%-50s : %zu", "Free variable set maximum size",
                pFVMaxSize);
        PRINTF(context, "\n%-50s : %ldms (%2.2f%%)",
                "Time spent in step functions", nano2ms(pAccuStepTime),
                (pAccuStepTime / (double ) dl) * 100.0);
        PRINTF(context, "\n%-50s : %ld", "Current variable count", pVarCount);
        PRINTF(context, "\n%-50s : %ld", "Peak variable count", pPeakVarCount);
        PRINTF(context, "\n%-50s : %ld", "Current construction count",
                pConsCount);
        PRINTF(context, "\n%-50s : %ld", "Total construction count",
                pTotalConsCount);
        PRINTF(context, "\n%-50s : %ld", "String key pool size", pKeyPoolSize);
        PRINTF(context, "\n%-50s : %ldms", "Total time", nano2ms(dl));

    }
}

//
//static void termSize(Term term, long* size, long* memuse, int sharing);

void crsxpPrintStats(Context context, Term term)
{
    if (context->internal)
    {
        if ((pStepCount % pSampleRate) == 0)
        {
            pSampleRate = (rand() % 500) + 500;
//
//            // Update peak sizes
//            long size = 0;
//            long memuse = 0;
//            termSize(term, &size, &memuse, 0);
//
//            int p = size > pPeakTermSize;
//
//            pPeakTermSize = p ? size : pPeakTermSize;
//            pPeakTermMemuse =
//                    (memuse > pPeakTermMemuse) ? memuse : pPeakTermMemuse;
//
//            long nssize = 0;
//            long nsmemuse = 0;
//            termSize(term, &nssize, &nsmemuse, 1);
//            pNSPeakTermSize =
//                    (nssize > pNSPeakTermSize) ? nssize : pNSPeakTermSize;
//            pNSPeakTermMemuse =
//                    (nsmemuse > pNSPeakTermMemuse) ?
//                            nsmemuse : pNSPeakTermMemuse;

            time_t now;
            time(&now);
            if (difftime(now, pTime) > 1)
            {
                pTime = now;

                printf("\n%-8ld# stats: substitution count: %ld", pStepCount,
                        pMetaSubstituteCount);

                fflush(stdout);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Compute term size without check overhead

//static void termSize2(Term term, long* size, long* memuse, int sharing);
//
//// size    : total number of nodes, excluding property links.
//// memuse  : total memory use.
//// sharing : when true, account for shared terms.
//static void termSize(Term term, long* size, long* memuse, int sharing)
//{
//    pMarker++;
//    termSize2(term, size, memuse, sharing);
//}
//
//static void termSize2(Term term, long* size, long* memuse, int sharing)
//{
//    if (!sharing && term->marker == pMarker)
//        return;
//
//    term->marker = pMarker;
//
//    (*size)++;
//
//    if (IS_VARIABLE_USE(term))
//    {
//        Variable v = VARIABLE(term);
//
//        (*memuse) += sizeof(struct _Variable);
//        (*memuse) += strlen(v->name) + 1;
//    } else
//    {
//        (*memuse) += sizeof(struct _Construction);
//
//        Construction construction = asConstruction(term);
//        if (construction->fvs && construction->fvs != AllFreeVariables
//                && (sharing || construction->fvs->marker != pMarker))
//        {
//            construction->fvs->marker = pMarker;
//
//            (*memuse) += sizeof(struct _Hashset);
//            (*memuse) += construction->fvs->capacity * sizeof(size_t);
//        }
//        if (construction->nfvs && construction->nfvs != AllFreeVariables
//                && (sharing || construction->nfvs->marker != pMarker))
//        {
//            construction->nfvs->marker = pMarker;
//
//            (*memuse) += sizeof(struct _Hashset);
//            (*memuse) += construction->nfvs->capacity * sizeof(size_t);
//        }
//        if (construction->vfvs && construction->vfvs != AllFreeVariables
//                && (sharing || construction->vfvs->marker != pMarker))
//        {
//            construction->vfvs->marker = pMarker;
//
//            (*memuse) += sizeof(struct _Hashset);
//            (*memuse) += construction->vfvs->capacity * sizeof(size_t);
//        }
//
//        if (construction->properties)
//        {
//            NamedPropertyLink link;
//
//            for (link = construction->properties->namedProperties; link; link =
//                    link->link)
//            {
//                if (!sharing && link->marker == pMarker)
//                    break;
//
//                link->marker = pMarker;
//
//                if (link->name)
//                {
//                    termSize2(link->u.term, size, memuse, sharing);
//                } else
//                {
//                    (*memuse) += memoryUsedHS2(link->u.propset);
//                }
//
//                (*memuse) += sizeof(struct _NamedPropertyLink);
//            }
//        }
//
//        if (construction->properties)
//        {
//            VariablePropertyLink link;
//            for (link = construction->properties->variableProperties; link;
//                    link = link->link)
//            {
//                if (!sharing && link->marker == pMarker)
//                    break;
//
//                if (link->variable)
//                {
//                    termSize2(link->u.term, size, memuse, sharing);
//
//                    (*memuse) += sizeof(struct _Variable);
//                } else
//                {
//                    (*memuse) += memoryUsedHS2(link->u.propset);
//                }
//
//                (*memuse) += sizeof(struct _VariablePropertyLink);
//            }
//        }
//
//        const int arity = ARITY(term);
//        int i;
//        for (i = 0; i < arity; ++i)
//        {
//            termSize2(SUB(term, i), size, memuse, sharing);
//        }
//    }
//}

#else

void crsxpInit(Context context)
{}
void crsxpDestroy(Context context)
{}
void crsxpBeforeStep(Context context, Term term)
{}
void crsxpAfterStep(Context context)
{}
void crsxpBeforeSubstitution(Context context, Term term)
{}
void crsxpAfterSubstitution(Context context)
{}
void crsxpBeforeCall(Context context)
{}
void crsxpAfterCall(Context context)
{}
void crsxpVSCreated(Context context)
{}
void crsxpVSAdded(Context context, Hashset set)
{}
void crsxpVSContains(Hashset set)
{}
void crsxpVSFreed(Context context)
{}
void crsxpVSRehashed(Context context)
{}
void crsxpBeforeMergeProperties(Context context)
{}
void crsxpAfterMergeProperties(Context context)
{}
void crsxpNamedPropertiesMerged(Context context, int count)
{}
void printProfiling(Context context)
{}
void pIncMetaCountFunction(Context context, char* functionName, long memuse)
{}
void crsxpReleasePools(Context context)
{}
void crsxpMakeVariable(Context context)
{}
void crsxpFreeVariable(Context context)
{}
void crsxpMakeConstruction(Context context)
{}
void crsxpFreeConstruction(Context context)
{}
void crsxpBeforePropagateFV(Context context)
{}
void crsxpAfterPropagateFV(Context context)
{}
void crsxpMergeBacktrace(Context context, FILE* file)
{}
void crsxpInstrumentEnter(Context context, Variable id, char* name)
{}
void crsxpInstrumentExit(Context context, Variable id)
{}

#endif
