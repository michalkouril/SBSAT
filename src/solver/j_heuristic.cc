/* =========FOR INTERNAL USE ONLY. NO DISTRIBUTION PLEASE ========== */

/*********************************************************************
 Copyright 1999-2003, University of Cincinnati.  All rights reserved.
 By using this software the USER indicates that he or she has read,
 understood and will comply with the following:

 --- University of Cincinnati hereby grants USER nonexclusive permission
 to use, copy and/or modify this software for internal, noncommercial,
 research purposes only. Any distribution, including commercial sale
 or license, of this software, copies of the software, its associated
 documentation and/or modifications of either is strictly prohibited
 without the prior consent of University of Cincinnati.  Title to copyright
 to this software and its associated documentation shall at all times
 remain with University of Cincinnati.  Appropriate copyright notice shall
 be placed on all software copies, and a complete copy of this notice
 shall be included in all copies of the associated documentation.
 No right is  granted to use in advertising, publicity or otherwise
 any trademark,  service mark, or the name of University of Cincinnati.


 --- This software and any associated documentation is provided "as is"

 UNIVERSITY OF CINCINNATI MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS
 OR IMPLIED, INCLUDING THOSE OF MERCHANTABILITY OR FITNESS FOR A
 PARTICULAR PURPOSE, OR THAT  USE OF THE SOFTWARE, MODIFICATIONS, OR
 ASSOCIATED DOCUMENTATION WILL NOT INFRINGE ANY PATENTS, COPYRIGHTS,
 TRADEMARKS OR OTHER INTELLECTUAL PROPERTY RIGHTS OF A THIRD PARTY.

 University of Cincinnati shall not be liable under any circumstances for
 any direct, indirect, special, incidental, or consequential damages
 with respect to any claim by USER or any third party on account of
 or arising from the use, or inability to use, this software or its
 associated documentation, even if University of Cincinnati has been advised
 of the possibility of those damages.
 *********************************************************************/

// assert USE_LEMMA_VAR_HEURISTIC is not set... !

#include "ite.h"
#include "solver.h"

extern int *arrLemmaVbleCountsPos;
extern int *arrLemmaVbleCountsNeg;

extern SmurfState **arrRegSmurfInitialStates;
extern int gnMaxVbleIndex;
double *arrJWeights; 

ITE_INLINE SmurfState * GetSmurfState(int i);
ITE_INLINE void J_SetupHeuristicScores();
ITE_INLINE void J_Setup_arrJWeights();
ITE_INLINE void GetHeurScoresFromSmurf(int i);

/* 

 other files and j_heuristic.cc
 ------------------------------

 select_bp.cc -- calls this heuristic
 -- push the stack

 backtrack.cc -- pop the stack 

 init_solver.cc -- allocates arrHeurScoresNeg, Pos

 bt_smurfs.cc  -- one part (Update heuristic scores from 
 info stored on the transition)

 smurffactory.cc -- jheuristic 
 -- jheuristic_optimization:
 * calls HeuristicDisplayTransitionInfo
 * setup heuristic (delta arrays) info on transitions

 */


// The constant c is taken from the documentation on the heuristics
// which was provided in the Summer 2000 progress report.
// The constant is added into the positive and negative scores of
// each variable.  So, for example, the positive heuristic score
// for a variable is c plus the sum of the weights of the positive transitions
// for that variable in all of the constraints which mention the variable.
// The Summer 2000 report set c = 1/4096.  MvF used c = 1 in his prototype.
//static const double c_heur = 1.0 / 4096.0;
static const double c_heur = 1.0;

extern int nNumSpecialFuncs;
extern int nNumRegSmurfs;	// Number of regular Smurfs.
extern int nNumVariables;
extern int nNumChoicePts;

HeurScores *arrHeurScores=NULL;

extern SpecialFunc *arrSpecialFuncs;

ITE_INLINE void Mark_arrHeurScoresStack(int vx);

void
DisplayUpdatesToHeuristicTransScores(Transition *pTransition);

void
J_UpdateHeuristicScoresFromTransition(Transition *pTransition);

#define HEUR_SCORES_STACK_ALLOC_MULT 6 /* >= 2 */
#define MAX_HEUR_SCORES_STACK_POOL 100
int nCurHeurScoresVersion=1;
int nHeurScoresStackIdx=0;
int nMaxHeurScoresStackIdx=0;
tHeurScoresStack *arrHeurScoresStack=NULL;
int *arrHeurScoresFlags=NULL;
typedef struct { tHeurScoresStack *stack; int max; } tHeurScoresStackPool;
tHeurScoresStackPool *arrHeurScoresStackPool=NULL;
int nHeurScoresStackPool=0;    
int nHeurScoresStackPoolMax=MAX_HEUR_SCORES_STACK_POOL;

ITE_INLINE void
J_AllocateHeurScoresStack(int newsize)
{
   assert(newsize > 0);

   if (arrHeurScoresFlags==NULL) {
      arrHeurScoresFlags = (int*)ite_calloc(nNumVariables, sizeof(int),
            9, "arrHeurScoresFlags");
      arrHeurScoresStackPool = (tHeurScoresStackPool*)ite_calloc(
            nHeurScoresStackPoolMax, sizeof (tHeurScoresStackPool),
            9, "arrHeurScoresStackPool");
   }

   if (nHeurScoresStackPool >= nHeurScoresStackPoolMax) {
      fprintf(stderr, "Increase MAX_HEUR_SCORES_STACK_POOL or realloc array\n");
      exit(1);
   }

   if (arrHeurScoresStackPool[nHeurScoresStackPool].stack == NULL) {
      newsize += INIT_STACK_BACKTRACKS_ALLOC*4; 

      arrHeurScoresStackPool[nHeurScoresStackPool].max = newsize;

      arrHeurScoresStackPool[nHeurScoresStackPool].stack =
         (tHeurScoresStack*)ite_calloc(newsize, sizeof(tHeurScoresStack), 2,
                                   "j heuristic states stack");

      tHeurScoresStack* prev_arrHeurScoresStack = arrHeurScoresStack;
      arrHeurScoresStack = arrHeurScoresStackPool[nHeurScoresStackPool].stack;

      /* save the prev index*/
      arrHeurScoresStack[0].u.index_pool  = nHeurScoresStackIdx;
      arrHeurScoresStack[1].u.next_pool   = (void *)prev_arrHeurScoresStack;
      arrHeurScoresStack[2].v             = POOL_START;
      arrHeurScoresStack[newsize-1].v     = POOL_END;

   } else {
      arrHeurScoresStack = arrHeurScoresStackPool[nHeurScoresStackPool].stack;
   }

   nHeurScoresStackIdx    = 2;
}

ITE_INLINE void
J_FreeHeurScoresStack ()
{
   if (arrHeurScoresStackPool) {
      for (int i=0;i<=nHeurScoresStackPoolMax && arrHeurScoresStackPool[i].stack;i++)
         free(arrHeurScoresStackPool[i].stack);
      free(arrHeurScoresStackPool);
      free(arrHeurScoresFlags);
   }
   if (arrHeurScores) free(arrHeurScores);
}

ITE_INLINE void
J_InitHeuristicScores()
// Initialize the heuristic scores for all of the variables
// the problem.  Called before the search begins.
{
   d2_printf1("Initializing heuristicScores\n");

   pTrueSmurfState->fNodeHeuristicWeight = JHEURISTIC_K_TRUE;

   J_Setup_arrJWeights();
   J_SetupHeuristicScores();

      for (int i = 0; i < nNumSpecialFuncs; i++) {
         for (int j=0; j<arrSpecialFuncs[i].rhsVbles.nNumElts; j++)
            arrSumRHSUnknowns[i] += arrJWeights[arrSpecialFuncs[i].rhsVbles.arrElts[j]];
         arrPrevSumRHSUnknowns[i] = arrSumRHSUnknownsNew[i] = arrSumRHSUnknowns[i];
      }


   for (int i = 0; i < nNumVariables; i++)
   {
      arrHeurScores[i].Pos = arrHeurScores[i].Neg = 0; //c_heur;
   }

   // Loop through the regular Smurfs.
   for (int i = 0; i < nNumRegSmurfs; i++)
   {
      GetHeurScoresFromSmurf(i);
   } 

   // Loop through the special functions.
   for (int i = 0; i < nNumSpecialFuncs; i++)
   {
      switch (arrSpecialFuncs[i].nFunctionType) {
       case AND: GetHeurScoresFromSpecialFunc_AND_C(i); break;
       //case AND: GetHeurScoresFromSpecialFunc_AND(i); break;
       case XOR: GetHeurScoresFromSpecialFunc_XOR_C(i); break;
       //case XOR: GetHeurScoresFromSpecialFunc_XOR(i); break;
       default: assert(0); break;
      }
   }

   /* 
    DisplayJHeuristicValues();
    int i = 0;
    while ((pState = GetSmurfState(i)) != NULL)
    {
    fprintf(stddbg, "State ");
    fprintf(stddbg, "%2d: ", i);
    for(int j=0;j<pState->vbles.nNumElts;j++) {
    fprintf(stddbg, "%d ", arrSolver2IteVarMap[pState->vbles.arrElts[j]]);
    }
    fprintf(stddbg, "\n");
    if (pState->pFunc) { printBDDerr(pState->pFunc); fprintf(stddbg, "\n"); }
    fprintf(stddbg, "score: %f\n", pState->fNodeHeuristicWeight);
    for(int j=0;j<pState->nNumHeuristicXors;j++)
    fprintf(stddbg, "[%d]: %f\n", j, pState->arrHeuristicXors[j]);
    i++;
    }
    */
}

ITE_INLINE void
J_FreeHeuristicScores()
{
   if (arrJWeights) ite_free((void*)arrJWeights);
}

#define J_ONE 0

// CLASSIC
#define J_WEIGHT(x) (J_ONE+x.Pos) * (J_ONE+x.Neg)

// ABSOLUTE MAXIMUM
//#define J_WEIGHT(x) (x.Neg > x.Pos ? x.Neg : x.Pos)

// BERM
//#define J_WEIGHT(x) (x.Neg>x.Pos?((x.Pos*2) + x.Neg):((x.Neg*2) + x.Pos)) 

// ADDITION
//#define J_WEIGHT(x) (J_ONE+x.Pos) + (J_ONE+x.Neg)

// NEW??
//#define J_WEIGHT(x) (((x.nPosInfs+1)*x.nPos)* (x.nNegInfs+1)*x.nNeg)

// slider_80_unsat -- the order from the best to worst is (all J_ONE = 0)
// CLASSIC
// BERM
// ADDITION
// ABSOLUTE MAXIMUM


// * arrLemmaVbleCountsPos[i] * arrLemmaVbleCountsNeg[i]

ITE_INLINE void
J_OptimizedHeuristic(int *pnBranchAtom, int *pnBranchValue)
{
   int nBestVble;
   int i;
   double fMaxWeight = 0.0;
   double fVbleWeight;

   D_8(DisplayJHeuristicValues(););
   d8_printf1("\n");

   //
   // SEARCH INDEPENDENT VARIABLES
   //
   // Determine the variable with the highest weight:
   // 
   // Initialize to the lowest indexed variable whose value is uninstantiated.
   nBestVble = -1;

   for (i = 1; i < nIndepVars+1; i++)
   {
      if (arrSolution[i] == BOOL_UNKNOWN)
      {
        //if (arrHeurScores[i].Pos == 0 && arrHeurScores[i].Neg == 0) continue;
         nBestVble = i;
         fMaxWeight = J_WEIGHT(arrHeurScores[i]);
         break;
      }
   }

   if (nBestVble >= 0)
   {
      // Search through the remaining uninstantiated independent variables.
      for (i = nBestVble + 1; i < nIndepVars+1; i++)
      {
         if (arrSolution[i] == BOOL_UNKNOWN)
         {
            //if (arrHeurScores[i].Pos == 0 && arrHeurScores[i].Neg == 0) continue;
            fVbleWeight = J_WEIGHT(arrHeurScores[i]);
            if (fVbleWeight > fMaxWeight)
            {
               fMaxWeight = fVbleWeight;
               nBestVble = i;
            }
         }
      }

# ifdef TRACE_HEURISTIC
      cout << endl << "X" << nBestVble << "*: then "
         << arrHeurScores[nBestVble].Pos
         << " else " << arrHeurScores[nBestVble].Neg
         << " -> " << fMaxWeight << "  ";
# endif

      goto ReturnHeuristicResult;
   }

   //
   // SEARCH DEPENDENT VARIABLES
   //
   // Initialize to first uninstantiated variable.
   for (i = nIndepVars+1; i < nIndepVars+1+nDepVars; i++)
   {
      if (arrSolution[i] == BOOL_UNKNOWN)
      {
         //if (arrHeurScores[i].Pos == 0 && arrHeurScores[i].Neg == 0) continue;
         nBestVble = i;
         fMaxWeight = J_WEIGHT(arrHeurScores[i]);
         break;
      }
   }

   if (nBestVble >= 0)
   {
      // Search through the remaining uninstantiated independent variables.
      for (i = nBestVble + 1; i < nIndepVars+1+nDepVars; i++)
      {
         if (arrSolution[i] == BOOL_UNKNOWN)
         {
            //if (arrHeurScores[i].Pos == 0 && arrHeurScores[i].Neg == 0) continue;
            fVbleWeight = J_WEIGHT(arrHeurScores[i]);
            if (fVbleWeight > fMaxWeight)
            {
               fMaxWeight = fVbleWeight;
               nBestVble = i;
            }
         }
      }

#ifdef TRACE_HEURISTIC
      cout << endl << "X" << nBestVble << ": then "
         << arrHeurScores[nBestVble].Pos
         << " else " << arrHeurScores[nBestVble].Neg
         << " -> " << fMaxWeight << "  ";
#endif
      goto ReturnHeuristicResult;
   }

   //
   // SEARCH TEMP VARIABLES
   //
   // Initialize to first uninstantiated variable.
   for (i = nIndepVars+1+nDepVars; i < nNumVariables; i++)
   {
      if (arrSolution[i] == BOOL_UNKNOWN)
      {
         nBestVble = i;
         fMaxWeight = J_WEIGHT(arrHeurScores[i]);
         break;
      }
   }

   if (nBestVble >= 0)
   {
      // Search through the remaining uninstantiated independent variables.
      for (i = nBestVble + 1; i < nNumVariables; i++)
      {
         if (arrSolution[i] == BOOL_UNKNOWN)
         {
            fVbleWeight = J_WEIGHT(arrHeurScores[i]);
            if (fVbleWeight > fMaxWeight)
            {
               fMaxWeight = fVbleWeight;
               nBestVble = i;
            }
         }
      }

      goto ReturnHeuristicResult;
   }
   else
   {
      dE_printf1 ("Error in heuristic routine:  No uninstantiated variable found\n");
      exit (1);
   }

ReturnHeuristicResult:
   assert (arrSolution[nBestVble] == BOOL_UNKNOWN);
   *pnBranchAtom = nBestVble;
   if (arrHeurScores[nBestVble].Pos >= arrHeurScores[nBestVble].Neg)
   {
      *pnBranchValue = BOOL_TRUE;
   }
   else
   {
      *pnBranchValue = BOOL_FALSE;
   }
   d8_printf6("JHeuristic: %c%d (%.10f,%.10f) because of %f\n", 
         (*pnBranchValue==BOOL_TRUE?'+':'-'),
         nBestVble, 
         arrHeurScores[nBestVble].Pos,
         arrHeurScores[nBestVble].Neg,
         fMaxWeight);
}


ITE_INLINE
double
GetHeurWeight(SmurfState *pState, int i, int nVble, int nValue)
   // Returns the heuristic weight of a transition.
{
   Transition *pTransition;

   if (pState == pTrueSmurfState) return 0;

   assert(i<pState->vbles.nNumElts && pState->vbles.arrElts[i] == nVble);
/*
   else {
      int j=0;
      if (pState->vbles.nNumElts <= i || pState->vbles.arrElts[i] != nVble) {
         for (j=0; j<pState->vbles.nNumElts; j++)
            if (pState->vbles.arrElts[j] == nVble) break;
         if (j==pState->vbles.nNumElts) return 0;
         i = j;
      }
   }
*/

   assert(pState->arrTransitions != 0);
/*
   if (pState->arrTransitions == 0)
   {
      // 'true' state -- all variables have weight 0
      return 0.0;
   }
*/
   if ((pTransition=FindTransition(pState, i, nVble, nValue))==NULL) return 0;

   return pTransition->fHeuristicWeight;
}

ITE_INLINE
void
J_InitHeuristic()
{
   arrHeurScores = (HeurScores *)ite_calloc(nNumVariables, sizeof(HeurScores), 2,
         "heuristic scores");

   J_AllocateHeurScoresStack (nNumVariables * HEUR_SCORES_STACK_ALLOC_MULT);
}


ITE_INLINE
void
J_PushHeuristicScores()
{
   nCurHeurScoresVersion++;
   Mark_arrHeurScoresStack(LEVEL_START);
}

ITE_INLINE
void
Mark_arrHeurScoresStack(int vx)
{
   nHeurScoresStackIdx++;
   if (arrHeurScoresStack[nHeurScoresStackIdx].v == POOL_END)
   {
      nHeurScoresStackPool++;
      J_AllocateHeurScoresStack (nNumVariables * HEUR_SCORES_STACK_ALLOC_MULT);
      nHeurScoresStackIdx++;
   }
   arrHeurScoresStack[nHeurScoresStackIdx].v = vx;
}

ITE_INLINE
void
Add_arrHeurScoresStack(int vx)
{
   nHeurScoresStackIdx++;
   if (arrHeurScoresStack[nHeurScoresStackIdx].v == POOL_END)
   {
      nHeurScoresStackPool++;
      J_AllocateHeurScoresStack (nNumVariables * HEUR_SCORES_STACK_ALLOC_MULT);
      nHeurScoresStackIdx++;
   }

   arrHeurScoresStack[nHeurScoresStackIdx].v = vx;  
   arrHeurScoresStack[nHeurScoresStackIdx].h = arrHeurScores[vx];
   arrHeurScoresStack[nHeurScoresStackIdx].prev  = arrHeurScoresFlags[vx]; 
   arrHeurScoresFlags[vx]=nCurHeurScoresVersion;
}

ITE_INLINE
void
J_PopHeuristicScores()
{
   /* pop heur scores stack */
   assert(nHeurScoresStackIdx>0);

   /* until LEVEL_START */
   while (arrHeurScoresStack[nHeurScoresStackIdx].v != LEVEL_START) 
   {
      int v=arrHeurScoresStack[nHeurScoresStackIdx].v;
      if (v == POOL_START) {
         nHeurScoresStackPool--;
         nHeurScoresStackIdx--;
         tHeurScoresStack *new_arrHeurScoresStack = 
            (tHeurScoresStack*)
            (arrHeurScoresStack[nHeurScoresStackIdx--].u.next_pool);
         nHeurScoresStackIdx =
            arrHeurScoresStack[nHeurScoresStackIdx].u.index_pool;
         assert(new_arrHeurScoresStack[nHeurScoresStackIdx].v==POOL_END);
         arrHeurScoresStack = new_arrHeurScoresStack;
      } else
         if (v >= 0) {
            arrHeurScores[v] =arrHeurScoresStack[nHeurScoresStackIdx].h;
            arrHeurScoresFlags[v]=arrHeurScoresStack[nHeurScoresStackIdx].prev;
         }
      nHeurScoresStackIdx--;
   }
   nHeurScoresStackIdx--; /* skip the LEVEL_START */
   assert(nHeurScoresStackIdx>0);
   nCurHeurScoresVersion--;
}

ITE_INLINE void
DisplayJHeuristicValues()
{
   fprintf(stddbg, "JHeuristic values: \n");
   for(int i=0;i<gnMaxVbleIndex;i++)
   {
      if (arrSolution[i]==BOOL_UNKNOWN) {
         fprintf(stddbg, "+%d(%d): %f%c\n", i, arrSolver2IteVarMap[i], arrHeurScores[i].Pos, arrSolution[i]!=BOOL_UNKNOWN?'*':' ');
         fprintf(stddbg, "-%d(%d): %f%c\n", i, arrSolver2IteVarMap[i], arrHeurScores[i].Neg, arrSolution[i]!=BOOL_UNKNOWN?'*':' ');
      }
   }
}


// Update scores of RHS variables.
ITE_INLINE void
J_Update_RHS_AND(SpecialFunc * pSpecialFunc, HWEIGHT fPosDelta, HWEIGHT fNegDelta)
{
   int nNumRHSUnknowns = pSpecialFunc->rhsVbles.nNumElts;
   int *arrRHSVbles = pSpecialFunc->rhsVbles.arrElts;
   int *arrRHSPolarities = pSpecialFunc->arrRHSPolarities;
   int nRHSVble;
   for (int i = 0; i < nNumRHSUnknowns; i++)
   {
      nRHSVble = arrRHSVbles[i];
      assert(nRHSVble>0);
      if (arrSolution[nRHSVble] == BOOL_UNKNOWN)
      { 
         Save_arrHeurScores(nRHSVble);
         if (arrRHSPolarities[i] == BOOL_TRUE)
         {
            // Variable polarity is positive.
            arrHeurScores[nRHSVble].Pos += fPosDelta;
            arrHeurScores[nRHSVble].Neg += fNegDelta;
         }
         else
         {
            // Variable polarity is negative.
            arrHeurScores[nRHSVble].Pos += fNegDelta;
            arrHeurScores[nRHSVble].Neg += fPosDelta;
         }
      }
      /*
       else
       {
       arrHeurScores[nRHSVble].Pos = 0;
       arrHeurScores[nRHSVble].Neg = 0;
       }
       */
   } // for each RHS vble
}

// Update scores of RHS variables.
ITE_INLINE void
J_Update_RHS_AND_C(SpecialFunc * pSpecialFunc, 
     double fLastSum, double fLastConstPos, double fLastMultiPos, double fLastConstNeg, double fLastMultiNeg, 
     double fSum, double fConstPos, double fMultiPos, double fConstNeg, double fMultiNeg)
{
   int nNumRHSUnknowns = pSpecialFunc->rhsVbles.nNumElts;
   int *arrRHSVbles = pSpecialFunc->rhsVbles.arrElts;
   int *arrRHSPolarities = pSpecialFunc->arrRHSPolarities;
   int nRHSVble;

   d9_printf4("J_Update_RHS_AND_C(pSpecialFunc, \n fLastSum=%f, fLastConstPos=%f, fLastMultiPos=%f, ", 
         fLastSum, fLastConstPos, fLastMultiPos);
   d9_printf3("fLastConstNeg=%f, fLastMultiNeg=%f,\n ", fLastConstNeg, fLastMultiNeg);
   d9_printf4("fSum=%f, fConstPos=%f, fMultiPos=%f, ", fSum, fConstPos, fMultiPos);
   d9_printf3("fConstNeg=%f, fMultiNeg=%f)\n", fConstNeg, fMultiNeg);

   for (int i = 0; i < nNumRHSUnknowns; i++)
   {
      nRHSVble = arrRHSVbles[i];
      assert(nRHSVble>0);
      if (arrSolution[nRHSVble] == BOOL_UNKNOWN)
      { 
         Save_arrHeurScores(nRHSVble);
         HWEIGHT fPosDelta = fConstPos - fLastConstPos + 
            (fSum-arrJWeights[nRHSVble])*fMultiPos - 
            (fLastSum-arrJWeights[nRHSVble])*fLastMultiPos;
         HWEIGHT fNegDelta = fConstNeg - fLastConstNeg + 
            (fSum-arrJWeights[nRHSVble])*fMultiNeg - 
            (fLastSum-arrJWeights[nRHSVble])*fLastMultiNeg;
         if (arrRHSPolarities[i] == BOOL_TRUE)
         {
            // Variable polarity is positive.
            arrHeurScores[nRHSVble].Pos += fPosDelta;
            arrHeurScores[nRHSVble].Neg += fNegDelta;
         }
         else
         {
            // Variable polarity is negative.
            arrHeurScores[nRHSVble].Pos += fNegDelta;
            arrHeurScores[nRHSVble].Neg += fPosDelta;
         }
      }
      /*
       else
       {
       arrHeurScores[nRHSVble].Pos = 0;
       arrHeurScores[nRHSVble].Neg = 0;
       }
       */
   } // for each RHS vble
}

ITE_INLINE void
J_SetHeurWeightsTrueState(int nNumXors)
{
         if (pTrueSmurfState->nNumHeuristicXors < nNumXors) {
            pTrueSmurfState->arrHeuristicXors 
               = (double*)realloc(pTrueSmurfState->arrHeuristicXors, nNumXors*sizeof(double));
            pTrueSmurfState->nNumHeuristicXors = nNumXors;
            pTrueSmurfState->arrHeuristicXors[0] = 0;       
            pTrueSmurfState->arrHeuristicXors[1] = 0;      
            pTrueSmurfState->arrHeuristicXors[2] = 1;
            for(int j=3;j<nNumXors;j++) {
               pTrueSmurfState->arrHeuristicXors[j] = arrXorEqWght[j-1];
            }
         };
}

ITE_INLINE double
J_SumInferenceWeights(Transition *pTransition)
{
  //return pTransition->positiveInferences.nNumElts +
  //       pTransition->negativeInferences.nNumElts;

//#define MK_WEIGHTS
#ifdef MK_WEIGHTS
  return pTransition->pState->vbles.nNumElts - (pTransition->pNextState->vbles.nNumElts + 1);
#endif

  int i=0;
  double fSum = 0;

  for(i=0;i<pTransition->positiveInferences.nNumElts;i++)
    fSum += arrJWeights[pTransition->positiveInferences.arrElts[i]];

  for(i=0;i<pTransition->negativeInferences.nNumElts;i++)
    fSum += arrJWeights[pTransition->negativeInferences.arrElts[i]];

//#define MK_WEIGHTS_X
#ifdef MK_WEIGHTS_X
  fSum += (pTransition->pState->vbles.nNumElts - (pTransition->pNextState->vbles.nNumElts + 1 +
      pTransition->positiveInferences.nNumElts + pTransition->negativeInferences.nNumElts));
     ///JHEURISTIC_K;
#endif

  return fSum;
}

ITE_INLINE double
J_SetHeurScoreTransition(SmurfState *pState, int i, Transition *pTransition, int nRegSmurfIndex, int nNumXors, int polarity)
{
   double fTransitionWeight = J_SumInferenceWeights(pTransition) +
                          pTransition->pNextState->fNodeHeuristicWeight;
   pTransition->fHeuristicWeight = fTransitionWeight;

   if (nNumXors) {
        if (pState->vbles.arrElts[i] != arrSmurfEqualityVble[nRegSmurfIndex]) {
           for (int j=3;j<nNumXors;j++) {
                  pState->arrHeuristicXors[j] += pTransition->pNextState->arrHeuristicXors[j];
        }
      } else {
        pState->arrHeuristicXors[polarity] = pTransition->fHeuristicWeight;
      }
   }
   return fTransitionWeight;
}

ITE_INLINE void
J_SetHeurScoresForSmurfs_Counting(int nRegSmurfIndex, SmurfState *pState, int nNumXors)
{
   if (pState == pTrueSmurfState) {
      return;
   }

   // FIND OUT IF THE HEUR ALREADY COMPUTED 
   if (pState->cFlag == 2) return;
   pState->cFlag = 2;

   double fTotalCount = 0;

   for (int i=0;i<pState->vbles.nNumElts;i++)
   {
      /* ----- POSITIVE TRANSITIONS ------ */
      {
         Transition *pTransition = FindTransition(pState, i, pState->vbles.arrElts[i], BOOL_TRUE);
         J_SetHeurScoresForSmurfs_Counting(nRegSmurfIndex, pTransition->pNextState, nNumXors);
         fTotalCount += pTransition->pNextState->fNodeHeuristicWeight;
      }

      /* ----- NEGATIVE TRANSITIONS ------ */
      {
         Transition *pTransition = FindTransition(pState, i, pState->vbles.arrElts[i], BOOL_FALSE);
         J_SetHeurScoresForSmurfs_Counting(nRegSmurfIndex, pTransition->pNextState, nNumXors);
         fTotalCount += pTransition->pNextState->fNodeHeuristicWeight;
      }
   }

   pState->fNodeHeuristicWeight = 1+fTotalCount;

   for (int i=0;i<pState->vbles.nNumElts;i++)
   {
      Transition *pTransition;
      pTransition = FindTransition(pState, i, pState->vbles.arrElts[i], BOOL_TRUE);
      pTransition->fHeuristicWeight = 
         pTransition->pState->fNodeHeuristicWeight - pTransition->pNextState->fNodeHeuristicWeight;

      pTransition = FindTransition(pState, i, pState->vbles.arrElts[i], BOOL_FALSE);
      pTransition->fHeuristicWeight =
         pTransition->pState->fNodeHeuristicWeight - pTransition->pNextState->fNodeHeuristicWeight;
   }
}

ITE_INLINE void
J_SetHeurScoresForSmurfs(int nRegSmurfIndex, SmurfState *pState, int nNumXors)
{
   if (pState == pTrueSmurfState) {
      if (pTrueSmurfState->nNumHeuristicXors < nNumXors)
          J_SetHeurWeightsTrueState(nNumXors);
      return;
   }

   // FIND OUT IF THE HEUR ALREADY COMPUTED 
   if (pState->cFlag == 2 && pState->nNumHeuristicXors >= nNumXors) return;
   pState->cFlag = 2;

   if (pState->nNumHeuristicXors < nNumXors) {
        if (pState->arrHeuristicXors) free(pState->arrHeuristicXors);
         pState->arrHeuristicXors = (double*)ite_calloc(nNumXors, sizeof(double),
               9, "pState->arrHeuristicXors");
         pState->nNumHeuristicXors = nNumXors;
   }

   double fTotalTransitions  = 0;
   for (int i=0;i<pState->vbles.nNumElts;i++)
   {
      /* ----- POSITIVE TRANSITIONS ------ */
      {
         Transition *pTransition = FindTransition(pState, i, pState->vbles.arrElts[i], BOOL_TRUE);
         J_SetHeurScoresForSmurfs(nRegSmurfIndex, pTransition->pNextState, nNumXors);
         fTotalTransitions  += J_SetHeurScoreTransition(pState, i, pTransition, nRegSmurfIndex, nNumXors, 1);
      }

      /* ----- NEGATIVE TRANSITIONS ------ */
      {
         Transition *pTransition = FindTransition(pState, i, pState->vbles.arrElts[i], BOOL_FALSE);
         J_SetHeurScoresForSmurfs(nRegSmurfIndex, pTransition->pNextState, nNumXors);
         fTotalTransitions  += J_SetHeurScoreTransition(pState, i, pTransition, nRegSmurfIndex, nNumXors, 0);
      }
   }

   pState->fNodeHeuristicWeight = fTotalTransitions / (pState->vbles.nNumElts * 2 * JHEURISTIC_K);

   if (nNumXors) {
      /* only for base case */
      pState->arrHeuristicXors[2] = pState->fNodeHeuristicWeight;
      for (int j=3;j<nNumXors;j++) {
          pState->arrHeuristicXors[j] += (j-1)*2* pState->arrHeuristicXors[j-1];
          pState->arrHeuristicXors[j] /= (2.0 * (pState->vbles.nNumElts-1+j-1) * JHEURISTIC_K);
      }
   }

}

ITE_INLINE void
J_SetupHeuristicScores()
{
   // Loop through the regular Smurfs.
   for (int i = 0; i < nNumRegSmurfs; i++)
   {
      int nRHSXorVbles = 0;
      int specfn = arrSmurfChain[i].specfn;
      if (specfn != -1)  nRHSXorVbles = arrSpecialFuncs[specfn].rhsVbles.nNumElts+1;
//#define COUNTING_HEURISTIC
#ifdef COUNTING_HEURISTIC
      assert(nRHSXorVbles == 0);
      J_SetHeurScoresForSmurfs_Counting(i, arrRegSmurfInitialStates[i], nRHSXorVbles);
#else
      J_SetHeurScoresForSmurfs(i, arrRegSmurfInitialStates[i], nRHSXorVbles);
#endif

   } 
}

/*
 * -H j -- standard Johnson (inference = 1)
 * -H js -- inference weight is the sum of # of smurfs, specfn, lemmas
 * -H jq -- sqared sum
 * -H jr -- squared and scaled
 *
 */
ITE_INLINE void
J_Setup_arrJWeights()
{
   double max=0;
   arrJWeights = (double*)ite_calloc(gnMaxVbleIndex+1, sizeof(double), 2, "arrJWeights");

   for (int nVble = 0; nVble <= gnMaxVbleIndex; nVble++)
   {
      if (sHeuristic[1] == 0) {
         arrJWeights[nVble] = 1;
      } else {
         assert(BREAK_XORS == 0);
         long sum = arrAFS[nVble].nNumRegSmurfsAffected + 
            arrAFS[nVble].nNumSpecialFuncsAffected;
         if (arrLemmaVbleCountsPos && arrLemmaVbleCountsNeg) {
            sum += arrLemmaVbleCountsPos[nVble];
            sum += arrLemmaVbleCountsNeg[nVble];
         }
         if (sHeuristic[1] == 's' || sHeuristic[1] == 'S') {
            arrJWeights[nVble] += sum;
         }
         if (sHeuristic[1] == 'S') {
            arrJWeights[nVble] += 1;
         }
         if (sHeuristic[1] == 'q' || sHeuristic[1] == 'Q') {
            arrJWeights[nVble] += sqrt((double)sum);
         }
         if (sHeuristic[1] == 'Q') {
            arrJWeights[nVble] += 1;
         }
         if (sHeuristic[1] == 'r' || sHeuristic[1] == 'R') {
            arrJWeights[nVble] += sqrt((double)sum);
            if (max < arrJWeights[nVble]) max = arrJWeights[nVble];
         }
         if (sHeuristic[1] == 'R') {
            arrJWeights[nVble] += 1;
         }
         if (sHeuristic[2] == 'd') {
            if (nVble <= nIndepVars) arrJWeights[nVble] *= 2;
         }
      }
   }
   if (sHeuristic[1] == 'r' || sHeuristic[1] == 'R') {
      for (int nVble = 0; nVble <= gnMaxVbleIndex; nVble++)
      {
         arrJWeights[nVble] /= max;
      }
   }
}
