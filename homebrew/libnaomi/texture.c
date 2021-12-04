#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/system.h"
#include "naomi/ta.h"
#include "naomi/thread.h"
#include "irqinternal.h"

static int twiddletab[1024];
#define TWIDDLE(u, v) (twiddletab[(v)] | (twiddletab[(u)] << 1))

void _ta_init_twiddletab()
{
    for(int addr = 0; addr < 1024; addr++)
    {
        twiddletab[addr] = (
            (addr & 1) |
            ((addr & 2) << 1) |
            ((addr & 4) << 2) |
            ((addr & 8) << 3) |
            ((addr & 16) << 4) |
            ((addr & 32) << 5) |
            ((addr & 64) << 6) |
            ((addr & 128) << 7) |
            ((addr & 256) << 8) |
            ((addr & 512) << 9)
        );
    }
}

#define FLAGS_IN_USE 0x1

typedef struct allocated_texture
{
    uint32_t offset;
    uint32_t size;
    uint32_t flags;

    struct allocated_texture *prev;
    struct allocated_texture *next;
} allocated_texture_t;

static allocated_texture_t *head = 0;
static int initialized = 0;
static mutex_t texalloc_mutex;

#define TEXRAM_HIGH ((UNCACHED_MIRROR | TEXRAM_BASE) + TEXRAM_SIZE)

void _ta_init_texture_allocator()
{
    // Allow for reinitialization if we change video modes.
    while (head != 0)
    {
        // Grab current, set the head to the next one (or NULL if this was the last one).
        allocated_texture_t *cur = head;
        head = head->next;

        // Free the current texture tracking structure.
        if (cur) { free(cur); }
    }

    if (!initialized)
    {
        mutex_init(&texalloc_mutex);
        initialized = 1;
    }

    // Insert one allocated texture chunk spanning the entire free texture RAM area.
    head = malloc(sizeof(allocated_texture_t));
    if (head == 0)
    {
        _irq_display_invariant("memory failure", "cannot allocate memory for texture tracking structure!");
    }
    memset(head, 0, sizeof(allocated_texture_t));

    // Set up the location and size of the head chunk.
    head->offset = (uint32_t)ta_texture_base();
    head->size = TEXRAM_HIGH - head->offset;
}

void *ta_texture_malloc(int uvsize, int bitsize)
{
    // First, make sure they gave us a valid uv and bitsize.
    if (uvsize != 8 && uvsize != 16 && uvsize != 32 && uvsize != 64 && uvsize != 128 && uvsize != 256 && uvsize != 512 && uvsize != 1024)
    {
        return 0;
    }
    if (bitsize != 4 && bitsize != 8 && bitsize != 16 && bitsize != 32)
    {
        return 0;
    }

    void *texture = 0;
    mutex_lock(&texalloc_mutex);
    {
        // Calculate the actual size in bytes of this texture, so we know where to slot it in.
        uint32_t actual_size = (uvsize * uvsize * bitsize) / 8;

        // We aren't so much concerned with fragmentation here, since all textures
        // are powers of two sizes. So, we can try all we want to group similar textures
        // together, but at the end of the day there won't be any slots not occupiable
        // by some texture. Especially if a game is using similar texture sizes for every
        // asset, this will super not be an issue.
        allocated_texture_t *potential = 0;

        // First, go through and see if we can't find a slot that exactly matches this
        // texture. This is fairly likely in scenarios where all textures are the same
        // size and random allocations/deallocations happen.
        allocated_texture_t *cur = head;
        while (cur != 0)
        {
            if ((cur->flags & FLAGS_IN_USE) == 0)
            {
                if (cur->size == actual_size)
                {
                    // Use this one exactly.
                    potential = cur;
                    break;
                }
                if (cur->size > actual_size)
                {
                    // Use this one, unless a better one comes along.
                    potential = cur;
                }
            }

            cur = cur->next;
        }

        // Now, did we get any potential at all? If not, then we're out of memory!
        if (potential)
        {
            if (potential->size == actual_size)
            {
                // If this is exactly the right size, then all we need to do is mark it in use.
                potential->flags |= FLAGS_IN_USE;
                texture = (void *)potential->offset;
            }
            else
            {
                // We need to split this into two allocation chunks.
                allocated_texture_t *newchunk = malloc(sizeof(allocated_texture_t));
                if (newchunk == 0)
                {
                    _irq_display_invariant("memory failure", "cannot allocate memory for texture tracking structure!");
                }

                // Set up all parameters of this chunk based on the one we're forking it from.
                newchunk->offset = potential->offset;
                newchunk->size = actual_size;

                potential->offset += actual_size;
                potential->size -= actual_size;

                newchunk->flags = potential->flags | FLAGS_IN_USE;

                newchunk->next = potential;
                newchunk->prev = potential->prev;
                potential->prev = newchunk;

                if (newchunk->prev != 0)
                {
                    newchunk->prev->next = newchunk;
                }

                // Since we added this to the beginning, its possible that head points at potential
                // so in that case we need it to point at us instead.
                if (head == potential)
                {
                    head = newchunk;
                }

                uint32_t offset = potential->next ? potential->next->offset : TEXRAM_HIGH;
                if (newchunk->offset + newchunk->size + potential->size != offset)
                {
                    _irq_display_invariant("texture allocator failure", "failed invariant check on line %d with current chunk size!", __LINE__);
                }

                // Finally, return the memory managed by this chunk.
                texture = (void *)newchunk->offset;
            }
        }
    }
    mutex_unlock(&texalloc_mutex);

    return texture;
}

void ta_texture_free(void *texture)
{
    mutex_lock(&texalloc_mutex);
    {
        // First, find the texture itself in our allocation tracking structure.
        allocated_texture_t *cur = head;
        allocated_texture_t *actual = 0;
        while (cur != 0)
        {
            if (cur->offset == (uint32_t)texture)
            {
                actual = cur;
                break;
            }

            cur = cur->next;
        }

        if (actual == 0)
        {
            // We couldn't find this texture to free, just exit without doing anything.
            mutex_unlock(&texalloc_mutex);
            return;
        }

        // Now, mark the flags of this chunk as being free.
        cur->flags = cur->flags & (~FLAGS_IN_USE);

        // Now, see if we can collapse this into the next chunk.
        if (cur->next != 0)
        {
            allocated_texture_t *collapsible = cur->next;
            if ((collapsible->flags & FLAGS_IN_USE) == 0)
            {
                // We can!
                if (cur->offset + cur->size != collapsible->offset)
                {
                    _irq_display_invariant("texture allocator failure", "failed invariant check on line %d with current chunk size!", __LINE__);
                }

                cur->next = collapsible->next;
                cur->size += collapsible->size;

                if (collapsible->next != 0)
                {
                    collapsible->next->prev = cur;
                }

                free(collapsible);

                uint32_t offset = cur->next ? cur->next->offset : TEXRAM_HIGH;
                if (cur->offset + cur->size != offset)
                {
                    _irq_display_invariant("texture allocator failure", "failed invariant check on line %d with current chunk size!", __LINE__);
                }
            }
        }

        // Now, see if we can collapse this into the previous chunk.
        if (cur->prev != 0)
        {
            allocated_texture_t *collapsible = cur->prev;
            if ((collapsible->flags & FLAGS_IN_USE) == 0)
            {
                // We can!
                if (collapsible->offset + collapsible->size != cur->offset)
                {
                    _irq_display_invariant("texture allocator failure", "failed invariant check on line %d with current chunk size!", __LINE__);
                }

                collapsible->next = cur->next;
                collapsible->size += cur->size;

                if (cur->next != 0)
                {
                    cur->next->prev = collapsible;
                }

                free(cur);

                uint32_t offset = collapsible->next ? collapsible->next->offset : TEXRAM_HIGH;
                if (collapsible->offset + collapsible->size != offset)
                {
                    _irq_display_invariant("texture allocator failure", "failed invariant check on line %d with current chunk size!", __LINE__);
                }
            }
        }
    }
    mutex_unlock(&texalloc_mutex);
}

struct mallinfo ta_texture_mallinfo()
{
    struct mallinfo info;
    memset(&info, 0, sizeof(info));

    if (head != 0)
    {
        mutex_lock(&texalloc_mutex);
        info.arena = TEXRAM_HIGH - head->offset;

        size_t uordblks = 0;
        size_t fordblks = 0;

        allocated_texture_t *cur = head;
        while (cur != 0)
        {
            if ((cur->flags & FLAGS_IN_USE) == 0)
            {
                fordblks += cur->size;
            }
            else
            {
                uordblks += cur->size;
            }

            cur = cur->next;
        }

        info.uordblks = uordblks;
        info.fordblks = fordblks;
        mutex_unlock(&texalloc_mutex);
    }

    return info;
}

int ta_texture_load(void *offset, int uvsize, int bitsize, void *data)
{
    if (uvsize != 8 && uvsize != 16 && uvsize != 32 && uvsize != 64 && uvsize != 128 && uvsize != 256 && uvsize != 512 && uvsize != 1024)
    {
        return -1;
    }
    if (offset == 0 || data == 0)
    {
        return -1;
    }

    switch (bitsize)
    {
        case 8:
        {
            uint16_t *tex = (uint16_t *)(((uint32_t)offset) | UNCACHED_MIRROR);
            uint8_t *src = (uint8_t *)data;

            for(int v = 0; v < uvsize; v+= 2)
            {
                for(int u = 0; u < uvsize; u++)
                {
                    tex[TWIDDLE(u, v) >> 1] = src[(u + (v * uvsize))] | (src[u + ((v + 1) * uvsize)] << 8);
                }
            }
            break;
        }
        default:
        {
            // Currently only support loading 8bit textures here.
            return -1;
        }
    }

    return 0;
}
