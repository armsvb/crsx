// Copyright (c) 2014 IBM Corporation.
#include "prof.h"
#include "crsx.h"

#ifdef CRSX_ENABLE_PROFILING

#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

// Global profiler state.

char* profStepStack[16384];
unsigned profStepStackSize;
long profMemuseMetaSubstitutes; // in KB
long pMetaSubstituteCount; // Count total number of metasubstitutes
ProfMetaSubstitute profLargeMetaSubstitutes;
ProfBufferCopy profLargeBufferCopy;
ProfFunctionEntry profFunctions[16384]; // use array for sorting
unsigned profFunctionsCount = 0;
long pPeakTermSize, pNSPeakTermSize;
long pPeakTermMemuse, pNSPeakTermMemuse; // in Bytes
long pDuplicateMemuse;
size_t pMarker = 0;
long pStepCount;
time_t pTime; // Keep track of time
int pSampleRate; // Random term size sample rate

// Measure memory use after computation.
struct rusage crsxpComputeUse;

// Measure memory use during substitution.
struct rusage crsxpSubstitutionUse;



static void crsxpPrintStats(Context context, Term term);

void crsxpInit(Context context)
{
    if (context->profiling)
    {
        getrusage(RUSAGE_SELF, &crsxpComputeUse);
        profStepStackSize = 0;
        profFunctionsCount = 0;
        profMemuseMetaSubstitutes = 0l;
        pPeakTermSize = pNSPeakTermSize = 0l;
        pPeakTermMemuse = pNSPeakTermMemuse = 0l;
        pDuplicateMemuse = 0l;
        pStepCount = 0l;
        pMetaSubstituteCount = 0l;
        time(&pTime);
        pSampleRate = (rand() % 500) + 500;
    }
}

void crsxpDestroy(Context context)
{
    if (context->profiling)
    {
        printProfiling(context);

        struct rusage usageafter;
        getrusage(RUSAGE_SELF, &usageafter);
        long use = (usageafter.ru_maxrss - crsxpComputeUse.ru_maxrss) / 1024.0;
        PRINTF(context, "\nmemory use: %ldM\n", use);

        int i = 0;
        while (i < profFunctionsCount)
        {
            FREE(context, profFunctions[i]);
            profFunctions[i] = NULL;
            i ++;
        }
    }
}

void crsxpBeforeStep(Context context, Term term)
{
    if (context->profiling)
    {
        profStepStack[profStepStackSize++] = SYMBOL(term);
        profAddStepFunction(context, SYMBOL(term));
        crsxpPrintStats(context, term);
        ++pStepCount;
    }
}

void crsxpAfterStep(Context context)
{
    if (context->profiling)
    {
        profStepStackSize --;
    }
}

void crsxpBeforeSubstitution(Context context)
{
    if (context->profiling)
    {
        getrusage(RUSAGE_SELF, &crsxpSubstitutionUse);
    }
}

void crsxpAfterSubstitution(Context context)
{
    if (context->profiling)
    {
        pMetaSubstituteCount ++;

        struct rusage uafter;
        getrusage(RUSAGE_SELF, &uafter);

        long memuse = uafter.ru_maxrss - crsxpSubstitutionUse.ru_maxrss;
        if (memuse > 50)
            PRINTF(context, "\n%-8ld# large meta substitution size: %ldKb", pStepCount, memuse);
    }
}

void profAddStepFunction(Context context, char* name)
{
    if (context->profiling)
    {
        if (profFunctionsCount >= 16384)
            return;

        char* stem = name;

        int i = 0;
        while (i < profFunctionsCount)
        {

            if (strcmp(profFunctions[i]->name, stem) == 0)
            {
                break;
            }
            i ++;
        }

        if (i < profFunctionsCount)
        {
            profFunctions[i]->count ++;
        }
        else
        {
            ProfFunctionEntry record = ALLOCATE(context, sizeof(struct _ProfFunctionEntry));
            record->name = stem;
            record->count = 1;
            record->metaCount = 0;
            record->metaMemuse = 0;
            profFunctions[profFunctionsCount ++] = record;
        }
    }
}

void pIncMetaCountFunction(Context context, char* name, long memuse)
{
    if (context->profiling)
    {
        // Search for existing entry
        // function name are of the form R4a-Form-let2$XX

    //    char* d = strchr(name, '$');
    //    char* stem = name;
    //    if (d)
    //    {
    //        int len = strlen(name) - strlen(d);
    //        stem = ALLOCATE(context, len + 1);
    //        memcpy(stem, name, len);
    //        stem[len] = '\0';
    //    }
        char * stem = name;

        int i = 0;
        while (i < profFunctionsCount)
        {

            if (strcmp(profFunctions[i]->name, stem) == 0)
            {
                break;
            }
            i ++;
        }

        if (i < profFunctionsCount)
        {
            profFunctions[i]->metaCount ++;
            profFunctions[i]->metaMemuse += memuse;
    //        if (d)
    //           FREE(context, stem);
        }
    }
}

static int profEntryCmp(const void* p1, const void* p2)
{

    ProfFunctionEntry e1 = *(ProfFunctionEntry*) p1;
    ProfFunctionEntry e2 = *(ProfFunctionEntry*) p2;

    if (e1->count == e2->count)
        return 0;
    if (e1->count > e2->count)
        return -1;
//        if (e1->metaCount == e2->metaCount)
//            return 0;
//        if (e1->metaCount > e2->metaCount)
//            return -1;
//        if (e1->metaMemuse == e2->metaMemuse)
//               return 0;
//           if (e1->metaMemuse > e2->metaMemuse)
//               return -1;

        return 1;
}

void printProfiling(Context context)
{
    if (context->profiling)
    {
        if (profLargeMetaSubstitutes || profLargeBufferCopy)
        {
            PRINTF(context, "Profiling information...");

            if (profLargeMetaSubstitutes)
            {
                PRINTF(context, "\n\nReport large meta substitutes...\n");

                ProfMetaSubstitute c = profLargeMetaSubstitutes;
                while (c)
                {
                    PRINTF(context, "\n===========================");
                    PRINTF(context, "\n  Large meta substitute");
                    printMetasubstituteRecord(context, c);
                    c = c->next;

                    PRINTF(context, "\n");
                }
            }

            if (profLargeBufferCopy)
            {
                PRINTF(context, "\n\nReport large environment copy...\n");

                ProfBufferCopy b = profLargeBufferCopy;
                while (b)
                {
                    PRINTF(context, "\n===========================");
                    PRINTF(context, "\n  Large environment copy");
                    PRINTF(context, "\n  size      : %u", b->size);
                    PRINTF(context, "\n  backtrace : ");

                    int i =  b->backtraceSize - 1;
                    while (i >= 0)
                    {
                        PRINTF(context, "\n    %s", b->backtrace[i]);
                        i --;
                    }

                    b = b->next;

                    PRINTF(context, "\n");
                }
            }
        }

        PRINTF(context, "\n\nReport function count  (call count) (meta count) (memuse, M)...\n");

        qsort (profFunctions, profFunctionsCount, sizeof(ProfFunctionEntry), profEntryCmp);

        int i = 0;
        while (i < 50 && i < profFunctionsCount)
        {
            PRINTF(context, "\n%-50s : %10d %10d %5ld", profFunctions[i]->name, profFunctions[i]->count, profFunctions[i]->metaCount, profFunctions[i]->metaMemuse / 1024);
            i++;
        }

        PRINTF(context, "\nTotal memory use used by meta substitution: %ldM", (profMemuseMetaSubstitutes / 1024));
        PRINTF(context, "\nPeak term size (sample)          : %ld nodes", pPeakTermSize);
        PRINTF(context, "\nPeak term memory use (sample)    : %ldM\n", (pPeakTermMemuse / 1024 / 1024));
        //PRINTF(context, "\nMemory use due to Duplicate      : %ldM\n", (pDuplicateMemuse / 1024));
    }
}

void printMetasubstituteRecord(Context context, ProfMetaSubstitute c)
{
    PRINTF(context, "\n  size               : %u", c->size);
    PRINTF(context, "\n  term size          : %u", c->termSize);
    PRINTF(context, "\n  environment size   : %u", c->envSize);
    PRINTF(context, "\n  normal form        : %s", (c->nf ? "no" : "yes"));
    PRINTF(context, "\n  memory use         : %ldM", (c->memuse / 1024));
    PRINTF(context, "\n  backtrace          : ");

    int i =  c->backtraceSize - 1;
    while (i >= 0)
    {
        PRINTF(context, "\n    %s", c->backtrace[i]);
        i --;
    }
}

static void termSize(Term term, long* size, long* memuse, int sharing);

void crsxpPrintStats(Context context, Term term)
{
    if (context->profiling)
    {
        // Computing term size is expensive: just get a sample.
        if ((pStepCount % pSampleRate) ==0)
        {
            pSampleRate = (rand() % 500) + 500;

            // Update peak sizes
            long size = 0;
            long memuse = 0;
            termSize(term, &size, &memuse, 0);

            int p = size > pPeakTermSize;

            pPeakTermSize = p ? size : pPeakTermSize;
            pPeakTermMemuse = (memuse > pPeakTermMemuse) ? memuse : pPeakTermMemuse;

            long nssize = 0;
            long nsmemuse = 0;
            termSize(term, &nssize, &nsmemuse, 1);
            pNSPeakTermSize = (nssize > pNSPeakTermSize) ? nssize : pNSPeakTermSize;
            pNSPeakTermMemuse =
                    (nsmemuse > pNSPeakTermMemuse) ? nsmemuse : pNSPeakTermMemuse;

            time_t now;
            time(&now);
            if (difftime(now, pTime) > 1)
            {
                pTime = now;

                // TODO: print total term size, from the root.
                //printf("(%ld) stats: term node count (w/o sharing): %ld (%ld), memory use: %ldM (%ldM), substitution count %ld", pStepCount, size, nssize, (memuse / 1024 / 1024), (nsmemuse / 1024 / 1024), pMetaSubstituteCount );
                printf("\n%-8ld# stats: peak term node count (w/o sharing) : %ld (%ld), substitution count: %ld", pStepCount, pPeakTermSize, pNSPeakTermSize,
                        pMetaSubstituteCount);

                fflush(stdout);
            }
        }

    }
}


/////////////////////////////////////////////////////////////////////////////////
// Compute term size without check overhead

static void termSize2(Term term, long* size, long* memuse, int sharing);


// size    : total number of nodes, excluding property links.
// memuse  : total memory use.
// sharing : when true, account for shared terms.
static void termSize(Term term, long* size, long* memuse, int sharing)
{
    pMarker ++;
    termSize2(term, size, memuse, sharing);
}

static void termSize2(Term term, long* size, long* memuse, int sharing)
{
    if (!sharing && term->marker == pMarker)
        return;

    term->marker = pMarker;

    (*size) ++;

    if (IS_VARIABLE_USE(term))
    {
        Variable v = VARIABLE(term);

        (*memuse) += sizeof(struct _Variable);
        (*memuse) += strlen(v->name) + 1;
    }
    else
    {
        (*memuse) += sizeof(struct _Construction);

        Construction construction = asConstruction(term);
        if (construction->fvs && construction->fvs != AllFreeVariables && (sharing || construction->fvs->marker != pMarker))
        {
            construction->fvs->marker = pMarker;

            (*memuse) += sizeof(struct _Hashset);
            (*memuse) += construction->fvs->capacity * sizeof(size_t);
        }
        if (construction->nfvs && construction->nfvs != AllFreeVariables && (sharing || construction->nfvs->marker != pMarker))
        {
            construction->nfvs->marker = pMarker;

            (*memuse) += sizeof(struct _Hashset);
            (*memuse) += construction->nfvs->capacity * sizeof(size_t);
        }
        if (construction->vfvs &&  construction->vfvs != AllFreeVariables && (sharing || construction->vfvs->marker != pMarker))
        {
            construction->vfvs->marker = pMarker;

            (*memuse) += sizeof(struct _Hashset);
            (*memuse) += construction->vfvs->capacity * sizeof(size_t);
        }

        if (construction->properties)
        {
            NamedPropertyLink link;

            for (link = construction->properties->namedProperties; link; link = link->link)
            {
                if (!sharing && link->marker == pMarker)
                    break;

                link->marker = pMarker;

                if (link->name)
                {
                    termSize2(link->u.term, size, memuse, sharing);
                }
                else
                {
                    (*memuse) += memoryUsedHS2(link->u.propset);
                }

                (*memuse) += sizeof(struct _NamedPropertyLink);
            }
        }

        if (construction->properties)
        {
            VariablePropertyLink link;
            for (link = construction->properties->variableProperties; link; link = link->link)
            {
                if (!sharing && link->marker == pMarker)
                    break;

                if (link->variable)
                {
                    termSize2(link->u.term, size, memuse, sharing);

                    (*memuse) += sizeof(struct _Variable);
                }
                else
                {
                    (*memuse) += memoryUsedHS2(link->u.propset);
                }

                (*memuse) += sizeof(struct _VariablePropertyLink);
            }
        }

        const int arity = ARITY(term);
        int i;
        for (i = 0; i < arity; ++i)
        {
            termSize2(SUB(term,i), size, memuse, sharing);
        }
    }
}


#else


void crsxpInit(Context context) {};
void crsxpDestroy(Context context) {};
void crsxpBeforeStep(Context context, Term term) {};
void crsxpAfterStep(Context context) {};
void crsxpBeforeSubstitution(Context context) {};
void crsxpAfterSubstitution(Context context) {};


void printProfiling(Context context) {};
void printMetasubstituteRecord(Context context, ProfMetaSubstitute c) {};
void profAddStepFunction(Context context, char* functionName) {};
void pIncMetaCountFunction(Context context, char* functionName, long memuse) {};



#endif
