#include "ite.h"
#include "bddnode.h"
#include "symtable.h"
#include "functions.h"
#include "p3.h"

int p3_max = 0;
t_ctl *p3 = NULL;

void
p3_done()
{
   p3[0].top_and = 1;
   p3_traverse_ref(0, -1);
   //p3_dump();
   p3_top_bdds(0);
}

void
p3_add_ref(int idx, int ref)
{
   if (p3[idx].refs_idx+1 >= p3[idx].refs_max) {
      p3[idx].refs_max += 10;
      p3[idx].refs = (int*)realloc(p3[idx].refs, p3[idx].refs_max*sizeof(int));
   }
   p3[idx].refs[p3[idx].refs_idx++] = ref;
}

void
p3_traverse_ref(int i, int ref)
{
   int idx = -1*i;
   assert(idx >= 0);
   p3[idx].ref++;
   if (ref >= 0) p3_add_ref(idx, ref);
   if (p3[idx].ref > 1) return;
   if (p3[idx].top_and && p3[idx].tag == AND_Tag) {
      if (p3[idx].arg1 < 0 && p3[-1*p3[idx].arg1].tag == AND_Tag) 
         p3[-1*p3[idx].arg1].top_and = 1;
      if (p3[idx].argc == 2 && p3[idx].arg2 < 0 && p3[-1*p3[idx].arg2].tag == AND_Tag) 
         p3[-1*p3[idx].arg2].top_and = 1;
   }
   if (p3[idx].arg1 < 0) p3_traverse_ref(p3[idx].arg1, idx);
   if (p3[idx].argc == 2 && p3[idx].arg2 < 0) p3_traverse_ref(p3[idx].arg2, idx);
}



void
p3_alloc(int max)
{
   p3 = (t_ctl*)realloc((void*)p3, max*sizeof(t_ctl));
   memset((char*)p3+p3_max*sizeof(t_ctl), 0, (max-p3_max)*sizeof(t_ctl));
   p3_max = max;
}

void
prover3_free()
{
   for (int i=0;i<p3_max;i++) {
      ite_free((void**)&p3[i].refs);
   }
   ite_free((void**)&p3); p3_max = 0;
}
      
void
p3_add(char *dst, int argc, int tag, char *arg1, char *arg2)
{
   int i_dst = s2id(dst);
   int i_arg1 = 0;
   int i_arg2 = 0;

   if (arg1[0] == 'S') i_arg1 = -1*atoi(arg1+1);
   else i_arg1 = i_getsym(arg1, SYM_VAR); 
   if (argc == 2) {
      if (arg2[0] == 'S') i_arg2 = -1*atoi(arg2+1);
      else i_arg2 = i_getsym(arg2, SYM_VAR); 
   }
   if (p3_max <= i_dst) p3_alloc(i_dst+10);

   p3[i_dst].tag = tag;
   p3[i_dst].argc = argc;
   p3[i_dst].arg1 = i_arg1;
   p3[i_dst].arg2 = i_arg2;
/*
   if (argc == 1) 
      fprintf(stderr, "%s = not %s \n", dst, arg1);
   else
      fprintf(stderr, "%s = %s %s \n", dst, arg1, arg2);
*/
}

int
s2id(char *s_id)
{
   int i;
   if(s_id[0] != 'S') { fprintf(stderr, "temp var without 'S'\n"); exit(1); };
   i = atoi(s_id+1);
   return i;
}

void
p3_dump()
{
   int i;
   for(i=0;i<p3_max;i++) {
      switch (p3[i].argc) {
       case 2: fprintf(stderr, "%cS%d = %d %d (%d)\n", 
                     p3[i].top_and?'*':' ', i, p3[i].arg1, p3[i].arg2, p3[i].ref); break;
       case 1: fprintf(stderr, "%cS%d = %d (%d)\n", 
                     p3[i].top_and?'*':' ', i, p3[i].arg1, p3[i].ref); break;
       default: break;
      }
   }
}

BDDNode *
p3_bdds(int idx, int *vars)
{
   BDDNode *bdd = NULL;
   if (idx < 0) {
      BDDNode *bdd1 = NULL;
      BDDNode *bdd2 = NULL;
      int vars1 = 0;
      int vars2 = 0;
      idx = -1*idx;
      if (p3[idx].bdd != NULL) {
         *vars = p3[idx].vars;
         return p3[idx].bdd;
      }
      bdd1 = p3_bdds(p3[idx].arg1, &vars1);
      if (vars1 > prover3_max_vars) {
         bdd1 = tmp_equ_var(bdd1);
         vars1 = 1;
      }
      if (p3[idx].argc == 2) {
         bdd2 = p3_bdds(p3[idx].arg2, &vars2);
         if (vars2 > prover3_max_vars) {
            bdd2 = tmp_equ_var(bdd2);
            vars2 = 1;
         }
      }
      switch(p3[idx].tag) {
       case OR_Tag: bdd = ite_or(bdd1, bdd2); break;
       case AND_Tag: bdd = ite_and(bdd1, bdd2); break;
       case IMP_Tag: bdd = ite_imp(bdd1, bdd2); break;
       case EQUIV_Tag: bdd = ite_equ(bdd1, bdd2); break;
       case NOT_Tag: bdd = ite_not(bdd1); break;
       default: assert(0); exit(1); break;
      }
      
      if (vars1+vars2 > prover3_max_vars) {
         bdd = tmp_equ_var(bdd);
      }
      
      *vars = vars1 + vars2;
      p3[idx].bdd = bdd;
      p3[idx].vars = *vars;
   } else {
      /* variable */
      bdd = ite_var(idx);
      *vars = 1;
   }
   return bdd;
}

void
p3_top_bdds(int idx)
{
   int vars;
   if (idx > 0 || p3[-1*idx].top_and == 0) {
      functions_add(p3_bdds(idx, &vars), UNSURE, 0);
   } else {
      idx = -1*idx;
      d3_printf4("%d vars=%d functions=%d\r", idx, vars_max, functions_max);
      p3_top_bdds(p3[idx].arg1);
      if (p3[idx].argc==2) p3_top_bdds(p3[idx].arg2);
   }
}

