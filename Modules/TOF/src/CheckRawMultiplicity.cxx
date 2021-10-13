// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

///
/// \file   CheckRawMultiplicity.cxx
/// \author Nicolo' Jacazio
/// \brief  Checker for the raw hit multiplicity obtained with the TaskDigits
///

// QC
#include "TOF/CheckRawMultiplicity.h"
#include "QualityControl/QcInfoLogger.h"

// ROOT
#include <TH1.h>
#include <TPaveText.h>
#include <TObjArray.h>
#include <TList.h>

using namespace std;

namespace o2::quality_control_modules::tof
{

void CheckRawMultiplicity::configure(std::string)
{
  if (auto param = mCustomParameters.find("RunningMode"); param != mCustomParameters.end()) {
    mRunningMode = ::atoi(param->second.c_str());
  }
  switch (mRunningMode) {
    case kModeCollisions:
    case kModeCosmics:
      break;
    default:
      ILOG(Fatal, Support) << "Run mode not correct " << mRunningMode << ENDM;
      break;
  }
  if (auto param = mCustomParameters.find("MinRawHits"); param != mCustomParameters.end()) {
    mMinRawHits = ::atof(param->second.c_str());
  }
  if (auto param = mCustomParameters.find("MaxRawHits"); param != mCustomParameters.end()) {
    mMaxRawHits = ::atof(param->second.c_str());
  }
  if (auto param = mCustomParameters.find("MaxFractAtZeroMult"); param != mCustomParameters.end()) {
    mMaxFractAtZeroMult = ::atof(param->second.c_str());
  }
  if (auto param = mCustomParameters.find("MaxFractAtLowMult"); param != mCustomParameters.end()) {
    mMaxFractAtLowMult = ::atof(param->second.c_str());
  }
}

Quality CheckRawMultiplicity::check(std::map<std::string, std::shared_ptr<MonitorObject>>* moMap)
{

  Quality result = Quality::Null;

  for (auto& [moName, mo] : *moMap) {
    (void)moName;
    if (mo->getName() == "TOFRawsMulti") {
      auto* h = dynamic_cast<TH1I*>(mo->getObject());
      if (h->GetEntries() == 0) { // Histogram is empty
        result = Quality::Medium;
        shifter_msg.push_back("No counts!");
      } else { // Histogram is non empty

        // Computing variables to check
        mRawHitsMean = h->GetMean();
        mRawHitsZeroMultIntegral = h->Integral(1, 1);
        mRawHitsLowMultIntegral = h->Integral(1, 10);
        mRawHitsIntegral = h->Integral(2, h->GetNbinsX());

        if (mRawHitsIntegral == 0) { //if only "0 hits per event" bin is filled -> error
          if (h->GetBinContent(1) > 0) {
            result = Quality::Bad;
            shifter_msg.push_back("Only events at 0 filled!");
          }
        } else {
          const bool isZeroBinContentHigh = (mRawHitsZeroMultIntegral > (mMaxFractAtZeroMult * mRawHitsIntegral));
          const bool isLowMultContentHigh = (mRawHitsLowMultIntegral > (mMaxFractAtLowMult * mRawHitsIntegral));
          const bool isAverageLow = (mRawHitsMean < mMinRawHits);
          const bool isAverageHigh = (mRawHitsMean > mMaxRawHits);
          switch (mRunningMode) {
            case kModeCollisions: // Collisions
              if (isZeroBinContentHigh) {
                result = Quality::Medium;
                shifter_msg.push_back("Zero-multiplicity counts are high");
                shifter_msg.push_back(Form("(%.2f higher than total)!", mMaxFractAtZeroMult));
              } else if (isLowMultContentHigh) {
                result = Quality::Medium;
                shifter_msg.push_back("Low-multiplicity counts are high");
                shifter_msg.push_back(Form("(%.2f higher than total)!", mMaxFractAtLowMult));
              } else if (isAverageLow) {
                result = Quality::Medium;
                shifter_msg.push_back(Form("Average lower than expected (%.2f)!", mMinRawHits));
              } else if (isAverageHigh) {
                result = Quality::Medium;
                shifter_msg.push_back(Form("Average higher than expected (%.2f)!", mMaxRawHits));
              } else {
                result = Quality::Good;
                shifter_msg.push_back("Average within limits");
              }
              break;
            case kModeCosmics: // Cosmics
              if (mRawHitsMean < 10.) {
                result = Quality::Good;
                shifter_msg.push_back("Average within limits");
                // flag = AliQAv1::kINFO;
              } else {
                result = Quality::Medium;
                shifter_msg.push_back("Average outside limits!");
                // flag = AliQAv1::kWARNING;
              }
              break;
            default:
              ILOG(Error, Support) << "Not running in correct mode " << mRunningMode;
              break;
          }
        }
      }
    }
  }
  return result;
}

std::string CheckRawMultiplicity::getAcceptedType() { return "TH1I"; }

void CheckRawMultiplicity::beautify(std::shared_ptr<MonitorObject> mo, Quality checkResult)
{
  if (mo->getName() == "TOFRawsMulti") {
    auto* h = dynamic_cast<TH1I*>(mo->getObject());
    TPaveText* msg = new TPaveText(0.5, 0.5, 0.9, 0.75, "blNDC");
    h->GetListOfFunctions()->Add(msg);
    msg->SetBorderSize(1);
    msg->SetTextColor(kWhite);
    msg->SetFillColor(kBlack);
    msg->AddText(Form("Default message for %s", mo->GetName()));
    msg->SetName(Form("%s_msg", mo->GetName()));
    msg->Clear();
    for (const auto& line : shifter_msg) {
      msg->AddText(line.c_str());
    }
    shifter_msg.clear();

    if (checkResult == Quality::Good) {
      msg->AddText(Form("Mean value = %5.2f", mRawHitsMean));
      msg->AddText(Form("Reference range: %5.2f-%5.2f", mMinRawHits, mMaxRawHits));
      msg->AddText(Form("Events with 0 hits = %5.2f%%", mRawHitsZeroMultIntegral * 100. / mRawHitsIntegral));
      msg->AddText("OK!");
      msg->SetFillColor(kGreen);
      msg->SetTextColor(kBlack);
    } else if (checkResult == Quality::Bad) {
      msg->AddText("Call TOF on-call.");
      msg->SetFillColor(kRed);
      msg->SetTextColor(kBlack);
    } else if (checkResult == Quality::Medium) {
      ILOG(Info, Support) << "Quality::medium, setting to yellow";
      msg->AddText("IF TOF IN RUN email TOF on-call.");
      msg->SetFillColor(kYellow);
      msg->SetTextColor(kBlack);
    }
  } else {
    ILOG(Error, Support) << "Did not get correct histo from " << mo->GetName();
  }
}

} // namespace o2::quality_control_modules::tof
