/*
Copyright (c) 2016, Blue Brain Project
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _H_MEMORY_
#define _H_MEMORY_

#include <string.h>

#include "coreneuron/nrniv/nrn_assert.h"

/// for gpu builds with unified memory support
#if (defined(__CUDACC__) || defined(_OPENACC))

#include <cuda_runtime_api.h>

// TODO : error handling for CUDA routines
inline void alloc_memory(void*& pointer, size_t num_bytes, size_t /*alignment*/) {
    cudaMallocManaged(&pointer, num_bytes);
}

inline void calloc_memory(void*& pointer, size_t num_bytes, size_t /*alignment*/) {
    alloc_memory(pointer, num_bytes, 64);
    cudaMemset(pointer, 0, num_bytes);
}

inline void free_memory(void* pointer) {
    cudaFree(pointer);
}

/// for cpu builds use posix memalign
#else

#include <stdlib.h>

inline void alloc_memory(void*& pointer, size_t num_bytes, size_t alignment) {
    nrn_assert(posix_memalign(&pointer, alignment, num_bytes) == 0);
}

inline void calloc_memory(void*& pointer, size_t num_bytes, size_t alignment) {
    alloc_memory(pointer, num_bytes, alignment);
    memset(pointer, 0, num_bytes);
}

inline void free_memory(void* pointer) {
    free(pointer);
}

#endif


namespace coreneuron {

/** Independent function to compute the needed chunkding,
    the chunk argument is the number of doubles the chunk is chunkded upon.
*/
template <int chunk>
inline int soa_padded_size(int cnt, int layout) {
    int imod = cnt % chunk;
    if (layout == 1)
        return cnt;
    if (imod) {
        int idiv = cnt / chunk;
        return (idiv + 1) * chunk;
    }
    return cnt;
}

/** Check for the pointer alignment.
 */
inline bool is_aligned(void* pointer, size_t alignment) {
    return (((uintptr_t)(const void*)(pointer)) % (alignment) == 0);
}

/** Allocate the aligned memory.
 */
inline void* emalloc_align(size_t size, size_t alignment) {
    void* memptr;
    alloc_memory(memptr, size, alignment);
    nrn_assert(is_aligned(memptr, alignment));
    return memptr;
}

/** Allocate the aligned memory and set it to 1.
 */
inline void* ecalloc_align(size_t n, size_t alignment, size_t size) {
    void* p;
    if (n == 0) {
        return (void*)0;
    }
    calloc_memory(p, n*size, alignment);
    nrn_assert(is_aligned(p, alignment));
    return p;
}
}  // namespace coreneuron

#endif
