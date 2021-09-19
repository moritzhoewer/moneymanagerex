/*******************************************************
 Copyright (C) 2006 Madhan Kanagavel
 Copyright (C) 2011, 2012 Nikolay & Stefano Giorgio
 Copyright (C) 2015, 2017, 2021 Nikolay Akimov
 Copyright (C) 2021 Mark Whalley (mark@ipx.co.uk)

 This program is free software; you can redistribute transcation and/or modify
 transcation under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that transcation will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 ********************************************************/

#include "transactions.h"
#include "attachmentdialog.h"
#include "constants.h"
#include "htmlbuilder.h"
#include "util.h"
#include "model/allmodel.h"
#include <algorithm>
#include <vector>

mmReportTransactions::mmReportTransactions(mmFilterTransactionsDialog* transDialog)
    : mmPrintableBase("Transaction Report")
    , m_transDialog(transDialog)
    , trans_()
{
}

mmReportTransactions::~mmReportTransactions()
{
    // incase the user wants to print a report, we maintain the transaction dialog
    // until we are finished with the report.
    m_transDialog->Destroy();
}

wxString mmReportTransactions::getHTMLText()
{
    Run(m_transDialog);

    wxArrayInt selectedAccounts = m_transDialog->getAccountsID();
    wxString accounts = _("All Accounts");
    int allAccounts = true;
    if (m_transDialog->getAccountCheckBox() && !m_transDialog->getAccountsID().empty()) {
        accounts.clear();
        allAccounts = false;
        for (const auto& acc : selectedAccounts) {
            Model_Account::Data* a = Model_Account::instance().get(acc);
            accounts += (accounts.empty() ? "" : ", ") + a->ACCOUNTNAME;
        }
    }

    const wxString extra_style = R"(
table {
  width: 100%;
}
  th.ID, th.Status {
  width: 3%;
}
  th.Color, th.Date, th.Number, th.Type {
  width: 5%;
}
  th.Amount {
  width: 8%;
}
  th.Account, th.Category, th.Payee {
  width: 10%;
}
)";

    mmHTMLBuilder hb;
    hb.init(false, extra_style);
    hb.addReportHeader(getReportTitle());
    wxDateTime start,end;
    start.ParseISODate(m_transDialog->getBeginDate());
    end.ParseISODate(m_transDialog->getEndDate());
    hb.DisplayDateHeading(start, end
        , m_transDialog->getDateRangeCheckBox() || m_transDialog->getStartDateCheckBox()
        , m_transDialog->getStartDateCheckBox());
    hb.DisplayFooter(_("Accounts: ") + accounts);

    std::map<int, double> total; //Store transaction amount with original currency
    std::map<int, double> total_in_base_curr; //Store transactions amount daily converted to base currency
    std::map<int, double> total_extrans; //Store transaction amount with original currency - excluding TRANSFERS
    std::map<int, double> total_in_base_curr_extrans; //Store transactions amount daily converted to base currency - excluding TRANSFERS

    hb.addDivContainer("shadow");
    {
         hb.startSortTable();
        {
            hb.startThead();
            {
                hb.startTableRow();
                {
                    if (showColumnById(0)) hb.addTableHeaderCell(_("ID"), "ID");
                    if (showColumnById(1)) hb.addTableHeaderCell(_("Color"), "Color");
                    if (showColumnById(2)) hb.addTableHeaderCell(_("Date"), "Date");
                    if (showColumnById(3)) hb.addTableHeaderCell(_("Number"), "Number");
                    if (showColumnById(4)) hb.addTableHeaderCell(_("Account"), "Account");
                    if (showColumnById(5)) hb.addTableHeaderCell(_("Payee"), "Payee");
                    if (showColumnById(6)) hb.addTableHeaderCell(_("Status"), "Status");
                    if (showColumnById(7)) hb.addTableHeaderCell(_("Category"), "Category");
                    if (showColumnById(8)) hb.addTableHeaderCell(_("Type"), "Type");
                    if (showColumnById(9)) hb.addTableHeaderCell(_("Amount"), "Amount text-right");
                    if (showColumnById(10)) hb.addTableHeaderCell(_("Notes"), "Notes");
                }
                hb.endTableRow();
            }
            hb.endThead();

            hb.startTbody();
            {
                const wxString& AttRefType = Model_Attachment::reftype_desc(Model_Attachment::TRANSACTION);

                // Display the data for each row
                for (auto& transaction : trans_)
                {
                    // If a transfer between two accounts in the list of accounts being reported then we
                    // should report both the transfer in and transfer out, i.e. two transactionsa
                    int noOfTrans = 1; 
                    if ((Model_Checking::type(transaction) == Model_Checking::TRANSFER) &&
                        (allAccounts ||
                        ((selectedAccounts.Index(transaction.ACCOUNTID) != wxNOT_FOUND)
                        && (selectedAccounts.Index(transaction.TOACCOUNTID) != wxNOT_FOUND))))
                            noOfTrans = 2;

                    while (noOfTrans--)
                    {
                        hb.startTableRow();
                        {
                         /*  if ((Model_Checking::type(transaction) == Model_Checking::TRANSFER)
                                && m_transDialog->getTypeCheckBox() && */
                            if (showColumnById(0)) {
                                hb.addTableCellLink(wxString::Format("trx:%d", transaction.TRANSID)
                                    , wxString::Format("%i", transaction.TRANSID));
                            }
                            if (showColumnById(1)) hb.addColorMarker(getUDColour(transaction.FOLLOWUPID).GetAsString());
                            if (showColumnById(2)) hb.addTableCellDate(transaction.TRANSDATE);
                            if (showColumnById(3)) hb.addTableCell(transaction.TRANSACTIONNUMBER);
                            if (showColumnById(4)) {
                                hb.addTableCellLink(wxString::Format("trxid:%d", transaction.TRANSID)
                                    , noOfTrans ? transaction.TOACCOUNTNAME : transaction.ACCOUNTNAME);
                            }
                            if (showColumnById(5)) hb.addTableCell(noOfTrans ? "< " + transaction.ACCOUNTNAME : transaction.PAYEENAME);
                            if (showColumnById(6)) hb.addTableCell(transaction.STATUS);
                            if (showColumnById(7)) hb.addTableCell(transaction.CATEGNAME);
                            if (showColumnById(8))
                            {
                                if (Model_Checking::foreignTransactionAsTransfer(transaction))
                                    hb.addTableCell("< " + wxGetTranslation(transaction.TRANSCODE));
                                else
                                    hb.addTableCell(wxGetTranslation(transaction.TRANSCODE));
                            }

                            Model_Account::Data* acc;
                            const Model_Currency::Data* curr;
                            acc = Model_Account::instance().get(transaction.ACCOUNTID);
                            curr = Model_Account::currency(acc);

                            if (acc)
                            {
                                double amount = Model_Checking::balance(transaction, acc->ACCOUNTID);
                                if (noOfTrans || (!allAccounts && (selectedAccounts.Index(transaction.ACCOUNTID) == wxNOT_FOUND)))
                                    amount = -amount;
                                const double convRate = Model_CurrencyHistory::getDayRate(curr->CURRENCYID, transaction.TRANSDATE);
                                if (showColumnById(9)) hb.addCurrencyCell(amount, curr);
                                total[curr->CURRENCYID] += amount;
                                total_in_base_curr[curr->CURRENCYID] += amount * convRate;
                                if (Model_Checking::type(transaction) != Model_Checking::TRANSFER)
                                {
                                    total_extrans[curr->CURRENCYID] += amount;
                                    total_in_base_curr_extrans[curr->CURRENCYID] += amount * convRate;
                                }
                            }
                            else
                            {
                                wxFAIL_MSG("account for transaction not found");
                                if (showColumnById(9)) hb.addEmptyTableCell();
                            }

                            // Attachments
                            wxString AttachmentsLink = "";
                            if (Model_Attachment::instance().NrAttachments(AttRefType, transaction.TRANSID))
                            {
                                AttachmentsLink = wxString::Format(R"(<a href = "attachment:%s|%d" target="_blank">%s</a>)",
                                    AttRefType, transaction.TRANSID, mmAttachmentManage::GetAttachmentNoteSign());
                            }

                            //Notes
                            if (showColumnById(10)) hb.addTableCell(AttachmentsLink + transaction.NOTES);

                        }
                        hb.endTableRow();
                    }
                }
            }
            hb.endTbody();
        }
        hb.endTable();
    }
    hb.endDiv();

    hb.addDivContainer("shadow");
    {
        hb.startTable();
        {
            // display the total balance 
            hb.startThead();
            {
                hb.startTableRow();
                {
                    hb.addTableHeaderCell(_("All Transactions: Withdrawals, Deposits, and Transfers"));
                    hb.addTableHeaderCell("");
                    hb.addTableHeaderCell("");
                }
                hb.endTableRow();
            }
            hb.endThead();
            hb.startTbody();
            {
                double grand_total = 0;
                for (const auto& curr_total : total)
                {
                    const auto curr = Model_Currency::instance().get(curr_total.first);
                    const bool isBaseCurr = (curr->CURRENCY_SYMBOL == Model_Currency::GetBaseCurrency()->CURRENCY_SYMBOL);
                    grand_total += total_in_base_curr[curr_total.first];
                    if (total.size() > 1 || !isBaseCurr)
                    {
                        const wxString totalStr_curr = isBaseCurr ? "" : Model_Currency::toCurrency(curr_total.second, curr);
                        const wxString totalStr = Model_Currency::toCurrency(total_in_base_curr[curr_total.first], Model_Currency::GetBaseCurrency());
                        const std::vector<wxString> v{ totalStr_curr,  totalStr };
                        hb.addTotalRow(curr->CURRENCY_SYMBOL, 2, v);
                    }
                }
                const wxString totalStr = Model_Currency::toCurrency(grand_total, Model_Currency::GetBaseCurrency());
                const std::vector<wxString> v{ "", totalStr };
                hb.addTotalRow(_("Grand Total:"), 2, v);
            }
            hb.endTbody();

            // display the total balance (excluding TRANSFERS)
            hb.startThead();
            {
                hb.startTableRow();
                {
                    hb.addTableHeaderCell(_("All Transactions excluding Transfers"));
                    hb.addTableHeaderCell("");
                    hb.addTableHeaderCell("");
                }
                hb.endTableRow();
            }
            hb.endThead();
            hb.startTbody();
            {
                double grand_total = 0;
                for (const auto& curr_total : total_extrans)
                {
                    const auto curr = Model_Currency::instance().get(curr_total.first);
                    const bool isBaseCurr = (curr->CURRENCY_SYMBOL == Model_Currency::GetBaseCurrency()->CURRENCY_SYMBOL);
                    grand_total += total_in_base_curr_extrans[curr_total.first];
                    if (total_extrans.size() > 1 || !isBaseCurr)
                    {
                        const wxString totalStr_curr = isBaseCurr ? "" : Model_Currency::toCurrency(curr_total.second, curr);
                        const wxString totalStr = Model_Currency::toCurrency(total_in_base_curr_extrans[curr_total.first], Model_Currency::GetBaseCurrency());
                        const std::vector<wxString> v{ totalStr_curr,  totalStr };
                        hb.addTotalRow(curr->CURRENCY_SYMBOL, 2, v);
                    }
                }
                const wxString totalStr = Model_Currency::toCurrency(grand_total, Model_Currency::GetBaseCurrency());
                const std::vector<wxString> v{ "", totalStr };
                hb.addTotalRow(_("Grand Total:"), 2, v);
            }
            hb.endTbody();
        }
        hb.endTable();
    }
    hb.endDiv();
    hb.addDivContainer("shadow");
    {
        m_transDialog->getDescription(hb);
    }
    hb.endDiv();
    hb.end();

    return hb.getHTMLText();
}

void mmReportTransactions::Run(mmFilterTransactionsDialog* dlg)
{
    trans_.clear();
    const auto splits = Model_Splittransaction::instance().get_all();
    for (const auto& tran : Model_Checking::instance().all()) //TODO: find should be faster
    {
        if (!dlg->checkAll(tran, splits)) continue;
        Model_Checking::Full_Data full_tran(tran, splits);

        full_tran.PAYEENAME = full_tran.real_payee_name(full_tran.ACCOUNTID);
        if (m_transDialog->getCategoryCheckBox() && full_tran.has_split()) 
        {
            full_tran.CATEGNAME.clear();
            full_tran.TRANSAMOUNT = 0;
            for (const auto& split : full_tran.m_splits)
            {
                const wxString split_info = wxString::Format("%s = %s | "
                    , Model_Category::full_name(split.CATEGID, split.SUBCATEGID)
                    , wxString::Format("%.2f", split.SPLITTRANSAMOUNT));
                full_tran.CATEGNAME.Append(split_info);
                if (split.CATEGID != m_transDialog->getCategId() ) continue;
                if (split.SUBCATEGID != m_transDialog->getSubCategId() 
                    && !m_transDialog->getSimilarStatus()) continue;

                full_tran.TRANSAMOUNT += split.SPLITTRANSAMOUNT;
            }
            full_tran.CATEGNAME.RemoveLast(2);
        }

            trans_.push_back(full_tran);
    }
    std::stable_sort(trans_.begin(), trans_.end(), SorterByTRANSDATE());
}

bool mmReportTransactions::showColumnById(int num)
{
    if (m_transDialog->getHideColumnsCheckBox())
    {
        wxArrayInt columns = m_transDialog->getHideColumnsID();
        return columns.Index(num) == wxNOT_FOUND;
    }
    return true;
}
