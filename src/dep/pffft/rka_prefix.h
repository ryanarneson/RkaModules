/* Renames every symbol pffft.c exports, so this plugin's copy cannot meet
   the pffft inside libRack (Rack exports pffft_*). Compiled in with
   -include for pffft.c, and included by src/Fft.hpp before pffft.h, so
   the declarations and the definitions carry the same names. Check after
   an update that every global `nm -g build/test/dep/pffft.o` lists (apart
   from libc's) starts with rka_ or _rka_; pffft.c also exports cfftf1_ps,
   cffti1_ps and validate_pffft_simd, renamed below. */
#ifndef RKA_PFFFT_PREFIX_H
#define RKA_PFFFT_PREFIX_H

#define pffft_aligned_free rka_pffft_aligned_free
#define pffft_aligned_malloc rka_pffft_aligned_malloc
#define pffft_cplx_finalize rka_pffft_cplx_finalize
#define pffft_cplx_preprocess rka_pffft_cplx_preprocess
#define pffft_destroy_setup rka_pffft_destroy_setup
#define pffft_new_setup rka_pffft_new_setup
#define pffft_simd_size rka_pffft_simd_size
#define pffft_transform rka_pffft_transform
#define pffft_transform_internal rka_pffft_transform_internal
#define pffft_transform_ordered rka_pffft_transform_ordered
#define pffft_zconvolve_accumulate rka_pffft_zconvolve_accumulate
#define pffft_zreorder rka_pffft_zreorder
#define cfftf1_ps rka_pffft_cfftf1_ps
#define cffti1_ps rka_pffft_cffti1_ps
#define validate_pffft_simd rka_pffft_validate_simd

#endif
