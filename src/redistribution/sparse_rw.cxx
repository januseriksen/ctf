/*Copyright (c) 2011, Edgar Solomonik, all rights reserved.*/

#include "sparse_rw.h"
#include "pad.h"
#include "../shared/util.h"



namespace CTF_int {
  void permute_keys(int              order,
                    int              num_pair,
                    int const *      edge_len,
                    int const *      new_edge_len,
                    int * const *    permutation,
                    char *           pairs_buf,
                    int64_t *        new_num_pair,
                    algstrct const * sr){
    TAU_FSTART(permute_keys);
  #ifdef USE_OMP
    int mntd = omp_get_max_threads();
  #else
    int mntd = 1;
  #endif
    int64_t counts[mntd];
    std::fill(counts,counts+mntd,0);
  #ifdef USE_OMP
    #pragma omp parallel
  #endif
    { 
      int i, j, tid, ntd, outside;
      int64_t lda, wkey, knew, kdim, tstart, tnum_pair, cnum_pair;
  #ifdef USE_OMP
      tid = omp_get_thread_num();
      ntd = omp_get_num_threads();
  #else
      tid = 0;
      ntd = 1;
  #endif
      tnum_pair = num_pair/ntd;
      tstart = tnum_pair * tid + MIN(tid, num_pair % ntd);
      if (tid < num_pair % ntd) tnum_pair++;

      //std::vector< tkv_pair<dtype> > my_pairs;
      //allocate buffer of same size of pairs, 
      //FIXME: not all space may be used, so a smaller buffer is possible
      char my_pairs_buf[sr->pair_size()*tnum_pair];
      PairIterator my_pairs(sr, my_pairs_buf);
      PairIterator pairs(sr, pairs_buf);
      cnum_pair = 0;

      for (i=tstart; i<tstart+tnum_pair; i++){
        wkey = pairs[i].k();
        lda = 1;
        knew = 0;
        outside = 0;
        for (j=0; j<order; j++){
          kdim = wkey%edge_len[j];
          if (permutation[j] != NULL){
            if (permutation[j][kdim] == -1){
              outside = 1;
            } else{
              knew += lda*permutation[j][kdim];
            }
          } else {
            knew += lda*kdim;
          }
          lda *= new_edge_len[j];
          wkey = wkey/edge_len[j];
        }
        if (!outside){
          char tkp[sr->pair_size()];
          //tkp.k = knew;
          //tkp.d = pairs[i].d;
          sr->set_pair(tkp, knew, pairs[i].d());
          my_pairs[cnum_pair].write(tkp);
          cnum_pair++;
        }
      }
      counts[tid] = cnum_pair;
      {
  #ifdef USE_OMP
        #pragma omp barrier
  #endif
        int64_t pfx = 0;
        for (i=0; i<tid; i++){
          pfx += counts[i];
        }
        pairs[pfx].write(my_pairs,cnum_pair);
      }
    } 
    *new_num_pair = 0;
    for (int i=0; i<mntd; i++){
      *new_num_pair += counts[i];
    }
    TAU_FSTOP(permute_keys);
  }

  void depermute_keys(int              order,
                      int              num_pair,
                      int const *      edge_len,
                      int const *      new_edge_len,
                      int * const *    permutation,
                      char *           pairs_buf,
                      algstrct const * sr){
    TAU_FSTART(depermute_keys);
  #ifdef USE_OMP
    int mntd = omp_get_max_threads();
  #else
    int mntd = 1;
  #endif
    int64_t counts[mntd];
    std::fill(counts,counts+mntd,0);
    int ** depermutation = (int**)CTF_int::alloc(order*sizeof(int*));
    TAU_FSTART(form_depermutation);
    for (int d=0; d<order; d++){
      if (permutation[d] == NULL){
        depermutation[d] = NULL;
      } else {
        depermutation[d] = (int*)CTF_int::alloc(new_edge_len[d]*sizeof(int));
        std::fill(depermutation[d],depermutation[d]+new_edge_len[d], -1);
        for (int i=0; i<edge_len[d]; i++){
          depermutation[d][permutation[d][i]] = i;
        }
      }
    }
    TAU_FSTOP(form_depermutation);
  #ifdef USE_OMP
    #pragma omp parallel
  #endif
    { 
      int i, j, tid, ntd;
      int64_t lda, wkey, knew, kdim, tstart, tnum_pair;
  #ifdef USE_OMP
      tid = omp_get_thread_num();
      ntd = omp_get_num_threads();
  #else
      tid = 0;
      ntd = 1;
  #endif
      tnum_pair = num_pair/ntd;
      tstart = tnum_pair * tid + MIN(tid, num_pair % ntd);
      if (tid < num_pair % ntd) tnum_pair++;

      char my_pairs_buf[sr->pair_size()*tnum_pair];

      PairIterator my_pairs(sr, my_pairs_buf);
      PairIterator pairs(sr, pairs_buf);

      for (i=tstart; i<tstart+tnum_pair; i++){
        wkey = pairs[i].k();
        lda = 1;
        knew = 0;
        for (j=0; j<order; j++){
          kdim = wkey%new_edge_len[j];
          if (depermutation[j] != NULL){
            ASSERT(depermutation[j][kdim] != -1);
            knew += lda*depermutation[j][kdim];
          } else {
            knew += lda*kdim;
          }
          lda *= edge_len[j];
          wkey = wkey/new_edge_len[j];
        }
        pairs[i].write_key(knew);
      }
    }
    for (int d=0; d<order; d++){
      if (permutation[d] != NULL)
        CTF_int::cdealloc(depermutation[d]);
    }
    CTF_int::cdealloc(depermutation);

    TAU_FSTOP(depermute_keys);
  }



  void assign_keys(int              order,
                   int64_t          size,
                   int              nvirt,
                   int const *      edge_len,
                   int const *      sym,
                   int const *      phase,
                   int const *      phys_phase,
                   int const *      virt_dim,
                   int *            phase_rank,
                   char const *     vdata,
                   char *           vpairs,
                   algstrct const * sr){
    int i, imax, act_lda, idx_offset, act_max, buf_offset;
    int64_t p;
    int * idx, * virt_rank, * edge_lda;  
    if (order == 0){
      ASSERT(size <= 1);
      if (size == 1){
        sr->set_pair(vpairs, 0, vdata);
      }
      return;
    }

    TAU_FSTART(assign_keys);
    CTF_int::alloc_ptr(order*sizeof(int), (void**)&idx);
    CTF_int::alloc_ptr(order*sizeof(int), (void**)&virt_rank);
    CTF_int::alloc_ptr(order*sizeof(int), (void**)&edge_lda);
    
    memset(virt_rank, 0, sizeof(int)*order);
    
    edge_lda[0] = 1;
    for (i=1; i<order; i++){
      edge_lda[i] = edge_lda[i-1]*edge_len[i-1];
    }
    for (p=0;;p++){
      char const * data = vdata + sr->el_size*p*(size/nvirt);
      PairIterator pairs = PairIterator(sr, vpairs + sr->pair_size()*p*(size/nvirt));

      idx_offset = 0, buf_offset = 0;
      for (act_lda=1; act_lda<order; act_lda++){
        idx_offset += phase_rank[act_lda]*edge_lda[act_lda];
      } 
    
      //printf("size = %d\n", size); 
      memset(idx, 0, order*sizeof(int));
      imax = edge_len[0]/phase[0];
      for (;;){
        if (sym[0] != NS)
          imax = idx[1]+1;
          /* Increment virtual bucket */
        for (i=0; i<imax; i++){
          ASSERT(buf_offset+i<size);
          if (p*(size/nvirt) + buf_offset + i >= size){ 
            printf("exceeded how much I was supposed to read %ld/%ld\n", p*(size/nvirt)+buf_offset+i,size);
            ABORT;
          }
          pairs[buf_offset+i].write_key(idx_offset+i*phase[0]+phase_rank[0]);
          pairs[buf_offset+i].write_val(data+(buf_offset+i)*sr->el_size);
        }
        buf_offset += imax;
        /* Increment indices and set up offsets */
        for (act_lda=1; act_lda < order; act_lda++){
          idx_offset -= (idx[act_lda]*phase[act_lda]+phase_rank[act_lda])
                  *edge_lda[act_lda];
          idx[act_lda]++;
          act_max = edge_len[act_lda]/phase[act_lda];
          if (sym[act_lda] != NS) act_max = idx[act_lda+1]+1;
          if (idx[act_lda] >= act_max)
            idx[act_lda] = 0;
          idx_offset += (idx[act_lda]*phase[act_lda]+phase_rank[act_lda])
                  *edge_lda[act_lda];
          ASSERT(edge_len[act_lda]%phase[act_lda] == 0);
          if (idx[act_lda] > 0)
            break;
        }
        if (act_lda >= order) break;
      }
      for (act_lda=0; act_lda < order; act_lda++){
        phase_rank[act_lda] -= virt_rank[act_lda]*phys_phase[act_lda];
        virt_rank[act_lda]++;
        if (virt_rank[act_lda] >= virt_dim[act_lda])
          virt_rank[act_lda] = 0;
        phase_rank[act_lda] += virt_rank[act_lda]*phys_phase[act_lda];
        if (virt_rank[act_lda] > 0)
          break;
      }
      if (act_lda >= order) break;
    }
    ASSERT(buf_offset == size/nvirt);
    CTF_int::cdealloc(idx);
    CTF_int::cdealloc(virt_rank);
    CTF_int::cdealloc(edge_lda);
    TAU_FSTOP(assign_keys);
  }
 
  void bucket_by_pe(int               order,
                    int64_t           num_pair,
                    int64_t           np,
                    int const *       phys_phase,
                    int const *       virt_phase,
                    int const *       bucket_lda,
                    int const *       edge_len,
                    ConstPairIterator mapped_data,
                    int64_t *         bucket_counts,
                    int64_t *         bucket_off,
                    PairIterator      bucket_data,
                    algstrct const *  sr){

    memset(bucket_counts, 0, sizeof(int64_t)*np); 
  #ifdef USE_OMP
    int64_t * sub_counts, * sub_offs;
    CTF_int::alloc_ptr(np*sizeof(int64_t)*omp_get_max_threads(), (void**)&sub_counts);
    CTF_int::alloc_ptr(np*sizeof(int64_t)*omp_get_max_threads(), (void**)&sub_offs);
    memset(sub_counts, 0, np*sizeof(int64_t)*omp_get_max_threads());
  #endif


    TAU_FSTART(bucket_by_pe_count);
    /* Calculate counts */
  #ifdef USE_OMP
    #pragma omp parallel for schedule(static,256) 
  #endif
    for (int64_t i=0; i<num_pair; i++){
      int64_t k = mapped_data[i].k();
      int64_t loc = 0;
  //    int tmp_arr[order];
      for (int j=0; j<order; j++){
  /*      tmp_arr[j] = (k%edge_len[j])%phase[j];
        tmp_arr[j] = tmp_arr[j]/virt_phase[j];
        tmp_arr[j] = tmp_arr[j]*bucket_lda[j];*/
        loc += (k%phys_phase[j])*bucket_lda[j];
        k = k/edge_len[j];
      }
  /*    for (j=0; j<order; j++){
        loc += tmp_arr[j];
      }*/
      ASSERT(loc<np);
  #ifdef USE_OMP
      sub_counts[loc+omp_get_thread_num()*np]++;
  #else
      bucket_counts[loc]++;
  #endif
    }
    TAU_FSTOP(bucket_by_pe_count);

  #ifdef USE_OMP
    for (int j=0; j<omp_get_max_threads(); j++){
      for (int64_t i=0; i<np; i++){
        bucket_counts[i] = sub_counts[j*np+i] + bucket_counts[i];
      }
    }
  #endif

    /* Prefix sum to get offsets */
    bucket_off[0] = 0;
    for (int64_t i=1; i<np; i++){
      bucket_off[i] = bucket_counts[i-1] + bucket_off[i-1];
    }
    
    /* reset counts */
  #ifdef USE_OMP
    memset(sub_offs, 0, sizeof(int64_t)*np);
    for (int i=1; i<omp_get_max_threads(); i++){
      for (int64_t j=0; j<np; j++){
        sub_offs[j+i*np]=sub_counts[j+(i-1)*np]+sub_offs[j+(i-1)*np];
      }
    }
  #else
    memset(bucket_counts, 0, sizeof(int64_t)*np); 
  #endif

    /* bucket data */
    TAU_FSTART(bucket_by_pe_move);
  #ifdef USE_OMP
    #pragma omp parallel for schedule(static,256) 
  #endif
    for (int64_t i=0; i<num_pair; i++){
      int64_t k = mapped_data[i].k();
      int64_t loc = 0;
      for (int j=0; j<order; j++){
        loc += (k%phys_phase[j])*bucket_lda[j];
        k = k/edge_len[j];
      }
  #ifdef USE_OMP
      bucket_data[bucket_off[loc] + sub_offs[loc+omp_get_thread_num()*np]].write(mapped_data[i]);
      sub_offs[loc+omp_get_thread_num()*np]++;
  #else
      bucket_data[bucket_off[loc] + bucket_counts[loc]].write(mapped_data[i]);
      bucket_counts[loc]++;
  #endif
    }
  #ifdef USE_OMP
    CTF_int::cdealloc(sub_counts);
    CTF_int::cdealloc(sub_offs);
  #endif
    TAU_FSTOP(bucket_by_pe_move);
  }
  
  void bucket_by_virt(int               order,
                      int               num_virt,
                      int64_t           num_pair,
                      int const *       phys_phase,
                      int const *       virt_phase,
                      int const *       edge_len,
                      ConstPairIterator mapped_data,
                      PairIterator      bucket_data,
                      algstrct const *  sr){
    int64_t * virt_counts, * virt_prefix, * virt_lda;
    TAU_FSTART(bucket_by_virt);
    
    CTF_int::alloc_ptr(num_virt*sizeof(int64_t), (void**)&virt_counts);
    CTF_int::alloc_ptr(num_virt*sizeof(int64_t), (void**)&virt_prefix);
    CTF_int::alloc_ptr(order*sizeof(int64_t),    (void**)&virt_lda);
   
   
    if (order > 0){
      virt_lda[0] = 1;
      for (int i=1; i<order; i++){
        ASSERT(virt_phase[i] > 0);
        virt_lda[i] = virt_phase[i-1]*virt_lda[i-1];
      }
    }

    memset(virt_counts, 0, sizeof(int64_t)*num_virt); 
  #ifdef USE_OMP
    int64_t * sub_counts, * sub_offs;
    CTF_int::alloc_ptr(num_virt*sizeof(int64_t)*omp_get_max_threads(), (void**)&sub_counts);
    CTF_int::alloc_ptr(num_virt*sizeof(int64_t)*omp_get_max_threads(), (void**)&sub_offs);
    memset(sub_counts, 0, num_virt*sizeof(int64_t)*omp_get_max_threads());
  #endif


    /* bucket data */
  #ifdef USE_OMP
    TAU_FSTART(bucket_by_virt_omp_cnt);
    #pragma omp parallel for schedule(static) 
    for (int64_t i=0; i<num_pair; i++){
      int64_t k = mapped_data[i].k();
      int64_t loc = 0;
      //#pragma unroll
      for (int j=0; j<order; j++){
        loc += ((k/phys_phase[j])%virt_phase[j])*virt_lda[j];
        k = k/edge_len[j];
      }
      
      //bucket_data[loc*num_pair_virt + virt_counts[loc]] = mapped_data[i];
      sub_counts[loc+omp_get_thread_num()*num_virt]++;
    }
    TAU_FSTOP(bucket_by_virt_omp_cnt);
    TAU_FSTART(bucket_by_virt_assemble_offsets);
    for (int j=0; j<omp_get_max_threads(); j++){
      for (int64_t i=0; i<num_virt; i++){
        virt_counts[i] = sub_counts[j*num_virt+i] + virt_counts[i];
      }
    }
    virt_prefix[0] = 0;
    for (int64_t i=1; i<num_virt; i++){
      virt_prefix[i] = virt_prefix[i-1] + virt_counts[i-1];
    }

    memset(sub_offs, 0, sizeof(int64_t)*num_virt);
    for (int i=1; i<omp_get_max_threads(); i++){
      for (int64_t j=0; j<num_virt; j++){
        sub_offs[j+i*num_virt]=sub_counts[j+(i-1)*num_virt]+sub_offs[j+(i-1)*num_virt];
      }
    }
    TAU_FSTOP(bucket_by_virt_assemble_offsets);
    TAU_FSTART(bucket_by_virt_move);
    #pragma omp parallel for schedule(static)
    for (int64_t i=0; i<num_pair; i++){
      int64_t k = mapped_data[i].k();
      int64_t loc = 0;
      //#pragma unroll
      for (int j=0; j<order; j++){
        loc += ((k/phys_phase[j])%virt_phase[j])*virt_lda[j];
        k = k/edge_len[j];
      }
      bucket_data[virt_prefix[loc] + sub_offs[loc+omp_get_thread_num()*num_virt]].write(mapped_data[i]);
      sub_offs[loc+omp_get_thread_num()*num_virt]++;
    }
    TAU_FSTOP(bucket_by_virt_move);
  #else
    for (int64_t i=0; i<num_pair; i++){
      int64_t k = mapped_data[i].k();
      int64_t loc = 0;
      for (int j=0; j<order; j++){
        loc += ((k/phys_phase[j])%virt_phase[j])*virt_lda[j];
        k = k/edge_len[j];
      }
      virt_counts[loc]++;
    }

    virt_prefix[0] = 0;
    for (int64_t i=1; i<num_virt; i++){
      virt_prefix[i] = virt_prefix[i-1] + virt_counts[i-1];
    }
    memset(virt_counts, 0, sizeof(int64_t)*num_virt); 

    for (int64_t i=0; i<num_pair; i++){
      int64_t k = mapped_data[i].k();
      int64_t loc = 0;
      for (int j=0; j<order; j++){
        loc += ((k/phys_phase[j])%virt_phase[j])*virt_lda[j];
        k = k/edge_len[j];
      }
      bucket_data[virt_prefix[loc] + virt_counts[loc]].write(mapped_data[i]);
      virt_counts[loc]++;
    }
  #endif

    TAU_FSTART(bucket_by_virt_sort);
  #ifdef USE_OMP
    #pragma omp parallel for
  #endif
    for (int64_t i=0; i<num_virt; i++){
      /*std::sort(bucket_data+virt_prefix[i],
          bucket_data+(virt_prefix[i]+virt_counts[i]));*/
      bucket_data[virt_prefix[i]].sort(virt_counts[i]);
    }
    TAU_FSTOP(bucket_by_virt_sort);
  #if DEBUG >= 1
  // FIXME: Can we handle replicated keys?
  /*  for (i=1; i<num_pair; i++){
      ASSERT(bucket_data[i].k != bucket_data[i-1].k);
    }*/
  #endif
  #ifdef USE_OMP
    CTF_int::cdealloc(sub_counts);
    CTF_int::cdealloc(sub_offs);
  #endif
    CTF_int::cdealloc(virt_prefix);
    CTF_int::cdealloc(virt_counts);
    CTF_int::cdealloc(virt_lda);
    TAU_FSTOP(bucket_by_virt);
  }

  void readwrite(int              order,
                 int64_t          size,
                 char const *     alpha,
                 char const *     beta,
                 int              nvirt,
                 int const *      edge_len,
                 int const *      sym,
                 int const *      phase,
                 int const *      phys_phase,
                 int const *      virt_dim,
                 int *            phase_rank,
                 char *           vdata,
                 char *           pairs_buf,
                 char             rw,
                 algstrct const * sr){
    int act_lda;
    int64_t idx_offset, act_max, buf_offset, pr_offset, p;
    int64_t * idx, * virt_rank, * edge_lda;  
    
    PairIterator pairs = PairIterator(sr, pairs_buf);

    if (order == 0){
      if (size > 0){
        if (size > 1){
          for (int64_t i=1; i<size; i++){
            //check for write conflicts
            //FIXME this makes sense how again?
            ASSERT(pairs[i].k() == 0 || pairs[i].d() != pairs[0].d());
          }
        }
    //    printf("size = " PRId64 "\n",size);
    //    ASSERT(size == 1);
        if (rw == 'r'){
          pairs[0].write_val(vdata);
        } else {
          //vdata[0] = pairs[0].d;
          pairs[0].read_val(vdata);
        }
      }
      return;
    }
    TAU_FSTART(readwrite);
    CTF_int::alloc_ptr(order*sizeof(int64_t), (void**)&idx);
    CTF_int::alloc_ptr(order*sizeof(int64_t), (void**)&virt_rank);
    CTF_int::alloc_ptr(order*sizeof(int64_t), (void**)&edge_lda);
    
    memset(virt_rank, 0, sizeof(int64_t)*order);
    edge_lda[0] = 1;
    for (int i=1; i<order; i++){
      edge_lda[i] = edge_lda[i-1]*edge_len[i-1];
    }

    pr_offset = 0;
    buf_offset = 0;
    char * data = vdata;// + buf_offset;

    for (p=0;;p++){
      data = data + sr->el_size*buf_offset;
      idx_offset = 0, buf_offset = 0;
      for (act_lda=1; act_lda<order; act_lda++){
        idx_offset += phase_rank[act_lda]*edge_lda[act_lda];
      } 
     
      memset(idx, 0, order*sizeof(int64_t));
      int64_t imax = edge_len[0]/phase[0];
      for (;;){
        if (sym[0] != NS)
          imax = idx[1]+1;
        /* Increment virtual bucket */
        for (int64_t i=0; i<imax;){// i++){
          if (pr_offset >= size)
            break;
          else {
            if (pairs[pr_offset].k() == idx_offset +i*phase[0]+phase_rank[0]){
              if (rw == 'r'){
                if (alpha == NULL){
                  pairs[pr_offset].write_val(data+sr->el_size*(buf_offset+i));
/*                if (sr->isbeta == 0.0){
                  char wval[sr->pair_size()];
                  sr->mul(alpha,data + sr->el_size*(buf_offset+i), wval);
                  pairs[pr_offset].write_val(wval);*/
                } else {
                /* should it be the opposite? No, because 'pairs' was passed in and 'data' is being added to pairs, so data is operand, gets alpha. */
                  //pairs[pr_offset].d = alpha*data[buf_offset+i]+beta*pairs[pr_offset].d;
                  char wval[sr->pair_size()];
                  sr->mul(alpha, data + sr->el_size*(buf_offset+i), wval);
                  char wval2[sr->pair_size()];
                  sr->mul(beta,  pairs[pr_offset].d(), wval2);
                  sr->add(wval, wval2, wval);
                  pairs[pr_offset].write_val(wval);
                }
              } else {
                ASSERT(rw =='w');
                //data[(int64_t)buf_offset+i] = beta*data[(int64_t)buf_offset+i]+alpha*pairs[pr_offset].d;
                if (alpha == NULL)
                  pairs[pr_offset].read_val(data+sr->el_size*(buf_offset+i));
                else {
                  char wval[sr->pair_size()];
                  sr->mul(beta, data + sr->el_size*(buf_offset+i), wval);
                  char wval2[sr->pair_size()];
                  sr->mul(alpha,  pairs[pr_offset].d(), wval2);
                  sr->add(wval, wval2, wval);
                  sr->copy(data + sr->el_size*(buf_offset+i), wval);
                }
              }
              pr_offset++;
              //Check for write conflicts
              //Fixed: allow and handle them!
              while (pr_offset < size && pairs[pr_offset].k() == pairs[pr_offset-1].k()){
  //              printf("found overlapped write of key %ld and value %lf\n", pairs[pr_offset].k, pairs[pr_offset].d);
                if (rw == 'r'){
                  if (alpha == NULL){
                    pairs[pr_offset].write_val(data + sr->el_size*(buf_offset+i));
                  } else {
//                  pairs[pr_offset].d = alpha*data[buf_offset+i]+beta*pairs[pr_offset].d;
                    char wval[sr->pair_size()];
                    sr->mul(alpha, data + sr->el_size*(buf_offset+i), wval);
                    char wval2[sr->pair_size()];
                    sr->mul(beta,  pairs[pr_offset].d(), wval2);
                    sr->add(wval, wval2, wval);
                    pairs[pr_offset].write_val(wval);
                  }
                } else {
                  //FIXME: may be problematic if someone writes entries of a symmetric tensor redundantly
                  if (alpha == NULL){
                    sr->add(data + (buf_offset+i)*sr->el_size, 
                            pairs[pr_offset].d(),
                            data + (buf_offset+i)*sr->el_size);
                  } else {
                  //data[(int64_t)buf_offset+i] = beta*data[(int64_t)buf_offset+i]+alpha*pairs[pr_offset].d;
                    char wval[sr->pair_size()];
                    sr->mul(alpha,  pairs[pr_offset].d(), wval);
                    sr->add(wval, data + sr->el_size*(buf_offset+i), wval);
                    sr->copy(data + sr->el_size*(buf_offset+i), wval);
                  }
                }
  //              printf("rw = %c found overlapped write and set value to %lf\n", rw, data[(int64_t)buf_offset+i]);
                pr_offset++;
              }
            } else {
              i++;
  /*          DEBUG_PRINTF("%d key[%d] %d not matched with %d\n",
                            (int)pairs[pr_offset-1].k,
                            pr_offset, (int)pairs[pr_offset].k,
                            (idx_offset+i*phase[0]+phase_rank[0]));*/
            }
          }
        }
        buf_offset += imax;
        if (pr_offset >= size)
          break;
        /* Increment indices and set up offsets */
        for (act_lda=1; act_lda < order; act_lda++){
          idx_offset -= (idx[act_lda]*phase[act_lda]+phase_rank[act_lda])
                        *edge_lda[act_lda];
          idx[act_lda]++;
          act_max = edge_len[act_lda]/phase[act_lda];
          if (sym[act_lda] != NS) act_max = idx[act_lda+1]+1;
          if (idx[act_lda] >= act_max)
            idx[act_lda] = 0;
          idx_offset += (idx[act_lda]*phase[act_lda]+phase_rank[act_lda])
                        *edge_lda[act_lda];
          ASSERT(edge_len[act_lda]%phase[act_lda] == 0);
          if (idx[act_lda] > 0)
            break;
        }
        if (act_lda == order) break;
      }
      for (act_lda=0; act_lda < order; act_lda++){
        phase_rank[act_lda] -= virt_rank[act_lda]*phys_phase[act_lda];
        virt_rank[act_lda]++;
        if (virt_rank[act_lda] >= virt_dim[act_lda])
          virt_rank[act_lda] = 0;
        phase_rank[act_lda] += virt_rank[act_lda]*phys_phase[act_lda];
        if (virt_rank[act_lda] > 0)
          break;
      }
      if (act_lda == order) break;
    }
    TAU_FSTOP(readwrite);
    //printf("pr_offset = " PRId64 "/" PRId64 "\n",pr_offset,size);
    ASSERT(pr_offset == size);
    CTF_int::cdealloc(idx);
    CTF_int::cdealloc(virt_rank);
    CTF_int::cdealloc(edge_lda);
  }

  void wr_pairs_layout(int              order,
                       int              np,
                       int64_t          inwrite,
                       char const *     alpha,
                       char const *     beta,
                       char             rw,
                       int              num_virt,
                       int const *      sym,
                       int const *      edge_len,
                       int const *      padding,
                       int const *      phase,
                       int const *      phys_phase,
                       int const *      virt_phase,
                       int *            virt_phys_rank,
                       int const *      bucket_lda,
                       char *           wr_pairs_buf,
                       char *           rw_data,
                       CommData         glb_comm,
                       algstrct const * sr){
    int64_t new_num_pair, nwrite, swp;
    int64_t * bucket_counts, * recv_counts;
    int64_t * recv_displs, * send_displs;
    int * depadding, * depad_edge_len;
    int * ckey;
    int j, is_out, sign, is_perm;
    char * swap_datab, * buf_datab;


    CTF_int::alloc_ptr(inwrite*sr->pair_size(), (void**)&buf_datab);
    CTF_int::alloc_ptr(inwrite*sr->pair_size(), (void**)&swap_datab);
    CTF_int::alloc_ptr(np*sizeof(int64_t),     (void**)&bucket_counts);
    CTF_int::alloc_ptr(np*sizeof(int64_t),     (void**)&recv_counts);
    CTF_int::alloc_ptr(np*sizeof(int64_t),     (void**)&send_displs);
    CTF_int::alloc_ptr(np*sizeof(int64_t),     (void**)&recv_displs);

    PairIterator buf_data  = PairIterator(sr, buf_datab);
    PairIterator swap_data = PairIterator(sr, swap_datab);
    PairIterator wr_pairs  = PairIterator(sr, wr_pairs_buf);


  #if DEBUG >= 1
    int64_t total_tsr_size = 1;
    for (int i=0; i<order; i++){
      total_tsr_size *= edge_len[i];
    }

    for (int64_t i=0; i<inwrite; i++){
      ASSERT(wr_pairs[i].k() >= 0);
      ASSERT(wr_pairs[i].k() < total_tsr_size);
    }
  #endif
    TAU_FSTART(wr_pairs_layout);

    /* Copy out the input data, do not touch that array */
  //  memcpy(swap_data, wr_pairs, nwrite*sizeof(tkv_pair<dtype>));
    CTF_int::alloc_ptr(order*sizeof(int), (void**)&depad_edge_len);
    for (int i=0; i<order; i++){
      depad_edge_len[i] = edge_len[i] - padding[i];
    } 
    CTF_int::alloc_ptr(order*sizeof(int), (void**)&ckey);
    TAU_FSTART(check_key_ranges);

    //calculate the number of keys that need to be vchanged first
    int64_t nchanged = 0;
    for (int64_t i=0; i<inwrite; i++){
      cvrt_idx(order, depad_edge_len, wr_pairs[i].k(), ckey);
      is_out = 0;
      sign = 1;
      is_perm = 1;
      while (is_perm && !is_out){
        is_perm = 0;
        for (j=0; j<order-1; j++){
          if ((sym[j] == SH || sym[j] == AS) && ckey[j] == ckey[j+1]){
            is_out = 1;
            break;
          } else if (sym[j] != NS && ckey[j] > ckey[j+1]){
            swp       = ckey[j];
            ckey[j]   = ckey[j+1];
            ckey[j+1] = swp;
            if (sym[j] == AS){
              sign     *= -1;
            }
            is_perm = 1;
          }/* else if (sym[j] == AS && ckey[j] > ckey[j+1]){
            swp       = ckey[j];
            ckey[j]   = ckey[j+1];
            ckey[j+1] = swp;
            is_perm = 1;
          } */
        }
      } 
      if (!is_out){
        int64_t skey;
        cvrt_idx(order, depad_edge_len, ckey, &skey);
        if (rw == 'r' && skey != wr_pairs[i].k()){
          nchanged++;
        }
      } else if (rw == 'r'){
        nchanged++;
      } 
    }

    nwrite = 0;

    int64_t * changed_key_indices;
    char * new_changed_pairs;
    int * changed_key_scale;
    
    CTF_int::alloc_ptr(nchanged*sizeof(int64_t), (void**)&changed_key_indices);
    CTF_int::alloc_ptr(nchanged*sr->pair_size(),  (void**)&new_changed_pairs);
    CTF_int::alloc_ptr(nchanged*sizeof(int),     (void**)&changed_key_scale);

    nchanged = 0;
    for (int64_t i=0; i<inwrite; i++){
      cvrt_idx(order, depad_edge_len, wr_pairs[i].k(), ckey);
      is_out = 0;
      sign = 1;
      is_perm = 1;
      while (is_perm && !is_out){
        is_perm = 0;
        for (j=0; j<order-1; j++){
          if ((sym[j] == SH || sym[j] == AS) && ckey[j] == ckey[j+1]){
            is_out = 1;
            break;
          } else if (sym[j] != NS && ckey[j] > ckey[j+1]){
            swp       = ckey[j];
            ckey[j]   = ckey[j+1];
            ckey[j+1] = swp;
            if (sym[j] == AS){
              sign     *= -1;
            }
            is_perm = 1;
          }/* else if (sym[j] == AS && ckey[j] > ckey[j+1]){
            swp       = ckey[j];
            ckey[j]   = ckey[j+1];
            ckey[j+1] = swp;
            is_perm = 1;
          } */
        }
      } 
      if (!is_out){
        int64_t ky = swap_data[nwrite].k();
        cvrt_idx(order, depad_edge_len, ckey, &ky);
        swap_data[nwrite].write_key(ky);
        if (sign == 1)
          swap_data[nwrite].write_val(wr_pairs[i].d());
        else {
          char ainv[sr->el_size];
          sr->addinv(wr_pairs[i].d(), ainv);
          swap_data[nwrite].write_val(ainv);
        }
        if (rw == 'r' && swap_data[nwrite].k() != wr_pairs[i].k()){
          /*printf("the %lldth key has been set from %lld to %lld\n",
                   i, wr_pairs[i].k, swap_data[nwrite].k);*/
          changed_key_indices[nchanged]= i;
          swap_data[nwrite].read(new_changed_pairs+nchanged*sr->pair_size());
          changed_key_scale[nchanged] = sign;
          nchanged++;
        }
        nwrite++;
      } else if (rw == 'r'){
        changed_key_indices[nchanged] = i;
        wr_pairs[i].read(new_changed_pairs+nchanged*sr->pair_size());
        changed_key_scale[nchanged] = 0;
        nchanged++;
      } 
    }
    CTF_int::cdealloc(ckey);
    TAU_FSTOP(check_key_ranges);

    /* If the packed tensor is padded, pad keys */
    pad_key(order, nwrite, depad_edge_len, padding, swap_data, sr);
    CTF_int::cdealloc(depad_edge_len);

    /* Figure out which processor the value in a packed layout, lies for each key */
    bucket_by_pe(order, nwrite, np,
                 phys_phase, virt_phase, bucket_lda,
                 edge_len, swap_data, bucket_counts,
                 send_displs, buf_data, sr);

    /* Exchange send counts */
    MPI_Alltoall(bucket_counts, 1, MPI_INT64_T,
                 recv_counts, 1, MPI_INT64_T, glb_comm.cm);

    /* calculate offsets */
    recv_displs[0] = 0;
    for (int i=1; i<np; i++){
      recv_displs[i] = recv_displs[i-1] + recv_counts[i-1];
    }
    new_num_pair = recv_displs[np-1] + recv_counts[np-1];

    /*for (i=0; i<np; i++){
      bucket_counts[i] = bucket_counts[i]*sizeof(tkv_pair<dtype>);
      send_displs[i] = send_displs[i]*sizeof(tkv_pair<dtype>);
      recv_counts[i] = recv_counts[i]*sizeof(tkv_pair<dtype>);
      recv_displs[i] = recv_displs[i]*sizeof(tkv_pair<dtype>);
    }*/

    if (new_num_pair > nwrite){
      CTF_int::cdealloc(swap_datab);
      CTF_int::alloc_ptr(sr->pair_size()*new_num_pair, (void**)&swap_datab);
      swap_data = PairIterator(sr, swap_datab);
    }

    /* Exchange data according to counts/offsets */
    //ALL_TO_ALLV(buf_data, bucket_counts, send_displs, MPI_CHAR,
    //            swap_data, recv_counts, recv_displs, MPI_CHAR, glb_comm);
    glb_comm.all_to_allv(buf_data.ptr, bucket_counts, send_displs, sr->pair_size(),
                    swap_data.ptr, recv_counts, recv_displs);
    

    /*printf("[%d] old_num_pair = %d new_num_pair = %d\n",
                  glb_comm->rank, nwrite, new_num_pair);*/

    if (new_num_pair > nwrite){
      CTF_int::cdealloc(buf_datab);
      CTF_int::alloc_ptr(sr->pair_size()*new_num_pair, (void**)&buf_datab);
      buf_data = PairIterator(sr, buf_datab);
    }

    /* Figure out what virtual bucket each key belongs to. Bucket
       and sort them accordingly */
    bucket_by_virt(order, num_virt, new_num_pair, phys_phase, virt_phase,
                       edge_len, swap_data, buf_data, sr);

    /* Write or read the values corresponding to the keys */
    readwrite(order,
              new_num_pair,
              alpha,
              beta,
              num_virt,
              edge_len,
              sym,
              phase,
              phys_phase,
              virt_phase,
              virt_phys_rank,
              rw_data,
              buf_datab,
              rw,
              sr);

    /* If we want to read the keys, we must return them to where they
       were requested */
    if (rw == 'r'){
      CTF_int::alloc_ptr(order*sizeof(int), (void**)&depadding);
      /* Sort the key-value pairs we determine*/
      //std::sort(buf_data, buf_data+new_num_pair);
      buf_data.sort(new_num_pair);
      /* Search for the keys in the order in which we received the keys */
      for (int64_t i=0; i<new_num_pair; i++){
        /*el_loc = std::lower_bound(buf_data,
                                  buf_data+new_num_pair,
                                  swap_data[i]);*/
        int64_t el_loc = buf_data.lower_bound(new_num_pair, swap_data[i]);
  #if (DEBUG>=5)
        ///if (el_loc < buf_data || el_loc >= buf_data+new_num_pair){
        if (el_loc < 0 || el_loc >= new_num_pair){
          DEBUG_PRINTF("swap_data[%d].k = %d, not found\n", i, (int)swap_data[i].k());
          ASSERT(0);
        }
  #endif
        swap_data[i].write_val(buf_data[el_loc].d());
      }
    
      /* Inverse the transpose we did above to get the keys back to requestors */
      //ALL_TO_ALLV(swap_data, recv_counts, recv_displs, MPI_CHAR,
      //            buf_data, bucket_counts, send_displs, MPI_CHAR, glb_comm);
      glb_comm.all_to_allv(swap_data.ptr, recv_counts, recv_displs, sr->pair_size(),
                      buf_data.ptr, bucket_counts, send_displs);
      

      /* unpad the keys if necesary */
      for (int i=0; i<order; i++){
        depadding[i] = -padding[i];
      } 
      pad_key(order, nwrite, edge_len, depadding, buf_data, sr);

      /* Sort the pairs that were sent out, now with correct values */
//      std::sort(buf_data, buf_data+nwrite);
      buf_data.sort(nwrite);
      /* Search for the keys in the same order they were requested */
      j=0;
      for (int64_t i=0; i<inwrite; i++){
        if (j<(int64_t)nchanged && changed_key_indices[j] == i){
          if (changed_key_scale[j] == 0){
            wr_pairs[i].write_val(sr->addid());
          } else {
            //el_loc = std::lower_bound(buf_data, buf_data+nwrite, new_changed_pairs[j]);
            //wr_pairs[i].d = changed_key_scale[j]*el_loc[0].d;
            int64_t el_loc = buf_data.lower_bound(nwrite, ConstPairIterator(sr, new_changed_pairs+j*sr->pair_size()));
            if (changed_key_scale[j] == -1){
              char aspr[sr->el_size];
              sr->addinv(buf_data[el_loc].d(), aspr);
              wr_pairs[i].write_val(aspr);
            } else
              wr_pairs[i].write_val(buf_data[el_loc].d());
          }
          j++;
        } else {
          int64_t el_loc = buf_data.lower_bound(nwrite, wr_pairs[i]);
//          el_loc = std::lower_bound(buf_data, buf_data+nwrite, wr_pairs[i]);
          wr_pairs[i].write_val(buf_data[el_loc].d());
        }
      }
      CTF_int::cdealloc(depadding);
    }
    //FIXME: free here?
    cdealloc(changed_key_indices);
    cdealloc(changed_key_scale);
    cdealloc(new_changed_pairs);
    TAU_FSTOP(wr_pairs_layout);

    CTF_int::cdealloc(swap_datab);
    CTF_int::cdealloc(buf_datab);
    CTF_int::cdealloc((void*)bucket_counts);
    CTF_int::cdealloc((void*)recv_counts);
    CTF_int::cdealloc((void*)send_displs);
    CTF_int::cdealloc((void*)recv_displs);

  }

  void read_loc_pairs(int              order,
                      int64_t          nval,
                      int              num_virt,
                      int const *      sym,
                      int const *      edge_len,
                      int const *      padding,
                      int const *      phase,
                      int const *      phys_phase,
                      int const *      virt_phase,
                      int *            phase_rank,
                      int64_t *        nread,
                      char const *     data,
                      char **          pairs,
                      algstrct const * sr){
    int64_t i;
    int * prepadding;
    char * dpairsb;
    CTF_int::alloc_ptr(sr->pair_size()*nval, (void**)&dpairsb);
    CTF_int::alloc_ptr(sizeof(int)*order,   (void**)&prepadding);
    memset(prepadding, 0, sizeof(int)*order);
    /* Iterate through packed layout and form key value pairs */
    assign_keys(order,
                nval,
                num_virt,
                edge_len,
                sym,
                phase,
                phys_phase,
                virt_phase,
                phase_rank,
                data,
                dpairsb,
                sr);
/*    for (i=0; i<nval; i++){
      printf("\nX[%ld] ", ((int64_t*)(dpairsb+i*sr->pair_size()))[0]);
      sr->print(dpairsb+i*sr->pair_size()+sizeof(int64_t));
    }
*/
    /* If we need to unpad */
    int64_t new_num_pair;
    int * depadding;
    int * pad_len;
    char * new_pairsb;
    CTF_int::alloc_ptr(sr->pair_size()*nval, (void**)&new_pairsb);
   
    PairIterator new_pairs = PairIterator(sr, new_pairsb); 

    CTF_int::alloc_ptr(sizeof(int)*order,   (void**)&depadding);
    CTF_int::alloc_ptr(sizeof(int)*order,   (void**)&pad_len);

    for (i=0; i<order; i++){
      pad_len[i] = edge_len[i]-padding[i];
    }
    /* Get rid of any padded values */
    depad_tsr(order, nval, pad_len, sym, padding, prepadding,
              dpairsb, new_pairsb, &new_num_pair, sr);

    CTF_int::cdealloc(dpairsb);
    if (nval == 0) CTF_int::cdealloc(new_pairsb);
    *pairs = new_pairsb;
    *nread = new_num_pair;

    for (i=0; i<order; i++){
      depadding[i] = -padding[i];
    }
    
    /* Adjust keys to remove padding */
    pad_key(order, new_num_pair, edge_len, depadding, new_pairs, sr);
    CTF_int::cdealloc((void*)pad_len);
    CTF_int::cdealloc((void*)depadding);
    CTF_int::cdealloc(prepadding);
  }

}
       
