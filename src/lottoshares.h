#ifndef LOTTOSHARES_H
#define LOTTOSHARES_H

#include "main.h"
#include "bignum.h"
#include <stdio.h>
#include <string>


using namespace std;
using namespace boost;

class lottoshares
{
public:
    lottoshares();
};

void checkForCheckpoints(std::vector<CTransaction> vtx, bool makeFileQueue, bool logBlock);

bool checkForPayouts(std::vector<CTransaction> &vtx, int64 &feesFromPayout, bool addTransactions, bool logBlock);

int64 calculateTicketIncome(std::vector<CTransaction> vtx);

void addShareDrops(CBlock &block);

void writeLogInfoForBlock(uint256 logBlockHash);

#endif // LOTTOSHARES_H
