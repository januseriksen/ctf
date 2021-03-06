/*Copyright (c) 2011, Edgar Solomonik, all rights reserved.*/

#include "../shared/iter_tsr.h"
#include "../shared/util.h"
#include <limits.h>
#include "sym_seq_sum.h"

namespace CTF_int {

  #define SCAL_B do {                                                      \
    if (!sr_B->isequal(beta, sr_B->mulid())){                                 \
      memset(idx_glb, 0, sizeof(int)*idx_max);                             \
      idx_A = 0, idx_B = 0;                                                \
      sym_pass = 1;                                                        \
      for (;;){                                                            \
        if (sym_pass){                                                     \
          sr_B->mul(beta, B+idx_B*sr_B->el_size, B+idx_B*sr_B->el_size);      \
          CTF_FLOPS_ADD(1);                                                \
        }                                                                  \
        for (idx=0; idx<idx_max; idx++){                                   \
          imin = 0, imax = INT_MAX;                                        \
          GET_MIN_MAX(B,1,2);                                              \
          if (rev_idx_map[2*idx+1] == -1) imax = imin+1;                   \
          idx_glb[idx]++;                                                  \
          if (idx_glb[idx] >= imax){                                       \
             idx_glb[idx] = imin;                                          \
          }                                                                \
          if (idx_glb[idx] != imin) {                                      \
             break;                                                        \
          }                                                                \
        }                                                                  \
        if (idx == idx_max) break;                                         \
        CHECK_SYM(B);                                                      \
        if (!sym_pass) continue;                                           \
        if (order_B > 0)                                                   \
          RESET_IDX(B);                                                    \
      }                                                                    \
    } } while (0)


  #define SCAL_B_inr do {                                                  \
    if (!sr_B->isequal(beta, sr_B->mulid())){                                 \
      memset(idx_glb, 0, sizeof(int)*idx_max);                             \
      idx_A = 0, idx_B = 0;                                                \
      sym_pass = 1;                                                        \
      for (;;){                                                            \
        if (sym_pass){                                                     \
          sr_B->scal(inr_stride, beta, B+idx_B*inr_stride*sr_B->el_size, 1); \
          CTF_FLOPS_ADD(inr_stride);                                       \
        }                                                                  \
        for (idx=0; idx<idx_max; idx++){                                   \
          imin = 0, imax = INT_MAX;                                        \
          GET_MIN_MAX(B,1,2);                                              \
          if (rev_idx_map[2*idx+1] == -1) imax = imin+1;                   \
          idx_glb[idx]++;                                                  \
          if (idx_glb[idx] >= imax){                                       \
             idx_glb[idx] = imin;                                          \
          }                                                                \
          if (idx_glb[idx] != imin) {                                      \
             break;                                                        \
          }                                                                \
        }                                                                  \
        if (idx == idx_max) break;                                         \
        CHECK_SYM(B);                                                      \
        if (!sym_pass) continue;                                           \
        if (order_B > 0)                                                   \
          RESET_IDX(B);                                                    \
      }                                                                    \
    } } while (0)

  int sym_seq_sum_ref( char const *     alpha,
                       char const *     A,
                       algstrct const * sr_A,
                       int              order_A,
                       int const *      edge_len_A,
                       int const *      sym_A,
                       int const *      idx_map_A,
                       char const *     beta,
                       char *           B,
                       algstrct const * sr_B,
                       int              order_B,
                       int const *      edge_len_B,
                       int const *      sym_B,
                       int const *      idx_map_B){
    TAU_FSTART(sym_seq_sum_ref);
    int idx, i, idx_max, imin, imax, iA, iB, j, k;
    int off_idx, sym_pass;
    int * idx_glb, * rev_idx_map;
    int * dlen_A, * dlen_B;
    int64_t idx_A, idx_B, off_lda;

    inv_idx(order_A,       idx_map_A,
            order_B,       idx_map_B,
            &idx_max,     &rev_idx_map);

    dlen_A = (int*)CTF_int::alloc(sizeof(int)*order_A);
    dlen_B = (int*)CTF_int::alloc(sizeof(int)*order_B);
    memcpy(dlen_A, edge_len_A, sizeof(int)*order_A);
    memcpy(dlen_B, edge_len_B, sizeof(int)*order_B);

    idx_glb = (int*)CTF_int::alloc(sizeof(int)*idx_max);

    SCAL_B;

    memset(idx_glb, 0, sizeof(int)*idx_max);
    idx_A = 0, idx_B = 0;
    sym_pass = 1;
    for (;;){
      if (sym_pass){
    /*    printf("B[%d] = %lf*(A[%d]=%lf)+%lf*(B[%d]=%lf\n",
                idx_B,alpha,idx_A,A[idx_A],beta,idx_B,B[idx_B]);*/
//        printf("adding to %d ",idx_B); sr_B->print(B+sr_B->el_size*idx_B); printf("\n");
        if (alpha != NULL){
          char tmp[sr_B->el_size];
          sr_B->mul(A+sr_A->el_size*idx_A, alpha, tmp);
          sr_B->add(tmp, B+sr_B->el_size*idx_B, B+sr_B->el_size*idx_B);
          CTF_FLOPS_ADD(2);
        } else {
          sr_B->add(A+sr_A->el_size*idx_A, B+sr_B->el_size*idx_B, B+sr_B->el_size*idx_B);
          CTF_FLOPS_ADD(1);
        }
//        printf("computed %d ",idx_B); sr_B->print(B+sr_B->el_size*idx_B); printf("\n");
      }

      for (idx=0; idx<idx_max; idx++){
        imin = 0, imax = INT_MAX;

        GET_MIN_MAX(A,0,2);
        GET_MIN_MAX(B,1,2);

        ASSERT(idx_glb[idx] >= imin && idx_glb[idx] < imax);

        idx_glb[idx]++;

        if (idx_glb[idx] >= imax){
          idx_glb[idx] = imin;
        }
        if (idx_glb[idx] != imin) {
          break;
        }
      }
      if (idx == idx_max) break;

      CHECK_SYM(A);
      if (!sym_pass) continue;
      CHECK_SYM(B);
      if (!sym_pass) continue;
      
      if (order_A > 0)
        RESET_IDX(A);
      if (order_B > 0)
        RESET_IDX(B);
    }
    CTF_int::cdealloc(dlen_A);
    CTF_int::cdealloc(dlen_B);
    CTF_int::cdealloc(idx_glb);
    CTF_int::cdealloc(rev_idx_map);
    TAU_FSTOP(sym_seq_sum_ref);
    return 0;
  }

  int sym_seq_sum_inr( char const *     alpha,
                       char const *     A,
                       algstrct const * sr_A,
                       int              order_A,
                       int const *      edge_len_A,
                       int const *      sym_A,
                       int const *      idx_map_A,
                       char const *     beta,
                       char *           B,
                       algstrct const * sr_B,
                       int              order_B,
                       int const *      edge_len_B,
                       int const *      sym_B,
                       int const *      idx_map_B,
                       int              inr_stride){
    TAU_FSTART(sym_seq_sum_inr);
    int idx, i, idx_max, imin, imax, iA, iB, j, k;
    int off_idx, sym_pass;
    int * idx_glb, * rev_idx_map;
    int * dlen_A, * dlen_B;
    int64_t idx_A, idx_B, off_lda;

    inv_idx(order_A,       idx_map_A,
            order_B,       idx_map_B,
            &idx_max,     &rev_idx_map);

    dlen_A = (int*)CTF_int::alloc(sizeof(int)*order_A);
    dlen_B = (int*)CTF_int::alloc(sizeof(int)*order_B);
    memcpy(dlen_A, edge_len_A, sizeof(int)*order_A);
    memcpy(dlen_B, edge_len_B, sizeof(int)*order_B);

    idx_glb = (int*)CTF_int::alloc(sizeof(int)*idx_max);

    SCAL_B_inr;

    memset(idx_glb, 0, sizeof(int)*idx_max);
   
    idx_A = 0, idx_B = 0;
    sym_pass = 1;
    for (;;){
      if (sym_pass){
    /*    printf("B[%d] = %lf*(A[%d]=%lf)+%lf*(B[%d]=%lf\n",
                idx_B,alpha,idx_A,A[idx_A],beta,idx_B,B[idx_B]);*/
      //  B[idx_B] = alpha*A[idx_A] + beta*B[idx_B];
    /*    if (beta != 1.0){
          cxaxpy<dtype>(inr_stride, beta-1.0, B+idx_B*inr_stride, 1, B+idx_B*inr_stride, 1);
          CTF_FLOPS_ADD(2*inr_stride);
        }*/
        //cxaxpy<dtype>(inr_stride, alpha, A+idx_A*inr_stride, 1, B+idx_B*inr_stride, 1); 
        sr_B->axpy(inr_stride, alpha, A+idx_A*sr_A->el_size*inr_stride, 1, B+idx_B*sr_B->el_size*inr_stride, 1); 
        CTF_FLOPS_ADD(2*inr_stride);
      }

      for (idx=0; idx<idx_max; idx++){
        imin = 0, imax = INT_MAX;

        GET_MIN_MAX(A,0,2);
        GET_MIN_MAX(B,1,2);

        ASSERT(idx_glb[idx] >= imin && idx_glb[idx] < imax);

        idx_glb[idx]++;

        if (idx_glb[idx] >= imax){
          idx_glb[idx] = imin;
        }
        if (idx_glb[idx] != imin) {
          break;
        }
      }
      if (idx == idx_max) break;

      CHECK_SYM(A);
      if (!sym_pass) continue;
      CHECK_SYM(B);
      if (!sym_pass) continue;
      
      if (order_A > 0)
        RESET_IDX(A);
      if (order_B > 0)
        RESET_IDX(B);
    }
    CTF_int::cdealloc(dlen_A);
    CTF_int::cdealloc(dlen_B);
    CTF_int::cdealloc(idx_glb);
    CTF_int::cdealloc(rev_idx_map);
    TAU_FSTOP(sym_seq_sum_inr);
    return 0;
  }

  int sym_seq_sum_cust(char const *     alpha,
                       char const *     A,
                       algstrct const * sr_A,
                       int              order_A,
                       int const *      edge_len_A,
                       int const *      sym_A,
                       int const *      idx_map_A,
                       char const *     beta,
                       char *           B,
                       algstrct const * sr_B,
                       int              order_B,
                       int const *      edge_len_B,
                       int const *      sym_B,
                       int const *      idx_map_B,
                       univar_function  func){
    TAU_FSTART(sym_seq_sum_cust);
    int idx, i, idx_max, imin, imax, iA, iB, j, k;
    int off_idx, sym_pass;
    int * idx_glb, * rev_idx_map;
    int * dlen_A, * dlen_B;
    int64_t idx_A, idx_B, off_lda;

    inv_idx(order_A,       idx_map_A,
            order_B,       idx_map_B,
            &idx_max,     &rev_idx_map);

    dlen_A = (int*)CTF_int::alloc(sizeof(int)*order_A);
    dlen_B = (int*)CTF_int::alloc(sizeof(int)*order_B);
    memcpy(dlen_A, edge_len_A, sizeof(int)*order_A);
    memcpy(dlen_B, edge_len_B, sizeof(int)*order_B);

    idx_glb = (int*)CTF_int::alloc(sizeof(int)*idx_max);
    memset(idx_glb, 0, sizeof(int)*idx_max);

    SCAL_B;

    idx_A = 0, idx_B = 0;
    sym_pass = 1;
    for (;;){
      if (sym_pass){
        if (alpha != NULL){
          char tmp[sr_B->el_size];
          sr_A->mul(A+sr_A->el_size*idx_A, alpha, tmp);
          func.apply_f(tmp, tmp);
          sr_B->add(B+idx_B*sr_B->el_size, tmp, B+sr_B->el_size*idx_B);
          CTF_FLOPS_ADD(2);
        } else {
          func.apply_f(A+idx_A*sr_A->el_size, B+idx_B*sr_B->el_size);
          CTF_FLOPS_ADD(1);
        }
      }

      for (idx=0; idx<idx_max; idx++){
        imin = 0, imax = INT_MAX;

        GET_MIN_MAX(A,0,2);
        GET_MIN_MAX(B,1,2);

        ASSERT(idx_glb[idx] >= imin && idx_glb[idx] < imax);

        idx_glb[idx]++;

        if (idx_glb[idx] >= imax){
          idx_glb[idx] = imin;
        }
        if (idx_glb[idx] != imin) {
          break;
        }
      }
      if (idx == idx_max) break;

      CHECK_SYM(A);
      if (!sym_pass) continue;
      CHECK_SYM(B);
      if (!sym_pass) continue;
      
      if (order_A > 0)
        RESET_IDX(A);
      if (order_B > 0)
        RESET_IDX(B);
    }
    CTF_int::cdealloc(dlen_A);
    CTF_int::cdealloc(dlen_B);
    CTF_int::cdealloc(idx_glb);
    CTF_int::cdealloc(rev_idx_map);
    TAU_FSTOP(sym_seq_sum_cust);
    return 0;
  }
}
