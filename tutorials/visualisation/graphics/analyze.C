/// \file
/// \ingroup tutorial_graphics
/// \notebook -js
///  \preview This macro produces the flowchart of TFormula::Analyze.
///
/// \macro_image
/// \macro_code
///
/// \author Rene Brun

void analyze()
{
   TCanvas *c1 = new TCanvas("c1", "Analyze.mac", 620, 790);
   c1->Range(-1, 0, 19, 30);
   TPaveLabel *pl1 = new TPaveLabel(0, 27, 3.5, 29, "Analyze");
   pl1->SetFillColor(42);
   pl1->Draw();
   TPaveText *pt1 = new TPaveText(0, 22.8, 4, 25.2);
   TText *t1 = pt1->AddText("Parenthesis matching");
   TText *t2 = pt1->AddText("Remove unnecessary");
   TText *t2a = pt1->AddText("parenthesis");
   pt1->Draw();
   TPaveText *pt2 = new TPaveText(6, 23, 10, 25);
   TText *t3 = pt2->AddText("break of");
   TText *t4 = pt2->AddText("Analyze");
   pt2->Draw();
   TPaveText *pt3 = new TPaveText(0, 19, 4, 21);
   t4 = pt3->AddText("look for simple");
   TText *t5 = pt3->AddText("operators");
   pt3->Draw();
   TPaveText *pt4 = new TPaveText(0, 15, 4, 17);
   TText *t6 = pt4->AddText("look for an already");
   TText *t7 = pt4->AddText("defined expression");
   pt4->Draw();
   TPaveText *pt5 = new TPaveText(0, 11, 4, 13);
   TText *t8 = pt5->AddText("look for usual");
   TText *t9 = pt5->AddText("functions :cos sin ..");
   pt5->Draw();
   TPaveText *pt6 = new TPaveText(0, 7, 4, 9);
   TText *t10 = pt6->AddText("look for a");
   TText *t11 = pt6->AddText("numeric value");
   pt6->Draw();
   TPaveText *pt7 = new TPaveText(6, 18.5, 10, 21.5);
   TText *t12 = pt7->AddText("Analyze left and");
   TText *t13 = pt7->AddText("right part of");
   TText *t14 = pt7->AddText("the expression");
   pt7->Draw();
   TPaveText *pt8 = new TPaveText(6, 15, 10, 17);
   TText *t15 = pt8->AddText("Replace expression");
   pt8->Draw();
   TPaveText *pt9 = new TPaveText(6, 11, 10, 13);
   TText *t16 = pt9->AddText("Analyze");
   pt9->SetFillColor(42);
   pt9->Draw();
   TPaveText *pt10 = new TPaveText(6, 7, 10, 9);
   TText *t17 = pt10->AddText("Error");
   TText *t18 = pt10->AddText("Break of Analyze");
   pt10->Draw();
   TPaveText *pt11 = new TPaveText(14, 22, 17, 24);
   pt11->SetFillColor(42);
   TText *t19 = pt11->AddText("Analyze");
   TText *t19a = pt11->AddText("Left");
   pt11->Draw();
   TPaveText *pt12 = new TPaveText(14, 19, 17, 21);
   pt12->SetFillColor(42);
   TText *t20 = pt12->AddText("Analyze");
   TText *t20a = pt12->AddText("Right");
   pt12->Draw();
   TPaveText *pt13 = new TPaveText(14, 15, 18, 18);
   TText *t21 = pt13->AddText("StackNumber++");
   TText *t22 = pt13->AddText("operator[StackNumber]");
   TText *t23 = pt13->AddText("= operator found");
   pt13->Draw();
   TPaveText *pt14 = new TPaveText(12, 10.8, 17, 13.2);
   TText *t24 = pt14->AddText("StackNumber++");
   TText *t25 = pt14->AddText("operator[StackNumber]");
   TText *t26 = pt14->AddText("= function found");
   pt14->Draw();
   TPaveText *pt15 = new TPaveText(6, 7, 10, 9);
   TText *t27 = pt15->AddText("Error");
   TText *t28 = pt15->AddText("break of Analyze");
   pt15->Draw();
   TPaveText *pt16 = new TPaveText(0, 2, 7, 5);
   TText *t29 = pt16->AddText("StackNumber++");
   TText *t30 = pt16->AddText("operator[StackNumber] = 0");
   TText *t31 = pt16->AddText("value[StackNumber] = value found");
   pt16->Draw();
   TArrow *ar = new TArrow(2, 27, 2, 25.4, 0.012, "|>");
   ar->SetFillColor(1);
   ar->Draw();
   ar->DrawArrow(2, 22.8, 2, 21.2, 0.012, "|>");
   ar->DrawArrow(2, 19, 2, 17.2, 0.012, "|>");
   ar->DrawArrow(2, 15, 2, 13.2, 0.012, "|>");
   ar->DrawArrow(2, 11, 2, 9.2, 0.012, "|>");
   ar->DrawArrow(2, 7, 2, 5.2, 0.012, "|>");
   ar->DrawArrow(4, 24, 6, 24, 0.012, "|>");
   ar->DrawArrow(4, 20, 6, 20, 0.012, "|>");
   ar->DrawArrow(4, 16, 6, 16, 0.012, "|>");
   ar->DrawArrow(4, 12, 6, 12, 0.012, "|>");
   ar->DrawArrow(4, 8, 6, 8, 0.012, "|>");
   ar->DrawArrow(10, 20, 14, 20, 0.012, "|>");
   ar->DrawArrow(12, 23, 14, 23, 0.012, "|>");
   ar->DrawArrow(12, 16.5, 14, 16.5, 0.012, "|>");
   ar->DrawArrow(10, 12, 12, 12, 0.012, "|>");
   TText *ta = new TText(2.2, 22.2, "err = 0");
   ta->SetTextFont(71);
   ta->SetTextSize(0.015);
   ta->SetTextColor(4);
   ta->SetTextAlign(12);
   ta->Draw();
   ta->DrawText(2.2, 18.2, "not found");
   ta->DrawText(2.2, 6.2, "found");
   TText *tb = new TText(4.2, 24.1, "err != 0");
   tb->SetTextFont(71);
   tb->SetTextSize(0.015);
   tb->SetTextColor(4);
   tb->SetTextAlign(11);
   tb->Draw();
   tb->DrawText(4.2, 20.1, "found");
   tb->DrawText(4.2, 16.1, "found");
   tb->DrawText(4.2, 12.1, "found");
   tb->DrawText(4.2, 8.1, "not found");
   TLine *l1 = new TLine(12, 16.5, 12, 23);
   l1->Draw();
}
