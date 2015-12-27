/*
 *  Delphes: a framework for fast simulation of a generic collider experiment
 *  Copyright (C) 2012-2014  Universite catholique de Louvain (UCL), Belgium
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/** \class Isolation
 *
 *  Sums transverse momenta of isolation objects (tracks, calorimeter towers, etc)
 *  within a DeltaR cone around a candidate and calculates fraction of this sum
 *  to the candidate's transverse momentum. outputs candidates that have
 *  the transverse momenta fraction within (PTRatioMin, PTRatioMax].
 *
 *  \author P. Demin, M. Selvaggi, R. Gerosa - UCL, Louvain-la-Neuve
 *
 */

#include "modules/Isolation.h"

#include "classes/DelphesClasses.h"
#include "classes/DelphesFactory.h"
#include "classes/DelphesFormula.h"

#include "ExRootAnalysis/ExRootResult.h"
#include "ExRootAnalysis/ExRootFilter.h"
#include "ExRootAnalysis/ExRootClassifier.h"

#include "TMath.h"
#include "TString.h"
#include "TFormula.h"
#include "TRandom3.h"
#include "TObjArray.h"
#include "TDatabasePDG.h"
#include "TLorentzVector.h"

#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <sstream>

using namespace std;

//------------------------------------------------------------------------------

class IsolationClassifier : public ExRootClassifier
{
public:

  IsolationClassifier() {}

  Int_t GetCategory(TObject *object);

  Double_t fPTMin;
};

//------------------------------------------------------------------------------

Int_t IsolationClassifier::GetCategory(TObject *object)
{
  Candidate *track = static_cast<Candidate*>(object);
  const TLorentzVector &momentum = track->Momentum;

  if(momentum.Pt() < fPTMin) return -1;

  return 0;
}

//------------------------------------------------------------------------------

Isolation::Isolation() :
  fClassifier(0), fFilter(0),
  fItIsolationInputArray(0), fItCandidateInputArray(0),
  fItRhoInputArray(0)
{
  fClassifier = new IsolationClassifier;
}

//------------------------------------------------------------------------------

Isolation::~Isolation()
{
}

//------------------------------------------------------------------------------

void Isolation::Init()
{
  const char *rhoInputArrayName;

  fDeltaRMax = GetDouble("DeltaRMax", 0.5);

  Iso_p0 = GetDouble("Iso_p0", 2.6);
  Iso_p1 = GetDouble("Iso_p1", 0);
  
  Iso_p0_ee = GetDouble("Iso_p0_ee", 2.3);
  Iso_p1_ee = GetDouble("Iso_p1_ee", 0);
    
  std::cout << "Iso_p0: " << Iso_p0 << std::endl;
  std::cout << "Iso_p1: " << Iso_p1 << std::endl;
  std::cout << "Iso_p0_ee: " << Iso_p0_ee << std::endl;
  std::cout << "Iso_p1_ee: " << Iso_p1_ee << std::endl;
  
  fPTRatioMax = GetDouble("PTRatioMax", 0.1);

  fPTSumMax = GetDouble("PTSumMax", 5.0);

  fUsePTSum = GetBool("UsePTSum", false);
  std::cout << "UsePTSum: " << fUsePTSum << std::endl;
  fUseLooseID = GetBool("UseLooseID", false);
  std::cout << "UseLooseID: " << fUseLooseID << std::endl;

  fUseRhoCorrection = GetBool("UseRhoCorrection", true);

  fClassifier->fPTMin = GetDouble("PTMin", 0.5);

  // import input array(s)

  fIsolationInputArray = ImportArray(GetString("IsolationInputArray", "Delphes/partons"));
  fItIsolationInputArray = fIsolationInputArray->MakeIterator();

  fFilter = new ExRootFilter(fIsolationInputArray);

  fCandidateInputArray = ImportArray(GetString("CandidateInputArray", "Calorimeter/electrons"));
  fItCandidateInputArray = fCandidateInputArray->MakeIterator();

  rhoInputArrayName = GetString("RhoInputArray", "");
  if(rhoInputArrayName[0] != '\0')
  {
    fRhoInputArray = ImportArray(rhoInputArrayName);
    fItRhoInputArray = fRhoInputArray->MakeIterator();
  }
  else
  {
    fRhoInputArray = 0;
  }

  // create output array

  fOutputArray = ExportArray(GetString("OutputArray", "electrons"));
}

//------------------------------------------------------------------------------

void Isolation::Finish()
{
  if(fItRhoInputArray) delete fItRhoInputArray;
  if(fFilter) delete fFilter;
  if(fItCandidateInputArray) delete fItCandidateInputArray;
  if(fItIsolationInputArray) delete fItIsolationInputArray;
}

//------------------------------------------------------------------------------

void Isolation::Process()
{
  Candidate *candidate, *isolation, *object;
  TObjArray *isolationArray;
  Double_t sumChargedNoPU, sumChargedPU, sumNeutral, sumAllParticles;
  Double_t sumDBeta, ratioDBeta, sumRhoCorr, ratioRhoCorr, sum, ratio;
  Int_t counter;
  Double_t eta = 0.0;
  Double_t rho = 0.0;

  // select isolation objects
  fFilter->Reset();
  isolationArray = fFilter->GetSubArray(fClassifier, 0);

  if(isolationArray == 0) return;

  TIter itIsolationArray(isolationArray);

  // loop over all input jets
  fItCandidateInputArray->Reset();
  while((candidate = static_cast<Candidate*>(fItCandidateInputArray->Next())))
  {
    const TLorentzVector &candidateMomentum = candidate->Momentum;
    eta = TMath::Abs(candidateMomentum.Eta());

    // find rho
    rho = 0.0;
    if(fRhoInputArray)
    {
      fItRhoInputArray->Reset();
      while((object = static_cast<Candidate*>(fItRhoInputArray->Next())))
      {
        if(eta >= object->Edges[0] && eta < object->Edges[1])
        {
          rho = object->Momentum.Pt();
        }
      }
    }

    // loop over all input tracks

    sumNeutral = 0.0;
    sumChargedNoPU = 0.0;
    sumChargedPU = 0.0;
    sumAllParticles = 0.0;

    counter = 0;
    itIsolationArray.Reset();

    while((isolation = static_cast<Candidate*>(itIsolationArray.Next())))
    {
      const TLorentzVector &isolationMomentum = isolation->Momentum;

      if(candidateMomentum.DeltaR(isolationMomentum) <= fDeltaRMax &&
         candidate->GetUniqueID() != isolation->GetUniqueID())
      {
        sumAllParticles += isolationMomentum.Pt();
        if(isolation->Charge != 0)
        {
          if(isolation->IsRecoPU)
          {
            sumChargedPU += isolationMomentum.Pt();
          }
          else
          {
            sumChargedNoPU += isolationMomentum.Pt();
          }
        }
        else
        {
          sumNeutral += isolationMomentum.Pt();
        }
        ++counter;
      }
    }

    // find rho
    rho = 0.0;
    if(fRhoInputArray)
    {
      fItRhoInputArray->Reset();
      while((object = static_cast<Candidate*>(fItRhoInputArray->Next())))
      {
        if(eta >= object->Edges[0] && eta < object->Edges[1])
        {
          rho = object->Momentum.Pt();
        }
      }
    }

    double IsoCut;
    if ( fabs( candidateMomentum.Eta() ) < 1.488 )
      {
	IsoCut = Iso_p0 + Iso_p1*candidateMomentum.Pt();
      }
    else
      {
	IsoCut = Iso_p0_ee + Iso_p1_ee*candidateMomentum.Pt();
      }
    
    // correct sum for pile-up contamination
    sumDBeta = sumChargedNoPU + TMath::Max(sumNeutral - 0.5*sumChargedPU, 0.0);
    sumRhoCorr = sumChargedNoPU + TMath::Max(sumNeutral - TMath::Max(rho, 0.0)*fDeltaRMax*fDeltaRMax*TMath::Pi(), 0.0);

    ratioDBeta = sumDBeta/candidateMomentum.Pt();
    ratioRhoCorr = sumRhoCorr/candidateMomentum.Pt();

    candidate->IsolationVar = ratioDBeta;
    candidate->IsolationVarRhoCorr = ratioRhoCorr;
    candidate->SumPtCharged = sumChargedNoPU;
    candidate->SumPtNeutral = sumNeutral;
    candidate->SumPtChargedPU = sumChargedPU;
    candidate->SumPt = sumAllParticles;

    //std::cout << "IsoCut: " << IsoCut << " sumDBeta: " << sumDBeta << std::endl;
    //if((fUsePTSum && sumDBeta > fPTSumMax) || (!fUsePTSum && ratioDBeta > fPTRatioMax) || (fUseLooseID && sumDBeta > IsoCut)) continue;
    
    sum = fUseRhoCorrection ? sumRhoCorr : sumDBeta;
    
    ratio = fUseRhoCorrection ? ratioRhoCorr : ratioDBeta;
    
    if ( fUsePTSum && !fUseLooseID && sum > fPTSumMax )
      {
	//std::cout << "fUsePTSum Iso failed, removing" << std::endl;
	continue;
      }
    else if ( fUseLooseID && !fUsePTSum && sum > IsoCut )
      {
	//std::cout << "fUseLooseID Iso failed, removing" << std::endl;
	continue;
      }
    else if ( !fUsePTSum && ratio > fPTRatioMax )
      {
	//std::cout << "Default Relative Iso failed, removing" << std::endl;
	continue;
      }

    fOutputArray->Add(candidate);
  }
}

//------------------------------------------------------------------------------
