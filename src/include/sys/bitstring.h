/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * bitstring.h - generic bit-array primitives.
 *
 * Models a bit array as a contiguous run of 'bitstr_t' words (one
 * uint64 = 64 bits per word). Operations are O(1), expand to a single
 * shift + mask + load/store.
 */

#ifndef _AV6_BITSTRING_H_
#define _AV6_BITSTRING_H_

#include "sys/types.h"

typedef uint64 bitstr_t;

#define _BITSTR_T_BITS          64

/* number of bitstr_t words required to hold _n bits */
#define BITSTR_NWORDS(_n)       (((_n) + _BITSTR_T_BITS - 1) / _BITSTR_T_BITS)

/* word index of bit '_b', and that word's single-bit mask */
#define _BIT_IDX(_b)            ((_b) / _BITSTR_T_BITS)
#define _BIT_MASK(_b)           (1UL << ((_b) % _BITSTR_T_BITS))

/* Declare a bit array */
#define bit_decl(_name, _nbits) bitstr_t _name[BITSTR_NWORDS(_nbits)]
#define bit_set(_a, _b)         ((_a)[_BIT_IDX(_b)] |= _BIT_MASK(_b))
#define bit_clear(_a, _b)       ((_a)[_BIT_IDX(_b)] &= ~_BIT_MASK(_b))
#define bit_test(_a, _b)        (((_a)[_BIT_IDX(_b)] & _BIT_MASK(_b)) != 0)

/*
 * bit_ffc - find first clear bit
 *
 * Per word it inverts (1 = a clear bit) and takes the lowest set bit with
 * __builtin_ctzll (count trailing a clear bit -> rbit+clz on AArch64). The
 * _bit < _nbits guard rejects a clear bit living in the unused high part of
 * the final word.
 */
static inline void bit_ffc(const bitstr_t *_bitstr, int _nbits, int *_result)
{
    int _w, _nwords, _bit;
    bitstr_t _free;

    _nwords = BITSTR_NWORDS(_nbits);
    for (_w = 0; _w < _nwords; _w++) {
        _free = ~_bitstr[_w];
        if (_free != 0) {
            _bit = _w * _BITSTR_T_BITS + __builtin_ctzll(_free);
            if (_bit < _nbits) {
                *_result = _bit;
                return;
            }
            break;
        }
    }
    *_result = -1;
}

#endif  /* _AV6_BITSTRING_H_ */
