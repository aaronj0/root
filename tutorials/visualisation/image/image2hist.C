/// \file
/// \ingroup tutorial_image
/// \notebook
/// Create a 2-D histogram from an image.
///
/// \macro_image
/// \macro_code
///
/// \author Olivier Couet

void image2hist()
{
   TASImage image("$ROOTSYS/tutorials/visualisation/image/rose512.jpg");
   UInt_t yPixels = image.GetHeight();
   UInt_t xPixels = image.GetWidth();
   UInt_t *argb = image.GetArgbArray();

   TH2D *h = new TH2D("h", "Rose histogram", xPixels, -1, 1, yPixels, -1, 1);

   for (int row = 0; row < xPixels; ++row) {
      for (int col = 0; col < yPixels; ++col) {
         int index = col * xPixels + row;
         float grey = float(argb[index] & 0xff) / 256;
         h->SetBinContent(row + 1, yPixels - col, grey);
      }
   }

   gStyle->SetPalette(kCubehelix);
   h->Draw("colz");
}
