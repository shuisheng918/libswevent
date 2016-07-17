#ifndef INC_SW_TIMER_HEAP_H
#define INC_SW_TIMER_HEAP_H

#include "sw_event_internal.h"

extern void* (*sw_ev_malloc)(size_t);
void  (*sw_ev_free)(void *);
void* (*sw_ev_realloc)(void *, size_t);

typedef struct sw_timer_heap /* it's a min heap */
{
    sw_ev_timer_t ** timers;
    unsigned size, capacity;
} sw_timer_heap_t;

static inline void            sw_timer_heap_ctor(sw_timer_heap_t *heap);
static inline void            sw_timer_heap_dtor(sw_timer_heap_t *heap);
static inline void            sw_timer_heap_elem_init(sw_ev_timer_t *e);
static inline int             sw_timer_heap_elem_greater(sw_ev_timer_t *left, sw_ev_timer_t *right);
static inline unsigned        sw_timer_heap_size(sw_timer_heap_t *heap);
static inline sw_ev_timer_t * sw_timer_heap_top(sw_timer_heap_t *heap);
static inline int             sw_timer_heap_reserve(sw_timer_heap_t *heap, unsigned size);
static inline int             sw_timer_heap_push(sw_timer_heap_t *heap, sw_ev_timer_t *e);
static inline sw_ev_timer_t * sw_timer_heap_pop(sw_timer_heap_t *heap);
static inline int             sw_timer_heap_erase(sw_timer_heap_t *heap, sw_ev_timer_t *e);
static inline void            sw_timer_heap_shift_up_(sw_timer_heap_t *heap, unsigned hole_index, sw_ev_timer_t *e);
static inline void            sw_timer_heap_shift_down_(sw_timer_heap_t *heap, unsigned hole_index, sw_ev_timer_t *e);

int sw_timer_heap_elem_greater(sw_ev_timer_t *left, sw_ev_timer_t *right)
{
    return left->next_expire_time > right->next_expire_time;
}

void sw_timer_heap_ctor(sw_timer_heap_t *heap)
{
    heap->timers = 0;
    heap->size = 0;
    heap->capacity = 0;
}

void sw_timer_heap_dtor(sw_timer_heap_t *heap)
{
    if(heap->timers)
    {
        sw_ev_free(heap->timers);
        heap->timers = 0;
        heap->size = 0;
        heap->capacity = 0;
    }
}

void sw_timer_heap_elem_init(sw_ev_timer_t *e)
{
    e->index_in_heap = -1;
}

int sw_timer_heap_empty(sw_timer_heap_t *heap)
{
    return 0 == heap->size;
}

unsigned sw_timer_heap_size(sw_timer_heap_t *heap)
{
    return heap->size;
}

sw_ev_timer_t *sw_timer_heap_top(sw_timer_heap_t *heap)
{
    return heap->size ? *heap->timers : 0;
}

int sw_timer_heap_push(sw_timer_heap_t* heap, sw_ev_timer_t* e)
{
    if(sw_timer_heap_reserve(heap, heap->size + 1))
    {
        return -1;
    }
    sw_timer_heap_shift_up_(heap, heap->size++, e);
    return 0;
}

struct sw_ev_timer* sw_timer_heap_pop(sw_timer_heap_t* heap)
{
    if(heap->size)
    {
        sw_ev_timer_t* e = *heap->timers;
        sw_timer_heap_shift_down_(heap, 0u, heap->timers[--heap->size]);
        e->index_in_heap = -1;
        return e;
    }
    return 0;
}

int sw_timer_heap_erase(sw_timer_heap_t* heap, sw_ev_timer_t* e)
{
    if(((unsigned int)-1) != e->index_in_heap)
    {
        sw_ev_timer_t *last = heap->timers[--heap->size];
        unsigned parent = (e->index_in_heap - 1) / 2;
        /* replace e with last element*/
        if (e->index_in_heap > 0 && sw_timer_heap_elem_greater(heap->timers[parent], last))
        {
            sw_timer_heap_shift_up_(heap, e->index_in_heap, last);
        }
        else
        {
            sw_timer_heap_shift_down_(heap, e->index_in_heap, last);
        }
        e->index_in_heap = -1;
        return 0;
    }
    return -1;
}

int sw_timer_heap_reserve(sw_timer_heap_t* heap, unsigned size)
{
    if(heap->capacity < size)
    {
        sw_ev_timer_t **timers;
        unsigned capacity = heap->capacity ? heap->capacity * 2 : 8;
        if(capacity < size)
            capacity = size;
        if(!(timers = (sw_ev_timer_t**)sw_ev_realloc(heap->timers, capacity * sizeof *timers)))
            return -1;
        heap->timers = timers;
        heap->capacity = capacity;
    }
    return 0;
}

void sw_timer_heap_shift_up_(sw_timer_heap_t* heap, unsigned hole_index, sw_ev_timer_t* e)
{
    unsigned parent = (hole_index - 1) / 2;
    while(hole_index && sw_timer_heap_elem_greater(heap->timers[parent], e))
    {
        (heap->timers[hole_index] = heap->timers[parent])->index_in_heap = hole_index;
        hole_index = parent;
        parent = (hole_index - 1) / 2;
    }
    (heap->timers[hole_index] = e)->index_in_heap = hole_index;
}

void sw_timer_heap_shift_down_(sw_timer_heap_t* heap, unsigned hole_index, sw_ev_timer_t* e)
{
    unsigned min_child = 2 * (hole_index + 1);
    while(min_child <= heap->size)
	{
        if (min_child == heap->size || sw_timer_heap_elem_greater(heap->timers[min_child], heap->timers[min_child - 1]))
        {
            min_child -= 1;
        }
        if(!(sw_timer_heap_elem_greater(e, heap->timers[min_child])))
        {
            break;
        }
        (heap->timers[hole_index] = heap->timers[min_child])->index_in_heap = hole_index;
        hole_index = min_child;
        min_child = 2 * (hole_index + 1);
	}
    sw_timer_heap_shift_up_(heap, hole_index,  e);
}


#endif
