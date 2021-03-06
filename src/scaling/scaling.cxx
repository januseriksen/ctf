#include "scaling.h"
#include "strp_tsr.h"
#include "../mapping/mapping.h"
#include "../mapping/distribution.h"
#include "../tensor/untyped_tensor.h"
#include "../shared/util.h"
#include "../shared/memcontrol.h"
#include "sym_seq_scl.h"

namespace CTF_int {

  using namespace CTF;

  scaling::scaling(tensor *     A_,
                   int const *  idx_map_,
                   char const * alpha_){
    A = A_;
    alpha = alpha_;
    is_custom = 0;
    
    idx_map = (int*)alloc(sizeof(int)*A->order);
    memcpy(idx_map, idx_map_, sizeof(int)*A->order);
  }

  scaling::scaling(tensor *     A_,
                   char const * cidx_map,
                   char const * alpha_){
    A = A_;
    alpha = alpha_;
    is_custom = 0;
    
    conv_idx(A->order, cidx_map, &idx_map);
  }

  scaling::scaling(tensor * A_, 
                   int const * idx_map_,
                   endomorphism func_,
                   char const * alpha_){
    A = A_;
    alpha = alpha_;
    func = func_;
    is_custom = 1;
    
    idx_map = (int*)alloc(sizeof(int)*A->order);
    memcpy(idx_map, idx_map_, sizeof(int)*A->order);
  }

  scaling::scaling(tensor * A_, 
                   char const * cidx_map,
                   endomorphism func_,
                   char const * alpha_){
    A = A_;
    alpha = alpha_;
    func = func_;
    is_custom = 1;
    
    conv_idx(A->order, cidx_map, &idx_map);
  }

  scaling::~scaling(){
    cdealloc(idx_map);
  }

  int scaling::execute(){
    int st, is_top, order_tot, iA,  ret, itopo, btopo;
    int64_t blk_sz, vrt_sz;
    distribution * old_dst = NULL;
    int * virt_dim, * idx_arr;
    int * virt_blk_len, * blk_len;
    int64_t nvirt;
    int64_t memuse, bmemuse;
    mapping * map;
    tensor * tsr, * ntsr;
    strp_tsr * str;
    scl * hscl = NULL, ** rec_scl = NULL;

    is_top = 1;
    tsr = A;
    if (tsr->has_zero_edge_len){
      return SUCCESS;
    }

  #if DEBUG>=1
    if (tsr->wrld->cdt.rank == 0){
      printf("Scaling tensor %s.\n", tsr->name);
      printf("The index mapping is");
      for (int i=0; i<tsr->order; i++){
        printf(" %d",idx_map[i]);
      }
      printf("\n");
      printf("Old mapping for tensor %s\n",tsr->name);
    }
    tsr->print_map(stdout);
  #endif
   
  #ifdef HOME_CONTRACT 
    int was_home = tsr->is_home;
    if (was_home){
      ntsr = new tensor(tsr, 0, 0);
      ntsr->data = tsr->data;
      ntsr->home_buffer = tsr->home_buffer;
      ntsr->is_home = 1;
      ntsr->is_mapped = 1;
      ntsr->topo = tsr->topo;
      copy_mapping(tsr->order, tsr->edge_map, ntsr->edge_map);
      ntsr->set_padding();
    } else ntsr = tsr;    
  #else
    ntsr = tsr;
  #endif
    ntsr->unfold();
    ntsr->set_padding();
    inv_idx(ntsr->order, idx_map, 
            &order_tot, &idx_arr);

    CTF_int::alloc_ptr(sizeof(int)*ntsr->order, (void**)&blk_len);
    CTF_int::alloc_ptr(sizeof(int)*ntsr->order, (void**)&virt_blk_len);
    CTF_int::alloc_ptr(sizeof(int)*order_tot, (void**)&virt_dim);

    btopo = -1;
    if (!check_self_mapping(ntsr, idx_map)){
      old_dst = new distribution(ntsr);
      bmemuse = INT64_MAX;
      for (itopo=tsr->wrld->cdt.rank; itopo<(int)tsr->wrld->topovec.size(); itopo+=tsr->wrld->cdt.np){
        ntsr->clear_mapping();
        ntsr->set_padding();
        ntsr->topo = tsr->wrld->topovec[itopo];
        ret = map_self_indices(ntsr, idx_map);
        if (ret!=SUCCESS) continue;
        ret = ntsr->map_tensor_rem(ntsr->topo->order,
                             ntsr->topo->dim_comm, 0);
        if (ret!=SUCCESS) continue;
        ret = map_self_indices(ntsr, idx_map);
        if (ret!=SUCCESS) continue;
        if (check_self_mapping(ntsr, idx_map)) {
          ntsr->set_padding();
          memuse = ntsr->size;

          if (memuse >= proc_bytes_available()){
            DPRINTF(1,"Not enough memory to scale tensor on topo %d\n", itopo);
            continue;
          }

          nvirt = ntsr->calc_nvirt();
          ASSERT(nvirt != 0);
          if (btopo == -1){
            btopo = itopo;
            bmemuse = memuse;
          } else if (memuse < bmemuse){
            btopo = itopo;
            bmemuse = memuse;
          }
        }
      }
      if (btopo == -1){
        bmemuse = INT64_MAX;
      }
      btopo = get_best_topo(1, btopo, tsr->wrld->cdt, 0.0, bmemuse);

      if (btopo == -1 || btopo == INT_MAX) {
        if (tsr->wrld->cdt.rank==0)
          printf("ERROR: FAILED TO MAP TENSOR SCALE\n");
        return ERROR;
      }

      ntsr->clear_mapping();
      ntsr->set_padding();
      ntsr->is_cyclic = 1;
      ntsr->topo = tsr->wrld->topovec[btopo];
      ret = map_self_indices(ntsr, idx_map);
      if (ret!=SUCCESS) ABORT;
      ret = ntsr->map_tensor_rem(ntsr->topo->order,
                           ntsr->topo->dim_comm, 0);
      if (ret!=SUCCESS) ABORT;
      ret = map_self_indices(ntsr, idx_map);
      if (ret!=SUCCESS) ABORT;
      ntsr->set_padding();
  #if DEBUG >=1
      if (tsr->wrld->cdt.rank == 0){
        printf("New mapping for tensor %s\n",ntsr->name);
      }
      ntsr->print_map(stdout);
  #endif
      TAU_FSTART(redistribute_for_scale);
      ntsr->redistribute(*old_dst);
      TAU_FSTOP(redistribute_for_scale);
    }

    blk_sz = ntsr->size;
    calc_dim(ntsr->order, blk_sz, ntsr->pad_edge_len, ntsr->edge_map,
             &vrt_sz, virt_blk_len, blk_len);

    st = strip_diag(ntsr->order, order_tot, idx_map, vrt_sz,
                           ntsr->edge_map, ntsr->topo, ntsr->sr,
                           blk_len, &blk_sz, &str);
    if (st){
      if (tsr->wrld->cdt.rank == 0)
        DPRINTF(1,"Stripping tensor\n");
      strp_scl * sscl = new strp_scl;
      sscl->sr_A = tsr->sr;
      hscl = sscl;
      is_top = 0;
      rec_scl = &sscl->rec_scl;

      sscl->rec_strp = str;
    }

    nvirt = 1;
    for (int i=0; i<order_tot; i++){
      iA = idx_arr[i];
      if (iA != -1){
        map = &ntsr->edge_map[iA];
        while (map->has_child) map = map->child;
        if (map->type == VIRTUAL_MAP){
          virt_dim[i] = map->np;
          if (st) virt_dim[i] = virt_dim[i]/str->strip_dim[iA];
        }
        else virt_dim[i] = 1;
      } else virt_dim[i] = 1;
      nvirt *= virt_dim[i];
    }

    /* Multiply over virtual sub-blocks */
    if (nvirt > 1){
      scl_virt * sclv = new scl_virt;
      sclv->sr_A = tsr->sr;
      if (is_top) {
        hscl = sclv;
        is_top = 0;
      } else {
        *rec_scl = sclv;
      }
      rec_scl = &sclv->rec_scl;

      sclv->num_dim   = order_tot;
      sclv->virt_dim  = virt_dim;
      sclv->order_A  = ntsr->order;
      sclv->blk_sz_A  = vrt_sz;
      sclv->idx_map_A = idx_map;
      sclv->buffer  = NULL;
    } else CTF_int::cdealloc(virt_dim);

    seq_tsr_scl * sclseq = new seq_tsr_scl;
    sclseq->sr_A = tsr->sr;
    if (is_top) {
      hscl = sclseq;
      is_top = 0;
    } else {
      *rec_scl = sclseq;
    }
    sclseq->alpha       = alpha;
    hscl->alpha         = alpha;
    if (is_custom){
      sclseq->func      = func;
      sclseq->is_custom = 1;
    } else {
      sclseq->is_custom = 0;
    }
    sclseq->order       = ntsr->order;
    sclseq->idx_map     = idx_map;
    sclseq->edge_len    = virt_blk_len;
    sclseq->sym         = ntsr->sym;

    hscl->A   = ntsr->data;

    CTF_int::cdealloc(idx_arr);
    CTF_int::cdealloc(blk_len);

    hscl->run();
    delete hscl;
    


  #ifdef HOME_CONTRACT
    if (was_home && !ntsr->is_home){
      if (tsr->wrld->cdt.rank == 0)
        DPRINTF(2,"Migrating tensor %s back to home\n", tsr->name);
      if (old_dst != NULL) delete old_dst;
      old_dst = new distribution(ntsr);
/*      save_mapping(ntsr,
                   &old_phase, &old_rank, 
                   &old_virt_dim, &old_pe_lda, 
                   &old_size,  
                   &was_cyclic, &old_padding, 
                   &old_edge_len, &ntsr->topo);*/
      tsr->data = ntsr->data;
      tsr->is_home = 0;
      TAU_FSTART(redistribute_for_scale_home);
      tsr->redistribute(*old_dst);
      TAU_FSTOP(redistribute_for_scale_home);
      memcpy(tsr->home_buffer, tsr->data, tsr->size*tsr->sr->el_size);
      CTF_int::cdealloc(tsr->data);
      tsr->data = tsr->home_buffer;
      tsr->is_home = 1;
      ntsr->is_data_aliased = 1;
      delete ntsr;
    } else if (was_home){
      if (ntsr->data != tsr->data){
        printf("Tensor %s is a copy of %s and did not leave home but buffer is %p was %p\n", ntsr->name, tsr->name, ntsr->data, tsr->data);
        ABORT;

      }
      ntsr->has_home = 0;
      ntsr->is_data_aliased = 1;
      delete ntsr;
    }
  #endif

   if (old_dst != NULL) delete old_dst;
  #if DEBUG>=1
    if (tsr->wrld->cdt.rank == 0)
      printf("Done scaling tensor %s.\n", tsr->name);
  #endif

    return SUCCESS;

  }


}
