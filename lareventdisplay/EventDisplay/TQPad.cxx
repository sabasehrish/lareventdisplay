///
/// \file    TQPad.cxx
/// \brief   Drawing pad for time or charge histograms
/// \author  messier@indiana.edu
///
#include "lareventdisplay/EventDisplay/TQPad.h"

#include "TBox.h"
#include "TH1F.h"
#include "TF1.h"
#include "TPad.h"
#include "TPolyLine.h"
#include "TText.h"

#include "art/Utilities/make_tool.h"
#include "cetlib_except/exception.h"

#include "nutools/EventDisplayBase/View2D.h"
#include "nutools/EventDisplayBase/EventHolder.h"
#include "lareventdisplay/EventDisplay/RawDrawingOptions.h"
#include "lareventdisplay/EventDisplay/RecoDrawingOptions.h"
#include "lareventdisplay/EventDisplay/ColorDrawingOptions.h"
#include "lareventdisplay/EventDisplay/RawDataDrawer.h"
#include "lareventdisplay/EventDisplay/RecoBaseDrawer.h"
#include "lareventdisplay/EventDisplay/wfHitDrawers/IWFHitDrawer.h"
#include "lareventdisplay/EventDisplay/wfHitDrawers/IWaveformDrawer.h"
#include "larcore/Geometry/Geometry.h"
#include "larcorealg/Geometry/CryostatGeo.h"
#include "larcorealg/Geometry/TPCGeo.h"
#include "larcorealg/Geometry/PlaneGeo.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardata/ArtDataHelper/MVAReader.h"

// C/C++ standard libraries
#include <algorithm> // std::min(), std::max()


namespace evd{

static const int kRAW      = 0;
static const int kCALIB    = 1;
static const int kRAWCALIB = 2;
static const int kQ        = 0;
static const int kTQ       = 1;

//......................................................................

TQPad::TQPad(const char* nm, const char* ti,
             double x1, double y1,
             double x2, double y2,
             const char *opt,
             unsigned int plane,
             unsigned int wire) :
    DrawingPad(nm, ti, x1, y1, x2, y2),
    fWire(wire),
    fPlane(plane),
    fFrameHist(0),
    fRawHisto(0),
    fRecoHisto(0)
{
    art::ServiceHandle<geo::Geometry> geo;
    unsigned int planes = geo->Nplanes();

    this->Pad()->cd();

    this->Pad()->SetLeftMargin  (0.050);
    this->Pad()->SetRightMargin (0.050);

    this->Pad()->SetTopMargin   (0.005);
    this->Pad()->SetBottomMargin(0.110);

    // there has to be a better way of doing this that does
    // not have a case for each number of planes in a detector
    if(planes == 2 && fPlane > 0){
        this->Pad()->SetTopMargin   (0.110);
        this->Pad()->SetBottomMargin(0.010);
    }
    else if(planes > 2){
        if(fPlane == 1){
            this->Pad()->SetTopMargin   (0.005);
            this->Pad()->SetBottomMargin(0.010);
        }
        else if(fPlane == 2){
            this->Pad()->SetTopMargin   (0.110);
            this->Pad()->SetBottomMargin(0.010);
        }
    }

    std::string opts(opt);
    if(opts == "TQ") {
        fTQ = kTQ;
        // BB adjust the vertical spacing
        this->Pad()->SetTopMargin   (0);
        this->Pad()->SetBottomMargin(0.2);
    }
    if(opts == "Q" ){
        fTQ = kQ;
    }

    this->BookHistogram();
    fView = new evdb::View2D();
    
    art::ServiceHandle<evd::RawDrawingOptions>  rawOptions;
    art::ServiceHandle<evd::RecoDrawingOptions> recoOptions;

    fHitDrawerTool      = art::make_tool<evdb_tool::IWFHitDrawer>(recoOptions->fHitDrawerParams);
    fRawDigitDrawerTool = art::make_tool<evdb_tool::IWaveformDrawer>(rawOptions->fRawDigitDrawerParams);
    fWireDrawerTool     = art::make_tool<evdb_tool::IWaveformDrawer>(recoOptions->fWireDrawerParams);

    fHitFuncVec.clear();
}

//......................................................................

TQPad::~TQPad()
{
    if (fView)      { delete fView;      fView      = 0; }
    if (fFrameHist) { delete fFrameHist; fFrameHist = 0; }
    if (fRawHisto)  { delete fRawHisto;  fRawHisto  = 0; }
    if (fRecoHisto) { delete fRecoHisto; fRecoHisto = 0; }
}

//......................................................................
void TQPad::Draw()
{
    art::ServiceHandle<evd::RawDrawingOptions> drawopt;

    if (!fRawHisto || !fRecoHisto) return;

    //grab the singleton with the event
    const art::Event* evt = evdb::EventHolder::Instance()->GetEvent();
    if(!evt) return;

    art::ServiceHandle<geo::Geometry> geoSvc;
    
    //check if raw (dual phase) or deconvoluted (single phase) waveform was fitted
    auto hitResults = anab::FVectorReader<recob::Hit, 4>::create(*evt, "dprawhit");

    if(hitResults) //raw waveform (dual phase)
    {
        fPad->Clear();
        fPad->cd();

        std::vector<double> htau1;
        std::vector<double> htau2;
        std::vector<double> hamplitudes;
        std::vector<double> hpeaktimes;
        std::vector<int>    hstartT;
        std::vector<int>    hendT;
        std::vector<int>    hNMultiHit;
        std::vector<int>    hLocalHitIndex;

        if(fTQ == kTQ)
        {
            fRawHisto->Reset("ICEM");
            fRecoHisto->Reset("ICEM");

            this->RawDataDraw()->FillTQHisto(*evt,
                                             fPlane,
                                             fWire,
                                             fRawHisto);

            this->RecoBaseDraw()->FillTQHistoDP(*evt,
                                                fPlane,
                                                fWire,
                                                fRecoHisto,
                                                htau1,
                                                htau2,
                                                hamplitudes,
                                                hpeaktimes,
                                                hstartT,
                                                hendT,
                                                hNMultiHit,
                                                hLocalHitIndex);


            // draw with histogram style, only (square) lines, no errors
            static const std::string defaultDrawOptions = "HIST";

            switch (drawopt->fDrawRawDataOrCalibWires) {
                case kRAW:
                    fRawHisto->Draw(defaultDrawOptions.c_str());
                    break;
                case kCALIB:
                    fRecoHisto->Draw(defaultDrawOptions.c_str());
                    break;
                case kRAWCALIB:
                    fRawHisto->SetMaximum(1.2*std::max(fRawHisto->GetMaximum(), fRecoHisto->GetMaximum()));
                    fRawHisto->SetMinimum(1.2*std::min(fRawHisto->GetMinimum(), fRecoHisto->GetMinimum()));
                    fRawHisto->Draw(defaultDrawOptions.c_str());
                    fRecoHisto->Draw((defaultDrawOptions + " same").c_str());
                    break;
            } // switch

            // this loop draws the double-exponential shapes for identified hits in the reco histo
            for (size_t i = 0; i < hamplitudes.size() && drawopt->fDrawRawDataOrCalibWires != kRAW; ++i) {
                // If there is more than one peak in this fit, draw the sum of all peaks
                if(hNMultiHit[i] > 1 && hLocalHitIndex[i] == 0)
                {
                    // create TPolyLine that actually gets drawn
                    TPolyLine& p2 = fView->AddPolyLine(1001,kRed,3,1);

                    // set coordinates of TPolyLine based fitted function
                    for(int j = 0; j<1001; ++j)
                    {
                        double x = hstartT[i]+j*(hendT[i+hNMultiHit[i]-1]-hstartT[i])/1000;
                        double y = RecoBaseDraw()->EvalMultiExpoFit(x,i,hNMultiHit[i],htau1,htau2,hamplitudes,hpeaktimes);
                        p2.SetPoint(j, x, y);
                    }

                    p2.Draw("same");
                }

                // Always draw the single peaks in addition to the sum of all peaks
                // create TPolyLine that actually gets drawn
                TPolyLine& p1 = fView->AddPolyLine(1001,kOrange+7,3,1);

                // set coordinates of TPolyLine based fitted function
                for(int j = 0; j<1001; ++j){
                    double x = hstartT[i-hLocalHitIndex[i]]+j*(hendT[i+hNMultiHit[i]-hLocalHitIndex[i]-1]-hstartT[i-hLocalHitIndex[i]])/1000;
                    double y = RecoBaseDraw()->EvalExpoFit(x,htau1[i],htau2[i],hamplitudes[i],hpeaktimes[i]);
                    p1.SetPoint(j, x, y);
                }
      
                p1.Draw("same");
            }

            if     (drawopt->fDrawRawDataOrCalibWires == kCALIB) fRecoHisto->Draw((defaultDrawOptions + " same").c_str());
            else if(drawopt->fDrawRawDataOrCalibWires == kRAWCALIB){
                fRawHisto->Draw((defaultDrawOptions + " same").c_str());
                fRecoHisto->Draw((defaultDrawOptions + " same").c_str());
            }

            fRawHisto->SetLabelSize  (0.15,"X");
            fRawHisto->SetLabelOffset(0.01,"X");
            fRawHisto->SetTitleSize  (0.15,"X");
            fRawHisto->SetTitleOffset(0.60,"X");
     
            fRawHisto->SetLabelSize  (0.15,"Y");
            fRawHisto->SetLabelOffset(0.002,"Y");
            fRawHisto->SetTitleSize  (0.15,"Y");
            fRawHisto->SetTitleOffset(0.16,"Y");
     
            fRecoHisto->SetLabelSize  (0.15,"X");
            fRecoHisto->SetLabelOffset(0.01,"X");
            fRecoHisto->SetTitleSize  (0.15,"X");
            fRecoHisto->SetTitleOffset(0.60,"X");
     
            fRecoHisto->SetLabelSize  (0.15,"Y");
            fRecoHisto->SetLabelOffset(0.002,"Y");
            fRecoHisto->SetTitleSize  (0.15,"Y");
            fRecoHisto->SetTitleOffset(0.16,"Y");
        } // end if fTQ == kTQ
    } //end raw waveform (dual phase)
    else //deconvoluted waveform (single phase)
    {
        fPad->Clear();
        fPad->cd();
        
        fHitFuncVec.clear();

        if(fTQ == kTQ){
            // Recover a channel number from current information
            raw::ChannelID_t channel = geoSvc->PlaneWireToChannel(fPlane,fWire,drawopt->fTPC,drawopt->fCryostat);
            
            // Call the tools to fill the histograms for RawDigits and Wire data
            fRawDigitDrawerTool->Fill(*fView, channel, this->RawDataDraw()->StartTick(), this->RawDataDraw()->TotalClockTicks());
            fWireDrawerTool->Fill(*fView, channel, this->RawDataDraw()->StartTick(), this->RawDataDraw()->TotalClockTicks());
            
            // Vertical limits set for the enclosing histogram, then draw it with axes only
            fFrameHist->SetMaximum(1.1*std::max(fRawDigitDrawerTool->getMaximum(), fWireDrawerTool->getMaximum()));
            fFrameHist->SetMinimum(1.1*std::min(fRawDigitDrawerTool->getMinimum(), fWireDrawerTool->getMinimum()));
            fFrameHist->Draw("AXIS");

            // draw with histogram style, only (square) lines, no errors
            static const std::string defaultDrawOptions = "HIST same";

            // Draw the desired histograms
            // If its not just the raw hists then we output the wire histograms
            if (drawopt->fDrawRawDataOrCalibWires != kRAW)
            {
                fWireDrawerTool->Draw(defaultDrawOptions.c_str());
                
                fHitDrawerTool->Draw(*fView, fHitFuncVec, channel);
                
                for(auto& func : fHitFuncVec) func->Draw((defaultDrawOptions + " same").c_str());
            }

            // Likewise, if it is not just the calib hists then we output the raw histogram
            if (drawopt->fDrawRawDataOrCalibWires != kCALIB) fRawDigitDrawerTool->Draw(defaultDrawOptions.c_str());

            // This is a remnant from a time long past...
            fFrameHist->SetTitleOffset(0.2, "Y");
        } // end if fTQ == kTQ
 
        // I am not sure what the block below is trying to do... I don't see where the hists are actually filled.
        if(fTQ == kQ){

            // figure out the signal type for this plane, assume that
            // plane n in each TPC/cryostat has the same type
            geo::PlaneID planeid(drawopt->CurrentTPC(), fPlane);
            geo::SigType_t sigType = geoSvc->SignalType(planeid);

            art::ServiceHandle<evd::ColorDrawingOptions> cst;

            TH1F *hist;

            int ndiv = 0;
            if(drawopt->fDrawRawDataOrCalibWires != kCALIB){
                hist = fRawHisto;
                hist->SetMinimum(cst->fRawQLow [(size_t)sigType]);
                hist->SetMaximum(cst->fRawQHigh[(size_t)sigType]);
                ndiv = cst->fRawDiv[(size_t)sigType];
            }
            if(drawopt->fDrawRawDataOrCalibWires == kCALIB){
                hist = fRecoHisto;
                hist->SetMinimum(cst->fRecoQLow [(size_t)sigType]);
                hist->SetMaximum(cst->fRecoQHigh[(size_t)sigType]);
                ndiv = cst->fRecoDiv[(size_t)sigType];
            }

            hist->SetLabelSize(0, "X");
            hist->SetLabelSize(0, "Y");
            hist->SetTickLength(0, "X");
            hist->SetTickLength(0, "Y");
            hist->Draw("pY+");

            //
            // Use this to fill the histogram with colors from the color scale
            //
            double x1, x2, y1, y2;
            x1 = 0.;
            x2 = 1.;

            for(int i = 0; i < ndiv; ++i){
                y1 = hist->GetMinimum() + i*(hist->GetMaximum()-hist->GetMinimum())/(1.*ndiv);
                y2 = hist->GetMinimum() + (i + 1)*(hist->GetMaximum()-hist->GetMinimum())/(1.*ndiv);
  
                int c = 1;
                if (drawopt->fDrawRawDataOrCalibWires==kRAW) {
                    c = cst->RawQ(sigType).GetColor(0.5*(y1+y2));
                }
                if (drawopt->fDrawRawDataOrCalibWires!=kRAW) {
                    c= cst->CalQ(sigType).GetColor(0.5*(y1+y2));
                }
  
                TBox& b = fView->AddBox(x1,y1,x2,y2);
                b.SetFillStyle(1001);
                b.SetFillColor(c);
                b.Draw();
            } // end loop over Q histogram bins
     
            hist->Draw("same");
        } // end if fTQ == kQ
    } //if-else dual single phase
    
    return;
}


//......................................................................
void TQPad::BookHistogram()
{
    if (fFrameHist)
    {
        delete fFrameHist;
        fFrameHist = 0;
    }
    if (fRawHisto) {
        delete fRawHisto;
        fRawHisto = 0;
    }
    if (fRecoHisto) {
        delete fRecoHisto;
        fRecoHisto = 0;
    }
  
    art::ServiceHandle<evd::ColorDrawingOptions> cst;
    art::ServiceHandle<evd::RawDrawingOptions> drawopt;

    // figure out the signal type for this plane, assume that
    // plane n in each TPC/cryostat has the same type
    geo::PlaneID planeid(drawopt->CurrentTPC(), fPlane);
    art::ServiceHandle<geo::Geometry> geo;
    geo::SigType_t sigType = geo->SignalType(planeid);
  
    /// \todo decide if ndivraw and ndivreco are useful
    //     int    ndivraw   = cst->fRawDiv;
    //     int    ndivreco  = cst->fRecoDiv;
    double qxloraw   = cst->fRawQLow[(size_t)sigType];
    double qxhiraw   = cst->fRawQHigh[(size_t)sigType];
    double qxloreco  = cst->fRecoQLow[(size_t)sigType];
    double qxhireco  = cst->fRecoQHigh[(size_t)sigType];
    double tqxlo     = 1.*this->RawDataDraw()->StartTick();
    double tqxhi     = 1.*this->RawDataDraw()->TotalClockTicks();
  
    switch (fTQ) {
        case kQ:
            fFrameHist = new TH1F("fFrameHist", ";t [ticks];[ADC]", 2,0.,1.);
            fFrameHist->SetMaximum(qxhiraw);
            fFrameHist->SetMinimum(qxloraw);
            
            fRawHisto = new TH1F("fRAWQHisto", ";;", 2,0.,1.);
            fRawHisto->SetMaximum(qxhiraw);
            fRawHisto->SetMinimum(qxloraw);
            
            fRecoHisto = new TH1F("fCALQHisto", ";;", 1,0.,1.);
            fRecoHisto->SetMaximum(qxhireco);
            fRecoHisto->SetMinimum(qxloreco);
            break; // kQ
        case kTQ:
            fFrameHist = new TH1F("fFrameHist", ";t [ticks];q [ADC]", (int)tqxhi,tqxlo,tqxhi+tqxlo);
            fRawHisto = new TH1F("fRAWTQHisto", ";t [ticks];q [ADC]", (int)tqxhi,tqxlo,tqxhi+tqxlo);
            fRecoHisto = new TH1F("fCALTQHisto", ";t [ticks];q [ADC]", (int)tqxhi,tqxlo,tqxhi+tqxlo);
            fRecoHisto->SetLineColor(kBlue);
            fRecoHisto->SetLineWidth(1);
            break;
        default:
            throw cet::exception("TQPad") << __func__ << ": unexpected quantity #" << fTQ << "\n";
    }//end if fTQ == kTQ
    
    fFrameHist->SetLabelSize  (0.15,"X");
    fFrameHist->SetLabelOffset(0.00,"X");
    fFrameHist->SetTitleSize  (0.15,"X");
    fFrameHist->SetTitleOffset(0.80,"X");
    
    fFrameHist->SetLabelSize  (0.10,"Y");
    fFrameHist->SetLabelOffset(0.01,"Y");
    fFrameHist->SetTitleSize  (0.15,"Y");
    fFrameHist->SetTitleOffset(0.80,"Y");
    
    fRawHisto->SetLabelSize  (0.15,"X");
    fRawHisto->SetLabelOffset(0.00,"X");
    fRawHisto->SetTitleSize  (0.15,"X");
    fRawHisto->SetTitleOffset(0.80,"X");
    
    fRawHisto->SetLabelSize  (0.10,"Y");
    fRawHisto->SetLabelOffset(0.01,"Y");
    fRawHisto->SetTitleSize  (0.15,"Y");
    fRawHisto->SetTitleOffset(0.80,"Y");

    fRecoHisto->SetLabelSize  (0.15,"X");
    fRecoHisto->SetLabelOffset(0.00,"X");
    fRecoHisto->SetTitleSize  (0.15,"X");
    fRecoHisto->SetTitleOffset(0.80,"X");
  
    fRecoHisto->SetLabelSize  (0.15,"Y");
    fRecoHisto->SetLabelOffset(0.00,"Y");
    fRecoHisto->SetTitleSize  (0.15,"Y");
    fRecoHisto->SetTitleOffset(0.80,"Y");
}

}
//////////////////////////////////////////////////////////////////////////
