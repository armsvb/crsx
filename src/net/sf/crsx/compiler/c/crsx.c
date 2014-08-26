// Copyright (c) 2010, 2014 IBM Corporation.
// $Id: crsx.c,v 3.123 2014/02/11 15:56:48 villardl Exp $

// Implementations of externals from the header file.
#include "crsx.h"
#include <stdint.h>
#include <ctype.h>
#include <alloca.h>
#include <stdlib.h>
#include <string.h>
#include "prof.h"
#include "invariant.h"

#include <unicode/umachine.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>

/////////////////////////////////////////////////////////////////////////////////
// Debugging && profiling


// Debug printfs
#if 0
#  define crsx_debug fprintf
#else
#  define crsx_debug if (0) fprintf
#endif

// Print debugging information while normalizing.
#if defined(DEBUG)
long stepCount = 0L;
int stepNesting = 0;
#endif


void enableProfiling(Context context)
{
#ifdef CRSX_ENABLE_PROFILING
    context->profiling = 1;
#endif
}

/////////////////////////////////////////////////////////////////////////////////
// Variable allocation.

Variable makeVariable(Context context, char *name, unsigned int bound, unsigned int linear, unsigned int block, unsigned int shallow)
{
    ASSERT(context, context && name);
    Variable v = ALLOCATE(context, sizeof(struct _Variable));
    v->nr = 1;
    v->uses = 0;

    // TODO: too expensive.
    int len = strlen(name);
    char *nameu = name;
    while (nameu < name+len && (*nameu != '_' || *(nameu+1) < '0' ||  *(nameu+1) > '9')) ++nameu;
    if (name[0] == 'v' && name[1] == '"' && name[len-1] == '"')
    {
        if (nameu < name+len)
            v->name = ALLOCATENF(context, 100, "%.*s_%u%s", (int)(nameu-name), name, ++context->stamp, "\"");
        else
            v->name = ALLOCATENF(context, 100, "%.*s_%u%s", (int)(strlen(name)-1), name, ++context->stamp, "\"");
    }
    else
    {
        v->name = ALLOCATENF(context, 100, "%.*s_%u", (int)(nameu < name+len ? nameu-name : len), name, ++context->stamp);
    }

    v->bound = bound;
    v->linear = linear;
    v->block = block;
    v->shallow = shallow;
    v->track = 1; // Only relevant if fv_enabled is true

    DEBUGENV("crsx-debug-variables", DEBUGF(context, "//%*sMAKE_VARIABLE: %s\n", (int)stepNesting, "", v->name));
    return v;
}

void freeVariable(Context context, Variable variable)
{
    //ASSERT(context, variable->uses == 0);

    FREE(context, variable->name);
    FREE(context, variable);
}

void setVariableBaseName(Context context, Variable variable, char *newbase)
{
    if (variable)
    {
        char *oldname = variable->name;
        char *oldext = strchr(oldname, '_');
        if (!oldext)
            variable->name = newbase;
        else
        {
            size_t basez = strlen(newbase);
            size_t extz = strlen(oldext);
            variable->name = ALLOCATE(context, basez + extz + 1);
            memcpy(variable->name, newbase, basez);
            memcpy(variable->name+basez, oldext, extz);
            variable->name[basez + extz] = '\0';
        }
    }
}

VariableUse makeVariableUse(Context context, Variable variable)
{
    ASSERT(context, variable->nr > 0);

    VariableUse use = ALLOCATE(context, sizeof(struct _VariableUse));
    use->term.nr = 1;
#ifdef CRSX_ENABLE_PROFILING
    use->term.marker = 0;
#endif
    use->variable = variable; // No need to link.
    use->term.descriptor = NULL; // this ensures the term is understood as a VariableUse

    variable->uses++;
    return use;
}

/////////////////////////////////////////////////////////////////////////////////
// Construction allocation.

static
Construction makeConstruction(Context context, ConstructionDescriptor descriptor)
{
    Construction construction;
    int poolIndex = (descriptor->size - sizeof(struct _Construction)) / sizeof(Construction);
    if (poolIndex < CONS_POOL_MAX_SIZE_SIZE && context->consPoolSize[poolIndex] > 0)
        construction = context->consPool[poolIndex][--context->consPoolSize[poolIndex]];
    else
        construction = ALLOCATE(context, descriptor->size);

    construction->term.descriptor = descriptor;
    construction->term.nr = 1;

#ifdef CRSX_ENABLE_PROFILING
    construction->term.marker = 0;
#endif

    return construction;
}

static
void freeConstruction(Context context, Construction construction)
{
    unlinkProperties(context, construction->properties);
    construction->properties = NULL;

    UNLINK_VARIABLESET(context, construction->fvs);
    construction->fvs = NULL;
    UNLINK_VARIABLESET(context, construction->nfvs);
    construction->nfvs = NULL;
    UNLINK_VARIABLESET(context, construction->vfvs);
    construction->nfvs = NULL;

    // Subterms and binders.
    Term term = &construction->term;

    const int arity = ARITY(term);
    int i;
    for (i = 0; i < arity; ++i)
    {
        UNLINK(context, SUB(term, i));
        const int rank = RANK(term,i);
        int j;
        for (j = 0; j < rank; ++j)
        {
            Variable b = BINDER(term,i,j);
            UNBIND(b);
            unlinkVariable(context, b);
        }
    }

    int poolIndex = (term->descriptor->size - sizeof(struct _Construction)) / sizeof(Construction);

    if (poolIndex < CONS_POOL_MAX_SIZE_SIZE && context->consPoolSize[poolIndex] < CONS_POOL_MAX_SIZE)
        context->consPool[poolIndex][context->consPoolSize[poolIndex]++] = construction;
    else
        FREE(context, term);
}


/////////////////////////////////////////////////////////////////////////////////
// Predefined components for ConstructorDescriptors.

char *dataName(Term term)
{
    //ASSERT((Context)0, term);
    ConstructionDescriptor descriptor = term->descriptor;
    return descriptor->sort->constructorNames[descriptor->sortoffset];
}

int dataStep(Sink sink, Term term)
{
    return 0; // no computation possible
}

const char *literalName(Term term)
{
    return asLiteral(term)->text;
}

int noBinderOffsets[1] = {0};

struct _ConstructionDescriptor literalConstructionDescriptor =
{
    NULL,                                       //.sort
    1,                                          //.sortoffset
    0,                                          //.arity
    sizeof(struct _Literal),                    //.size
    noBinderOffsets,                            //.binderoffset
    (char *(*)(Term term))&literalName,         //.name  (ugly cast added by sboag... Kris will have to evaluate doing the right fix.)
    &dataStep                                   //.step
};

/////////////////////////////////////////////////////////////////////////////////
// Literal allocation.

Term makeStringLiteral(Context context, const char *text)
{
    Literal literal = ALLOCATE(context, sizeof(struct _Literal));
    literal->construction.term.descriptor = &literalConstructionDescriptor;
    literal->construction.term.nr = 1;
#ifdef CRSX_ENABLE_PROFILING
    literal->construction.term.marker = 0;
#endif
    literal->construction.nf = 1;
    literal->construction.nostep = 1;
    literal->construction.properties = context->noProperties;

    literal->construction.fvs = NULL;
    literal->construction.nfvs = NULL;
    literal->construction.vfvs = NULL;

    literal->text = text; // Note: NOT allocated...
    DEBUGENV("crsx-debug-literals", DEBUGF(context, "//%*sMAKE_LITERAL: %s\n", stepNesting, "", literal->text));
    ///PRINTF(context, "Literal<%s>\n", text);
    return (Term) literal;
}


/////////////////////////////////////////////////////////////////////////////////
// Buffer manipulation.


/**
 * @Brief Returns the set of free variables of the given term.
 */
static
VARIABLESET freeVars(Context context, Term term)
{
    if (IS_VARIABLE_USE(term))
    {
        const Variable v = VARIABLE(term);
        if (v->track)
            return VARIABLESET_ADDVARIABLE(context, NULL, linkVariable(context, VARIABLE(term)));
        return NULL;
    }

    return LINK_VARIABLESET(context, asConstruction(term)->fvs);
}

/**
 * @Brief Push term on the stack
 */
static void bufferPush(Buffer buffer, Term term)
{
    ASSERT(buffer->sink.context, term);
    ++buffer->lastTop;
    if (!buffer->last || buffer->lastTop >= BUFFER_SEGMENT_SIZE)
    {
        if (buffer->last && buffer->last->next)
        {
            // Already got a segment ready
            ASSERT(buffer->sink.context, buffer->last->next->previous == buffer->last);
            buffer->last = buffer->last->next;
        }
        else if (!buffer->last && buffer->first)
        {
            // Reuse previous first segment.
            buffer->last = buffer->first;
        }
        else
        {
            // Insert an additional segment.
            BufferSegment segment = ALLOCATE(buffer->sink.context, sizeof(struct _BufferSegment));
            if (!buffer->first)
            {
                ASSERT(buffer->sink.context, !buffer->last);
                buffer->first = segment;
            }
            else if (buffer->last)
            {
                ASSERT(buffer->sink.context, buffer->first);
                buffer->last->next = segment;
            }
            segment->previous = buffer->last;
            segment->next = NULL;
            buffer->last = segment;
        }
        buffer->lastTop = 0;
    }
    BufferEntry entry = &buffer->last->entry[buffer->lastTop];
    entry->term = term;
    entry->index = 0;
}

/**
 * @Brief Pop term stack
 */
static
void bufferPop(Buffer buffer)
{
    --buffer->lastTop;
    if (buffer->lastTop < 0 && buffer->last)
    {
        // Pop last segment.
        buffer->last = buffer->last->previous;

        // Reset top.
        if (buffer->last)
            buffer->lastTop = BUFFER_SEGMENT_SIZE-1;
    }
}

static
void bufferInsert(Buffer buffer, Term childTerm)
{
    ASSERT(buffer->sink.context, childTerm);
    if (buffer->lastTop < 0)
    {
        buffer->term = childTerm; // global result term
    }
    else //(buffer->lastTop >= 0)
    {
        BufferEntry entry = &buffer->last->entry[buffer->lastTop];
        SUB(entry->term, (entry->index)++) = childTerm;
    }
}

static inline BufferEntry bufferTop(Buffer buffer)
{
    ASSERT(buffer->sink.context, buffer->lastTop >= 0 && buffer->last);
    return &buffer->last->entry[buffer->lastTop];
}

// Buffer management.

#ifdef DEBUG
long eventCount = 0l;
long bufferCount = 0l;
#endif

Term bufferTerm(Sink sink)
{
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);

    Buffer buffer = (Buffer) sink;
    ASSERT(sink->context, buffer->lastTop < 0);

    Term term = buffer->term;
    CRSX_CHECK(sink->context, term);
    return term;
}

Sink bufferStart(Sink sink, ConstructionDescriptor descriptor)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;

    // Start construction
    Construction construction = makeConstruction(sink->context, descriptor);

    construction->properties =
      allocateProperties(sink->context,
                          buffer->pendingNamedPropertiesFreeVars,
                          buffer->pendingVariablePropertiesFreeVars,
                          buffer->pendingNamedProperties,
                          buffer->pendingVariableProperties);

    construction->fvs = NULL;
    construction->nfvs = LINK_VARIABLESET(sink->context, construction->properties->namedFreeVars);
    construction->vfvs = LINK_VARIABLESET(sink->context, construction->properties->variableFreeVars);

    construction->nf = 0;
    construction->nostep = buffer->blocking; // no normalization if at least one blocking binder
    //construction->nostep = 0; // no normalization if at least one blocking binder

    // term->sub and term->binders will be populated incrementally.
    bufferPush(buffer, (Term) construction); // suspend current construction in favor of children
    // Setup fresh context for first child.

    buffer->blocking = 0;
    buffer->pendingNamedProperties = NULL;
    buffer->pendingVariableProperties = NULL;
    buffer->pendingVariablePropertiesFreeVars = NULL;
    buffer->pendingNamedPropertiesFreeVars = NULL;

    return sink;
}

Sink bufferEnd(Sink sink, ConstructionDescriptor descriptor)
{
#   ifdef DEBUG
    ++eventCount;
#   endif

    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;

    ASSERT(sink->context, buffer->lastTop >= 0); // can't end non-started structure.
    Term childTerm = bufferTop(buffer)->term;

    if (ARITY(childTerm) != bufferTop(buffer)->index)
    {
        ERRORF(sink->context, Crsx, "Invalid number of sub terms for %s\n", descriptor->name(NULL));
        ASSERT(sink->context, ARITY(childTerm) == bufferTop(buffer)->index);
    }

    // Update nf and nostep fields of construction.
    if (IS_DATA(childTerm))
    {
        int nf = 1; // optimistic
        int i = ARITY(childTerm);
        while (nf && (--i) >= 0)
            if (! IS_NF(SUB(childTerm, i)))
            {
                nf = 0;
                break;
            }
        asConstruction(childTerm)->nf = nf;
    }
    else
    {
        asConstruction(childTerm)->nostep = 0;
    }

    // Propagate free variable onto childTerm.
    propagateFreeVariables(sink->context, childTerm);

    bufferPop(buffer);
    bufferInsert(buffer, childTerm);

    // Fresh context for next child.
    buffer->pendingNamedProperties = NULL;
    buffer->pendingVariableProperties = NULL;

    buffer->pendingVariablePropertiesFreeVars = NULL;
    buffer->pendingNamedPropertiesFreeVars = NULL;

    return sink;
}

Sink bufferLiteral(Sink sink, const char *text)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;

    bufferStart(sink, &literalConstructionDescriptor);
    Term literal =  bufferTop(buffer)->term;
    asLiteral(literal)->text = text; // descriptor set by start
    bufferEnd(sink, &literalConstructionDescriptor);

    return sink;
}

Sink bufferUse(Sink sink, Variable variable)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;
    //DEBUGF(sink->context, "//USE(%d)\n", buffer->lastTop);
    VariableUse use = makeVariableUse(sink->context, variable); // No need to link variable

    bufferInsert(buffer, (Term) use);

    // Fresh context for next child.
    buffer->pendingNamedProperties = NULL;
    buffer->pendingVariableProperties = NULL;
    return sink;
}

Sink bufferBinds(Sink sink, int size, Variable binds[])
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    const Buffer buffer = (Buffer) sink;
    const Context context = sink->context;

    ASSERT(context, buffer->lastTop >= 0); // can only have binders on proper construction subterms

    BufferEntry entry = bufferTop(buffer);
    int index = entry->index;
    Term term = entry->term;
    ASSERT(context, term->descriptor);
    ASSERT(context, 0 <= index && index < ARITY(term));
    ASSERT(context, size == RANK(term,index));

    unsigned int notrack = 1;
    buffer->blocking = 0;
    int i;
    for (i = 0; i < size; ++i)
    {
        Variable b = binds[i];
        BINDER(term,index,i) = b; // No need to link variables. Also binders are not considered used.
        buffer->blocking |= b->block;

        notrack &= b->block & b->shallow;
    }

    if (context->fv_enabled && notrack) // full meta-substitute won't be called, so don't track these binders
        for (i = 0; i < size; ++i)
            binds[i]->track = 0;

    return sink;
}

static
void bufferMergeProperties(Context context, Buffer buffer, Construction c);

Sink bufferCopy(Sink sink, Term term) // Transfer ref
{
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    ASSERT(sink->context, term->nr > 0);

    Buffer buffer = (Buffer) sink;
    const Context context = sink->context;

    // If no properties just share!
    if (IS_VARIABLE_USE(term)
            || (buffer->pendingNamedProperties == asConstruction(term)->properties->namedProperties
                    && buffer->pendingVariableProperties == asConstruction(term)->properties->variableProperties
               ))
    {
        UNLINK_NamedPropertyLink(context, buffer->pendingNamedProperties);
        buffer->pendingNamedProperties = NULL;
        UNLINK_VariablePropertyLink(context, buffer->pendingVariableProperties);
        buffer->pendingVariableProperties = NULL;

        UNLINK_VARIABLESET(context, buffer->pendingNamedPropertiesFreeVars);
        buffer->pendingNamedPropertiesFreeVars = NULL;
        UNLINK_VARIABLESET(context, buffer->pendingVariablePropertiesFreeVars);
        buffer->pendingVariablePropertiesFreeVars = NULL;

        bufferInsert(buffer, term);
        return sink;
    }

    // We have a construction with added properties so must update the root term.
#   ifdef DEBUG
    ++eventCount;
#   endif

    Construction c = asConstruction(term);

    if (LINK_COUNT(term) == 1)
    {
        // Reuse original term (with updated properties).
        bufferMergeProperties(context, buffer, c);
        bufferInsert(buffer, term);
    }
    else
    {
        if (IS_LITERAL(term))
        {
            LITERALU(sink, TEXT(term));
        }
        else
        {
            // All these pointer manipulation in order to reuse bufferMergeProperties
            NamedPropertyLink namedLink = buffer->pendingNamedProperties;
            buffer->pendingNamedProperties = LINK_NamedPropertyLink(context, c->properties->namedProperties);
            VariablePropertyLink variableLink = buffer->pendingVariableProperties;
            buffer->pendingVariableProperties = LINK_VariablePropertyLink(context, c->properties->variableProperties);

            VARIABLESET fvNamedLink = buffer->pendingNamedPropertiesFreeVars;
            buffer->pendingNamedPropertiesFreeVars = LINK_VARIABLESET(context, c->properties->namedFreeVars);
            VARIABLESET fvVariableLink = buffer->pendingVariablePropertiesFreeVars;
            buffer->pendingVariablePropertiesFreeVars = LINK_VARIABLESET(context, c->properties->variableFreeVars);

            sink->start(sink, c->term.descriptor);

            buffer->pendingNamedProperties = namedLink;
            buffer->pendingVariableProperties = variableLink;

            buffer->pendingNamedPropertiesFreeVars = fvNamedLink;
            buffer->pendingVariablePropertiesFreeVars = fvVariableLink;

            bufferMergeProperties(context, buffer, asConstruction(bufferTop(buffer)->term));

            int a = ARITY(term);
            int i;
            for (i = 0; i < a; i ++)
            {
                const int rank = RANK(term, i);
                if (rank == 0)
                {
                    // --  i'th subterm with no binders: just continue copying.
                    COPY(sink, LINK(context, SUB(term, i)));
                }
                else
                {
                    // Rename binders and substitute..

                    Variable *oldBinders = BINDERS(term, i);
                    Variable *subBinders = ALLOCA(context, rank*sizeof(Variable)); // does not escapes
                    VariableUse subUses[rank]; // does not escape
                    struct _SubstitutionFrame _subSubstitution = {NULL, 0, rank, oldBinders, (Term *) subUses, RENAME_ALL}; // does not escape
                    SubstitutionFrame subSubstitution = &_subSubstitution;

                    // --- populate per binder
                    int j;
                    for (j = 0; j < rank; ++j)
                    {
                        char *oldname = oldBinders[j]->name;
                        char *baseendp = strrchr(oldname, '_');
                        char *basename = oldname;
                        if (baseendp)
                        {
                            const int z = baseendp - oldname;
                            basename = ALLOCA(context, z+1); // does not escape
                            memcpy(basename, oldname, z);
                            basename[z] = '\0';
                        }
                        int isLinear = IS_LINEAR(oldBinders[j]);
                        subBinders[j] = makeVariable(context, oldBinders[j]->name, 1, isLinear, oldBinders[j]->block, oldBinders[j]->shallow); // escapes
                        subUses[j] = makeVariableUse(context, linkVariable(context, subBinders[j])); // escapes
                    }

                    // --- send new binders
                    BINDS(sink, rank, subBinders); // escape of subBinders. Variable references are transfered.
                    //FREE(context, subBinders) (no need: allocated on the stack). Variable references have been transferred.
                    // --- now process subterm!
                    metaSubstitute(sink, LINK(context, SUB(term, i)), subSubstitution);
                }
            }
            sink->end(sink, c->term.descriptor);
        }
        UNLINK(context, term);
    }

    ASSERT(context, !buffer->pendingNamedProperties);
    ASSERT(context, !buffer->pendingVariableProperties);

    return sink;
}

Sink bufferPropertyRef(Sink sink, Construction construction)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;
    //DEBUGF(sink->context, "//ADD_PROPERTY_REF(%d)\n", buffer->lastTop);

    // Must be first property event.
    ASSERT(sink->context, !buffer->pendingNamedProperties && !buffer->pendingVariableProperties);
    buffer->pendingNamedProperties = LINK_NamedPropertyLink(sink->context, construction->properties->namedProperties);
    buffer->pendingVariableProperties = LINK_VariablePropertyLink(sink->context, construction->properties->variableProperties);
    return sink;
}

Sink bufferProperties(Sink sink, VARIABLESET namedFreeVars, VARIABLESET variableFreeVars, NamedPropertyLink namedProperties, VariablePropertyLink variableProperties)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    const Context context = sink->context;

    ASSERT(context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;

    // Must be first property event.
    ASSERT(context, !buffer->pendingNamedProperties && !buffer->pendingVariableProperties);

    buffer->pendingNamedProperties = namedProperties;
    buffer->pendingVariableProperties = variableProperties;

    buffer->pendingNamedPropertiesFreeVars = namedFreeVars;
    buffer->pendingVariablePropertiesFreeVars = variableFreeVars;

    ASSERT(context, (!context->fv_enabled || (variableProperties && buffer->pendingVariablePropertiesFreeVars) || (!variableProperties && !buffer->pendingVariablePropertiesFreeVars)));

    return sink;
}

Sink bufferPropertyNamed(Sink sink, const char *name, Term term)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    ASSERT(sink->context, name);
    ASSERT(sink->context, term == NULL || term->nr > 0);

    Buffer buffer = (Buffer) sink;
    //DEBUGF(sink->context, "//ADD_PROPERTY_NAMED(%d,%s)\n", buffer->lastTop, name);

    NamedPropertyLink link = ALLOCATE_NamedPropertyLink(sink->context, GLOBAL(sink->context, name), term, buffer->pendingNamedProperties); // transfer refs for term and old link
    buffer->pendingNamedProperties = link;

    if (sink->context->fv_enabled)
        buffer->pendingNamedPropertiesFreeVars = VARIABLESET_MERGEALL(sink->context, buffer->pendingNamedPropertiesFreeVars, freeVars(sink->context, term));

    return sink;
}

Sink bufferPropertyVariable(Sink sink, Variable variable, Term term)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    ASSERT(sink->context, term == NULL || term->nr > 0);

    Buffer buffer = (Buffer) sink;
    //DEBUGF(sink->context, "//ADD_PROPERTY_VARIABLE(%d,%s)\n", buffer->lastTop, variable->name);

    VariablePropertyLink link = ALLOCATE(sink->context, sizeof(struct _VariablePropertyLink));
    link->link = buffer->pendingVariableProperties; // transfer ref
    link->nr = 1;
#ifdef CRSX_ENABLE_PROFILING
    link->marker = 0;
#endif
    link->variable = useVariable(sink->context, variable); // Transfer ref. Account for use
    link->u.term = term; // Transfer ref
    buffer->pendingVariableProperties = link;

    if (sink->context->fv_enabled)
    {
       buffer->pendingVariablePropertiesFreeVars = VARIABLESET_MERGEALL(sink->context, buffer->pendingVariablePropertiesFreeVars, freeVars(sink->context, term));
       buffer->pendingVariablePropertiesFreeVars = VARIABLESET_ADDVARIABLE(sink->context, buffer->pendingVariablePropertiesFreeVars, linkVariable(sink->context, variable));
    }
    return sink;
}

Sink bufferPropertiesReset(Sink sink)
{
#   ifdef DEBUG
    ++eventCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;
    //DEBUGF(sink->context, "//RESET_PROPERTIES(%d)\n", buffer->lastTop);

    UNLINK_NamedPropertyLink(sink->context, buffer->pendingNamedProperties);
    buffer->pendingNamedProperties = NULL;
    UNLINK_VariablePropertyLink(sink->context, buffer->pendingVariableProperties);
    buffer->pendingVariableProperties = NULL;

    UNLINK_VARIABLESET(sink->context, buffer->pendingNamedPropertiesFreeVars);
    buffer->pendingNamedPropertiesFreeVars = NULL;

    UNLINK_VARIABLESET(sink->context, buffer->pendingVariablePropertiesFreeVars);
    buffer->pendingVariablePropertiesFreeVars = NULL;

    return sink;
}

Sink initBuffer(Context context, Buffer buffer, int free)
{
#   ifdef DEBUG
    ++bufferCount;
#   endif
    // Insert default 
    // Allocate.
    ASSERT(context, buffer);
    // Initialize sink to record all events in buffer.
    buffer->sink.kind = SINK_IS_BUFFER;
    buffer->sink.context = context;
    buffer->sink.term = &bufferTerm;
    buffer->sink.start = &bufferStart;
    buffer->sink.end = &bufferEnd;
    buffer->sink.literal = &bufferLiteral;
    buffer->sink.use = &bufferUse;
    buffer->sink.binds = &bufferBinds;
    buffer->sink.copy = &bufferCopy;
    buffer->sink.propertyRef = &bufferPropertyRef;
    buffer->sink.properties = &bufferProperties;
    buffer->sink.propertyNamed = &bufferPropertyNamed;
    buffer->sink.propertyVariable = &bufferPropertyVariable;
    buffer->sink.propertiesReset = &bufferPropertiesReset;
    // Initialize buffer to be empty.
    buffer->first = NULL;
    buffer->lastTop = -1;
    buffer->last = NULL;;
    buffer->term = NULL;

    buffer->pendingNamedProperties = NULL;
    buffer->pendingVariableProperties = NULL;

    buffer->pendingNamedPropertiesFreeVars = NULL;
    buffer->pendingVariablePropertiesFreeVars = NULL;

    buffer->blocking = 0;
    buffer->free = free;
    // Return as sink for reception...
    return (Sink) buffer;
}

void freeBuffer(Sink sink)
{
#   ifdef DEBUG
    --bufferCount;
#   endif
    ASSERT(sink->context, sink->kind == SINK_IS_BUFFER);
    Buffer buffer = (Buffer) sink;
    BufferSegment first = buffer->first;
    while (first)
    {
        BufferSegment next = first->next;
        FREE(sink->context, first);
        first = next;
    }
    if (buffer->free)
        FREE(sink->context, buffer);
}

/**
 * @Brief Merge properties on construction with pending properties.
 */
static
void bufferMergeProperties(Context context, Buffer buffer, Construction construction)
{
    crsxpBeforeMergeProperties(context);

    if (buffer->pendingNamedProperties && construction->properties->namedProperties != buffer->pendingNamedProperties)
    {
        // There are new properties.

        if (!construction->properties->namedProperties) // no existing properties. Good.
            construction->properties = setNamedProperties(context, construction->properties, buffer->pendingNamedProperties); // transfer ref
        else
        {
            // New properties and existing properties... merge.
            // Merge property lists. Unlink the single-linked prefix with the event-generated properties.

            // TODO: avoid copying when buffer->pendingNamedProperties->nr == 1
            int count = 0;
            NamedPropertyLink link = buffer->pendingNamedProperties, newTop = NULL, newLast = NULL;
            for (; link; link = link->link)
            {
                if (link == construction->properties->namedProperties) // guard above ensures false on first iteration
                    break; // avoid deep duplication of lists

                NamedPropertyLink newLink = memcpy(ALLOCATE_NamedPropertyLink(context, NULL, NULL, NULL), link, sizeof(struct _NamedPropertyLink));

                newLink->link = NULL;
                newLink->nr = 0;
                if (newLink->name) // Always the case as pending free variables are not yet been linked.
                    (void) LINK(context, newLink->u.term);
                else
                    linkHS2(newLink->u.propset);

                if (newTop)
                    newLast->link = LINK_NamedPropertyLink(context, newLink);
                else
                    newTop = newLink;
                newLast = newLink;
                count++;
            }
            newLast->link = construction->properties->namedProperties; // transfer ref to old properties to tail of new.

            // Set final links
            construction->properties = setNamedProperties(context, construction->properties, LINK_NamedPropertyLink(context, newTop));
            UNLINK_NamedPropertyLink(context, buffer->pendingNamedProperties);

            crsxpNamedPropertiesMerged(context, count);
        }
        buffer->pendingNamedProperties = NULL;

        if (context->fv_enabled)
        {
            VARIABLESET freeVars = construction->properties->namedFreeVars;

            // Merge free variables and put them in front of property list
            freeVars = VARIABLESET_MERGEALL(context, freeVars, LINK_VARIABLESET(context, buffer->pendingNamedPropertiesFreeVars));

            if (freeVars)
            {
                construction->properties = setNamedFreeVars(context, construction->properties, freeVars);

                // And add the new pending free vars also to the construction itself
                construction->nfvs = VARIABLESET_MERGEALL(context, construction->nfvs, buffer->pendingNamedPropertiesFreeVars);
                buffer->pendingNamedPropertiesFreeVars = NULL; // reference has just transferred
            }
        }
    }
    else
    {
        UNLINK_NamedPropertyLink(context, buffer->pendingNamedProperties);
        buffer->pendingNamedProperties = NULL;

        UNLINK_VARIABLESET(context, buffer->pendingNamedPropertiesFreeVars);
        buffer->pendingNamedPropertiesFreeVars = NULL;
    }

    // Do the same for variable properties

    if (buffer->pendingVariableProperties && construction->properties->variableProperties != buffer->pendingVariableProperties)
    {
        if (!construction->properties->variableProperties)
            construction->properties = setVariableProperties(context, construction->properties, buffer->pendingVariableProperties);
        else
        {
            // Merge property lists.
            VariablePropertyLink link = buffer->pendingVariableProperties, newTop = NULL, newLast = NULL;
            for (; link; link = link->link)
            {
                if (link == construction->properties->variableProperties)
                    break;

                VariablePropertyLink newLink = memcpy(ALLOCATE(context, sizeof(struct _VariablePropertyLink)), link, sizeof(struct _VariablePropertyLink));
                newLink->variable = useVariable(context, linkVariable(context, link->variable));
#ifdef CRSX_ENABLE_PROFILING
                newLink->marker = 0;
#endif
                newLink->link = NULL;
                newLink->nr = 0;
                if (newLink->variable)
                {
                    (void) LINK(context, newLink->u.term);
                }
                else
                    linkHS2(newLink->u.propset);

                if (newTop)
                    newLast->link = LINK_VariablePropertyLink(context, newLink);
                else
                    newTop = newLink;
                newLast = newLink;

            }

            newLast->link = construction->properties->variableProperties;
            construction->properties = setVariableProperties(context, construction->properties, LINK_VariablePropertyLink(context, newTop));

            UNLINK_VariablePropertyLink(context, buffer->pendingVariableProperties);
        }
        buffer->pendingVariableProperties = NULL;

        if (context->fv_enabled)
        {
            VARIABLESET freeVars = construction->properties->variableFreeVars;

            // Merge free variables and put them in front of property list
            freeVars = VARIABLESET_MERGEALL(context, freeVars, LINK_VARIABLESET(context, buffer->pendingVariablePropertiesFreeVars));
            ASSERT(context, freeVars);

            construction->properties = setVariableFreeVars(context, construction->properties, freeVars);
            ASSERT_VARIABLE_PROPERTIES(context, construction->properties);

            // And add to the construction..

            construction->vfvs =  VARIABLESET_MERGEALL(context, construction->vfvs, buffer->pendingVariablePropertiesFreeVars);
            buffer->pendingVariablePropertiesFreeVars = NULL; // Ref has been transferred.
        }
    }
    else
    {
        UNLINK_VariablePropertyLink(context, buffer->pendingVariableProperties);
        buffer->pendingVariableProperties = NULL;
        UNLINK_VARIABLESET(context, buffer->pendingVariablePropertiesFreeVars);
        buffer->pendingVariablePropertiesFreeVars = NULL;
    }

    crsxpAfterMergeProperties(context);

}

/////////////////////////////////////////////////////////////////////////////////
// Variable sets.

// Some forward declarations
static void freeLL(VariableSet set);
static int addVariableLL(VariableSet set, Variable variable);
static int containsVariableLL(VariableSet set, Variable variable);

VariableSet makeVariableSet(Context context)
{
    VariableSet set = ALLOCATE(context, sizeof(struct _VariableSet));
    set->context = context;
    set->u.link = NULL;

    set->addVariable = addVariableLL;
    set->free = freeLL;
    set->containsVariable = containsVariableLL;

    return set;
}

int addVariable(VariableSet set, Variable variable)
{
    ASSERT(set->context, variable->name);
    return set->addVariable(set, variable);
}

int containsVariable(VariableSet set, Variable variable)
{
    return set != NULL && set->containsVariable(set, variable);
}

void freeVariableSet(VariableSet set)
{
    set->free(set);

    FREE(set->context, set);
}

/////////////////////////////////////////////////////////////////////////////////
// Linked list based variable sets.
//

static
VariableSetLink unlinkVariableSetLink(Context context, VariableSetLink link)
{
    while (link)
    {
        ASSERT(context, link->nr > 0);
        if (--link->nr > 0)
            break;

        VariableSetLink next = link->link;
        link->link = NULL;
        unlinkVariable(context, link->variable);
        link->variable = NULL;
        FREE(context, link);
        link = next;
    }
    return link;
}

static
int addVariableLL(VariableSet set, Variable variable) // Variable ref is transferred.
{
    ASSERT(set->context, variable->name);

    VariableSetLink link;
    for (link = set->u.link; link; link = link->link)
    {
        if (link->variable == variable)
        {
            unlinkVariable(set->context, variable);
            return 0;
        }
    }

    link = ALLOCATE(set->context, sizeof(struct _VariableSetLink));
    link->nr = 1;
    link->variable = variable; // Transfer reference.
    link->link = set->u.link;
    set->u.link = link;
    return 1;
}

static
void freeLL(VariableSet set)
{
    unlinkVariableSetLink(set->context, set->u.link);
}

static
VariableSetLink variableSetLinkFor(VariableSetLink link, Variable variable)
{
    for (; link; link = link->link)
    {
        if (link->nr <= 0)
            assert(link->nr > 0);

        if (link->variable == variable)
            return link;
    }
    return NULL;
}

static
int containsVariableLL(VariableSet set, Variable variable)
{
    return variableSetLinkFor(set->u.link, variable) == NULL ? 0 : 1 ;
}

/////////////////////////////////////////////////////////////////////////////////
// Linked list based copy-on-write variable sets.

VariableSetLink copyL(Context context, VariableSetLink set)
{
    ASSERT(context, set->nr > 0);

    VariableSetLink top = NULL;
    VariableSetLink last = NULL;
    VariableSetLink link = set;

    while (link)
    {
        VariableSetLink copy = ALLOCATE(context, sizeof(struct _VariableSetLink));
        copy->link = NULL;
        copy->nr = 1;
        copy->variable = linkVariable(context, link->variable);

        if (!top)
            top = copy;
        else
            last->link = copy;

        last = copy;
        link = link->link;
    }

    return top;
}

size_t countL(VariableSetLink set)
{
    return set ? 1 + countL(set->link) : 0;
}

VariableSetLink addVariableL(Context context, VariableSetLink set, Variable variable)
{
    if (variable == NULL)
        return set;

    if (set == NULL)
    {
        set = ALLOCATE(context, sizeof(struct _VariableSetLink));
        set->nr = 1;
        set->variable = variable; // Transfer ref
        set->link = NULL;
        return set;
    }

    if (!containsL(set, variable))
    {
        // Insert.
        if (set->nr > 1)
        {
            set->nr --;
            set = copyL(context, set);
        }

        VariableSetLink link = ALLOCATE(context, sizeof(struct _VariableSetLink));
        link->nr = 1;
        link->variable = variable; // Transfer Ref.
        link->link = set; // Transfer ref
        set = link;
    }
    else
    {
        unlinkVariable(context, variable);
    }

    return set;
}

int containsL(VariableSetLink set, Variable variable)
{
    return variableSetLinkFor(set, variable) != NULL;
}

VariableSetLink clearL(Context context, VariableSetLink link)
{
    unlinkVariableSetLink(context, link);
    return NULL;
}

int fprintVariable(Context context, FILE* out, Variable x);

void printfL(Context context, FILE* out, VariableSetLink set)
{
    if (!set)
        return;

    VariableSetLink link = set;
    char* sep = "";
    while (link)
    {
        FPRINTF(context, out, "%s", sep);
        fprintVariable(context, out, link->variable);
        sep = ",";
        link = link->link;
    }
}

// For debugging only use
int containsNameL(VariableSetLink link, char* name)
{
    for (; link; link = link->link)
        {
            if (link->nr <= 0)
                assert(link->nr > 0);

            if (!strcmp(link->variable->name, name))
                return 1;
        }
        return 0;
}

int containsAL(Variable* vars, int len, Variable variable)
{
    int i = 0;
    for (; i < len; i ++)
        if (vars[i] == variable)
            return 1;
    return 0;
}


VariableSetLink mergeAllL(Context context, VariableSetLink first, VariableSetLink second)
{
    if (first == NULL)
        return second;

    if (second == NULL)
        return first;

    first = minusL(context, first, second); // Remove duplicates.
    if (first == NULL)
        return second;

    if (first->nr > 1)
    {
        first->nr --;
        first = copyL(context, first);
    }

    VariableSetLink link = first;
    while (link->link)
    {
        ASSERT(context, link->nr == 1);
        link = link->link;
    }

    if (second->nr > 1)
    {
        second->nr --;
        second = copyL(context, second);
    }

    link->link = second; // transfer ref
    return first;
}

static VariableSetLink removeVariableL(Context context, VariableSetLink set, Variable variable)
{
    if (!set)
        return set;

    if (set->nr > 1)
    {
        set->nr --;
        set = copyL(context, set);
    }

    VariableSetLink link = set;
    VariableSetLink last = NULL;
    while (link)
    {
        ASSERT(context, link->nr == 1);

        if (variable == link->variable)
        {
            // Remove link
            VariableSetLink top;
            if (last)
            {
                last->link = link->link;
                top = set;
            }
            else
            {
                top = link->link;
            }

            link->link = NULL;
            unlinkVariableSetLink(context, link);

            return top;

        }

        last = link;
        link = link->link;
    }

    return set;
}


VariableSetLink minusL(Context context, VariableSetLink set, VariableSetLink other)
{
    if (other == NULL || set == NULL)
        return set;

    if (set->nr > 1)
    {
        set->nr --;
        set = copyL(context, set);
    }

    VariableSetLink link = other;
    while (link)
    {
        set = removeVariableL(context, set, link->variable);
        if (!set)
            break;
        link = link->link;
    }

    return set;
}

VariableSetLink removeAllL(Context context, VariableSetLink set, Variable* vars, int len)
{
    if (vars == NULL || set == NULL)
        return set;

    if (set->nr > 1)
    {
        set->nr --;
        set = copyL(context, set);
    }

    len --;
    for (; len >= 0; len --)
    {
        set = removeVariableL(context, set, vars[len]);
        if (!set)
            break;
    }

    return set;
}

VariableSetLink removeL(Context context, VariableSetLink set, Variable var)
{
    VariableSetLink top = set;
    VariableSetLink link = set;
    VariableSetLink prev = NULL;

    while (link)
    {
        ASSERT(context, link->nr == 1);

        if (link->variable == var)
        {
            if (prev == NULL) // First?
            {
                top = link->link;

                link->link = NULL;
                unlinkVariableSetLink(context, link);

                link = top;
            }
            else
            {
                prev->link = link->link;

                link->link = NULL;
                unlinkVariableSetLink(context, link);

                link = prev->link;
            }
        }
        else
            link = link->link;
    }

    return top;
}

///////////////////////////////////////////////////
// Simple array-based variable hash set

static const unsigned int prime_1 = 73;
static const unsigned int prime_2 = 5009;

static
int addItemHS(Hashset set, void* item)
{
    assert(set->nr == 1);

    size_t value = (size_t) item;
    size_t hash = value / 4;
    size_t ii;

    ii = set->mask & (prime_1 * hash);

    while (set->items[ii] != 0 && set->items[ii] != 1)
    {
        if (set->items[ii] == value)
            return 0;

        /* search free slot */
        ii = set->mask & (ii + prime_2);
    }

    set->nitems++;
    set->items[ii] = value;
    return 1;
}

static void maybeRehashHS(Context context, Hashset set)
{
    size_t *old_items;
    size_t old_capacity, ii;

    if ((float)set->nitems >= (size_t)((double)set->capacity * 0.85)) {
        old_items = set->items;
        old_capacity = set->capacity;
        set->nbits++;
        set->capacity = (size_t) (1 << set->nbits);
        set->mask = set->capacity - 1;
        set->items = ALLOCATE(context, set->capacity * sizeof(size_t));
        memset(set->items, 0, set->capacity * sizeof(size_t));
        set->nitems = 0;
        assert(set->items);
        for (ii = 0; ii < old_capacity; ii++) {
            if (old_items[ii] != 0 && old_items[ii] != 1)
                addItemHS(set, (void *)old_items[ii]);
        }
       FREE(context, old_items);
    }
}

Hashset makeHS(Context context)
{
     Hashset set = ALLOCATE(context, sizeof(struct _Hashset));
     set->nr = 1;
#ifdef CRSX_ENABLE_PROFILING
     set->marker = 0;
#endif
     set->nbits = 5;
     set->capacity = (size_t) (1 << set->nbits);
     set->mask = set->capacity - 1;
     set->items = ALLOCATE(context, set->capacity * sizeof(size_t));
     memset(set->items, 0, set->capacity * sizeof(size_t));
     set->nitems = 0;

     crsxpVSCreated(context);

     return set;
}

void freeHS(Context context, Hashset set)
{
    ssize_t i = set->capacity - 1;
    ssize_t r = set->nitems;
    for (; i >= 0; i --)
    {
        size_t item = set->items[i];
        if (item != 0 && item != 1)
        {
            unlinkVariable(context, (Variable) item);
            r --;
            if (r == 0)
                break;
        }
    }
    FREE(context, set->items);
    FREE(context, set);
}

Hashset copyHS(Context context, Hashset set)
{
    Hashset newset = ALLOCATE(context, sizeof(struct _Hashset));
    newset->nr = 1;
#ifdef CRSX_ENABLE_PROFILING
     newset->marker = 0;
#endif
    newset->nbits = set->nbits;
    newset->capacity = set->capacity;
    newset->mask = set->mask;
    newset->items = ALLOCATE(context, set->capacity * sizeof(size_t));

    ssize_t i = set->capacity - 1;
    for (; i >= 0; i --)
    {
        size_t item = set->items[i];
        newset->items[i] = item;
        if (item != 0 && item != 1)
            linkVariable(context, (Variable) item);
    }
    //memcpy(newset->items, set->items, set->capacity * sizeof(size_t));

    newset->nitems = set->nitems;

    crsxpVSCreated(context);

    return newset;
}

Hashset addVariableHS(Context context, Hashset set, Variable variable)
{
    if (set == AllFreeVariables)
    {
        unlinkVariable(context, variable);
        return set;
    }

    if (set && set->nitems > 100)
    {
        unlinkVariable(context, variable);

        UNLINK_Hashset(context, set);
        return AllFreeVariables;
    }

    if (!set)
        set = makeHS(context);
    else if (set->nr > 1)
    {
        set->nr --;
        set = copyHS(context, set);
    }

    if (!addItemHS(set, variable))
        unlinkVariable(context, variable);
    maybeRehashHS(context, set);

    crsxpVSAdded(context, set);

    return set;
}

Hashset removeVariableHS(Context context, Hashset set, Variable variable)
{
    if (set == AllFreeVariables)
        return set;

    if (!set)
        return NULL;

    if (set->nr > 1)
    {
        set->nr --;
        set = copyHS(context, set);
    }

     size_t value = (size_t) variable;
     size_t hash = value / 4;
     size_t ii = set->mask & (prime_1 * hash);
     size_t oii = ii;

     while (set->items[ii] != 0)
     {
         if (set->items[ii] == value) {
             unlinkVariable(context, variable);
             set->items[ii] = 1;
             set->nitems--;

             if (set->nitems == 0)
                 break;
             return set;
         }

         ii = set->mask & (ii + prime_2);

         if (oii == ii) // Looping?
             break;
     }

     return set;
}

int containsHS(Hashset set, Variable variable)
{
    if (!set)
        return 0;

    if (set == AllFreeVariables)
        return 1;

    size_t value = (size_t) variable;
    size_t hash = value / 4;
    size_t ii = set->mask & (prime_1 * hash);
    size_t oii = ii;
    while (set->items[ii] != 0) {
        if (set->items[ii] == value) {
            return 1;
        }

        ii = set->mask & (ii + prime_2);

        if (oii == ii) // looping?
            return 0;
    }
    return 0;
}

Hashset mergeAllHS(Context context, Hashset first, Hashset second)
{
    if (!first)
        return second;

    if (!second)
        return first;

    if (first == AllFreeVariables)
    {
        // too large
        if (second != AllFreeVariables)
            UNLINK_Hashset(context, second);
        return first;
    }

    if (second == AllFreeVariables)
    {
        UNLINK_Hashset(context, first);
        return AllFreeVariables;
    }

    if (first == second)
    {
        UNLINK_Hashset(context, second);
        return first;
    }

    if (first->nr > 1)
    {
        first->nr --;
        first = copyHS(context, first);
    }

    int i = second->capacity - 1;
    int r = second->nitems;
    for (; i >= 0; i --)
    {
        size_t item = second->items[i];
        if (item != 0 && item != 1)
        {
            first = addVariableHS(context, first, linkVariable(context, (Variable) item));
            r --;
            if (r == 0)
                break;
        }
    }

    UNLINK_Hashset(context, second);

    return first;
}

Hashset minusHS(Context context, Hashset set, Hashset other)
{
    if (!set || !other)
        return set;

    if (set->nr > 1)
    {
        set->nr --;
        set = copyHS(context, set);
    }

    int i = other->capacity - 1;
    int r = other->nitems;
    for (; i >= 0; i --)
    {
        size_t item = other->items[i];
        if (item != 0 && item != 1)
        {
            set = removeVariableHS(context, set, (Variable) item);
            r --;
            if (r == 0 || set->nitems == 0)
                break;
        }
    }
    return set;
}

Hashset removeAllHS(Context context, Hashset set, Variable* vars, int len)
{
    if (!set)
        return NULL;

    if (set == AllFreeVariables)
        return set;

    if (set->nr > 1)
    {
        set->nr --;
        set = copyHS(context, set);
    }

    len --;
    for (; len >= 0; len --)
    {
        set = removeVariableHS(context, set, vars[len]);
        if (set == AllFreeVariables || set->nitems == 0)
            break;
    }

    return set;
}

void printfHS(Context context, FILE* out, Hashset set)
{
    if (!set || set->nitems == 0)
        return;

    int i = set->capacity - 1;
    int r = set->nitems;
    char* sep = "";
    for (; i >= 0; i --)
    {
        size_t item = set->items[i];
        if (item != 0 && item != 1)
        {
            FPRINTF(context, out, "%s%s", sep, ((Variable) item)->name);
            sep = ",";
            r --;
            if (r == 0)
                break;
        }
    }
}

void addVariablesOfHS(Context context, VariableSet vars, Hashset set, int constrained, VariablePropertyLink props)
{
    ASSERT(context, set != AllFreeVariables);

    if (!set || set->nitems == 0)
        return;

    int i = set->capacity - 1;
    int r = set->nitems;
    for (; i >= 0; i --)
    {
        size_t item = set->items[i];
        if (item != 0 && item != 1)
        {
            Variable v = (Variable) item;
            if (!constrained || c_variableProperty(props, v) != NULL)
                addVariable(vars, linkVariable(context, v));
            r --;
            if (r == 0)
                break;
        }
    }
}

// Clear set
Hashset clearHS(Context context, Hashset set)
{
    if (!set || set == AllFreeVariables)
        return NULL;

    UNLINK_Hashset(context, set);
    return NULL;
}

///////////////////////////////////////////////////
// bucket-based hash set implementation

int equalsPtr(const void* left, const void* right)
{
    return left == right;
}

int equalsChars(const void* left, const void* right)
{
    return strcmp((char*) left, (char*) right) == 0;
}

static void freeKeyValue(Context context, const void* key, void* value)
{
    FREE(context, (void*) key);
    if (value)
        FREE(context, value);
}

static void freeValue(Context context, const void* key, void* value)
{
    if (value)
        FREE(context, value);
}

static LinkedList2 makeLL2(Context context, const void* key, void* value)
{
    LinkedList2 ll = ALLOCATE(context, sizeof(struct _LinkedList2));
    ll->key = key;
    ll->value = value;
    ll->next = NULL;
    return ll;
}

static inline LinkedList2 prependLL2(LinkedList2 list, LinkedList2 entry)
{
    entry->next = list;
    return entry;
}
//
//static LinkedList2 copyLL2(Context context, LinkedList2 head, void (*linkKey)(Context, const void*), void (*linkValue)(Context, void*))
//{
//    LinkedList2 newhead = NULL;
//    while (head)
//    {
//        LinkedList2 n = makeLL2(context, linkKey(context, head->key), linkValue(context, head->value));
//        n->next = newhead;
//        newhead = n;
//
//        head = head->next;
//    }
//    return newhead;
//}

static void freeLL2(Context context, LinkedList2 head, void (*unlink)(Context, const void*, void*))
{
    while (head)
    {
        LinkedList2 next = head->next;
        if (unlink)
            unlink(context, head->key, head->value);
        FREE(context, head);
        head = next;
    }
}

size_t hashChars(const void* key)
{
    const char* data = (const char*) key;

    size_t len = strlen(data);
    size_t h = len;
    while ( len > 3 )
    {
        h += data[0];
        h += data[1]<<8;
        h += data[2];
        h += data[3]<<8;
        len -= 4;
        data += 4;
    }
    switch ( len ) {
      case 3: h += data[2]; // no break
      case 2: h += data[1]<<8;// no break
      case 1: h += data[0];// no break
    }
    return h;
}

size_t hashPtr(const void* entry)
{
    size_t key = (size_t) entry;

    key = (~key) + (key << 21); // key = (key << 21) - key - 1;
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8); // key * 265
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4); // key * 21
    key = key ^ (key >> 28);
    key = key + (key << 31);

    return key;
}

static Hashset2 growHS2(Context context, Hashset2 set)
{
    int oldsize = set->nslots;
    LinkedList2 *oldentries = set->entries;

    set->nbits++;
    set->nslots *= 2;
    set->entries = CALLOCATE(context, set->nslots * sizeof(LinkedList2));

    size_t i;
    for (i = 0; i < oldsize; i++)
    {
        LinkedList2 l = oldentries[i];
        while (l)
        {
            LinkedList2 n = l->next;

            int index = set->hash(l->key) & ((1 << set->nbits) - 1);
            l->next = set->entries[index];
            set->entries[index] = l;

            l = n;
        }
    }
    FREE(context, oldentries);
    return set;
}

static void freeHS2(Context context, Hashset2 set)
{
    if (!set)
        return;

    size_t i;
    for (i = 0; i < set->nslots; i++)
      freeLL2(context, set->entries[i], set->unlink);

    FREE(context, set->entries);
    FREE(context, set);
}

Hashset2 linkHS2(Hashset2 set)
{
    set->nr ++;
    return set;
}

void unlinkHS2(Context context, Hashset2 set)
{
    if (--set->nr == 0)
        freeHS2(context, set);
}

Hashset2 makeHS2(Context context, unsigned int numbits, void (*unlink)(Context, const void*,void*), int (*equals)(const void*,const void*), size_t (*hash)(const void*))
{
    Hashset2 set = ALLOCATE(context, sizeof(struct _Hashset2));
    set->nr = 1;
    set->nbits = numbits;
    set->nslots = 1 << set->nbits;
    set->size = 0;

    set->entries = CALLOCATE(context, set->nslots * sizeof(LinkedList2));

    set->unlink = unlink;
    set->equals = equals;
    set->hash = hash;

    return set;
}

Hashset2 addValueHS2(Context context, Hashset2 set, const void* key, void* value)
{
    ASSERT(context, set->nr == 1); // Can't update when shared.

    size_t index;
    LinkedList2 slot;
    size_t cellno = 0;

    index = set->hash(key) & ((1 << set->nbits) - 1);
    slot = set->entries[index];

    while (slot != NULL)
    {
        if (set->equals(slot->key, key))
        {
            void* oldval = slot->value;
            slot->value = value; // Transfer ref.
            set->unlink(context, key, oldval);
            break; // already there
        }

        slot = slot->next;
        cellno++;
    }

    if (slot == NULL)
    {
        if (set->size >= 4 * set->nslots)
        {
            set = growHS2(context, set);
            index = set->hash(key) & ((1 << set->nbits) - 1);
        }

        LinkedList2 ll = makeLL2(context, key, value);
        set->entries[index] = prependLL2(set->entries[index], ll);
        set->size++;
    }

    return set;
}

Hashset2 removeHS2(Context context, Hashset2 set, const void* key)
{
    ASSERT(context, set->nr == 1); // Can't update when shared.

    size_t index;
    LinkedList2 slot, prevslot;

    index = set->hash(key) & ((1 << set->nbits) - 1);
    slot = set->entries[index];
    prevslot = NULL;

    while (slot)
    {
       if (set->equals(slot->key, key))
       {
           // Found it.
           if (prevslot == NULL)
           {
               set->entries[index] = slot->next;
           }
           else
           {
               prevslot->next = slot->next;
           }

           freeLL2(context, slot, set->unlink);
           set->size--;
           return set;
       }

       prevslot = slot;
       slot = slot->next;
    }

    return set;
}

int containsHS2(Hashset2 set, const void* key)
{
    size_t index = set->hash(key) & ((1 << set->nbits) - 1);
    LinkedList2 slot = set->entries[index];

    while (slot)
    {
        if (set->equals(slot->key, key))
            return 1;

        slot = slot->next;
    }

    return 0;
}

void* getValueHS2(Hashset2 set, const void* key)
{
    size_t index = set->hash(key) & ((1 << set->nbits) - 1);
    LinkedList2 slot = set->entries[index];
    while (slot)
    {
        if (set->equals(slot->key, key))
            return slot->value;

        slot = slot->next;
    }
    return NULL;
}

Hashset2 clearHS2(Context context, Hashset2 set)
{
    ASSERT(context, set->nr == 1); // Can't update when shared.

    size_t i;
    for (i = 0; i < set->nslots; i++)
    {
        freeLL2(context, set->entries[i], set->unlink);
        set->entries[i] = NULL;
    }

    return set;
}

Pair*
toArrayHS2(Context context, Hashset2 set)
{
    if (set->size == 0)
        return NULL;

    Pair* array = ALLOCATE(context, sizeof(struct _Pair)* set->size);

    int j = 0;
    int i;
    for (i = 0 ; i < set->nslots ; i++)
    {
        LinkedList2 slot = set->entries[i];
        while (slot)
        {
            Pair pair = ALLOCATE(context, sizeof(struct _Pair));
            pair->key = slot->key;
            pair->value = slot->value;
            array[j ++] = pair;
            slot = slot->next;
        }
    }
    return array;
}

static char* fprintProperty(Context context, FILE* out, int isNamed, size_t nr, const void *key, Term term, char* sep, int depth, VariableSet encountered, Hashset2 used, int indent, int *posp, int debug, int max);

/////////////////////////////////////////////////////////////////////////////////
// Map from variables to variables.
//

VariableMap makeVariableMap(Context context)
{
    VariableMap map = ALLOCATE(context, sizeof(struct _VariableMap));
    map->link = NULL;
    map->context = context;
    return map;
}

Variable lookupVariable(VariableMap map, Variable variable)
{
    if (map)
    {
        VariableMapLink link;
        for (link = map->link; link; link = link->link)
        {
            if (link->key == variable)
                return link->value;
        }
    }
    return NULL;
}

Variable lookupVariableLink(VariableMapLink link, Variable variable)
{
    for (; link; link = link->link)
    {
        if (link->key == variable)
            return link->value;
    }
    return NULL;
}

void addVariableMap(VariableMap map, Variable key, Variable value)
{
    ASSERT(map->context, key->name);
    VariableMapLink link = ALLOCATE(map->context, sizeof(struct _VariableMapLink));
    link->link = map->link;
    link->key = key;
    link->value = value;
    map->link = link;
}

void popVariableMap(VariableMap map, int n)
{
    for (; map->link && n > 0; --n)
    {
        VariableMapLink next = map->link->link;
        FREE(map->context, map->link);
        map->link = next;
    }
}

void freeVariableMap(VariableMap map)
{
    VariableMapLink link = map->link;
    while (link)
    {
        VariableMapLink next = link->link;
        FREE(map->context, link);
        link = next;
    }
    FREE(map->context, map);
}

/////////////////////////////////////////////////////////////////////////////////
// Map from names to variables.

VariableNameMapLink addNameMapLink(Context context, VariableNameMapLink parent, char *name, Variable variable)
{
    VariableNameMapLink link = ALLOCATE(context, sizeof(struct _VariableNameMapLink));
    link->key = name;
    link->value = variable;
    link->link = parent;
    return link;
}

Variable lookupNameLink(VariableNameMapLink link, char *name)
{
    for (; link; link = link->link)
        if (!strcmp(link->key, name))
            return link->value;
    return NULL;
}

void freeVariableNameMapLinks(Context context, VariableNameMapLink link)
{
    while (link)
    {
        VariableNameMapLink next = link->link;
        FREE(context, link);
        link = next;
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Property accessors.

// Return the named property or NULL if none.
// The reference is *NOT* transferred
// The name must have already been interned into the Context's keyPool
Term c_namedProperty(NamedPropertyLink link, char *name)
{
    for (; link; link = link->link)
    {
        if (! link->name) // hashset
        {
            Term term = (Term) getValueHS2(link->u.propset, name);
            if (term)
                return term;
        }
        else if (name == link->name)
        {
            return link->u.term;
        }
    }
    return NULL;
}

// Return the variable property or NULL if none.
// The reference is *NOT* transferred
Term c_variableProperty(VariablePropertyLink link, Variable variable)
{
    for (; link; link = link->link)
        if (variable == link->variable)
            return link->u.term;
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////////
// Global Environement accessor.

char* getEnvValue(Context context, const char *name)
{
    char* value = (char*) getValueHS2(context->env, (const void*) name);
    if (!value)
        value = getenv(name);
    return value;
}

void setContextEnv(Context context, const char *name, const char* value)
{
    addValueHS2(context, context->env, (const void*) name, (void*) value);
}

/////////////////////////////////////////////////////////////////////////////////
// Errors.

void *errorf(Context context, char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(STDERR, format, ap);
    va_end(ap);
    exit(1);
}

/////////////////////////////////////////////////////////////////////////////////
// Allocation.

#ifdef DEBUG
long allocateCount, freeCount;
#endif

void *crsx_allocate(Context context, size_t size)
{
    void *p = malloc(size);
    ASSERT(context, p);
#   ifdef DEBUG
    ++allocateCount;
    if (allocateCount % 1000000L == 0L)
        DEBUGF(context, "//%*sALLOCATION REACHED %ld\n", stepNesting, "", allocateCount);
#   endif
    return p;
}

void *crsx_callocate(Context context, size_t size)
{
    void *p = calloc(size, 1);
    ASSERT(context, p);
#   ifdef DEBUG
    ++allocateCount;
    if (allocateCount % 1000000L == 0L)
        DEBUGF(context, "//%*sALLOCATION REACHED %ld\n", stepNesting, "", allocateCount);
#   endif
    return p;
}

void *crsx_reallocate(Context context, void *pointer, size_t size)
{
    void *p = realloc(pointer, size);
    ASSERT(context, p);
    return p;
}

void crsx_free(Context context, void *pointer)
{
    DEBUGX(++freeCount);
    free(pointer);
}

void crsxAddPools(Context context)
{
    if (!context->poolRefCount)
    {
        context->env = makeHS2(context, 4, freeKeyValue, equalsChars, hashChars);
        context->stringPool = makeHS2(context, 16, freeValue, equalsChars, hashChars);
        context->keyPool = makeHS2(context, 16, freeValue, equalsChars, hashChars);

        context->consPool = ALLOCATE(context, CONS_POOL_MAX_SIZE_SIZE * sizeof(Construction));
        context->consPoolSize = ALLOCATE(context, CONS_POOL_MAX_SIZE_SIZE * sizeof(ssize_t));
        int i;
        for (i = 0; i < CONS_POOL_MAX_SIZE_SIZE; i ++)
        {
            context->consPool[i] = ALLOCATE(context, CONS_POOL_MAX_SIZE * sizeof(Construction));
            context->consPoolSize[i] = 0;
        }

    }
    ++context->poolRefCount;
}

void crsxReleasePools(Context context)
{
    crsxpReleasePools(context);

    --context->poolRefCount;
    if (! context->poolRefCount)
    {
        
        freeHS2(context, context->env);
        context->env = NULL;
        freeHS2(context, context->stringPool);
        context->stringPool = NULL;
        freeHS2(context, context->keyPool);
        context->keyPool = NULL;

        int i;
        for (i = 0; i < CONS_POOL_MAX_SIZE_SIZE; i ++)
        {
            ssize_t j = context->consPoolSize[i] - 1;
            while (j >= 0)
            {
                FREE(context, context->consPool[i][j]);
                j --;
            }
            FREE(context, context->consPool[i]);
        }
        FREE(context, context->consPool);
        FREE(context, context->consPoolSize);
    }
}

char *makeString(Context context, const char *src)
{
    size_t length = strlen(src);
    char *dest = ALLOCATE(context, length+1);
    memcpy(dest, src, length);
    dest[length] = '\0';
    ///PRINTF(context, "String<%s>\n", dest);
    return dest;
}

char *makeKeyString(Context context, const char *src)
{
    Hashset2 pool = context->keyPool;
    assert(pool);
    if (!pool)
        return NULL;
    char *interned_string = (char *) getValueHS2(pool, (const void*) src);
    if (interned_string)
        return interned_string;

    interned_string = makeString(context, src);
    addValueHS2(context, pool, (const void*) interned_string, (void*) interned_string);
    return interned_string;
}

char *makeSubstring(Context context, const char *src, size_t first, size_t length)
{
    size_t src_length = strlen(src);
    if (src_length < first+length)
        length = src_length - first;
    char *dest = ALLOCATE(context, length+1);
    memcpy(dest, src+first, length);
    dest[length] = '\0';
    ///PRINTF(context, "Substring<%s>=<%s>\n", src, dest);
    return dest;
}

static
void mangle(int length, const char *original, char *mangled)
{
    int i;
    for (i = 0; i <= length; ++i)
    {
        char c = original[i];
        mangled[i] = (c == '/' ? '_' : c);
    }
}

//char* makeMangle(Context context, const char* src)
//{
//    size_t l = strlen(src);
//    char* mangled = ALLOCATE(context, l + 1);
//    mangle(l, src, mangled);
//    return mangled;
//}

// Interpret one Unicode relaxed UTF-8 character starting at s into codepoint c.
// NOTE: leaves s on last character in sequence (this leaves s untouched for 7-bit characters)
static unsigned int onecodepoint(unsigned char **sp)
{
    unsigned int c = **sp;
    if (c <= 0x7F) // .7 bit: U+0000     U+007F     1     0xxxxxxx
    {}
    else if ((c & 0xC0) == 0x80) // continuation character out of place -- assume normal
    {}
    else if ((c & 0xE0) == 0xC0) // 11 bit: U+0080 U+07FF    2 110xxxxx 10xxxxxx
    {
        if ((*((*sp)+1) & 0xC0) == 0x80) // if not continued assume roque 8-bit!
        {
            c = (c & 0x1F) << 6;
            c |= *(++(*sp)) & 0x3F;
        }
    }
    else if ((c & 0xF0) == 0xE0) // 16 bit: U+0800 U+FFFF 3 1110xxxx 10xxxxxx 10xxxxxx
    {
        if ((*((*sp)+1) & 0xC0) == 0x80) // if not continued assume roque 8-bit!
        {
            c = (c & 0x1F) << 12;
            c |= (*(++(*sp)) & 0x3F) << 6;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= *(++(*sp)) & 0x3F;
        }
    }
    else if ((c & 0xF8) == 0xF0) // 21 bit: U+10000 U+1FFFFF 4     11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    {
        if ((*((*sp)+1) & 0xC0) == 0x80) // if not continued assume roque 8-bit!
        {
            c = (c & 0x1F)<<18;
            c |= (*(++(*sp)) & 0x3F) << 12;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= (*(++(*sp)) & 0x3F) << 6;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= *(++(*sp)) & 0x3F;
        }
    }
    else if ((c & 0xFC) == 0xF8) // 26 bit: U+200000 U+3FFFFFF 5 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
    {
        if ((*((*sp)+1) & 0xC0) == 0x80) // if not continued assume roque 8-bit!
        {
            c = (c & 0x1F)<<24;
            c |= (*(++(*sp)) & 0x3F) << 18;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= (*(++(*sp)) & 0x3F) << 12;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= (*(++(*sp)) & 0x3F) << 6;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= *(++(*sp)) & 0x3F;
        }
    }
    else if ((c & 0xFE) == 0xFC) // 31 bit: U+4000000 U+7FFFFFFF 6 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
    {
        if ((*((*sp)+1) & 0xC0) == 0x80) // if not continued assume roque 8-bit!
        {
            c = (c & 0x1F)<<30;
            c |= (*(++(*sp)) & 0x3F) << 24;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= (*(++(*sp)) & 0x3F) << 18;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= (*(++(*sp)) & 0x3F) << 12;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= (*(++(*sp)) & 0x3F) << 6;
            if ((*((*sp)+1) & 0xC0) == 0x80) c |= *(++(*sp)) & 0x3F;
        }
    }
    return c;
}


// Convert UTF-8 chars to external escaped string form.
// All characters in *sourcep are converted into characters starting at *targetp,
// not going beyond endtarget. Updates *sourcep and *targetp.
static const char HexDigits[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
static void escape(char **sourcep, char **targetp, char *endsource, char *endtarget)
{
    unsigned char *s = (unsigned char *)*sourcep;
    unsigned char *t = (unsigned char *)*targetp;
    for (; *s && s < (unsigned char *)endsource && t < (unsigned char *)endtarget-5; ++s)
    {
         unsigned int c = *s;
        switch (c)
        {
        case '\"' : *(t++) = '\\'; *(t++) = '\"'; break;
        case '\\' : *(t++) = '\\'; *(t++) = '\\'; break;
        case '\n' : *(t++) = '\\'; *(t++) = 'n'; break;
        case '\r' : *(t++) = '\\'; *(t++) = 'r'; break;
        case '\f' : *(t++) = '\\'; *(t++) = 'f'; break;
        case '\a' : *(t++) = '\\'; *(t++) = 'a'; break;
        case '\t' : *(t++) = '\\'; *(t++) = 't'; break;

        default :
            c = onecodepoint(&s);
            // Ready to externalize
            if (c >= ' ' && c <= '~')
            {
                *(t++) = c;
            }
            else if (c <= 0x7F) // non-printable 7 bit characters printed as octal.
            {
                *(t++) = '\\'; *(t++) = '0' + ((c>>6)&0x7); *(t++) = '0' + ((c>>3)&0x7); *(t++) = '0' + (c&0x7);
            }
            else if (c <= 0xFFFF) // most Unicode escapes
            {
                *(t++) = '\\'; *(t++) = 'u';
                *(t++) = HexDigits[(c>>12)&0xF]; *(t++) = HexDigits[(c>>8)&0xF]; *(t++) = HexDigits[(c>>4)&0xF]; *(t++) = HexDigits[c&0xF];
            }
            else // extreme Unicode
            {
                *(t++) = '\\'; *(t++) = 'U';
                *(t++) = HexDigits[(c>>28)&0xF]; *(t++) = HexDigits[(c>>24)&0xF]; *(t++) = HexDigits[(c>>20)&0xF]; *(t++) = HexDigits[(c>>16)&0xF];
                *(t++) = HexDigits[(c>>12)&0xF]; *(t++) = HexDigits[(c>>8)&0xF]; *(t++) = HexDigits[(c>>4)&0xF]; *(t++) = HexDigits[c&0xF];
            }
        }
    }
    *sourcep = (char*)s;
    *targetp = (char*)t;
}

// Convert escaped string characters (without quotes) to UTF-8 form.
// All characters in *sourcep are converted into characters starting at *targetp,
// not going beyond endtarget. Updates *sourcep and *targetp.
void rescape(char **sourcep, char **targetp, char *endsource, char *endtarget)
{
    unsigned char *s = (unsigned char *)*sourcep;
    unsigned char *t = (unsigned char *)*targetp;
    for (; *s && s < (unsigned char *)endsource && t < (unsigned char *)endtarget; ++s)
    {
        unsigned int c = *s;
        if (c == '\\')
        {
            switch (c = *(++s))
            {
            case 'a' : c =  '\n'; break;
            case 't' : c =  '\t'; break;
            case 'r' : c =  '\r'; break;
            case 'f' : c =  '\f'; break;
            case 'n' : c =  '\n'; break;
            case '0' : case '1' : case '2' : case '3' : case '4' : case '5' : case '6' : case '7' : { // octal escape \OOO
                c = (*s - '0'); unsigned char *e = s + 3;
                while (++s < e && '0' <= *s && *s <= '7') c = (c<<3) + (*s - '0');
                break;
            }
            case 'u' :
            case 'U' : { // hex Unicode escape \uXXXX and \UXXXXXXXX
                unsigned char *e = s + (c == 'u' ? 5 : 9);
                c = 0;
                while (++s < e)
                {
                    switch (*s)
                    {
                    case '0' : case '1' : case '2' : case '3' : case '4' :
                    case '5' : case '6' : case '7' : case '8' : case '9' :
                        c = (c<<4) + (*s - '0');
                        break;
                    case 'a' : case 'b' : case 'c' : case 'd' : case 'e' : case 'f' :
                        c = (c<<4) + (*s - 'a' + 10);
                        break;
                    case 'A' : case 'B' : case 'C' : case 'D' : case 'E' : case 'F' :
                        c = (c<<4) + (*s - 'A' + 10);
                        break;
                    default : e = s; // hack to break loop early
                    }
                }
                s--;
                break;
            }
            default :
                c = onecodepoint(&s);
            }
        }
        if (c == 0) // 'modified UTF-8' avoids \0 in string
        {
            *(t++) = 0xC0;
            *(t++) = 0x80;
        }
        else if (c <= 0x7F) // 7 bit: U+0000 U+007F 1 0xxxxxxx
        {
            *(t++) = c;
        }
        else if (c <= 0x7FF) // 11 bit: U+0080 U+07FF 2 110xxxxx 10xxxxxx
        {
            *(t++) = 0xC0 | (c>>6);
            *(t++) = 0x80 | (c&0x3F);
        }
        else if (c <= 0xFFFF) // 16 bit: U+0800 U+FFFF 3 1110xxxx     10xxxxxx     10xxxxxx
        {
            *(t++) = 0xE0 | (c>>12);
            *(t++) = 0x80 | ((c>>6)&0x3F);
            *(t++) = 0x80 | (c&0x3F);
        }
        else if (c <= 0x1FFFFF) // 21 bit: U+10000 U+1FFFFF 4 11110xxx 10xxxxxx     10xxxxxx     10xxxxxx
        {
            *(t++) = 0xF0 | (c>>18);
            *(t++) = 0x80 | ((c>>12)&0x3F);
            *(t++) = 0x80 | ((c>>6)&0x3F);
            *(t++) = 0x80 | (c&0x3F);
        }
        else if (c <= 0x3FFFFFF) // 26 bit: U+200000 U+3FFFFFF 5 111110xx     10xxxxxx     10xxxxxx     10xxxxxx     10xxxxxx
        {
            *(t++) = 0xF8 | (c>>24);
            *(t++) = 0x80 | ((c>>18)&0x3F);
            *(t++) = 0x80 | ((c>>12)&0x3F);
            *(t++) = 0x80 | ((c>>6)&0x3F);
            *(t++) = 0x80 | (c&0x3F);
        }
        else if (c <= 0x7FFFFFFF) // 31 bit: U+4000000     U+7FFFFFFF 6 1111110x     10xxxxxx     10xxxxxx     10xxxxxx     10xxxxxx     10xxxxxx
        {
            *(t++) = 0xFC | (c>>30);
            *(t++) = 0x80 | ((c>>24)&0x3F);
            *(t++) = 0x80 | ((c>>18)&0x3F);
            *(t++) = 0x80 | ((c>>12)&0x3F);
            *(t++) = 0x80 | ((c>>6)&0x3F);
            *(t++) = 0x80 | (c&0x3F);
        }
    }
    *sourcep = (char*)s;
    *targetp = (char*)t;
}

char *makeEscaped(Context context, const char *src)
{
    size_t src_length = strlen(src);
    size_t tmp_length = src_length*10+3; // enough space even if all are quotes!
    char *tmp = ALLOCA(context, tmp_length+1);
    char *s = (char*)src;
    char *t = tmp;
    *(t++) = '"';
    escape(&s, &t, ((char*)src)+src_length, tmp+tmp_length-2);
    *(t++) = '"';
    *(t++) = '\0';
    size_t dstz = t - tmp;
    char *dst = ALLOCATE(context, dstz);
    memcpy(dst, tmp, dstz);
    ///PRINTF(context, "Escaped<%s>\n", dst);
    return dst;
}

char *makeRescaped(Context context, const char *src)
{
    char *s1 = (char*)src; while (isspace(*s1)) ++s1; // trim front spaces
    char *s2 = (char*)src + strlen(src); while (isspace(*(s2-1)) && s1<s2) --s2; // trim back spaces
    if ((*s1 == '"' || *s1 == '\'') && *s1 == *(s2-1) && s1+2 <= s2) { ++s1; --s2; } // trim quotes
    size_t z = s2 - s1;
    if (z == 0)
        return "";
    char *tmp = ALLOCA(context, z+1);
    char *t = tmp;
    rescape(&s1, &t, s2, tmp+z);
    *(t++) = '\0';
    size_t dstz = t - tmp;
    char *dst = ALLOCATE(context, dstz);
    memcpy(dst, tmp, dstz);
    ///PRINTF(context, "Rescaped<%s>\n", dst);
    return dst;
}

char *makeMangled(Context context, const char *src)
{
    size_t srcz = strlen(src);
    if (!strncmp(src, "_M_", 3)) return memcpy(ALLOCATE(context, srcz+1), src, srcz+1);

    size_t tmp_length = srcz*3+5; // enough space even if all are special!
    char *tmp = ALLOCA(context, tmp_length);
    char *t = tmp;
    *(t++) = '_'; *(t++) = 'M'; *(t++) = '_';

    char *s = (char*)src, *e = (char*)src+srcz;
    for (; s < e; ++s)
    {
        char c = *s;
        switch (c)
        {
        case '-' : {
            if (s+1 >= e || ('A' <= *(s+1) && *(s+1) <= 'Z'))
                *(t++) = '_';
            else
            {
                *(t++) = '_'; *(t++) = '_';
            }
            break;
        }
        case '_' : *(t++) = '_'; *(t++) = 'x'; break;
        case '~' : *(t++) = '_'; *(t++) = 'w'; break;
        case '$' : *(t++) = '_'; *(t++) = 's'; break;
        case '#' : *(t++) = '_'; *(t++) = 'h'; break;
        default :
            if (c <= '~')
            {
                if (isalnum(c))
                    *(t++) = c;
                else
                {
                    *(t++) = '_';
                    *(t++) = HexDigits[(c>>4)&0xF]; *(t++) = HexDigits[c&0xF];
                }
            }
            else
            {
                *(t++) = '_'; *(t++) = 'u';
                *(t++) = HexDigits[(c>>12)&0xF]; *(t++) = HexDigits[(c>>8)&0xF]; *(t++) = HexDigits[(c>>4)&0xF]; *(t++) = HexDigits[c&0xF];
            }
        }
    }
    (*t++) = '\0';
    size_t dstz = t - tmp;
    char *dst = ALLOCATE(context, dstz);
    memcpy(dst, tmp, dstz);
    return dst;
}

char * makeEncodePoint(Context context, unsigned int c)
{
  unsigned char *res;
  if (c<0x80) {
    res = ALLOCATE(context, 2);
    res[0] = (c>>0  & 0x7F) | 0x00;
    res[1] = 0;
  } else if (c<0x0800) {
    res = ALLOCATE(context, 3);
    res[0] = (c>>6  & 0x1F) | 0xC0;
    res[1] = (c>>0  & 0x3F) | 0x80;
    res[2] = 0;
  } else if (c<0x010000) {
    res = ALLOCATE(context, 4);
    res[0] = (c>>12 & 0x0F) | 0xE0;
    res[1] = (c>>6  & 0x3F) | 0x80;
    res[2] = (c>>0  & 0x3F) | 0x80;
    res[3] = 0;
  } else if (c<0x110000) {
    res = ALLOCATE(context, 5);
    res[0] = (c>>18 & 0x07) | 0xF0;
    res[1] = (c>>12 & 0x3F) | 0x80;
    res[2] = (c>>6  & 0x3F) | 0x80;
    res[3] = (c>>0  & 0x3F) | 0x80;
    res[4] = 0;
  }
  else {
      res = NULL;
  }
  return (char*) res;
}


char *stringnf(Context context, size_t size, const char *format, ...)
{
    char buffer[size]; // temporary on stack...
    va_list ap;
    va_start(ap,format);
    int bytes = vsnprintf(buffer, size, format, ap);
    va_end(ap);
    if (bytes < size)
    {
        char *string = ALLOCATE(context, bytes+1);
        memcpy(string, buffer, bytes);
        string[bytes] = '\0';
        return string;
    }
    else
    {
        // Overflow...repair as best possible.
        char *string = ALLOCATE(context, size+4);
        memcpy(string, buffer, size);
        memcpy(string+size, "...", 3);
        string[size+3] = '\0';
        return string;
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Term hashing

static long long stringHashCode(const char* string)
{
    long long code = 0ll;
    int x;
    for (x = 0; string[x]; ++x)
        code = (code<<1) ^ string[x];
    return code;
}

long long termHashCode(Context context, Term term, VariableLink deBruijn)
{
    long long code = 0ll;
    if (IS_VARIABLE_USE(term))
    {
        for (; deBruijn; deBruijn = deBruijn->next)
        {
            if (deBruijn->variable == VARIABLE(term))
                return code * 19ll;
            ++code;
        }
        return (((long long) term)>>4) * 3ll;
    }
    const size_t arity = ARITY(term);
    size_t index;
    for (index = 0; index < arity; ++index)
    {
        const size_t rank = RANK(term, index);
        struct _VariableLink binders[rank];
        VariableLink local = deBruijn;
        int bx;
        for (bx = 0; bx < rank; ++bx)
        {
            binders[bx].variable = BINDER(term, index, bx);
            binders[bx].next = local;
            local = &binders[bx];
        }
        code ^= termHashCode(context, SUB(term, index), local) ^ (1<<index);
    }
    if (IS_LITERAL(term))
        return code ^ stringHashCode(asLiteral(term)->text);
    else
        return code ^ (term->descriptor->sortoffset*17ll + (((long long) (SORT(term)))>>4) * 7ll);
}

/////////////////////////////////////////////////////////////////////////////////
// Regex

int matchRegex(char* pat, char* str)
{
    // utf8 -> utf16
    int str_len = strlen(str);
    int strU16_cap = U16_MAX_LENGTH * (2 * str_len) + 1;
    UChar strU16[strU16_cap];
    int strU16_len;
    UErrorCode status = U_ZERO_ERROR;
    u_strFromUTF8(strU16, strU16_cap, &strU16_len, str, -1, &status);

    if (U_FAILURE(status))
        return 0; // TODO: error

    int pat_len = strlen(pat);
    int patU16_cap = U16_MAX_LENGTH * (2 * pat_len) + 1;
    UChar patU16[patU16_cap];
    int patU16_len;
    status = U_ZERO_ERROR;
    u_strFromUTF8(patU16, patU16_cap, &patU16_len, (char*)pat, -1, &status);

    if (U_FAILURE(status))
        return 0; // TODO: error

    // re generation
    status = U_ZERO_ERROR;
    URegularExpression* re = uregex_open(patU16, -1, 0, NULL, &status);

    if (U_FAILURE(status))
        return 0;

    status = U_ZERO_ERROR;
    uregex_setText(re, strU16, -1, &status);
    assert (U_SUCCESS(status));

    // matching
    status = U_ZERO_ERROR;
    int matched = uregex_find (re, 0, &status);
    if (U_FAILURE(status))
        return 0;

    uregex_close (re);

    return matched;
}

void sendSplit(char *string, char *sep, Sink sink)
{
  int depth = 0;
  int trailingNulls = 0;
  if (*sep && *string) {
      const size_t sepz = strlen(sep);
      char *word, *next;
      for (word = (char*)string; (next = strstr(word, sep)); word = next+sepz) {
          if (next==word) {          
                trailingNulls++;
                continue;
          }
          for (; trailingNulls; trailingNulls--) {
                START(sink, _M__sCons);
                LITERAL(sink, "");
                ++depth;
          }
          START(sink, _M__sCons);
          LITERAL(sink, makeSubstring(sink->context, word, 0, next-word));
          ++depth;
      }
      if (*word) {
         START(sink, _M__sCons);
         LITERAL(sink, makeString(sink->context, word));
         ++depth;
      }
  }
  START(sink, _M__sNil);
  END(sink, _M__sNil);
  while (depth-- > 0) END(sink, _M__sCons);
}


//void sendSplit(char* str, char* pat, Sink sink)
//{
//     int str_len = u_strlen ((UChar*) str);
//
//     URegularExpression* re;
//     UErrorCode status = U_ZERO_ERROR;
//
//     // Create regular expression
//     {
//        int pat_len = u_strlen ((UChar*) pat);
//        int patU16_cap = U16_MAX_LENGTH * (2 * pat_len) + 1;
//        UChar patU16[patU16_cap];
//        int patU16_len;
//        status = U_ZERO_ERROR;
//        u_strFromUTF8 (patU16, patU16_cap, &patU16_len, (char*)pat, -1, &status);
//
//        // re generation
//        status = U_ZERO_ERROR;
//
//        re = uregex_open (patU16, -1, 0, NULL, &status);
//
//        assert (U_SUCCESS (status));
//
//        unsigned short int emptyUTF16 = 0x0000;
//        uregex_setText (re, (unsigned short int*)&emptyUTF16, -1, &status);
//        if(uregex_matches (re, -1, &status))
//        {
//            ERRORF(sink->context, Crsx, "Invalid regular expression %s", pat);
//            return;
//        }
//     }
//    int ntok = 0;
//    if(str_len > 0)
//    {
//        int strU16_cap = U16_MAX_LENGTH * (2 * str_len) + 1;
//        UChar* strU16 = (UChar*)ALLOCATE(sink->context, strU16_cap);
//
//        // Set input
//        {
//            int strU16_len;
//            u_strFromUTF8 (strU16, strU16_cap, &strU16_len, (char*)str, -1, &status);
//            assert (U_SUCCESS (status));
//
//            status = U_ZERO_ERROR;
//            uregex_setText (re, strU16, -1, &status);
//            assert  (U_SUCCESS (status));
//        }
//
//        // Split...
//
//        int findStartIndex = 0;
//        int endIndex = 0;
//        while (uregex_find(re, findStartIndex, &status))
//        {
//            int startIndex = uregex_start(re, 0, &status);
//            endIndex = uregex_end(re, 0, &status);
//
//            status = U_ZERO_ERROR;
//            int lenOfToken = startIndex - findStartIndex;
//            if(lenOfToken > 0)
//            {
//                uint8_t* tok = (uint8_t*)ALLOCATE(sink->context, lenOfToken+1);
//                int32_t charsCopied = -1;
//                u_strToUTF8((char*)tok, lenOfToken, &charsCopied,
//                        strU16+findStartIndex, lenOfToken, &status);
//                tok[lenOfToken] = 0x00;
//
//                START(sink, _M__sCons); // $Cons
//                LITERALU(sink, (char*) tok);
//            }
//            else
//            {
//                START(sink, _M__sCons); // $Cons
//                LITERAL(sink, "");
//            }
//            ntok++;
//
//            findStartIndex = endIndex;
//        }
//
//        int lenOfToken = str_len - endIndex;
//        if(lenOfToken > 0)
//        {
//            uint8_t* tok = (uint8_t*)ALLOCATE(sink->context, lenOfToken+1);
//            int32_t charsCopied = -1;
//            u_strToUTF8((char*)tok, lenOfToken, &charsCopied,
//                    strU16+findStartIndex, lenOfToken, &status);
//            tok[lenOfToken] = 0x00;
//
//            START(sink, _M__sCons); // $Cons
//            LITERALU(sink, (char*) tok);
//
//        }
//        else
//        {
//            START(sink, _M__sCons); // $Cons
//            LITERAL(sink, "");
//        }
//        ntok++;
//
//        FREE(sink, strU16);
//    }
//
//    uregex_close (re);
//
//    // Close list
//    START(sink, _M__sNil); END(sink, _M__sNil); // $Nil
//    while (ntok-- > 0)
//        END(sink, _M__sCons); // $Cons
//}


/////////////////////////////////////////////////////////////////////////////////
// Constants.

const char *True = "$True";
const char *False = "$False";
const char *Nil = "$Nil";

VARIABLESET AllFreeVariables = (VARIABLESET) 1;

/////////////////////////////////////////////////////////////////////////////////
// Computation.

struct _ContextEntry
{
    Term term;
    unsigned short index;
};
typedef struct _ContextEntry ContextEntry;
SETUP_STACK_TYPE(ContextEntry)

// Note that step is special:
static int step(Sink sink, Term term); // helper

// Standard normalization function.
//
// Implements outermost evaluate-before-copy normalization: Repeatedly picks one of the following steps until DONE:
// (1) If term is marked as nf with no parent (so at the root of the nf subterm) then we are DONE.
// (2) If term is marked as nf with a parent and a following sibling then advance to the following sibling.
// (3) If term is marked as nf with a parent but no following sibling then pop to parent.
// (4) If term is a function invocation that is not marked as nostep and that we can in fact step then do so and update term to the result.
// (5) If term is a function invocation that is not marked as nostep and that we can in fact not step then mark it as nostep.
// (6) If term is a non-nf data term or a nostep function application with a non-nf child then clear nostep if it is a function, push term, and switch to that child.
// (7) If term is a non-nf data term or a nostep function application with only nf children then mark it as nf.
/// // (7) If term is a nostep function application with either no children or only nf children of which the first is a variable then mark it as nf.
/// // (8) If term is a nostep function application with only nf children including a first one which is a constructor then this is a COOKIE!
/// // (9) If term is a non-nf data term with only nf children then mark it as nf.

#ifdef DEBUG
long computeCount = 0l;
#endif

void normalize(Context context, Term *termp)
{
    // Work term.
#ifdef DEBUG
    Term topTerm = *termp;
    DEBUGF(context, "//%*sNORMALIZING\n", stepNesting+1, "");
    DEBUGT(context, stepNesting+3, topTerm);
#endif

    if (IS_NF(*termp))
        return; // (0) if term is a variable or already in normal form then we are DONE.

    Term term = *termp; // Transfer link.
    *termp = NULL;

    ContextEntryStack stack = makeContextEntryStack(context);
    while (1)
    {
        if (IS_NF(term))
        {
            if (emptyContextEntryStack(stack)) // stack empty
            {
                // (1) If term is marked as nf (or a variable) with no parent (so at the root of the nf subterm) then we are DONE.
                freeContextEntryStack(stack);
                *termp = term; // updated term; reference transferred back.
                return;
            }
            else
            {
                // Store term in normal form back to parent.
                Term parent = LINK(context, (topContextEntry(stack)->term));
                int parentIndex = topContextEntry(stack)->index;
                //UNLINK(context, SUB(parent, parentIndex)); // The parent doesn't own the sub anymore
                SUB(parent, parentIndex) = term; // term reference transferred to subterm pointer

                const int parentArity = ARITY(parent);
                if (++parentIndex < parentArity)
                {
                    // (2) If term is marked as nf (or a variable) with a parent and a following sibling then advance to the following sibling.
                    topContextEntry(stack)->index = parentIndex;

                    //term = LINK(context, SUB(parent, parentIndex)); // Transfer sub reference to local term
                    term = SUB(parent, parentIndex); // Transfer sub reference to local term
                    SUB(parent, parentIndex) = NULL;  // Not strictly needed

                    UNLINK(context, parent);
                }
                else
                {
                    // (3) If term is marked as nf (or a variable) with a parent but no following sibling then pop to parent.
                    term = parent; // transfer parent reference to term
                    UNLINK(context, (topContextEntry(stack)->term));
                    popContextEntry(stack);
                    // TODO: swap the transfers to avoid extra UNLINK.

                    propagateFreeVariables(context, parent);
                }
            }
        }
        else if (IS_FUNCTION(term) && ! IS_NOSTEP(term))
        {
            Sink sink = ALLOCA_BUFFER(context);
            if (step(sink, term)) // Reference is transferred and consumed only when step succeeds
            {
                // (4) If term is a function invocation that is not marked as nostep and that we can in fact step then do so and update term to the result.
                term = BUFFER_TERM(sink); // Reference is transferred

                DEBUGENV("crsx-debug-steps", DEBUGT(sink->context, stepNesting+5, term));
                DEBUGENV("crsx-debug-steps", DEBUGF(sink->context, "//%*s========\n", stepNesting+3, ""));
            }
            else
            {
                // (5) If term is a function invocation that is not marked as nostep and that we can in fact not step then mark it as nostep.
                // (Note: failed step has not output anything to buffer)
                asConstruction(term)->nostep = 1;
            }
            FREE_BUFFER(sink); // always free buffer, even when not actually used!
        }
        else
        {
            int index = 0;
            const int arity = ARITY(term);
            while (index < arity && IS_NF(SUB(term, index)))
                ++index;
            if (index < arity)
            {
                // (6) If term is a non-nf data term or a nostep function application with a non-nf child then clear nostep if it is a function, push term, and switch to that child.
                if (IS_FUNCTION(term))
                    asConstruction(term)->nostep = 0;

                ContextEntry entry = {LINK(context, term), index};
                pushContextEntry(stack, entry);
                UNLINK(context, term); // TODO: really just reference transfer from term to stack

                term = SUB(term, index); // Transfer sub reference to local term
                //SUB(term, index) = NULL  // Not strictly needed
            }
            else
            {
                // (7) If term is a non-nf data term or a nostep function application with only nf children then mark it as nf.
                asConstruction(term)->nf = 1;
                CRSX_CHECK(context, term);
            }
        }
    }
}

Term force(Context context, Term term)
{
    ASSERT(context, term);
    //DEBUGF(context, "//%*sFORCE: %s\n", stepNesting, "", SYMBOL(term));
    if (!IS_NOSTEP(term))
    {
        while (IS_FUNCTION(term))
        {
            //CRSX_CHECK(context, term);
            Sink sink = ALLOCA_BUFFER(context);
            if (!step(sink, term)) // Reference is transferred and consume only when succeed
            {
                //DEBUGF(context, "//NF DATA %s\n", SYMBOL(term));
                asConstruction(term)->nostep = 1;
                break;
            }
            term =  BUFFER_TERM(sink); // reload and try again...
            DEBUGENV("crsx-debug-steps", DEBUGT(sink->context, stepNesting+5, term));
            DEBUGENV("crsx-debug-steps", DEBUGF(sink->context, "//%*s========\n", stepNesting+3, ""));
            FREE_BUFFER(sink);
        }
    }
    return term;
}

// Helper to focus steps in a single place.
static int step(Sink sink, Term term)
{
#   ifdef DEBUG
    long int count = ++stepCount;
#   endif    

    crsxpBeforeStep(sink->context, term);

    ///PRINTF(sink->context, "STEP<%s>\n", SYMBOL(term));
    DEBUGF(sink->context, "//%*sSTEP(%ld): %s[%d] (%ld,%ld) ============\n", ++stepNesting, "", count, SYMBOL(term), check(sink->context, term), allocateCount, freeCount);
    DEBUGENV("crsx-debug-steps", DEBUGT(sink->context, stepNesting+4, term));
    DEBUGENV("crsx-debug-steps", DEBUGF(sink->context, "//%*sSTEP-INTERNALS(%ld): (%ld,%ld)\n", stepNesting, "", count, allocateCount, freeCount));
    //DEBUGF(sink->context, "//%*sREDEX:", stepNesting, "");
    //DEBUGT(sink->context, stepNesting+3, term);

    int step = term->descriptor->step(sink, term);

    DEBUGF(sink->context, "//%*sSTEP-%s(%ld): (%ld,%ld) ==============\n", stepNesting--, "", (step ? "OK" : "FAIL"), count, allocateCount, freeCount);

    crsxpAfterStep(sink->context);

    return step;
}

void InitCRSXContext(Context context)
{
    context->stamp = 0;

    context->env = NULL;

    context->poolRefCount = 0;
    context->stringPool = NULL;
    context->keyPool = NULL;
    context->consPool = NULL;

    crsxAddPools(context);

    context->str_filelocation = GLOBAL(context, "$FileLocation");
    context->str_linelocation = GLOBAL(context, "$LineLocation");
    context->str_columnlocation = GLOBAL(context, "$ColumnLocation");

    context->fv_enabled = 1;

    context->noProperties = ALLOCATE(context, sizeof(struct _Properties));


#ifdef CRSX_ENABLE_PROFILING
    context->profiling = 0;
#endif
}

Term compute(Context context, Term term)
{
    crsxpInit(context);
    crsxAddPools(context);

    normalize(context, &term);

    crsxReleasePools(context);
    crsxpDestroy(context);

#ifdef DEBUG
    DEBUGF(context, "END COMPUTE");
    DEBUGT(context, 1, term);
#endif

    return term;
}


/////////////////////////////////////////////////////////////////////////////////
// Shallow Substitution
//

void call(Sink sink, Term term, SubstitutionFrame values)
{
    ASSERT(sink->context, !values->parent);

    const Context context = sink->context;
    crsxpBeforeCall(context);

    const int arity = term->descriptor->arity;

    if (term->nr > 1) // Shared?
    {
        sink->start(sink, term->descriptor);

        int bound;
        int i;
        for (i = 0; i < arity; ++i)
        {
            Term sub = SUB(term, i);
            bound = 0;
            if (IS_VARIABLE_USE(sub))
            {
                Variable v = VARIABLE(sub);
                int j;
                for (j = 0; j < values->count; ++j)
                {
                    if (v == values->variables[j])
                    {
                        COPY(sink, values->substitutes[j]); // Transfer ref.

                        values->substitutes[j] = NULL; // Not strictly needed.

                        bound = 1;
                        break;
                    }
                }
            }
            if (!bound)
                COPY(sink, LINK(context, sub));
        }

        if (values->renamings != RENAME_ALL)
            asConstruction(bufferTop((Buffer) sink))->nostep = 0;

        sink->end(sink, term->descriptor);
    }
    else
    {
        int i;
        for (i = 0; i < arity; ++i)
        {
            Term sub = SUB(term, i);
            if (IS_VARIABLE_USE(sub))
            {
                Variable v = VARIABLE(sub);
                int j;
                for (j = 0; j < values->count; ++j)
                {
                    if (v == values->variables[j])
                    {
                        UNLINK(sink->context, sub);

                        SUB(term, i) = values->substitutes[j]; // Transfer ref

                        values->variables[j] = NULL;   // Not strictly needed.
                        values->substitutes[j] = NULL; // Not strictly needed.
                        break;
                    }
                }
            }
        }

        if (values->renamings != RENAME_ALL)
            asConstruction(term)->nostep = 0;

        COPY(sink, term);
    }

    crsxpAfterCall(context);
}


/////////////////////////////////////////////////////////////////////////////////
// Substitution.
//
// Usage:
//   metaSubstitute(sink, term, substitution)
//   sink - where we send events to.
//   term - the redex fragment that we are copying.
//   substitution - captures the maps v1->Arg1,... derived from the fact that the pattern
//     had #Meta[ v1, ..., vN ] and the contraction had #Meta[ Arg1, ..., ArgN ].
//
// NOTE: DOES unlink (or transfer) both term and substitution (substitutes only).

// The helpers take three additional arguments:
//   weakened - the variables in the substitution that have been eliminated locally by weakenings.
//   exhausted - the variables in the substitution that are linear and have been globally eliminated.
//   renamings - map of binders introduced in redex to corresponding binders in result.
//
#if defined(DEBUG)
long metaSubstituteCount = 0l;
long metaSubstituteWork;
#endif


// Forward declarations
static void metaSubstituteTerm(Sink sink, Term term, SubstitutionFrame substitution, int substitutionCount, BitSetP unexhausted, BitSetP unweakened, long *metaSubstituteSizep);
static void metaSubstituteProperties(Sink sink, Construction construction, SubstitutionFrame substitution, int substitutionCount, BitSetP unexhaustedp, BitSetP unweakened, long *metaSubstituteSizep);
static void metaSubstituteTermUpdate(Context context, Term *termp, SubstitutionFrame substitution, VariableSetLink pendingWeakenings, int substitutionCount, BitSetP unexhaustedp, BitSetP unweakened, long *metaSubstituteSizep);

void metaSubstitute(Sink sink, Term term, SubstitutionFrame substitution)
{
    ASSERT(sink->context, term->nr > 0);
    assert(!substitution || (substitution && !substitution->parent));
    const int substitutionCount = (substitution ? substitution->parentCount + substitution->count : 0);

    // When all substituted variables are (statically) shallow and blocking
    // then use lightweight substitution.
    if (IS_CONSTRUCTION(term))
    {
        unsigned int lightweight = 1;
        int i;
        for (i = 0; i < substitutionCount; ++i)
            lightweight &= substitution->variables[i]->block & substitution->variables[i]->shallow;

        if (lightweight)
            return call(sink, term, substitution);
    }

    // Prepare helper bitsets.

    BitSet unexhausted; MAKE_SET_LBITS(sink->context, &unexhausted, substitutionCount);
    BitSet unweakened; MAKE_SET_LBITS(sink->context, &unweakened, substitutionCount);
    long metaSubstituteSize = 0l;

# ifdef DEBUG
    unsigned envsize = 0;
    long termMemuse = 0;
    int size = checkTerm3(sink->context, term, 0, 0,&envsize, &termMemuse);
    metaSubstituteWork = 0l;
# endif

    crsxpBeforeSubstitution(sink->context, term);

    metaSubstituteTermUpdate(sink->context, &term, substitution, NULL, substitutionCount, &unexhausted, &unweakened, &metaSubstituteSize);
    COPY(sink, term);

    CHECK_METASUBSTITUTE_SIZE(sink->context, metaSubstituteSize);

    FREE_LBITS(sink->context, unexhausted);
    FREE_LBITS(sink->context, unweakened);

    int i;
    for (i = 0; i < substitutionCount; ++i)
        UNLINK(sink->context, substitution->substitutes[i]);

    crsxpAfterSubstitution(sink->context);
}

/**
 * Substitute variable in term and output result to sink.
 *
 * sink - where we send events to
 * term - the redex fragment that we are copying. Reference is transfered.
 * substitution - substitutions to perform in the redex: maps variables to non-redex terms
 * substitutionCount - size of substitution (number of substituted variables)
 * unexhaustedp - (pointer to global) bits for all substituted variables, cleared after linear variables have been substituted
 * unweakened - bits for all substituted variables, cleared under weakening of the variable
 * metaSubstituteSizep - pointer to size counter for this substitution (TODO: omit when optimizing?)
 *
 **/
static
void metaSubstituteTerm(Sink sink, Term term, SubstitutionFrame substitution, int substitutionCount, BitSetP unexhausted, BitSetP unweakened, long *metaSubstituteSizep)
{
    ++(*metaSubstituteSizep);

    const Context context = sink->context;

    if (IS_VARIABLE_USE(term))
    {
        Variable v = VARIABLE(term);
        SubstitutionFrame s;
        for (s = substitution; s; s = s->parent)
        {
            const int offset = s->parentCount;
            int i;
            for (i = 0; i < s->count; ++i)
            {
                if (v == s->variables[i])
                {
                    // (1) Found variable to substitute.
                    Term t = LINK(context, s->substitutes[i]);
                    COPY(sink, t); // Transfer reference

                    if (IS_LINEAR(v))
                    {
                        // Substitutes is not needed anymore. Clear bit
                        CLEAR_LBIT(unexhausted, offset+i);
                    }

                    UNLINK(context, term);
                    return;
                }
            }
        }

        // (2) Variable that is not substituted: just echo.
        // TODO: why not COPY(sink, term)?

        USE(sink, linkVariable(context, v));
        UNLINK(context, term);
    }
    else // IS_CONSTRUCTION(term)
    {
        Construction construction = asConstruction(term);

        // Add weakenings.
        BitSet localUnweakened;
        COPY_LBITS(context, &localUnweakened, substitutionCount, unweakened);

        if (context->fv_enabled)
        {
            SubstitutionFrame s = substitution;
            for (; s; s = s->parent)
            {
                const int offset = s->parentCount;
                int i;
                for (i = 0; i < s->count; ++i)
                {
                    Variable v = s->variables[i];
                    if (!VARIABLESET_CONTAINS(construction->fvs, v) && !VARIABLESET_CONTAINS(construction->nfvs, v) && !VARIABLESET_CONTAINS(construction->vfvs, v))
                    {
                        // - Variable we are substituting is not in the free var set: remove from bitmap!
                        CLEAR_LBIT(&localUnweakened, offset+i);
                    }
                }
            }
        }

        BitSet bitset;
        COPY_LBITS(context, &bitset, substitutionCount, unexhausted);
        MASK_LBITS(&bitset, &localUnweakened);
        if (!ANY_LBITS(&bitset))
        {
            // (3) Term with no further substitution needed so revert to native copying
            FREE_LBITS(context, &bitset);
            COPY(sink, term); // Transfer reference
            return;
        }
        FREE_LBITS(context, &bitset);

        // Meta-substitute properties (shared by (4) and (5))).
        BitSet localUnweakenedC;
        COPY_LBITS(context, &localUnweakenedC, substitutionCount, &localUnweakened);
        metaSubstituteProperties(sink, construction, substitution, substitutionCount, unexhausted, &localUnweakenedC, metaSubstituteSizep);
        FREE_LBITS(context, &localUnweakenedC);

        if (IS_LITERAL(term))
        {
            // (4) Literal: just recreated, including any properties.
            Literal literal = asLiteral(term);
            LITERALU(sink, literal->text);
            UNLINK(context, term);
            return;
        }

        // (5) Generic construction: Fall back to proper substitution of generic reconstruction.

        // - Start tag.
        sink->start(sink, construction->term.descriptor);

        // - Contents.
        const int arity = construction->term.descriptor->arity;
        int i;
        for (i = 0; i < arity; ++i)
        {
            const int rank = RANK(term, i);
            if (rank == 0)
            {
                // --  i'th subterm with no binders: just continue copying.
                BitSet localUnweakenedC2;
                COPY_LBITS(context, &localUnweakenedC2, substitutionCount, &localUnweakened);
                metaSubstituteTerm(sink, LINK(context, SUB(term, i)), substitution, substitutionCount, unexhausted, &localUnweakenedC2, metaSubstituteSizep);
                FREE_LBITS(context, &localUnweakenedC2);
            }
            else
            {
                // -- i'th subterm with binders, second and following copy: add new binders to substitution!
                const int subSubstitutionCount = substitutionCount + rank; // new substitution size
                // --- allocate substitution
                Variable *oldBinders = BINDERS(term, i);
                Variable *subBinders = ALLOCATE(context, rank*sizeof(Variable)); // does not escapes (ALLOCA?)
                VariableUse subUses[rank]; // does not escape
                struct _SubstitutionFrame _subSubstitution = {substitution, substitutionCount, rank, oldBinders, (Term *) subUses, RENAME_ALL}; // does not escape
                SubstitutionFrame subSubstitution = &_subSubstitution;

                // --- allocate bitmaps
                BitSet subUnexhausted;
                COPY_LBITS(context, &subUnexhausted, subSubstitutionCount, unexhausted);
                BitSet subUnweakened;
                COPY_LBITS(context, &subUnweakened, subSubstitutionCount, &localUnweakened);

                // --- populate per binder
                int j;
                for (j = 0; j < rank; ++j)
                {
                    char *oldname = oldBinders[j]->name;
                    char *baseendp = strrchr(oldname, '_');
                    char *basename = oldname;
                    if (baseendp)
                    {
                        const int z = baseendp - oldname;
                        basename = ALLOCA(context, z+1); // does not escape
                        memcpy(basename, oldname, z);
                        basename[z] = '\0';
                    }
                    int isLinear = IS_LINEAR(oldBinders[j]);
                    subBinders[j] = makeVariable(context, oldBinders[j]->name, 1, isLinear, oldBinders[j]->block, oldBinders[j]->shallow); // escapes

                    subUses[j] = ALLOCATE(context, sizeof(struct _VariableUse)); // escapes
                    subUses[j]->term.descriptor = NULL;
                    subUses[j]->term.nr = 1;
                    subUses[j]->variable = linkVariable(context, subBinders[j]);

                    SET_LBIT(&subUnexhausted, substitutionCount + j);
                    SET_LBIT(&subUnweakened, substitutionCount + j);
                }
                // --- send new binders
                BINDS(sink, rank, subBinders); // escape of subBinders!
                FREE(context, subBinders);

                // --- now process subterm!
                metaSubstituteTerm(sink, LINK(context, SUB(term, i)), subSubstitution, subSubstitutionCount, &subUnexhausted, &subUnweakened, metaSubstituteSizep);

                // --- (end of scope for subSubstitution, subUnweakened, and subUses)
                MASK_LBITS(unexhausted, &subUnexhausted);

                FREE_LBITS(context, &subUnexhausted);
                FREE_LBITS(context, &subUnweakened);

                for (j = 0; j < rank; ++j)
                   unlinkTerm(context, (Term) subUses[j]);
            }
        }
        FREE_LBITS(context, &localUnweakened);

        // - End tag.
        sink->end(sink, construction->term.descriptor);

        UNLINK(context, term);
    }
}

// Stacks for links.
SETUP_STACK_TYPE(NamedPropertyLink)
SETUP_STACK_TYPE(VariablePropertyLink)

#ifdef DEBUG
    int copyDepth;
#endif

static void substitutePropertiesPrefix(Sink sink, Construction construction, SubstitutionFrame substitution, int substitutionCount, BitSetP unexhausted, BitSetP unweakened, long *metaSubstituteSizep,
        int mergeNamedProperties, int mergeVariableProperties, NamedPropertyLinkStack namedStack, NamedPropertyLink *namedLinkp, VariablePropertyLinkStack variableStack, VariablePropertyLink *variableLinkp)
{
    NamedPropertyLink namedLink;
    {
        // Collect prefix of named properties that need substitution.
        BitSet localUnweakened; COPY_LBITS(sink->context, &localUnweakened, substitutionCount, unweakened);
        for (namedLink = construction->properties->namedProperties; namedLink; namedLink = namedLink->link)
        {
            BitSet bitset; COPY_LBITS(sink->context, &bitset, substitutionCount, unexhausted);
            MASK_LBITS(&bitset, &localUnweakened);

            // if (IS_PROPERTY_CLOSED(namedLink) || (!ANY_LBITS(&bitset) && !mergeNamedProperties))
	        if (1) // namedLink isn't where we store free variable info anymore
            {
                // No more variables to metaSubstitute...the prefix is done!
                break;
            }

            ++(*metaSubstituteSizep);

#ifdef DEBUG
            ++copyDepth;
#endif
            // The new link, if any.
            const char *key = namedLink->name;
            if (key)
            {
                // - Regular key-value link is always inserted into prefix after substitution of value.
                Sink propertysink = ALLOCA_BUFFER(sink->context);

                BitSet localUnweakenedC; COPY_LBITS(sink->context, &localUnweakenedC, substitutionCount, &localUnweakened);
                metaSubstituteTerm(propertysink, LINK(sink->context, namedLink->u.term), substitution, substitutionCount, unexhausted, &localUnweakenedC, metaSubstituteSizep);
                Term term = BUFFER_TERM(propertysink); // Transfer reference
                FREE_BUFFER(propertysink);

                NamedPropertyLink newLink = ALLOCATE_NamedPropertyLink(sink->context, key, term, NULL);

                pushNamedPropertyLink(namedStack, newLink);
                ++(*metaSubstituteSizep);
            }
            else
            {
                // The sink automatically computes free variables
            }
        }
        FREE_LBITS(sink->context, &localUnweakened);
    }
    *namedLinkp = namedLink;

    VariablePropertyLink variableLink;
    {
        // Collect prefix of variable properties that need substitution.
        BitSet localUnweakened; COPY_LBITS(sink->context, &localUnweakened, substitutionCount, unweakened);
        for (variableLink = construction->properties->variableProperties; variableLink; variableLink = variableLink->link)
        {
            BitSet bitset; COPY_LBITS(sink->context, &bitset, substitutionCount, unexhausted);
            MASK_LBITS(&bitset, &localUnweakened);

            //if (!ANY_LBITS(AND_LBITS(*unexhaustedp, localUnweakened)) && !mergeVariableProperties)
            if (!ANY_LBITS(&bitset) && !mergeVariableProperties)
            {
                // No more variables to metaSubstitute...the prefix is done!
                break;
            }
#ifdef DEBUG
            ++copyDepth;
#endif
            Variable key = variableLink->variable;
            if (key)
            {
                // - The link denotes an actual variable-value mapping.
                SubstitutionFrame s;
                for (s = substitution; s; s = s->parent)
                {
                    int i;
                    for (i = 0; i < s->count; ++i)
                    {
                        if (key == s->variables[i]) // TODO: Check BIT?
                        {
                            Term keySubstitution = s->substitutes[i]; // Just peek... no need to link
                            if (keySubstitution && IS_VARIABLE_USE(keySubstitution))
                            {
                                // This is a substitution of a variable for a variable!
                                key = VARIABLE(keySubstitution);
                            }
                            goto VariableInsert;
                        }
                    }
                }
            VariableInsert:
                {
                    // Mapped variable not (further) renamed - include in prefix after value substitution.
                    VariablePropertyLink newLink = ALLOCATE(sink->context, sizeof(struct _VariablePropertyLink));
                    newLink->link = NULL;
                    newLink->variable = useVariable(sink->context, linkVariable(sink->context, key));
                    newLink->nr = 1;
                    Sink propertysink = ALLOCA_BUFFER(sink->context);
                    BitSet localUnweakenedC; COPY_LBITS(sink->context, &localUnweakenedC, substitutionCount, &localUnweakened);
                    metaSubstituteTerm(propertysink, LINK(sink->context, variableLink->u.term), substitution, substitutionCount, unexhausted, &localUnweakenedC, metaSubstituteSizep);
                    newLink->u.term = BUFFER_TERM(propertysink); // Transfer reference
                    FREE_BUFFER(propertysink);
                    pushVariablePropertyLink(variableStack, newLink);
                    ++(*metaSubstituteSizep);
                }
            }
            else
            {
                // TODO: clear bit
                // The sink automatically computes free variables
            }
        }
        FREE_LBITS(sink->context, &localUnweakened);
    }
    *variableLinkp = variableLink;
}

static void metaSubstituteProperties(Sink sink, Construction construction, SubstitutionFrame substitution, int substitutionCount, BitSetP unexhausted, BitSetP unweakened, long *metaSubstituteSizep)
{
    if (!construction->properties || (!construction->properties->namedProperties && !construction->properties->variableProperties))
        return; // no properties.

    int mergeNamedProperties = 0, mergeVariableProperties = 0;
    if (SINK_IS_BUFFER(sink))
    {
        mergeNamedProperties = (((Buffer) sink)->pendingNamedProperties != NULL);
        mergeVariableProperties = (((Buffer) sink)->pendingVariableProperties != NULL);
    }

#ifdef DEBUG
    copyDepth = 0;
#endif

    // For prefix of named properties with substitution applied to values.
    NamedPropertyLinkStack namedStack = makeNamedPropertyLinkStack(sink->context); // stack of properties to copy
    NamedPropertyLink namedLink; // the first link not copied (NULL if all)

    // For prefix of variable properties with substitution applied to keys and values.
    VariablePropertyLinkStack variableStack = makeVariablePropertyLinkStack(sink->context); // stack of properties to copy
    VariablePropertyLink variableLink; // the first link not copied (NULL if all)

    // Collect prefixes.
    substitutePropertiesPrefix(sink, construction, substitution, substitutionCount, unexhausted, unweakened, metaSubstituteSizep,
            mergeNamedProperties, mergeVariableProperties, namedStack, &namedLink, variableStack, &variableLink);

    if (namedLink || variableLink)
    {
        // We need to emit a new reference
        ADD_PROPERTIES(sink, NULL, NULL, LINK_NamedPropertyLink(sink->context, namedLink), LINK_VariablePropertyLink(sink->context, variableLink));
        ++(*metaSubstituteSizep);
    }

#ifdef DEBUG
    else if (copyDepth > 1000) // DEBUGGING
    {
        PRINTF(sink->context, "//DeepCopy");
        SubstitutionFrame s;
        int offset = 0;
        for (s = substitution; s; s = s->parent)
        {
            int i;
            for (i = 0; i < s->count; ++i)
                if (LBIT(unweakened, offset+i))
                {
                    PRINTF(sink->context, " %s", s->variables[i]->name);
                    break;
                }
        }
        PRINTF(sink->context, "\n");
    }
#endif

    // Now "play" prefix events (backwards!).
    while (!emptyNamedPropertyLinkStack(namedStack))
    {
        NamedPropertyLink link = *topNamedPropertyLink(namedStack);
        popNamedPropertyLink(namedStack);
        const char *key = link->name;
        if (key)
            ADD_PROPERTY_NAMED(sink, key, LINK(sink->context, link->u.term));

        UNLINK_NamedPropertyLink(sink->context, link);
    }
    freeNamedPropertyLinkStack(namedStack);

    while (!emptyVariablePropertyLinkStack(variableStack))
    {
        VariablePropertyLink link = *topVariablePropertyLink(variableStack);
        popVariablePropertyLink(variableStack);
        Variable key = link->variable;
        if (key)
            ADD_PROPERTY_VARIABLE(sink, linkVariable(sink->context, key), LINK(sink->context, link->u.term));

        UNLINK_VariablePropertyLink(sink->context, link);
    }
    freeVariablePropertyLinkStack(variableStack);
}

// Helper: metaSubstituteTermUpdate(context, term, substitution, unexhaustedp, unweakened, renamings, substituteUnexhaustedp, substituteUnweakened)
//   termp - pointer to the redex fragment that we are updating
//
static void metaSubstituteTermUpdate(Context context, Term *termp, SubstitutionFrame substitution, VariableSetLink pendingWeakenings, int substitutionCount, BitSetP unexhausted, BitSetP unweakened, long *metaSubstituteSizep)
{
    if (LINK_COUNT(*termp) > 1)
    {
        // Can't update term as it's shared. Fallback to copy-mode.
        Sink sink = ALLOCA_BUFFER(context);
        metaSubstituteTerm(sink, *termp, substitution, substitutionCount, unexhausted, unweakened, metaSubstituteSizep);
        *termp = BUFFER_TERM(sink);
        FREE_BUFFER(sink);
        return;
    }

    // Recursively update term.
    if (IS_VARIABLE_USE(*termp))
    {
        Variable v = VARIABLE(*termp);
        SubstitutionFrame s;
        for (s = substitution; s; s = s->parent)
        {
            const int offset = s->parentCount;
            int i;
            for (i = 0; i < s->count; ++i)
            {
                if (LBIT(unweakened, offset+i) && v == s->variables[i])
                {
                    // (1) Found variable to substitute.
                    // - it is already fully built so no substitution context necessary.
                    // - Free variables on the substitute terms will be propagated up either in buffer (for top-level variables)
                    //   or during construction update (see below)
                    UNLINK(context, *termp);
                    *termp = LINK(context, s->substitutes[i]);
                    if (IS_LINEAR(v))
                        CLEAR_LBIT(unexhausted, offset+i);
                    return;
                }
            }
        }
        // (2) Variable that is not substituted: leave it.
        return;
    }
    else // IS_CONSTRUCTION(term)
    {
        Construction construction = asConstruction(*termp);

        // - Clear bit set based on weakenings.
        BitSet localUnweakened; COPY_LBITS(context, &localUnweakened, substitutionCount, unweakened);

        if (context->fv_enabled)
        {
            SubstitutionFrame s;
            for (s = substitution; s; s = s->parent)
            {
                const int offset = s->parentCount;
                int i;
                for (i = 0; i < s->count; ++i)
                {
                    Variable v = s->variables[i];
                    if (!VARIABLESET_CONTAINS(construction->fvs, v) && !VARIABLESET_CONTAINS(construction->nfvs, v) && !VARIABLESET_CONTAINS(construction->vfvs, v))
                    {
                        // - Variable we are substituting is not in the free var list: remove from bitmap!
                        CLEAR_LBIT(&localUnweakened, offset+i);
                    }
                }
            }
        }

        BitSet bitset; COPY_LBITS(context, &bitset, substitutionCount, unexhausted);
           MASK_LBITS(&bitset, &localUnweakened);
        if (!ANY_LBITS(&bitset))
        {
            // (3) No further substitution is possible, so leave it.
            return;
        }

        // (4) Literal, and
        // (5) Generic construction: Generate copy of properties and then fall back to recursive substitution...

        // - create buffer.
        Sink sink = ALLOCA_BUFFER(context);
        // - copy all properties into buffer.
        BitSet localUnweakenedC; COPY_LBITS(context, &localUnweakenedC, substitutionCount, &localUnweakened);
        metaSubstituteProperties(sink, construction, substitution, substitutionCount, unexhausted, &localUnweakenedC, metaSubstituteSizep);

        // - destroy property pointers in term itself (as they are now in the buffer pending list)
        UNLINK_NamedPropertyLink(sink->context, construction->properties->namedProperties);
        construction->properties->namedProperties = NULL; // no need to check for noProperties
        UNLINK_VariablePropertyLink(sink->context, construction->properties->variableProperties);
        construction->properties->variableProperties = NULL; // no need to check for noProperties

        // TODO : Should some or all of these be unlinked?
        // UNLINK_VARIABLESET(context, construction->fvs);
        // construction->fvs = NULL;
        // UNLINK_VARIABLESET(context, construction->nfvs);
        // construction->nfvs = NULL;
        // UNLINK_VARIABLESET(context, construction->vfvs);
        // construction->nfvs = NULL;

        VariableSetLink localPendingWeakenings = NULL;

        // - send property-less term to buffer (note: no substitution, relies on bufferCopy reference semantics).
        COPY(sink, (Term) construction);  // Transfer reference (for term also)
        // - overwrite term with the new term with updated properties.
        *termp = BUFFER_TERM(sink);
        // - done with the buffer.
        FREE_BUFFER(sink);

        // - now fall back to update the term itself!
        const int arity = (*termp)->descriptor->arity;
        const Term term = *termp;
        int i;
        for (i = 0; i < arity; ++i)
        {
            const int rank = RANK(term, i);
            if (rank > 0)
            {
                // -- duplicate nested binders should be considered weakenings.
                BitSet localUnweakened2; COPY_LBITS(context, &localUnweakened2, substitutionCount, unweakened);

                SubstitutionFrame s;
                for (s = substitution; s; s = s->parent)
                {
                    const int offset = s->parentCount;
                    int ii;
                    for (ii = 0; ii < s->count; ++ii)
                    {
                        if (LBIT(&localUnweakened2, offset+ii))
                        {
                            int j;
                            for (j = 0; j < rank; ++j)
                                if (s->variables[ii] == BINDER(term, i, j))
                                {
                                    CLEAR_LBIT(&localUnweakened2, offset+ii);
                                    break;
                                }
                        }
                    }
                }
                metaSubstituteTermUpdate(context, &SUB(term, i), substitution, localPendingWeakenings, substitutionCount, unexhausted, &localUnweakened2, metaSubstituteSizep);
                FREE_LBITS(context, &localUnweakened2);
            }
            else
            {
                BitSet unweakenedC; COPY_LBITS(context, &unweakenedC, substitutionCount, unweakened);
                metaSubstituteTermUpdate(context, &SUB(term, i), substitution, localPendingWeakenings, substitutionCount, unexhausted, &unweakenedC, metaSubstituteSizep);
            }
        }

        // Update nf field of construction.
        int nf = IS_DATA(term) ? 1 : 0;
        for (i = 0; nf && i < arity; ++i)
            if (! IS_NF(SUB(term, i)))
                nf = 0;
        asConstruction(term)->nf = nf;

        asConstruction(term)->nostep = 0;

        // Propagate free variables up
        propagateFreeVariables(context, term);
    }
}


/////////////////////////////////////////////////////////////////////////////////
// Free variables

void propagateFreeVariables(Context context, Term term)
{
    if (!context->fv_enabled)
        return;

    CHECK_METASUBSTITUTE_SIZE(context, 0);

    // Compute union of child term free vars
    //
    const unsigned arity = ARITY(term);
    Construction c = asConstruction(term);
    if (arity == 0)
    {
        if (c->nfvs != c->properties->namedFreeVars)
        {
            UNLINK_VARIABLESET(context, c->nfvs);
            c->nfvs = LINK_VARIABLESET(context, c->properties->namedFreeVars);
        }
        if (c->vfvs != c->properties->variableFreeVars)
        {
            UNLINK_VARIABLESET(context, c->vfvs);
            c->vfvs = LINK_VARIABLESET(context, c->properties->variableFreeVars);
        }
    }
    else
    {
        // General case: merge.
        // TODO: might be worthwhile to make a first pass to identify if only 1 sub has free variables.

        UNLINK_VARIABLESET(context, c->fvs);
        UNLINK_VARIABLESET(context, c->nfvs);
        UNLINK_VARIABLESET(context, c->vfvs);

        VARIABLESET fvs = NULL;
        VARIABLESET nfvs = LINK_VARIABLESET(context, c->properties->namedFreeVars);
        VARIABLESET vfvs = LINK_VARIABLESET(context, c->properties->variableFreeVars);

        int i = arity - 1;
        for (; i >= 0; --i)
        {
            Term sub = SUB(term, i);

            fvs = VARIABLESET_MERGEALL(context, fvs, freeVars(context, sub));

            if (IS_CONSTRUCTION(sub))
            {
                Construction sc = asConstruction(sub);

                nfvs = VARIABLESET_MERGEALL(context, nfvs, LINK_VARIABLESET(context, sc->nfvs));
                vfvs = VARIABLESET_MERGEALL(context, vfvs, LINK_VARIABLESET(context, sc->vfvs));
            }

            // Remove binders.
            unsigned rank = RANK(term, i);
            if (rank > 0)
            {
                fvs = VARIABLESET_REMOVEALL(context, fvs, BINDERS(term, i), rank);
                //nfvs = VARIABLESET_REMOVEALL(context, nfvs,  BINDERS(term, i), rank);
                //vfvs = VARIABLESET_REMOVEALL(context, vfvs, BINDERS(term, i), rank);
            }

        }

        if (VARIABLESET_ISEMPTY(fvs))
        {
            UNLINK_VARIABLESET(context, fvs);
            c->fvs = NULL;
        }
        else
            c->fvs = fvs;

        if (VARIABLESET_ISEMPTY(nfvs))
        {
            UNLINK_VARIABLESET(context, nfvs);
            c->nfvs = NULL;
        }
        else
            c->nfvs = nfvs;

        if (VARIABLESET_ISEMPTY(vfvs))
        {
            UNLINK_VARIABLESET(context, vfvs);
            c->vfvs = NULL;
        }
        else
            c->vfvs = vfvs;
    }
}


// Helper adding modified location properties.
void passLocationProperties(Context context, Term locTerm, Term term)
{
    if (IS_CONSTRUCTION(locTerm) && IS_CONSTRUCTION(term))
    {
        Construction construction = asConstruction(term);
        Construction locConstruction = asConstruction(locTerm);
        char *list[] = {context->str_filelocation,
                        context->str_linelocation,
                        context->str_columnlocation};
        int i;
        for (i = 0; i < 3; ++i)
        {
            char *key = list[i];
            Term value = NAMED_PROPERTY(context, construction, key);
            if (value)
            {
                Term locvalue = NAMED_PROPERTY(context, locConstruction, key);
                if (locvalue && strcmp(SYMBOL(value), SYMBOL(locvalue)))
                {
                    //VARIABLESET fvs = construction->properties->namedFreeVars;

                    // Location has been changed...update.
                    NamedPropertyLink link = ALLOCATE_NamedPropertyLink(context, GLOBAL(context, key), LINK(context, locvalue), construction->properties->namedProperties);

                    construction->properties = setNamedProperties(context, construction->properties, link);

                    //if (fvs)
                      //  construction->properties = setNamedFreeVars(context, construction->properties, LINK_VARIABLESET(context, fvs));
                }
            }
        }
    }

    UNLINK(context, locTerm);
}

/////////////////////////////////////////////////////////////////////////////////
// Memory management.


inline Term linkTerm(Context context, Term t)
{
    assert(t->nr > 0);
    ++LINK_COUNT(t);
    return t;
}


void freeTerm(Context context, Term term)
{
    if (IS_VARIABLE_USE(term))
    {
        Variable var = VARIABLE(term);
        unuseVariable(context, var);
        unlinkVariable(context, var);
        FREE(context, term);
    }
    else
    {
        Construction construction = asConstruction(term);
        freeConstruction(context, construction);
    }
}

void unlinkValueTerm(Context context, const void* key, void* value)
{
    unlinkTerm(context, (Term) value);
}

static void addAllTerms(Context context, Hashset2 to_set, Hashset2 from_set)
{
    int i;
    for (i = 0 ; i < from_set->nslots ; i++)
    {
        LinkedList2 slot = from_set->entries[i];
        while (slot)
        {
            if (!containsHS2(to_set, slot->key))
            {
                Term term = LINK(context, (Term) slot->value);
                ASSERT(context, term);
                addValueHS2(context, to_set, slot->key, term);
            }
            slot = slot->next;
        }
    }
}

NamedPropertyLink ALLOCATE_NamedPropertyLink(Context context, const char *name, Term term, NamedPropertyLink nlink)
{
    int count = nlink ? nlink->count + 1 : 1;
    if (count >= 100)
    {
        ASSERT(context, term);

        // Make a hashset
        Hashset2 set = makeHS2(context, 8, unlinkValueTerm, equalsChars, hashChars);
        
        // 0th = new, 1st = nlink, 2nd..count-1 = older
        addValueHS2(context, set, name, term); // Transfer term ref.

        // Copy initialize old elements
        NamedPropertyLink old_link = nlink;
        while (old_link)
        {
            if (old_link->name)
            {
                if (!containsHS2(set, (const void*) old_link->name)) // make sure not to override newer entries
                {
                    Term term = LINK(context, old_link->u.term);
                    ASSERT(context, term);
                    addValueHS2(context, set, old_link->name, term);
                }
            }
            else
            {
                Hashset2 old_set = old_link->u.propset;
                addAllTerms(context, set, old_set);
            }

            // move on
            old_link = old_link->link;
        }

        UNLINK_NamedPropertyLink(context, nlink);

        // Just allocate one link for the whole hashset
        NamedPropertyLink link = ALLOCATE(context, sizeof(struct _NamedPropertyLink));
        link->link = NULL; // sets always include everything else
        link->name = NULL; // NULL means it is a hashset
        link->u.propset = set;
#ifdef CRSX_ENABLE_PROFILING
        link->marker = 0;
#endif
        link->count = 0; // count==0 means group-allocation (hashset)
        link->nr = 1;
        return link;
    }
    else
    {
        // Just allocate one, link it, and return it
        NamedPropertyLink link = ALLOCATE(context, sizeof(struct _NamedPropertyLink));
        link->link = nlink;
        link->name = name;
        link->u.term = term; // Transfer ref
#ifdef CRSX_ENABLE_PROFILING
        link->marker = 0;
#endif
        link->count = nlink ? nlink->count + 1 : 1;
        link->nr = 1;
        return link;
    }
}

inline NamedPropertyLink LINK_NamedPropertyLink(Context context, NamedPropertyLink link)
{
    if (link)
        link->nr++;

    return link;
}

NamedPropertyLink UNLINK_NamedPropertyLink(Context context, NamedPropertyLink link)
{
    if (!link)
        return NULL;
    ASSERT(context, link->nr >0);

    if (--link->nr > 0)
        return link;

    // Free.
    if (link->name)
    {
        UNLINK(context, link->u.term);
    }
    else
    {
        unlinkHS2(context, link->u.propset);
        link->u.propset = NULL;
    }

    NamedPropertyLink next = link->link;

    link->link = NULL;
    link->name = NULL; // No need to free name as it is stored in the pool.
    FREE(context, link);
    return UNLINK_NamedPropertyLink(context, next); // tail.
}

VariablePropertyLink UNLINK_VariablePropertyLink(Context context, VariablePropertyLink link)
{
    if (link)
    {
        ASSERT(context, link->nr >0);
        if (--link->nr == 0)
        {
            if (link->variable)
            {
                unuseVariable(context, link->variable);
                UNLINK(context, link->u.term);
            }
            else
            {
                unlinkHS2(context, link->u.propset);
                link->u.propset = NULL;
            }

            UNLINK_VariablePropertyLink(context, link->link);
            link->link = NULL;
            FREE(context, link);
            return NULL;
        }
    }
    return link;
}

Properties allocateProperties(Context context, VARIABLESET namedFreeVars, VARIABLESET variableFreeVars,
                                 NamedPropertyLink namedProperties, VariablePropertyLink variableProperties)
{
    //if (!namedFreeVars && !variableFreeVars && !namedProperties && !variableProperties)
     //   return context->noProperties;

    Properties env = ALLOCATE(context, sizeof(struct _Properties));
    env->nr = 1;
    env->namedFreeVars = namedFreeVars;
    env->variableFreeVars = variableFreeVars;
    env->namedProperties = namedProperties;
    env->variableProperties = variableProperties;
    ASSERT_VARIABLE_PROPERTIES(context, env);
    return env;
}

Properties setProperties(Context context, Properties props, NamedPropertyLink namedProperties, VariablePropertyLink variableProperties)
{
    if (context->noProperties == props)
        return allocateProperties(context, NULL, NULL, namedProperties, variableProperties);

    props->namedProperties = namedProperties;
    props->variableProperties = variableProperties;
    return props;
}

Properties setNamedFreeVars(Context context, Properties props, VARIABLESET namedFreeVars)
{
    if (context->noProperties == props)
        return allocateProperties(context, namedFreeVars, NULL, NULL, NULL);

    props->namedFreeVars = namedFreeVars;
    return props;
}

Properties setVariableFreeVars(Context context, Properties props, VARIABLESET variableFreeVars)
{
    if (context->noProperties == props)
        return allocateProperties(context, NULL, variableFreeVars, NULL, NULL);

    props->variableFreeVars = variableFreeVars;
    return props;
}

Properties setNamedProperties(Context context, Properties props, NamedPropertyLink namedProperties)
{
    if (context->noProperties == props)
        return allocateProperties(context, NULL, NULL, namedProperties, NULL);

    props->namedProperties = namedProperties;
    return props;
}

Properties setVariableProperties(Context context, Properties props,
        VariablePropertyLink variableProperties)
{
    if (context->noProperties == props)
        return allocateProperties(context, NULL, NULL, NULL, NULL);

    props->variableProperties = variableProperties;
    return props;
}

inline Properties linkProperties(Context context, Properties env)
{
    env->nr++;
    return env;
}

Properties unlinkProperties(Context context, Properties env)
{
    if (env && env != context->noProperties)
    {
        ASSERT(context, env->nr > 0);

        if (--env->nr == 0)
        {
            UNLINK_VARIABLESET(context, env->namedFreeVars);
            env->namedFreeVars = NULL;

            UNLINK_VARIABLESET(context, env->variableFreeVars);
            env->variableFreeVars = NULL;

            UNLINK_NamedPropertyLink(context, env->namedProperties);
            env->namedProperties = NULL;

            UNLINK_VariablePropertyLink(context, env->variableProperties);
            env->variableProperties = NULL;

            FREE(context, env);
            return NULL;
        }
    }
    return env;
}
//
static void computeFreeVariables2(VariableSet freevars, Term term, VariableSetLink boundLink)
{
    if (IS_VARIABLE_USE(term))
    {
        Variable v = VARIABLE(term);
        if (!variableSetLinkFor(boundLink, v) && !containsVariable(freevars, v))
            addVariable(freevars, v);
    }
    else
    {
        const int arity = ARITY(term);
        int i = 0;
        for (; i < arity; ++i)
        {
            VariableSetLink subBoundLink = boundLink;
            int j, rank = RANK(term, i);
            for (j = 0; j < rank; ++j)
            {
                VariableSetLink newLink = ALLOCATE(freevars->context, sizeof(struct _VariableSetLink));
                newLink->nr = 1;
                newLink->variable = BINDER(term, i, j);
                newLink->link = subBoundLink;
                subBoundLink = newLink;
            }
            computeFreeVariables2(freevars, SUB(term, i), subBoundLink);
            for (j = 0; j < rank; ++j)
            {
                VariableSetLink oldLink = subBoundLink;
                subBoundLink = subBoundLink->link;
                FREE(freevars->context, oldLink);
            }
        }
        {
            NamedPropertyLink nlink = asConstruction(term)->properties->namedProperties;
            for (; nlink; nlink = nlink->link)
                if (nlink->name)
                    computeFreeVariables2(freevars, nlink->u.term, boundLink);
        }
        {
            VariablePropertyLink vlink = asConstruction(term)->properties->variableProperties;
            for (; vlink; vlink = vlink->link)
                if (vlink->variable)
                    computeFreeVariables2(freevars, vlink->u.term, boundLink);
        }
    }
}

//
static VariableSet computeFreeVariables(Context context, Term term)
{
    VariableSet free = makeVariableSet(context);
    computeFreeVariables2(free, term, (VariableSetLink) NULL);
    UNLINK(free->context, term);
    return free;
}

void checkFreeVariables(Context context, Term term)
{
    VARIABLESET set = freeVars(context, term);
    if (!IS_VARIABLE_USE(term))
    {
        Construction c = asConstruction(term);
        set = VARIABLESET_MERGEALL(context, set, LINK_VARIABLESET(context, c->nfvs));
        set = VARIABLESET_MERGEALL(context, set, LINK_VARIABLESET(context, c->vfvs));
    }


    VariableSet set2 = computeFreeVariables(context, term);

    VariableSetLink link = set2->u.link;
    int count = 0;
    for (; link; link = link->link)
    {
        if (!VARIABLESET_CONTAINS(set, link->variable))
            printf("Missing free variable: %s\n", link->variable->name);

        count ++;
    }

    int count1 = VARIABLESET_COUNT(set);

    if (count1 > count)
        printf("Too many free variables: %d\n", (count1 - count));
}

/////////////////////////////////////////////////////////////////////////////////
// Free variables and variable set manipulations.

VariableSet makeFreeVariableSet(Context context, Term term, SortDescriptor sort, int constrained, VariablePropertyLink props)
{
    VariableSet free = NULL;

    if (IS_VARIABLE_USE(term))
    {
        free = makeVariableSet(context);
        free->addVariable(free, linkVariable(context, VARIABLE(term)));
    }
    else
    {
        Construction c = asConstruction(term);

        if (c->fvs != AllFreeVariables && c->nfvs != AllFreeVariables && c->vfvs != AllFreeVariables)
        {
            free = makeVariableSet(context);
            VARIABLESET_ADDVARIABLESOF(context, free, c->fvs, constrained, props);
            VARIABLESET_ADDVARIABLESOF(context, free, c->nfvs, constrained, props);
            VARIABLESET_ADDVARIABLESOF(context, free, c->vfvs, constrained, props);
        }
        else
            return computeFreeVariables(context, term); // Transfer ref
    }

    UNLINK(context, term);

    return free;
}

/////////////////////////////////////////////////////////////////////////////////
// Deep Equality.

static int deepEqual2(Context context, Term term1, Term term2, int compenv, VariableMap map);

int deepEqual(Context context, Term term1, Term term2, int compenv)
{
    VariableMap map = makeVariableMap(context);
    int result = deepEqual2(context, term1, term2, compenv, map);
    freeVariableMap(map);
    return result;
}

static int deepEqual2(Context context, Term term1, Term term2, int compenv, VariableMap map)
{
    if (IS_VARIABLE_USE(term1))
    {
        if (!IS_VARIABLE_USE(term2)) return 0;
        Variable v1 = VARIABLE(term1);
        Variable v2 = VARIABLE(term2);
        Variable v1image = lookupVariable(map, v1);
        return v1image ? (v1image == v2) : (v1 == v2);
    }
    if (IS_VARIABLE_USE(term2)) return 0;

    if (compenv)
    {
        // Construction or literal: check properties.
        Construction construction1 = asConstruction(term1), construction2 = asConstruction(term2);
        {
            NamedPropertyLink link1, link2;
            for (link1 = construction1->properties->namedProperties; link1; link1 = link1->link)
            {
                const char *name = link1->name;
                if (! name) continue; // skip free vars
                Term value1 = link1->u.term;
                Term value2 = NULL;
                for (link2 = construction2->properties->namedProperties; link2; link2 = link2->link)
                {
                    if (! link2->name) continue; // skip free vars
                    if (!strcmp(name, link2->name))
                    {
                        value2 = link2->u.term;
                        break;
                    }
                }
                if (!value2) return 0;
                if (!deepEqual2(context, value1, value2, compenv, map)) return 0;
            }
            for (link2 = construction2->properties->namedProperties; link2; link2 = link2->link)
            {
                const char *name = link2->name;
                if (! name) continue; // skip free vars
                Term value1 = NULL;
                for (link1 = construction1->properties->namedProperties; link1; link1 = link1->link)
                {
                    if (! link1->name) continue; // skip free vars
                    if (!strcmp(name, link1->name))
                    {
                        value1 = link1->u.term;
                        break;
                    }
                }
                if (!value1) return 0; // just check that all keys are present
            }
        }
        {
            VariablePropertyLink link1, link2;
            for (link1 = construction1->properties->variableProperties; link1; link1 = link1->link)
            {
                Variable variable = link1->variable; // variable to look for
                Term value1 = link1->u.term;
                Variable v = lookupVariable(map, variable);
                if (v) variable = v;
                Term value2 = NULL;
                for (link2 = construction2->properties->variableProperties; link2; link2 = link2->link)
                {
                    if (variable == link2->variable)
                    {
                        value2 = link2->u.term;
                        break;
                    }
                }
                if (!value2) return 0;
                if (!deepEqual2(context, value1, value2, compenv, map)) return 0;
            }
            for (link2 = construction2->properties->variableProperties; link2; link2 = link2->link)
            {
                Variable variable = link2->variable;
                Term value1 = NULL;
                for (link1 = construction1->properties->variableProperties; link1; link1 = link1->link)
                {
                    Variable v1 = link1->variable;
                    Variable v = lookupVariable(map, v1);
                    if (variable == (v ? v : v1))
                    {
                        value1 = link1->u.term;
                        break;
                    }
                }
                if (!value1) return 0; // just check that all keys are present
            }
        }
    }
    if (IS_LITERAL(term1))
    {
        return IS_LITERAL(term2) && !strcmp(asLiteral(term1)->text, asLiteral(term2)->text);
    }
    if (IS_LITERAL(term2)) return 0;
    // Both are proper constructions.

    if (strcmp(SYMBOL(term1), SYMBOL(term2)))
        return 0;

    const int arity = ARITY(term1);
    if (arity != ARITY(term2)) return 0;
    int i;
    for (i = 0; i < arity; ++i)
    {
        const int rank = RANK(term1,i);
        if (rank != RANK(term2,i)) return 0;
        int j;
        for (j = 0; j < rank; ++j)
            addVariableMap(map, BINDER(term1, i, j), BINDER(term2, i, j));
        if (!deepEqual2(context, SUB(term1, i), SUB(term2, i), compenv, map)) return 0;
        popVariableMap(map, rank);
    }
    return 1;
}

/////////////////////////////////////////////////////////////////////////////////
// Constructor search helpers.

// Helper to search the sorted table of symbol descriptors.
ConstructionDescriptor lookupSymbolTableDescriptor(Context context, SymbolDescriptor table, size_t size, const char *symbol)
{
    size_t lo = 0, hi = size-1, x;
    while (hi >= lo)
    {
        x = (lo+hi)/2;
        ASSERT(context, lo <= x && x <= hi && strcmp(table[lo].symbol, symbol) <= 0 && strcmp(symbol, table[hi].symbol) <= 0);
        SymbolDescriptor candidate = &table[x];
        int compare = strcmp(symbol, candidate->symbol);
        if (compare == 0)
            return candidate->descriptor;
        if (compare < 0) // what we are searching for is in lower half
            hi = x-1;
        else //if (compare > 0) // what we are searching for is in upper half
            lo = x+1;
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////////
// I/O of terms.  (Loading is defined in crsx_scan.l)

FILE* fopen_in(Context context, char *name)
{
    int l = strlen(name);
    char *mangled = ALLOCATE(context, l+1);
    mangle(l, name, mangled);
    return fopen(mangled, "r");
}

FILE* fopen_out(Context context, char *name)
{
    int l = strlen(name);
    char *mangled = ALLOCATE(context, l+1);
    mangle(l, name, mangled);
    return fopen(mangled, "w");
}

int saveTerm(Context context, char *name, Term term)
{
    FILE *fp = FOPEN_OUT(context, name);
    if (!fp)
    {
        ERRORF(context, Crsx, "Cannot write file %s (%s)\n", name, strerror(errno));
        return 0;
    }
    fprintTerm(context, fp, term);
    fclose(fp);
    return 1;
}

// Printing.
NamedPropertyLink printCookieNameList = NULL;

static void fprintCookies(Context context)
{
    if (printCookieNameList)
    {
        PRINTF(context, "//Cookies found:\n");
        NamedPropertyLink cookie = printCookieNameList;
        for (; cookie; cookie = cookie->link)
        {
            PRINTF(context, "//   %s\n", cookie->name);
        }
    }
}

void fprintTerm(Context context, FILE* out, Term term)
{
    Hashset2 used = NULL;
    if (getenv("CANONICAL_VARIABLES"))
        used = makeHS2(context, 10, NULL, equalsPtr, hashPtr);

    VariableSet set = makeVariableSet(context);
    int pos = 0;
    fprintTermTop(context, out, term, -1, set, used, 1, &pos, getenv("omit-properties") ? 0 : 10, 0);
    FPRINTF(context, out, "\n");
    freeVariableSet(set);

    if (getenv("CANONICAL_VARIABLES"))
        unlinkHS2(context, used);
}

void printTerm(Context context, Term term)
{
    printCookieNameList = NULL;
    fprintTerm(context, STDOUT, term);
    fprintCookies(context);
}

void ppt(Context context, int nesting, Term term)
{
    printCookieNameList = NULL;
    VariableSet set = makeVariableSet(context);
    char *depthString = getenv("depth");
    int depth = depthString ? atoi(depthString) : 10;
    int pos = 0;
    fprintTermTop(context, STDOUT, term, depth, set, NULL, nesting, &pos, getenv("omit-properties") ? 0 : 10, getenv("include-variable-annotations") ? 1 : 0);
    PRINTF(context, "\n");
    freeVariableSet(set);
    fprintCookies(context);
}

void pt(Context context, Term term)
{
    ppt(context, 2, term);
}

void pwt(Context context, Term term)
{
    printCookieNameList = NULL;
    VariableSet set = makeVariableSet(context);
    char *depthString = getenv("depth");
    int depth = depthString ? atoi(depthString) : 10;
    int pos = 0;
    fprintTermTop(context, STDOUT, term, depth, set, NULL, 0, &pos, getenv("omit-properties") ? 0 : 10, 1);
    PRINTF(context, "\n");
    freeVariableSet(set);
    fprintCookies(context);
}

char *SPACES = "                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    ";

void fprintTermWithIndent(Context context, FILE* out, Term term)
{
    printCookieNameList = NULL;

    Hashset2 used = NULL;
    if (getenv("CANONICAL_VARIABLES"))
        used = makeHS2(context, 10, NULL, equalsPtr, hashPtr);

    VariableSet set = makeVariableSet(context);
    int pos = 0;
    fprintTermTop(context, out, term, INT16_MAX, set, used, 1, &pos, getenv("omit-properties") ? 0 : 10, 0);
    FPRINTF(context, out, "\n");
    freeVariableSet(set);
    fprintCookies(context);

    if (getenv("CANONICAL_VARIABLES"))
        unlinkHS2(context, used);
}

void printTermWithIndent(Context context, Term term)
{
    fprintTermWithIndent(context,STDOUT,term);
}

void printTermFullWithIndent(Context context,  Term term)
{
    printCookieNameList = NULL;

    Hashset2 used = NULL;
    if (getenv("CANONICAL_VARIABLES"))
        used = makeHS2(context, 10, NULL, equalsPtr, hashPtr);

    VariableSet set = makeVariableSet(context);
    int pos = 0;
    fprintTermTop(context, STDOUT, term, INT32_MAX, set, used, 1, &pos, INT32_MAX, 0);
    FPRINTF(context, STDOUT, "\n");
    freeVariableSet(set);
    fprintCookies(context);

    if (getenv("CANONICAL_VARIABLES"))
        unlinkHS2(context, used);
}


// Determines if the specified 8-bit character is an ISO control character. A character is considered to be an ISO
// control character if its code is in the range '\u0000' through '\u001F' or in the range '\u007F' through '\u009F'.
//
// Parameters:
//     ch - the character to be tested.
// Returns:
//     true if the character is an ISO control character; false otherwise.
int isISOControl(unsigned char c)
{
    return (c > 0x00 && c <= 0x1F) || (c >= 0x7F && c <= 0x9F);
}

// Print literal so it is reparseable.
// Literals are in double quotes.
int fprintLiteral(Context context, FILE* out, const char* literal)
{
    size_t literal_length = strlen(literal);
    size_t tmp_length = literal_length*10+3; // enough space even if all are quotes!
    char *tmp = ALLOCA(context, tmp_length+1);
    char *s = (char*)literal;
    char *t = tmp;
    *(t++) = '"';
    escape(&s, &t, ((char*)literal)+literal_length, tmp+tmp_length-2);
    *(t++) = '"';
    *(t++) = '\0';
    return FPRINTF(context, out, "%s", tmp);
}

// Print variable in form that can be safely reparsed.
int fprintVariable(Context context, FILE* out, Variable x)
{
    if (!x || !x->name || !x->name[0])
    {
        FPRINTF(context, out, "_");
        return 1;
    }
    char* name = x->name;
    int rawsafe = 1;
    unsigned char c = name[0];
    if (islower(c))
    {
        int i;
        for (i = 1; (c = name[i]); ++i)
        {
            switch (c)
            {
            case '_': case '-': case '$':
                break;
            default:
                if (!isalnum(c))
                    rawsafe = 0;
            }
        }
    }
    else
    {
        rawsafe = 0;
    }
    //
    int z = 0;
    if (rawsafe)
        z += FPRINTF(context, out, "%s", name);
        //z += FPRINTF(context, out, "%s%s", name, (IS_LINEAR(x) ? "\302\271" : ""));
    else
    {
        z += FPRINTF(context, out, "v'");
        int i;
        for (i = 0; (c = name[i]); ++i)
        {
            if ((isgraph(c) || c == ' ') && c != '\'')
                z += FPRINTF(context, out, "%c", c);
            else
                z += FPRINTF(context, out, "\\%03o", (int)c);
        }
        //z += FPRINTF(context, out, "%s'", (IS_LINEAR(x) ? "\302\271" : ""));
        z += FPRINTF(context, out, "'");
    }
    return z;
}


// Print canonical variable name
static
int fprintSafeVariableName(Context context, FILE* out, Variable v, Hashset2 used)
{
    if (!used)
        return fprintVariable(context, out, v);

    char* n = (char*) getValueHS2(used, (const void*) v);
    if (n == NULL)
    {
        // Search for [-_0-9] and retain prefix.
        n  = strdup(v->name);
        char* p = n;
        while ((*p) != '\0' && (*p) != '-' && (*p) != '_' && ((*p) < '0' || (*p) > '9'))
            p++;
        (*p) = '\0';

        // Generate char* for used size
        char* index = (char*) malloc(21); // size_t max digits = 20.
        snprintf(index, 21, "%zu", used->size + 1);
        size_t indexlen = strlen(index);

        // Replace all __V[0-9]+ by ""
        size_t len = strlen(n);
        char* replaced = (char*) malloc(len + indexlen + 3 + 1); // allocate enough for __Vindex

        char* pr = replaced;
        p = n;
        while ((*p) != '\0')
        {
            // Matches __V[0-9]?
            if ((*p) == '_' && (*(p + 1)) == '_' && (*(p + 2)) == 'V' && (*(p + 3)) >= '0' && (*(p + 3)) <= '9')
            {
                p += 4;
                // Skip trailing digits
                while ((*p) >= '0' && (*p) <= '9')
                  p ++;

                // (*p) is not a digit.
            }
            else
            {
                (*pr) = (*p);
                p ++;
                pr ++;
            }
        }
        (*pr) = '\0';

        // We don't need n anymore
        free(n);
        n = replaced;

        // Check the variable matches [a-z][A-Za-z0-9]*.
        int match = 1;
        if (n[0] == '\0'|| n[0] < 'a' || n[0] > 'z')
            match = 0;
        else
        {
            p = n + 1;
            while ((*p) != '\0' && (((*p) >= 'A' && (*p) <= 'Z') || ((*p) >= 'a' && (*p) <= 'z') || ((*p) >= '0' && (*p) <= '9')))
                p ++;

            match = (*p) == '\0';
        }
        if (!match)
            strcpy(n, "v");

        // Concat __V[index]
        strcat(n, "__V");
        strcat(n, index);
        free(index);

        addValueHS2(context, used, (const void*) v, (void*) n);
    }
    return FPRINTF(context, out, "%s", n);
}


// Print constructor in form that can be safely reparsed.
// Constructors use single quotes unless safe to dump directly.
int fprintConstructor(Context context, FILE* out, char* name)
{
    if (!name || !name[0])
    {
        FPRINTF(context, out, "''");
        return 2;
    }
    int rawsafe = 1;
    unsigned char c = name[0];
    if (isupper(c))
    {
        int i;
        for (i = 1; (c = name[i]); ++i)
        {
            switch (c)
            {
            case '_': case '-': case '$':
                break;
            default:
                if (!isalnum(c))
                    rawsafe = 0;
            }
        }
    }
    else if (isdigit(c))
    {
        int i;
        for (i = 1; (c = name[i]); ++i)
        {
            switch (c)
            {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
            case '.': case 'E': case 'e':
                break;
            default:
                rawsafe = 0;
            }
        }
    }
    else
    {
        rawsafe = 0;
    }
    //
    int z = 0;
    if (rawsafe)
        z += FPRINTF(context, out, "%s", name);
    else
    {
        z += FPRINTF(context, out, "'");
        int i;
        for (i = 0; (c = name[i]); ++i)
        {
            if ((isgraph(c) || c == ' ') && c != '\'')
                z += FPRINTF(context, out, "%c", c);
            else
                z += FPRINTF(context, out, "\\%03o", (int)c);
        }
        z += FPRINTF(context, out, "'");
    }
    return z;
}

void fprintTermTop(Context context, FILE* out, Term term, int depth, VariableSet encountered, Hashset2 used, int indent, int *posp, int includeprops, int debug)
{
    if (*posp < indent && depth > 1)
        *posp += FPRINTF(context, out, "%.*s", indent - *posp, SPACES);

    if(term != 0 && term->descriptor != 0){
        char* nm = term->descriptor->name(term);
        if(nm != 0){
            if(strcmp(nm, "GlobalConstant") == 0){
                depth = -1;
            }
        }
    }

    if (term == NULL)
    {
        *posp += FPRINTF(context, out, "_");
    }
    else if (depth == 0)
    {
        *posp += FPRINTF(context, out, "...");
    }
    else if (IS_VARIABLE_USE(term))
    {
        Variable x = VARIABLE(term);
        *posp += fprintSafeVariableName(context, out, x, used);
    }
    else
    {
        char* nm = term->descriptor->name(term);
        if (!strncmp("$Text", nm, 5))
        {
            // We have a text node!
            char *kind = nm+5;
            if (!strcmp(kind, "Cons"))
            {
                fprintTermTop(context, out, SUB(term, 0), depth, encountered, used, indent, posp, includeprops, debug);
                fprintTermTop(context, out, SUB(term, 1), depth, encountered, used, indent, posp, includeprops, debug);
                return;
            }
            else if (!strcmp(kind, "Nil"))
            {
                return;
            }
            else if (!strcmp(kind, "Chars"))
            {
                Term chars = SUB(term, 0);
                if (IS_LITERAL(chars))
                    *posp += FPRINTF(context, out, "%s", asLiteral(chars)->text);
                else
                {
                    fprintTermTop(context, out, chars, depth, encountered, used, indent, posp, includeprops, debug);
                }
                return;
            }
            else if (!strcmp(kind, "Break"))
            {
                Term chars = SUB(term, 0);
                if (IS_LITERAL(chars))
                {
                    const char *text = asLiteral(chars)->text;
                    if (*text)
                    {
                        *posp += FPRINTF(context, out, "%s", text);
                        char *nl = strrchr(text, '\n');
                        if (nl)
                            *posp = (int) ((text + strlen(text)) - nl);
                    }
                    else
                    {
                        FPRINTF(context, out, "\n");
                        *posp = 0;
                    }
                }
                else
                {
                    fprintTermTop(context, out, chars, depth, encountered, used, indent, posp, includeprops, debug);
                }
                return;
            }
            else if (!strcmp(kind, "String"))
            {
                Term chars = SUB(term, 0);
                if (IS_LITERAL(chars))
                {
                    *posp += FPRINTF(context, out, "\"%s\"", asLiteral(chars)->text);
                    // TODO: reset absolute position
                }
                else
                {
                    fprintTermTop(context, out, chars, depth, encountered, used, indent, posp, includeprops, debug);
                }
                return;
            }
            else if (!strcmp(kind, "Separator"))
            {
                return;
            }
            else if (!strcmp(kind, "Group"))
            {
                fprintTermTop(context, out, SUB(term, 0), depth, encountered, used, indent, posp, includeprops, debug);
                return;
            }
            else if (!strcmp(kind, "Indent"))
            {
                fprintTermTop(context, out, SUB(term, 0), depth, encountered, used, indent+2, posp, includeprops, debug);
                return;
            }
            else if (!strcmp(kind, "Embed"))
            {
                fprintTermTop(context, out, SUB(term, 0), depth, encountered, used, indent, posp, includeprops, debug);
                return;
            }
        }

        // Properties, if any.
        if (includeprops)
        {
            Construction construction = asConstruction(term);
            if (debug)
            {
                fprintFreeVars(context, out, construction->fvs);
                fprintFreeVars(context, out, construction->nfvs);
                fprintFreeVars(context, out, construction->vfvs);
            }
            if (construction->properties->namedProperties || construction->properties->variableProperties)
            {
                *posp += FPRINTF(context, out, "{");
                char *sep = "";
                sep = fprintNamedProperties(context, out, construction->properties->namedProperties, sep, depth==INT32_MAX?depth:depth-10, encountered, used, indent, posp, debug, includeprops);
                sep = fprintVariableProperties(context, out, construction->properties->variableProperties, sep, depth==INT32_MAX?depth:depth-10, encountered, used, indent, posp, debug, includeprops);
                if (indent && depth > 1) { FPRINTF(context, out, "\n%.*s", indent, SPACES); *posp = indent; }
                *posp += FPRINTF(context, out, "}");
            }
        }

        if (IS_LITERAL(term))
        {
            *posp += fprintLiteral(context, out, term->descriptor->name(term));
        }
        else
        {
            char* nm = term->descriptor->name(term);
            const int arity = ARITY(term);
            int isFunction = IS_FUNCTION(term);
            if (isFunction)
            {
                // Probably a cookie
                char *name;
                if (arity == 0)
                    name = nm;
                else
                {
                   char* cons = ALLOCATE(context, 500);
                   name = cons;

                   int sz = strlen(nm);
                   memcpy(cons, nm, sz);
                   cons += sz;
                   (*cons) = '[';
                   cons ++;
                   char* sub = SYMBOL(SUB(term, 0));
                   sz = strlen(sub);
                   memcpy(cons, sub, sz);
                   cons += sz;
                   (*cons) = '\0';
                }
                // This really should never have been using NamedPropertyLink
                // as a generic string list.  We should fix that, but given
                // that it is like this, link it after the allocate to
                // prevent the allocate from turning our string list into
                // a hashtable in the future.  (NamedPropertyLink stores
                // a set, not a list (ie. unordered), whereas cookie lists
                // are an ordered list of strings).
                NamedPropertyLink newCookie = ALLOCATE_NamedPropertyLink(context, name, NULL, NULL);
                newCookie->link = printCookieNameList;

                printCookieNameList = newCookie;
            }

            if(arity == 0 && strlen(nm) == 0)
            {
                return;
            }
            *posp += fprintConstructor(context, out, nm);
#ifdef DEBUG
            DEBUGENV("crsx-debug-addresses", FPRINTF(context, out, "{%p}", (void*)term));
///            if (asConstruction(term)->constant)
///                FPRINTF(context, out, "{C}");
///            if (asConstruction(term)->ground)
///                FPRINTF(context, out, "{G}");
            if (debug)
            {
                if (asConstruction(term)->nf)
                    FPRINTF(context, out, "{NF}");
                if (asConstruction(term)->nostep)
                    FPRINTF(context, out, "{NS}");

                FPRINTF(context, out, "{%d}", term->nr);
            }
#endif
            if (arity > 0)
            {
                int deep = 0;
                int i;
                for (i = 0; i < arity; ++i)
                    deep = deep || ARITY(SUB(term, i)) > 0;
                FPRINTF(context, out, (deep && indent && depth>1 ? "[\n" : "["));
                *posp = (deep && indent && depth>1 ? 0 : *posp+1);
                for (i = 0; i < arity; ++i)
                {
                    if (i > 0) { FPRINTF(context, out, (deep && indent && depth>1 ? ",\n" : ", ")); *posp = (deep && indent && depth>1 ? 0 : *posp+2); }
                    // Binders.
                    int rank = RANK(term,i);
                    if (rank > 0)
                    {
                        if (*posp < indent && depth > 1) *posp += FPRINTF(context, out, "%.*s ", indent-*posp, SPACES);
                        int j;
                        for (j = 0; j < rank; ++j)
                        {
                            // Bound variable that should be unique...
                            Variable binder = BINDER(term, i, j);
                            if (binder)
                            {
                                //ASSERT(context, !containsVariable(encountered, used, binder));
                                addVariable(encountered, binder);
                                *posp += fprintSafeVariableName(context, out, binder, used);
                                *posp += FPRINTF(context, out, " ");
                            }
                            else
                                *posp += FPRINTF(context, out, "vNULL ");
                        }
                        FPRINTF(context, out, (deep && indent && depth>1 ? ".\n" : ". "));
                        *posp = (deep && indent && depth>1 ? 0 : *posp+1);
                    }
                    // Subterm.
                    fprintTermTop(context, out, SUB(term, i), depth==INT32_MAX?depth:depth-1, encountered, used, (indent ? indent+(rank>0 ? 2 : 1) : 0), posp, includeprops, debug);
                }
                *posp += FPRINTF(context, out, "]");
            }
        }
    }
}

/** Print a single named or variable property (and return sep on empty, "; " otherwise).
 * Use isNamed=true for named properties, isNamed=false for variable properties. */
static char* fprintProperty(Context context, FILE* out, int isNamed, size_t nr, const void *key, Term term, char* sep, int depth, VariableSet encountered, Hashset2 used, int indent, int *posp, int debug, int max)
{
    *posp += FPRINTF(context, out, "%s", sep);
    char *nl = strrchr(sep, '\n');
    if (nl) *posp = ((sep + strlen(sep) - 1) - nl);
    if (indent && *posp < indent+2 && depth > 1)
      *posp += FPRINTF(context, out, "%.*s", indent + 2 - *posp, SPACES);
    if (debug)
      FPRINTF(context, out, "{%zd}", nr);
    if (isNamed)
    {
        *posp += fprintLiteral(context, out, (char *) key);
    }
    else
    {
        *posp += fprintSafeVariableName(context, out, (Variable) key, used);
    }
    *posp += FPRINTF(context, out, " : ");
    fprintTermTop(context, out, term, depth==INT32_MAX?depth:depth-2, encountered, used, (indent ? indent+4 : 0), posp, depth==INT32_MAX?depth:1, debug);
    sep = ";\n";
    return sep;
}

static char *fprintPropsHS2(Context context, FILE* out, int isNamed, Hashset2 set, char* sep, int depth, VariableSet encountered, Hashset2 used, int indent, int *posp, int debug, int max)
{
    int i;
    for (i = 0 ; i < set->nslots ; i++)
    {
        LinkedList2 slot = set->entries[i];
        while (slot)
        {
            Term term = (Term) slot->value;
            sep = fprintProperty(context, out, isNamed, set->nr, slot->key, term, sep, depth, encountered, used, indent, posp, debug, max);
            slot = slot->next;
        }
    }
    return sep;
}

void printPropsHS2(Context context, Hashset2 set)
{
    VariableSet encountered = makeVariableSet(context);
    Hashset2 used = makeHS2(context, 10, NULL, equalsChars, hashChars);
    int pos = 0;

    fprintPropsHS2(context, STDOUT, 1 /* names, not variables */,
                   set, ";\n", -1, encountered, used, 1, &pos, 0, 0);
}


/** Print named properties (and return sep on empty, "; " otherwise). */
char* fprintNamedProperties(Context context, FILE* out, NamedPropertyLink namedProperties, char* sep, int depth, VariableSet encountered, Hashset2 used, int indent, int *posp, int debug, int max /*includeprops*/)
{
    NamedPropertyLink link;
    for (link = namedProperties; link; link = link->link)
    {
        if (link->name)
        {
            sep = fprintProperty(context, out, 1, link->nr, link->name, link->u.term, sep, depth, encountered, used, indent, posp, debug, max);
        }
        else
        {
            sep = fprintPropsHS2(context, out, 1, link->u.propset, sep, depth, encountered, used, indent, posp, debug, max);
        }
        if ((max==INT32_MAX?max:max--) <=0)
            break;
    }
    return sep;
}

/** Print variable properties (and return sep on empty, "; " otherwise). */
char* fprintVariableProperties(Context context, FILE* out, VariablePropertyLink variableProperties, char* sep, int depth, VariableSet encountered, Hashset2 used, int indent, int *posp, int debug, int max /*includeprops*/)
{
    VariablePropertyLink link;
    for (link = variableProperties; link; link = link->link)
    {
        if (link->variable)
        {
            sep = fprintProperty(context, out, 0, link->nr, link->variable, link->u.term, sep, depth, encountered, used, indent, posp, debug, max);
        }
        else
        {
            sep = fprintPropsHS2(context, out, 0, link->u.propset, sep, depth, encountered, used, indent, posp, debug, max);
        }
        if ((max==INT32_MAX?max:max--) <=0)
            break;
    }
    return sep;
}

void pfv(Context context, VARIABLESET freeVars)
{
    fprintFreeVars(context, STDOUT, freeVars);
    FPRINTF(context, STDOUT, "\n");
}

/** Print free variable */
void fprintFreeVars(Context context, FILE* out, VARIABLESET freeVars)
{
    if (!VARIABLESET_ISEMPTY(freeVars))
    {
        FPRINTF(context, out, "<");
        VARIABLESET_PRINTF(context, out, freeVars);
        FPRINTF(context, out, ">");
    }
}

// Experimental term regeneration code...

void printCTerm2(Context context, Term term, VariableSet allocated, char *sink, int indent);
void printCTerm(Context context, Term term)
{
    VariableSet set = makeVariableSet(context);
    PRINTF(context, "void sendCTerm(Sink sink)\n");
    PRINTF(context, "{\n");
    printCTerm2(context, term, set, "sink", 1);
    PRINTF(context, "}\n");
    freeVariableSet(set);
}
void printCTerm2(Context context, Term term, VariableSet allocated, char *sink, int indent)
{
    if (IS_VARIABLE_USE(term))
    {
        Variable x = VARIABLE(term);
        ASSERT(context, x);
        char* name = x->name;
        ASSERT(context, name);
        if (!containsVariable(allocated, x))
        {
            PRINTF(context, "%.*sVariable %s = MAKE_%s%sVARIABLE(%s->context, \"%s\");\n", indent, SPACES, name, (IS_BOUND(x) ? "BOUND_" : "FRESH_"), (IS_LINEAR(x) ? "LINEAR_" : "PROMISCUOUS_"), sink, name);
            addVariable(allocated, x);
        }
        PRINTF(context, "%.*sUSE(%s, %s);\n", indent, SPACES, sink, name);
    }
    else // Construction.
    {
        // Properties, if any.
        {
            Construction construction = asConstruction(term);
            if (construction->properties->namedProperties || construction->properties->variableProperties)
            {
                {
                    NamedPropertyLink link;
                    for (link = construction->properties->namedProperties; link; link = link->link)
                        if (link->name)
                        {
                            PRINTF(context, "%.*s{\n", indent, SPACES);
                            char *bufferSink = ALLOCATENF(context, 100, "buf_%d", indent+1);
                            PRINTF(context, "%.*sSink %s = MAKE_BUFFER(%s->context);\n", indent+1, SPACES, bufferSink, sink);
                            printCTerm2(context, link->u.term, allocated, bufferSink, indent+1);
                            PRINTF(context, "%.*sADD_PROPERTY_NAMED(%s, \"%s\", BUFFER_TERM(%s));\n", indent+1, SPACES, sink, link->name, bufferSink);
                            PRINTF(context, "%.*sFREE_BUFFER(%s));\n", indent+1, SPACES, bufferSink);
                            FREE(context, bufferSink);
                            PRINTF(context, "%.*s}\n", indent, SPACES);
                        }
                }
                {
                    VariablePropertyLink link;
                    for (link = construction->properties->variableProperties; link; link = link->link)
                        if (link->variable)
                        {
                            PRINTF(context, "%.*s{\n", indent, SPACES);
                            char *bufferSink = ALLOCATENF(context, 100, "buf_%d", indent+1);
                            PRINTF(context, "%.*sSink %s = MAKE_BUFFER(%s->context);\n", indent+1, SPACES, bufferSink, sink);
                            printCTerm2(context, link->u.term, allocated, bufferSink, indent+1);
                            PRINTF(context, "%.*sADD_PROPERTY_VARIABLE(%s, %s, BUFFER_TERM(%s));\n", indent+1, SPACES, sink, link->variable->name, bufferSink);
                            PRINTF(context, "%.*sFREE_BUFFER(%s));\n", indent+1, SPACES, bufferSink);
                            FREE(context, bufferSink);
                            PRINTF(context, "%.*s}\n", indent, SPACES);
                        }
                }
            }
        }

        if (IS_LITERAL(term))
        {
            char* literal = term->descriptor->name(term);
            PRINTF(context, "%.*sLITERAL(%s, \"%s\");\n", indent, SPACES, sink, literal);
        }
        else
        {
            ASSERT(context, IS_DATA(term));
            int arity = ARITY(term);
            SortDescriptor sort = term->descriptor->sort;
            char *internalName = sort->constructorInternalNames[TAG(term)];
            PRINTF(context, "%.*sSTART(%s, %s);\n", indent, SPACES, sink, internalName);
            int i;
            for (i = 0; i < arity; ++i)
            {
                // Binders.
                int rank = RANK(term,i);
                if (rank > 0)
                {
                    int j;
                    for (j = 0; j < rank; ++j)
                    {
                        Variable binder = BINDER(term, i, j);
                        PRINTF(context, "%.*s%s = MAKE_%sVARIABLE(%s->context, \"%s\");\n", indent+1, SPACES, binder->name, (IS_LINEAR(binder) ? "LINEAR_" : ""), sink, binder->name);
                        addVariable(allocated, binder);
                    }
                    PRINTF(context, "%.*s{\n", indent+1, SPACES);
                    PRINTF(context, "%.*sVariable bind_%d[%d] = ALLOCATE(%s->context, %d*sizeof(Variable));\n", indent+2, SPACES, indent+2, rank, sink, rank);
                    for (j = 0; j < rank; ++j)
                    {
                        Variable binder = BINDER(term, i, j);
                        PRINTF(context, "%.*sbind_%d[%d] = %s;\n", indent+2, SPACES, indent+2, j, binder->name);
                        addVariable(allocated, binder);
                    }
                    PRINTF(context, "%.*sBINDS(%s, %d, bind_%d);\n", indent+2, SPACES, sink, rank, indent+2);
                    PRINTF(context, "%.*s}\n", indent+1, SPACES);
                }
                // Subterm.
                printCTerm2(context, SUB(term, i), allocated, sink, indent+1);
            }
            PRINTF(context, "%.*sEND(%s, %s);\n", indent, SPACES, sink, internalName);
        }
    }
}


///static void ptSubstitution(Context context, SubstitutionFrame substitution)
///{
///    for (; substitution; substitution = substitution->parent)
///    {
///        int i;
///        for (i = 0; i < substitution->count; ++i)
///        {
///            PRINTF(context, "  %s => ", substitution->variables[i]->name);
///            pt(context, substitution->substitutes[i]);
///        }
///    }
///}


/////////////////////////////////////////////////////////////////////////////////
