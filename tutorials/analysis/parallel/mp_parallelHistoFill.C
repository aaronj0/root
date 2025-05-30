/// \file
/// \ingroup tutorial_analysis_parallel
/// \notebook -js
/// Fill histogram in parallel with a multiprocessing approach
/// using TProcessExecutor and TExecutor::MapReduce.
///
/// \macro_image
/// \macro_code
///
/// \date January 2016
/// \author Danilo Piparo

const UInt_t poolSize = 4U;

Int_t mp_parallelHistoFill()
{
   TH1::AddDirectory(false);
   ROOT::TProcessExecutor pool(poolSize);
   auto fillRandomHisto = [](int seed = 0) {
      TRandom3 rndm(seed);
      auto h = new TH1F("myHist", "Filled in parallel", 128, -8, 8);
      for (auto i : ROOT::TSeqI(1000000)) {
         h->Fill(rndm.Gaus(0, 1));
      }
      return h;
   };

   auto seeds = ROOT::TSeqI(23);
   ROOT::ExecutorUtils::ReduceObjects<TH1F *> redfunc;
   auto sumRandomHisto = pool.MapReduce(fillRandomHisto, seeds, redfunc);

   auto c = new TCanvas();
   sumRandomHisto->Draw();
   return 0;
}
