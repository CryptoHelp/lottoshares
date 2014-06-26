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

void checkForCheckpoints(std::vector<CTransaction> vtx, bool makeFileQueue);

bool checkForPayouts(std::vector<CTransaction> &vtx, int64 &feesFromPayout, bool addTransactions);

int64 calculateTicketFees(std::vector<CTransaction> vtx);

void addShareDrops(CBlock &block);

#endif // LOTTOSHARES_H
