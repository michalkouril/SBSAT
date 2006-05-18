#include "sbsat.h"
#include "sbsat_solver.h"
#include "solver.h"

/********** Included in include/sbsat_solver.h *********************
struct SmurfStateEntry{
	// Static
	int nTransitionVar;
	int nVarIsTrueTransition;
	int nVarIsFalseTransition;
	double nHeurWghtofTrueTransition;
	double nHeurWghtofFalseTransition;
	int nVarIsAnInference;
	//This is 1 if nTransitionVar should be inferred True,
	//       -1 if nTransitionVar should be inferred False,
	//        0 if nTransitionVar should not be inferred.
	int nNextVarInThisState; //There are n SmurfStateEntries linked together,
	                         //where n is the number of variables in this SmurfStateEntry.
                          	 //All of these SmurfStateEntries represent the same function,
                            //but a different variable (nTransitionVar) is
                            //highlighted for each link in the list.
                            //If this is 0, we have reached the end of the list.
};

struct SmurfStack{
	int nNumFreeVars;
	int *arrSmurfStates;             //Pointer to array of size nNumSmurfs
};

struct ProblemState{
	// Static
	int nNumSmurfs;
	int nNumVars;
	int nNumSmurfStateEntries;
	SmurfStateEntry *arrSmurfStatesTable; //Pointer to the table of all smurf states.
	                                      //Will be of size nNumSmurfStateEntries
	int **arrVariableOccursInSmurf; //Pointer to lists of Smurfs, indexed by variable number, that contain that variable.
	                                //Max size would be nNumSmurfs * nNumVars, but this would only happen if every
               	                 //Smurf contained every variable. Each list is terminated by a -1 element.
	// Dynamic
	int nCurrSearchTreeLevel;
	double *arrPosVarHeurWghts; //Pointer to array of size nNumVars
	double *arrNegVarHeurWghts; //Pointer to array of size nNumVars
	int *arrInferenceQueue;  //Pointer to array of size nNumVars (dynamically indexed by arrSmurfStack[level].nNumFreeVars
	int *arrInferenceDeclaredAtLevel; //Pointer to array of size nNumVars
	SmurfStack *arrSmurfStack; //Pointer to array of size nNumVars
};
***********************************************************************/

SmurfStateEntry *TrueSimpleSmurfState;

#define HEUR_MULT 10000

ProblemState *SimpleSmurfProblemState;

int smurfs_share_paths=1;

void PrintSmurfStateEntry(SmurfStateEntry *ssEntry) {
	d9_printf8("Var=%d, v=T:%d, v=F:%d, TWght=%4.6f, FWght=%4.6f, Inf=%d, Next=%d\n", ssEntry->nTransitionVar, ssEntry->nVarIsTrueTransition, ssEntry->nVarIsFalseTransition, ssEntry->nHeurWghtofTrueTransition, ssEntry->nHeurWghtofFalseTransition, ssEntry->nVarIsAnInference,  ssEntry->nNextVarInThisState);
}

void PrintAllSmurfStateEntries() {
	for(int x = 0; x < SimpleSmurfProblemState->nNumSmurfStateEntries; x++) {
		d9_printf2("State %d, ", x);
		PrintSmurfStateEntry(&(SimpleSmurfProblemState->arrSmurfStatesTable[x]));
	}
}

int NumOfSmurfStatesInSmurf(SmurfState *pCurrentState) {
	int num_states = 0;
	if(pCurrentState != pTrueSmurfState) {
		pCurrentState->pFunc->flag = SimpleSmurfProblemState->nNumSmurfStateEntries;
		for(int nVbleIndex = 0; nVbleIndex < pCurrentState->vbles.nNumElts; nVbleIndex++) {
			BDDNode *infBDD = pCurrentState->pFunc;
			num_states++;
			infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[pCurrentState->vbles.arrElts[nVbleIndex]], 0);
			Transition *pTransition = &pCurrentState->arrTransitions[2*nVbleIndex];
			//Follow Positive inferences
			for(int infIdx = 0; infIdx < pTransition->positiveInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				infBDD->flag = 1;
				num_states++;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[pTransition->positiveInferences.arrElts[infIdx]], 1);
			}
			//Follow Negative inferences
			for(int infIdx = 0; infIdx < pTransition->negativeInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				infBDD->flag = 1;
				num_states++;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[pTransition->negativeInferences.arrElts[infIdx]], 0);
			}
			if(infBDD->flag == 0) {
				assert(pTransition->pNextState->pFunc == infBDD);
				num_states += NumOfSmurfStatesInSmurf(pTransition->pNextState);
			}
			
			infBDD = pCurrentState->pFunc;
			infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[pCurrentState->vbles.arrElts[nVbleIndex]], 1);
			pTransition = &pCurrentState->arrTransitions[2*nVbleIndex+1];
			//Follow Negative inferences
			for(int infIdx = 0; infIdx < pTransition->negativeInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				infBDD->flag = 1;
				num_states++;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[pTransition->negativeInferences.arrElts[infIdx]], 0);
				
			}
			//Follow Positive inferences
			for(int infIdx = 0; infIdx < pTransition->positiveInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				infBDD->flag = 1;
				num_states++;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[pTransition->positiveInferences.arrElts[infIdx]], 1);
			}
			if(infBDD->flag == 0) {
				assert(pTransition->pNextState->pFunc == infBDD);
				num_states +=NumOfSmurfStatesInSmurf(pTransition->pNextState);
			}
		}
	}
	return num_states;
}

int DetermineNumOfSmurfStates() {
	clear_all_bdd_flags();
	true_ptr->flag = 1;
	//Create the rest of the SmurfState entries
	int num_states = 0;
	for(int nSmurfIndex = 0; nSmurfIndex < SimpleSmurfProblemState->nNumSmurfs; nSmurfIndex++) {
		SmurfState *pInitialState = arrSolverFunctions[nSmurfIndex].fn_smurf.pInitialState;
		if(!smurfs_share_paths) { clear_all_bdd_flags(); true_ptr->flag = 1; }
		if(pInitialState->pFunc->flag == 0) {
			num_states += NumOfSmurfStatesInSmurf(pInitialState);
		}
	}
	d7_printf2("NumSmurfStates = %d\n", num_states);
	return num_states;
}

//This state, pCurrentState, cannot have any inferences, and has not been visited
void ReadSmurfStateIntoTable(SmurfState *pCurrentState) {
	if(pCurrentState != pTrueSmurfState) {
		//If this is the first transition in a SmurfState, mark this SmurfState as Visited      
		pCurrentState->pFunc->flag = SimpleSmurfProblemState->nNumSmurfStateEntries;
		for(int nVbleIndex = 0; nVbleIndex < pCurrentState->vbles.nNumElts; nVbleIndex++) {
			//fprintf(stderr, "%d, %d, %d: ", nVbleIndex, pCurrentState->vbles.arrElts[nVbleIndex], arrSolver2IteVarMap[pCurrentState->vbles.arrElts[nVbleIndex]]);
			//printBDDerr(pCurrentState->pFunc);
			//fprintf(stderr, "\n");
			//PrintAllSmurfStateEntries();
			//Add a SmurfStateEntry into the table
			SmurfStateEntry *CurrState = &(SimpleSmurfProblemState->arrSmurfStatesTable[SimpleSmurfProblemState->nNumSmurfStateEntries]);
			SimpleSmurfProblemState->nNumSmurfStateEntries++;
			CurrState->nTransitionVar = pCurrentState->vbles.arrElts[nVbleIndex];
			CurrState->nVarIsAnInference = 0;
			CurrState->nHeurWghtofFalseTransition
			  = pCurrentState->arrTransitions[2*nVbleIndex].fHeuristicWeight;
			//= (int)(pCurrentState->arrTransitions[2*nVbleIndex].fHeuristicWeight * HEUR_MULT); //???
			CurrState->nHeurWghtofTrueTransition
			  = pCurrentState->arrTransitions[2*nVbleIndex+1].fHeuristicWeight;
			//= (int)(pCurrentState->arrTransitions[2*nVbleIndex+1].fHeuristicWeight * HEUR_MULT); //???
			
			//Compute the SmurfState w/ nTransitionVar = False
			//Create Inference Transitions
			BDDNode *infBDD = pCurrentState->pFunc;
			infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[CurrState->nTransitionVar], 0);
			Transition *pTransition = &(pCurrentState->arrTransitions[2*nVbleIndex]);
			SmurfStateEntry *NextState = CurrState;
			//Follow Positive inferences
			for(int infIdx = 0; infIdx < pTransition->positiveInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				//Add a SmurfStateEntry into the table
				infBDD->flag = SimpleSmurfProblemState->nNumSmurfStateEntries;
				if(infIdx == 0) NextState->nVarIsFalseTransition = infBDD->flag; //The TransitionVar is False
				else NextState->nVarIsTrueTransition = infBDD->flag; //The inferences are True
				NextState = &(SimpleSmurfProblemState->arrSmurfStatesTable[SimpleSmurfProblemState->nNumSmurfStateEntries]);
				SimpleSmurfProblemState->nNumSmurfStateEntries++;
				NextState->nTransitionVar = pTransition->positiveInferences.arrElts[infIdx];
				NextState->nVarIsAnInference = 1;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[NextState->nTransitionVar], 1);
			}
			//Follow Negative inferences
			for(int infIdx = 0; infIdx < pTransition->negativeInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				//Add a SmurfStateEntry into the table
				infBDD->flag = SimpleSmurfProblemState->nNumSmurfStateEntries;
				NextState->nVarIsFalseTransition = infBDD->flag; //The TransitionVar is False and the inferences are False
				NextState = &(SimpleSmurfProblemState->arrSmurfStatesTable[SimpleSmurfProblemState->nNumSmurfStateEntries]);
				SimpleSmurfProblemState->nNumSmurfStateEntries++;
				NextState->nTransitionVar = pTransition->negativeInferences.arrElts[infIdx];
				NextState->nVarIsAnInference = -1;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[NextState->nTransitionVar], 0);
			}
			if(infBDD->flag != 0) {
				if(NextState->nVarIsAnInference == 1) {
					NextState->nVarIsTrueTransition = infBDD->flag;
				} else {
					NextState->nVarIsFalseTransition = infBDD->flag;
				}
			} else {
				assert(pTransition->pNextState->pFunc == infBDD);
				if(NextState->nVarIsAnInference == 1) {
					NextState->nVarIsTrueTransition = SimpleSmurfProblemState->nNumSmurfStateEntries;
				} else {
					NextState->nVarIsFalseTransition = SimpleSmurfProblemState->nNumSmurfStateEntries;
				}
				//Recurse on nTransitionVar == False transition
				ReadSmurfStateIntoTable(pTransition->pNextState);
			}
			
			//Compute the SmurfState w/ nTransitionVar = True
			//Create Inference Transitions
			infBDD = pCurrentState->pFunc;
			infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[CurrState->nTransitionVar], 1);
			pTransition = &(pCurrentState->arrTransitions[2*nVbleIndex+1]);
			NextState = CurrState;
			//Follow Negative inferences
			for(int infIdx = 0; infIdx < pTransition->negativeInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				//Add a SmurfStateEntry into the table
				infBDD->flag = SimpleSmurfProblemState->nNumSmurfStateEntries;
				if(infIdx == 0) NextState->nVarIsTrueTransition = infBDD->flag; //The TransitionVar is True
				else NextState->nVarIsFalseTransition = infBDD->flag; //The inferences are False
				NextState = &(SimpleSmurfProblemState->arrSmurfStatesTable[SimpleSmurfProblemState->nNumSmurfStateEntries]);
				SimpleSmurfProblemState->nNumSmurfStateEntries++;
				NextState->nTransitionVar = pTransition->negativeInferences.arrElts[infIdx];
				NextState->nVarIsAnInference = -1;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[NextState->nTransitionVar], 0);
			}
			//Follow Positive inferences
			for(int infIdx = 0; infIdx < pTransition->positiveInferences.nNumElts && infBDD->flag == 0; infIdx++) {
				//Add a SmurfStateEntry into the table
				infBDD->flag = SimpleSmurfProblemState->nNumSmurfStateEntries;
				NextState->nVarIsTrueTransition = infBDD->flag; //The TransitionVar is True and the inferences are True
				NextState = &(SimpleSmurfProblemState->arrSmurfStatesTable[SimpleSmurfProblemState->nNumSmurfStateEntries]);
				SimpleSmurfProblemState->nNumSmurfStateEntries++;
				NextState->nTransitionVar = pTransition->positiveInferences.arrElts[infIdx];
				NextState->nVarIsAnInference = 1;
				infBDD = set_variable_noflag(infBDD, arrSolver2IteVarMap[NextState->nTransitionVar], 1);
			}
			if(infBDD->flag != 0) {
				if(NextState->nVarIsAnInference == -1) {
					NextState->nVarIsFalseTransition = infBDD->flag;
				} else {
					NextState->nVarIsTrueTransition = infBDD->flag;
				}
			} else {
				assert(pTransition->pNextState->pFunc == infBDD);
				if(NextState->nVarIsAnInference == -1) {
					NextState->nVarIsFalseTransition = SimpleSmurfProblemState->nNumSmurfStateEntries;
				} else {
					NextState->nVarIsTrueTransition = SimpleSmurfProblemState->nNumSmurfStateEntries;
				}
				//Recurse on nTransitionVar == False transition
				ReadSmurfStateIntoTable(pTransition->pNextState);
			}
			
			if(nVbleIndex+1 < pCurrentState->vbles.nNumElts)
			  CurrState->nNextVarInThisState = SimpleSmurfProblemState->nNumSmurfStateEntries;
		}
	}
}

void ReadAllSmurfsIntoTable() {
	//Create the TrueSimpleSmurfState entry
	SimpleSmurfProblemState = (ProblemState *)ite_calloc(1, sizeof(ProblemState), 9, "SimpleSmurfProblemState");
	SimpleSmurfProblemState->nNumSmurfs = nmbrFunctions;
	SimpleSmurfProblemState->nNumVars = nNumVariables;
	SimpleSmurfProblemState->nNumSmurfStateEntries = 2;
	ite_counters[SMURF_STATES] = DetermineNumOfSmurfStates();
	SimpleSmurfProblemState->arrSmurfStatesTable = (SmurfStateEntry *)ite_calloc(ite_counters[SMURF_STATES]+3, sizeof(SmurfStateEntry), 9, "arrSmurfStatesTable");
	SimpleSmurfProblemState->arrSmurfStack = (SmurfStack *)ite_calloc(SimpleSmurfProblemState->nNumVars, sizeof(SmurfStack), 9, "arrSmurfStack");
	SimpleSmurfProblemState->arrVariableOccursInSmurf = (int **)ite_calloc(SimpleSmurfProblemState->nNumVars, sizeof(int *), 9, "arrVariablesOccursInSmurf");
	SimpleSmurfProblemState->arrPosVarHeurWghts = (double *)ite_calloc(SimpleSmurfProblemState->nNumVars, sizeof(double), 9, "arrPosVarHeurWghts");
	SimpleSmurfProblemState->arrNegVarHeurWghts = (double *)ite_calloc(SimpleSmurfProblemState->nNumVars, sizeof(double), 9, "arrNegVarHeurWghts");
	SimpleSmurfProblemState->arrInferenceQueue = (int *)ite_calloc(SimpleSmurfProblemState->nNumVars, sizeof(int), 9, "arrInferenceQueue");
	SimpleSmurfProblemState->arrInferenceDeclaredAtLevel = (int *)ite_calloc(SimpleSmurfProblemState->nNumVars, sizeof(int), 9, "arrInferenceDeclaredAtLevel");
	
	//Count the number of functions every variable occurs in.
	int *temp_varcount = (int *)ite_calloc(SimpleSmurfProblemState->nNumVars, sizeof(int), 9, "temp_varcount");
	for(int x = 0; x < SimpleSmurfProblemState->nNumSmurfs; x++)
	  for(int y = 0; y < arrSolverFunctions[x].fn_smurf.pInitialState->vbles.nNumElts; y++)
		 temp_varcount[arrSolverFunctions[x].fn_smurf.pInitialState->vbles.arrElts[y]]++;
	
	for(int x = 0; x < SimpleSmurfProblemState->nNumVars; x++) {
		SimpleSmurfProblemState->arrSmurfStack[x].arrSmurfStates
		  = (int *)ite_calloc(SimpleSmurfProblemState->nNumSmurfs, sizeof(int), 9, "arrSmurfStates");
		SimpleSmurfProblemState->arrVariableOccursInSmurf[x] = (int *)ite_calloc(temp_varcount[x]+1, sizeof(int), 9, "arrVariableOccursInSmurf[x]");
		SimpleSmurfProblemState->arrVariableOccursInSmurf[x][temp_varcount[x]] = -1;
		SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[x] = SimpleSmurfProblemState->nNumVars;
		temp_varcount[x] = 0;
	}
	
	//Fill in the variable occurance arrays for each function
	for(int x = 0; x < SimpleSmurfProblemState->nNumSmurfs; x++) {
		for(int y = 0; y < arrSolverFunctions[x].fn_smurf.pInitialState->vbles.nNumElts; y++) {
			int nVar = arrSolverFunctions[x].fn_smurf.pInitialState->vbles.arrElts[y];
			SimpleSmurfProblemState->arrVariableOccursInSmurf[nVar][temp_varcount[nVar]] = x;
			temp_varcount[nVar]++;
		}
	}
	
	ite_free((void **)&temp_varcount);
	
	//arrSmurfStatesTable[1] is reserved for the TrueSimpleSmurfState
	TrueSimpleSmurfState = &SimpleSmurfProblemState->arrSmurfStatesTable[1];
	TrueSimpleSmurfState->nTransitionVar = 0;
	TrueSimpleSmurfState->nVarIsTrueTransition = 1;
	TrueSimpleSmurfState->nVarIsFalseTransition = 1;
	TrueSimpleSmurfState->nHeurWghtofTrueTransition = 0;
	TrueSimpleSmurfState->nHeurWghtofFalseTransition = 0;
	TrueSimpleSmurfState->nVarIsAnInference = 0;
	TrueSimpleSmurfState->nNextVarInThisState = 0;
	
	clear_all_bdd_flags();
	true_ptr->flag = 1;
	//Create the rest of the SmurfState entries
	char p[128]; int str_length;
	D_3(
		 d3_printf1("Building Smurfs: ");
		 sprintf(p, "{0/%d}",  SimpleSmurfProblemState->nNumSmurfs);
		 str_length = strlen(p);
		 d3_printf1(p);
		 );
	for(int nSmurfIndex = 0; nSmurfIndex < SimpleSmurfProblemState->nNumSmurfs; nSmurfIndex++) {
		D_3(
			 if (nSmurfIndex % ((SimpleSmurfProblemState->nNumSmurfs/100)+1) == 0) {
				 for(int iter = 0; iter<str_length; iter++)
					d3_printf1("\b");
				 sprintf(p, "{%d/%d}", nSmurfIndex, SimpleSmurfProblemState->nNumSmurfs);
				 str_length = strlen(p);
				 d3_printf1(p);
			 }
			 );
		SmurfState *pInitialState = arrSolverFunctions[nSmurfIndex].fn_smurf.pInitialState;
		if(pInitialState->pFunc->flag != 0) {
			SimpleSmurfProblemState->arrSmurfStack[0].arrSmurfStates[nSmurfIndex] = pInitialState->pFunc->flag;
		} else {
			SimpleSmurfProblemState->arrSmurfStack[0].arrSmurfStates[nSmurfIndex] = SimpleSmurfProblemState->nNumSmurfStateEntries;
			LSGBSmurfSetHeurScores(nSmurfIndex, pInitialState);
			if(!smurfs_share_paths) { clear_all_bdd_flags(); true_ptr->flag = 1; }
			ReadSmurfStateIntoTable(pInitialState);
		}
	}
	D_3(
		 for(int iter = 0; iter<str_length; iter++)
		 d3_printf1("\b");
		 sprintf(p, "{%d/%d}\n", SimpleSmurfProblemState->nNumSmurfs, SimpleSmurfProblemState->nNumSmurfs);
		 str_length = strlen(p);
		 d3_printf1(p);
		 d3_printf2("%d SmurfStates Used\n", SimpleSmurfProblemState->nNumSmurfStateEntries);  
		 );
	
	PrintAllSmurfStateEntries();  
	assert(SimpleSmurfProblemState->nNumSmurfStateEntries == ite_counters[SMURF_STATES]+2);
}

void Calculate_Heuristic_Values() {
	for(int i = 0; i < SimpleSmurfProblemState->nNumVars; i++) {
		SimpleSmurfProblemState->arrPosVarHeurWghts[i] = 0;
		SimpleSmurfProblemState->arrNegVarHeurWghts[i] = 0;
	}
	
	int *arrSmurfStates = SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].arrSmurfStates;
	for(int nSmurfIndex = 0; nSmurfIndex < SimpleSmurfProblemState->nNumSmurfs; nSmurfIndex++) {
		SmurfStateEntry *pSmurfState = &(SimpleSmurfProblemState->arrSmurfStatesTable[arrSmurfStates[nSmurfIndex]]);
		do {
			SimpleSmurfProblemState->arrPosVarHeurWghts[pSmurfState->nTransitionVar] += 
			  pSmurfState->nHeurWghtofTrueTransition;
			SimpleSmurfProblemState->arrNegVarHeurWghts[pSmurfState->nTransitionVar] +=  
			  pSmurfState->nHeurWghtofFalseTransition;
			pSmurfState = &(SimpleSmurfProblemState->arrSmurfStatesTable[pSmurfState->nNextVarInThisState]);
		} while (pSmurfState->nTransitionVar != 0);
	}
	
	d7_printf1("JHeuristic values:\n");
	for(int nVar = 1; nVar < SimpleSmurfProblemState->nNumVars; nVar++) {
		d7_printf3(" %d: %4.6f\n", nVar, SimpleSmurfProblemState->arrPosVarHeurWghts[nVar]);
		d7_printf3("-%d: %4.6f\n", nVar, SimpleSmurfProblemState->arrNegVarHeurWghts[nVar]);
	}
	d7_printf1("\n");
}

int Simple_LSGB_Heuristic() {
	
   Calculate_Heuristic_Values();
	
   int nBestVble = -1;
   double fMaxWeight = 0;
   double fVbleWeight = 0;
   int nCurrInfLevel = SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars;
	
   // Determine the variable with the highest weight:
   // 
   // Initialize to the lowest indexed variable whose value is uninstantiated.
   int i;
   for(i = 1; i < SimpleSmurfProblemState->nNumVars; i++) {
		if(SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[i] >= nCurrInfLevel) {
			nBestVble = i;
			fMaxWeight = (1+SimpleSmurfProblemState->arrPosVarHeurWghts[i]) *
			  (1+SimpleSmurfProblemState->arrNegVarHeurWghts[i]);
			
			break;
		}
   }
	
   if(nBestVble > 0) {
		// Search through the remaining uninstantiated variables.
		for(i = nBestVble + 1; i < SimpleSmurfProblemState->nNumVars; i++) {
			if(SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[i] >= nCurrInfLevel) {
				fVbleWeight = (1+SimpleSmurfProblemState->arrPosVarHeurWghts[i]) *
				  (1+SimpleSmurfProblemState->arrNegVarHeurWghts[i]);
				if(fVbleWeight > fMaxWeight) {
					fMaxWeight = fVbleWeight;
					nBestVble = i;
				}
			}
		}
   } else {
		dE_printf1 ("Error in heuristic routine:  No uninstantiated variable found\n");
		exit (1);
   }
	
   return (SimpleSmurfProblemState->arrPosVarHeurWghts[nBestVble] >= SimpleSmurfProblemState->arrNegVarHeurWghts[nBestVble]?nBestVble:-nBestVble);
}

int ApplyInferenceToSmurfs(int nBranchVar, bool bBVPolarity) {
	int *arrSmurfStates = SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].arrSmurfStates;
	d7_printf2("  Transitioning Smurfs using %d\n", bBVPolarity?nBranchVar:-nBranchVar);
	for(int i = 0; SimpleSmurfProblemState->arrVariableOccursInSmurf[nBranchVar][i] != -1; i++) {
		int nSmurfNumber = SimpleSmurfProblemState->arrVariableOccursInSmurf[nBranchVar][i];
		d7_printf3("    Checking Smurf %d (State %d)\n", nSmurfNumber, arrSmurfStates[nSmurfNumber]);
		SmurfStateEntry *pSmurfState = &(SimpleSmurfProblemState->arrSmurfStatesTable[arrSmurfStates[nSmurfNumber]]);
		do {
			if(pSmurfState->nTransitionVar == nBranchVar) {
				//Follow this transition and apply all inferences found.
				int nNewSmurfState = (bBVPolarity?pSmurfState->nVarIsTrueTransition:pSmurfState->nVarIsFalseTransition);
				pSmurfState = &(SimpleSmurfProblemState->arrSmurfStatesTable[nNewSmurfState]);
				while(pSmurfState->nVarIsAnInference != 0) {
					int nInfVar = pSmurfState->nTransitionVar;
					bool bInfPolarity = pSmurfState->nVarIsAnInference > 0;
					
					//Try to insert inference into the inference Queue
					int nInfQueueHead = SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars;
					int nPrevInfLevel = SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[nInfVar];
					d7_printf5("      Inferring %d at Level %d (prior level = %d) (State %d)\n", 
								  bInfPolarity?nInfVar:-nInfVar, nInfQueueHead, nPrevInfLevel, nNewSmurfState);
					
					if(nPrevInfLevel < nInfQueueHead) {
						//Inference already in queue
						bool bPrevInfPolarity = SimpleSmurfProblemState->arrInferenceQueue[nPrevInfLevel] > 0;
						if(bPrevInfPolarity != bInfPolarity) {
							//Conflict Detected;
							d7_printf2("      Conflict when adding %d to inference queue\n", bInfPolarity?nInfVar:-nInfVar); 
							return 0;
						} else {
							//Value is already inferred the correct value
							//Do nothing
							d7_printf2("      Inference %d already inferred\n", bInfPolarity?nInfVar:-nInfVar); 
						}
					} else {
						//Inference is not in inference queue, insert it.
						//Then insert new inference
						SimpleSmurfProblemState->arrInferenceQueue[nInfQueueHead] = bInfPolarity?nInfVar:-nInfVar;
						SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[nInfVar] = nInfQueueHead;
						SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars++;
					}
					
					//Follow the transtion to the next SmurfState
					nNewSmurfState = (bInfPolarity?pSmurfState->nVarIsTrueTransition:pSmurfState->nVarIsFalseTransition);
					pSmurfState = &(SimpleSmurfProblemState->arrSmurfStatesTable[nNewSmurfState]);
				}
				
				//Record the transition.
				arrSmurfStates[nSmurfNumber] = nNewSmurfState;
				
				break;
			}
			pSmurfState = &(SimpleSmurfProblemState->arrSmurfStatesTable[pSmurfState->nNextVarInThisState]);
		} while (pSmurfState->nTransitionVar != 0); //If there is an ordering to the way the variables are
		//listed in nNextVarInThisState we can possibly exit this loop faster  
	}
	return 1;
}

int Init_SimpleSmurfSolver() {
	//Clear all FunctionTypes
	for(int x = 0; x < nmbrFunctions; x++)
	  functionType[x] = UNSURE;
	
	InitVarMap();
	InitVariables();
	InitFunctions();
	/* Convert bdds to smurfs */
	int ret = CreateFunctions();
	if (ret != SOLV_UNKNOWN) return ret;
	
	ReadAllSmurfsIntoTable();
	
	FreeFunctions();
	FreeVariables();
	FreeVarMap();
	
	return ret;
}

void SmurfStates_Push() {
	for(int i = 0; i < SimpleSmurfProblemState->nNumSmurfs; i++) {
		SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel+1].arrSmurfStates[i] = 
		  SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].arrSmurfStates[i];
	}
	SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel+1].nNumFreeVars =
	  SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars;
	
	SimpleSmurfProblemState->nCurrSearchTreeLevel++;
}

int SmurfStates_Pop() {
	d7_printf1("  Conflict - Backtracking\n");
	SimpleSmurfProblemState->nCurrSearchTreeLevel--;
	
	if(SimpleSmurfProblemState->nCurrSearchTreeLevel < 0)
	  return 0;
	return 1;  
}

int SimpleBrancher() {
	int nNumChoicePoints = 0;
	
	SimpleSmurfProblemState->nCurrSearchTreeLevel = 0;
	while(SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars < SimpleSmurfProblemState->nNumVars-1) {
		//Update heuristic values
		
		//Call Heuristic to get variable and polarity
		d7_printf2("Calling heuristic to choose choice point #%d\n", nNumChoicePoints);
		int nBranchLit = Simple_LSGB_Heuristic();
		nNumChoicePoints++;
		
		//Push stack
		SmurfStates_Push();
		
		//Insert heuristic var into inference queue
		int nInfQueueHead = SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars;
		//First clear out old inference
		SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[abs(SimpleSmurfProblemState->arrInferenceQueue[nInfQueueHead])] = 
		  SimpleSmurfProblemState->nNumVars;
		//Then insert new inference
		SimpleSmurfProblemState->arrInferenceQueue[nInfQueueHead] = nBranchLit;
		SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[abs(nBranchLit)] = nInfQueueHead;
		SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars++;
		d7_printf3("Inferring %c%d (choice point)\n", nBranchLit>0?'+':'-', abs(nBranchLit));
		//    d7_printf3("Choice Point #%d = %d\n", , nBranchLit);
		// 
		//While inference queue != empty {
		while(nInfQueueHead != SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars) {
			//Get inference
			nBranchLit = SimpleSmurfProblemState->arrInferenceQueue[nInfQueueHead];
			nInfQueueHead++;
			bool bBVPolarity = 1;
			if(nBranchLit < 0) bBVPolarity = 0;
			int nBranchVar = bBVPolarity?nBranchLit:-nBranchLit;
			
			//apply inference to all involved smurfs
			if(ApplyInferenceToSmurfs(nBranchVar, bBVPolarity) == 0) {
				//A conflict occured
				//Pop stack
				if(SmurfStates_Pop() == 0) {
					//We are at the top of the stack
					//return Unsatisifable
					d2_printf2("Number of Choice Points: %d\n", nNumChoicePoints);
					return SOLV_UNSAT;
				}
				
				//Empty the inference queue & reverse polarity of cp var
				//Clear Inference Queue
				for(int i = 1; i < SimpleSmurfProblemState->nNumVars; i++)
				  if(SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[i] > SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars ||
					  abs(SimpleSmurfProblemState->arrInferenceQueue[SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[i]]) != i)
					 SimpleSmurfProblemState->arrInferenceDeclaredAtLevel[i] = SimpleSmurfProblemState->nNumVars;
				
				nBranchLit = SimpleSmurfProblemState->arrInferenceQueue[SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars];
				SimpleSmurfProblemState->arrInferenceQueue[SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars] = -nBranchLit;
				nInfQueueHead = SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars;
				SimpleSmurfProblemState->arrSmurfStack[SimpleSmurfProblemState->nCurrSearchTreeLevel].nNumFreeVars++;
				d7_printf3("Flipping Value of Choicepoint at level %d to %d\n", nInfQueueHead, -nBranchLit);
				//continue the loop
			}
		}
		d7_printf1("\n");
	}
	
	d2_printf2("Number of Choice Points: %d\n", nNumChoicePoints);
	return SOLV_SAT;
}

int simpleSolve() {
	int ret = Init_SimpleSmurfSolver();
	if(ret != SOLV_UNKNOWN) return ret;
	ret = SimpleBrancher();  
	//Still need to do some backend stuff like record the solution and free memory.
	return ret;
}
