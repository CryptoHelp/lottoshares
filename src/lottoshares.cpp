#include "lottoshares.h"
#include "alert.h"
#include "checkpoints.h"
#include "db.h"
#include "txdb.h"
#include "net.h"
#include "init.h"
#include "auxpow.h"
#include "ui_interface.h"
#include "checkqueue.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
using namespace boost;

using namespace std;
using namespace boost;
#include <stdio.h>
#include <string>
#ifdef MAC_OSX
#include <CoreFoundation/CoreFoundation.h>
#endif

string TIMEKEEPERSIGNINGADDRESS     ="LTSLTSoPZPJuLThVeytMDbUHMFgJwYDtJq";
string TIMEKEEPERBROADCASTADDRESS   ="LTSLTSzCWmkJx7SYznmcVNoaMdQz7t19cT";
string DRAWMANAGERSIGNINGADDRESS    ="LTSLTSwXfArg2r9y7BcAcLNKJqu6LF2Py4";
string DRAWMANAGERBROADCASTADDRESS  ="LTSLTSKuQ35qr4mmz5aUBU1bm4v3Q8DJby";
string TICKETADDRESS                ="LTSLTSLTSLTSLTSLTSLTSLTSLTSLUWUscn";

lottoshares::lottoshares()
{
}

vector<unsigned char> signMessage(string strAddress, string strMessage)
{
    vector<unsigned char> vchSig;

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        return vchSig;

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        return vchSig;

    CKey key;
    if (!pwalletMain->GetKey(keyID, key))
        return vchSig;

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;


    if (!key.SignCompact(ss.GetHash(), vchSig))
        return vchSig;

    return vchSig;

    //return EncodeBase64(&vchSig[0], vchSig.size());
}

bool verifymessage(string strAddress, string strMessage, vector<unsigned char> vchSig)
{
    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        return false;

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        return false;

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

void checkForCheckpoints(std::vector<CTransaction> vtx, bool makeFileQueue){
    for (unsigned int i=0; i<vtx.size(); i++){
        //check each included transaction to see if it is a checkpoint
        if(vtx[i].IsCoinBase()){
               //This is a coinbase transaction, it can't be a checkpoint, skip
        }else{
            //printf("Check For Checkpoints 2:%d\n",vtx[i].vout.size());

            if(vtx[i].vout.size()==8 &&
                    vtx[i].vout[0].nValue==1 &&
                    vtx[i].vout[1].nValue==1 &&
                    vtx[i].vout[2].nValue==1 &&
                    vtx[i].vout[3].nValue==1 &&
                    vtx[i].vout[4].nValue==1 &&
                    vtx[i].vout[5].nValue==1 &&
                    vtx[i].vout[6].nValue==1){

                CTxDestination address;
                ExtractDestination(vtx[i].vout[0].scriptPubKey,address);
                std::string firstAddress=CBitcoinAddress(address).ToString().c_str();

                if(firstAddress==TIMEKEEPERBROADCASTADDRESS){
                    //Basic checks passed - extract checkpoint

                    vector<unsigned char> v;
                    vector<unsigned char> signature;
                    for(int k=1;k<7;k++){
                        ExtractDestination(vtx[i].vout[k].scriptPubKey,address);
                        std::string outputAddress=CBitcoinAddress(address).ToString().c_str();
                        std::vector<unsigned char> vchRet;
                        DecodeBase58Check(outputAddress, vchRet);
                        for(int j=1;j<21;j++){
                            if(signature.size()<65){
                                signature.push_back(vchRet[j]);
                            }else{
                                v.push_back(vchRet[j]);
                            }
                        }
                    }

                    int64 theHeight = *(int64*)&v[0];
                    int64 theTime = *(int64*)&v[8];
                    uint256 theHash = *(uint256*)&v[16];

                    char messageToSign[100];
                    snprintf(messageToSign, 100, "%llu:%llu:%s", theHeight, theTime, theHash.ToString().c_str());

                    if(verifymessage(TIMEKEEPERSIGNINGADDRESS,messageToSign,signature)){
                            Checkpoints::addCheckpoint(theTime, theHeight, theHash, makeFileQueue);
                    }
                }
            }
        }
    }
}

std::set<int> generateDrawNumbersFromString(char *ssc){

    //Hash seedString 6 times in a row
    std::set<int> drawnNumbers;

    uint256 hash2;
    SHA256((unsigned char*)ssc, strlen(ssc), (unsigned char*)&hash2);
    do{
        for(int i=0;i<4;i++){
            SHA256((unsigned char*)&hash2, sizeof(hash2), (unsigned char*)&hash2);
        }
        if(hash2.Get64(2)<ULLONG_MAX-21){ //ensure small numbers not slightly favoured
            int proposedNumber=(hash2.Get64(2)%42)+1;
            if(drawnNumbers.find(proposedNumber)==drawnNumbers.end()){
                drawnNumbers.insert(proposedNumber);
            }
        }
    }while(drawnNumbers.size()<6);

    std::set<int>::iterator it;
    for (it=drawnNumbers.begin(); it!=drawnNumbers.end(); ++it){
        printf(" %d",*it);
    }
    printf("\n");

    return drawnNumbers;
}

int countMatches(std::set<int> ticketNumbers, std::set<int> drawNumbers){
    int count=0;
    for (std::set<int>::iterator it=drawNumbers.begin(); it!=drawNumbers.end(); ++it){
        //int theNum=(int)*it;
        if (ticketNumbers.find(*it)!=ticketNumbers.end()){
            count++;
        }
    }
    return count;
}

void calculatePayoutRequirements(std::map<string, int64> &payoutRequirements,uint256 theTicketBlockHash, std::set<int> drawNumbers){

    printf("Calculate Payout Requirements\n");
    //Get the block
    CBlockIndex* ticketBlockHeader = pindexBest;
    while(ticketBlockHeader->GetBlockHash()!=theTicketBlockHash){
        ticketBlockHeader=ticketBlockHeader->pprev;
        printf("Looking For Matching Header, %s, %s\n",theTicketBlockHash.GetHex().c_str(),ticketBlockHeader->GetBlockHash().GetHex().c_str());
    }
    printf("Found Matching Header, %s, %s\n",theTicketBlockHash.GetHex().c_str(),ticketBlockHeader->GetBlockHash().GetHex().c_str());


    CBlock ticketBlock;
    ticketBlock.ReadFromDisk(ticketBlockHeader);

    //check for tickets
    for (unsigned int i=0; i<ticketBlock.vtx.size(); i++){
        //check each included transaction to see if it is a lottery ticket
        if(ticketBlock.vtx[i].IsCoinBase()){
                printf("Skipping Coinbase\n");
               //This is a coinbase transaction, it can't be a lottery ticket, skip
        }else{
            if(ticketBlock.vtx[i].vout.size()==8){
                //Lottery tickets always have 8 outputs
                printf("Transaction - Has 8 Outputs\n");

                //First 7 outputs must have ticket address
                bool validOutAddresses=true;
                int64 stake=0;
                for(int j=0;j<7;j++){
                    CTxDestination address;
                    ExtractDestination(ticketBlock.vtx[i].vout[j].scriptPubKey,address);
                    std::string outAddress=CBitcoinAddress(address).ToString().c_str();
                    stake=stake+ticketBlock.vtx[i].vout[j].nValue;
                    if(outAddress!=TICKETADDRESS){
                        printf("Not using ticket address %d %s\n",j,outAddress.c_str());
                        validOutAddresses=false;
                        break;
                    }
                }

                if(!validOutAddresses){
                    //This is not a ticket
                    printf("Skipping - not using ticket addres\n");
                    continue;
                }

                printf("Ticket Trx ID: %s\n",ticketBlock.vtx[i].GetHash().GetHex().c_str());

                printf("Ticket Numbers: ");
                std::set<int> ticketNumbers;
                for(int j=0;j<6;j++){
                    int64 ballNumber=ticketBlock.vtx[i].vout[j].nValue;
                    printf(" %llu",ballNumber);

                    if(ballNumber>0 && ballNumber<43){
                        ticketNumbers.insert(ballNumber);
                    }
                }
                printf("\n");

                std::set<int>::iterator it;
                printf("Draw Numbers: ");
                for (it=drawNumbers.begin(); it!=drawNumbers.end(); ++it){
                    printf(" %d",*it);
                }
                printf("\n");




                if(ticketNumbers.size()==6){
                    printf("Valid Ticket\n");

                    //Valid ticket
                    //check if tickets have won
                    //if so, add to payoutReqs
                    int matchingNumber=countMatches(ticketNumbers,drawNumbers);
                    printf("Matching Number %d\n",matchingNumber);

                    int64 prize=0;
                    if(matchingNumber==0){
                        prize=stake/10;
                    }else if(matchingNumber==1){
                        prize=stake/100;
                    }else if(matchingNumber==2){
                        prize=stake*1;
                    }else if(matchingNumber==3){
                        prize=stake*10;
                    }else if(matchingNumber==4){
                        prize=stake*100;
                    }else if(matchingNumber==5){
                        prize=stake*5000;
                    }else if(matchingNumber==6){
                        prize=stake*1000000;
                    }

                    printf("Prize %llu\n",prize);

                    if(prize>0){
                        CTxDestination address;
                        ExtractDestination(ticketBlock.vtx[i].vout[7].scriptPubKey,address);
                        std::string payoutAddress=CBitcoinAddress(address).ToString().c_str();
                        printf("Payout Address %s\n",payoutAddress.c_str());

                        payoutRequirements[payoutAddress]=payoutRequirements[payoutAddress]+prize;
                    }
                }
            }else{
                printf("Skipping Transaction - Not 8 Outputs\n");
            }
        }
    }

}

bool checkForPayouts(std::vector<CTransaction> &vtx, int64 &feesFromPayout, bool addTransactions){
    //printf("Check For Payouts:\n");
    std::map<string, int64> payoutRequirements;

    for (unsigned int i=0; i<vtx.size(); i++){
        //check each included transaction to see if it is a draw result
        if(vtx[i].IsCoinBase()){
            //This is a coinbase transaction, it can't be a draw result, skip
        }else{
            if(vtx[i].vout.size()==9 &&
                vtx[i].vout[0].nValue==1 &&
                vtx[i].vout[1].nValue==1 &&
                vtx[i].vout[2].nValue==1 &&
                vtx[i].vout[3].nValue==1 &&
                vtx[i].vout[4].nValue==1 &&
                vtx[i].vout[5].nValue==1 &&
                vtx[i].vout[6].nValue==1 &&
                vtx[i].vout[7].nValue==1){

                CTxDestination address;
                ExtractDestination(vtx[i].vout[0].scriptPubKey,address);
                std::string firstAddress=CBitcoinAddress(address).ToString().c_str();

                if(firstAddress==DRAWMANAGERBROADCASTADDRESS){
                    //Basic checks passed - extract checkpoint
                    vector<unsigned char> v;
                    vector<unsigned char> signature;
                    for(int k=1;k<8;k++){
                        ExtractDestination(vtx[i].vout[k].scriptPubKey,address);
                        std::string outputAddress=CBitcoinAddress(address).ToString().c_str();
                        std::vector<unsigned char> vchRet;
                        DecodeBase58Check(outputAddress, vchRet);
                        for(int j=1;j<21;j++){
                            if(signature.size()<65){
                                signature.push_back(vchRet[j]);
                            }else{
                                v.push_back(vchRet[j]);
                            }
                        }
                    }



                    int64 theHeight = *(int64*)&v[0];
                    int64 theTime = *(int64*)&v[8];
                    uint256 theHashNew = *(uint256*)&v[16];

                    char cN[20]={0};
                    for(int i=0;i<20;i++){
                        cN[i]=*(char*)&v[48+i];
                    }
                    char commaedNumbers[100];

                    sprintf(commaedNumbers, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                            cN[0],cN[1],cN[2],cN[3],cN[4],cN[5],cN[6],cN[7],cN[8],cN[9],
                            cN[10],cN[11],cN[12],cN[13],cN[14],cN[15],cN[16],cN[17],cN[18],cN[19]
                            );

                    char messageToSign[200];
                    sprintf(messageToSign, "%s:%llu:%llu:%s", commaedNumbers, theHeight, theTime, theHashNew.ToString().c_str());
                    printf("Message To Verify, %s %s",messageToSign, DRAWMANAGERSIGNINGADDRESS.c_str());
                    if(verifymessage(DRAWMANAGERSIGNINGADDRESS,messageToSign,signature)){
                        char randSeedString[300];
                        //string strSig=EncodeBase64(&signature[0], signature.size());
                        sprintf(randSeedString, "%s:%llu:%llu:%s", commaedNumbers, theHeight, theTime, theHashNew.ToString().c_str());
                        printf("Found payout:%s\n",randSeedString);

                        std::set<int> drawNumbers;
                        drawNumbers = generateDrawNumbersFromString(randSeedString);

                        //Note - the block may contain multiple draw results
                        calculatePayoutRequirements(payoutRequirements,theHashNew,drawNumbers);

                    }
                }
            }
        }
    }
    if(addTransactions){
        //Add payout outputs to VTX

        for (std::map<string, int64>::iterator it=payoutRequirements.begin(); it!=payoutRequirements.end(); ++it){
            vtx[0].vout.resize(vtx[0].vout.size()+1);
            CBitcoinAddress address(it->first);
            vtx[0].vout[vtx[0].vout.size()-1].scriptPubKey.SetDestination( address.Get() );
            vtx[0].vout[vtx[0].vout.size()-1].nValue = it->second;
            feesFromPayout=feesFromPayout+it->second;
        }
        //feesFromPayout=feesFromPayout/1000;
        printf("1 Fees From Payout - Calculated - %llu\n",feesFromPayout);

        return true;
    }else{
        //calculate total fees payout
        for (std::map<string, int64>::iterator it=payoutRequirements.begin(); it!=payoutRequirements.end(); ++it){
            feesFromPayout=feesFromPayout+it->second;
        }
        //feesFromPayout=feesFromPayout/1000;
        //printf("2 Fees From Payout - Calculated - %llu\n",feesFromPayout);

        //Check if payout outputs are present in coinbase transaction
        //return true if all present, return false if not

        for (std::map<string, int64>::iterator it=payoutRequirements.begin(); it!=payoutRequirements.end(); ++it){
            string addressString=it->first;

            bool foundIt=false;
            for (unsigned int i=0; i<vtx[0].vout.size(); i++){
                CTxDestination address;
                ExtractDestination(vtx[0].vout[i].scriptPubKey,address);

                if(addressString==CBitcoinAddress(address).ToString().c_str() && vtx[0].vout[i].nValue>=it->second){
                    //found
                    foundIt=true;
                    break;
                }

            }
            if(!foundIt){
                printf("Payout not found return false\n");
                return false;
            }
        }

        return true;
    }
}

int64 calculateTicketFees(std::vector<CTransaction> vtx){
    //Check lottery tickets included - commission is paid on all outputs to lottery ticket addresses
    //even if they do not form part of a valid ticket
    int64 totalStake=0;
    for (unsigned int i=0; i<vtx.size(); i++){
        //check each included transaction to see if it is a lottery ticket
        if(vtx[i].IsCoinBase()){
               //This is a coinbase transaction, it can't be a lottery ticket, skip
        }else{
            //Sum outputs sent to ticket address
            CTxDestination address;
            for(int k=0;k<vtx[i].vout.size();k++){
                ExtractDestination(vtx[i].vout[k].scriptPubKey,address);
                std::string outputAddress=CBitcoinAddress(address).ToString().c_str();
                if(outputAddress==TICKETADDRESS){
                    totalStake=totalStake+vtx[i].vout[k].nValue;
                }
            }
        }
    }
    return totalStake/100;
}

string convertAddress(const char address[], char newVersionByte){
    std::vector<unsigned char> v;
    DecodeBase58Check(address,v);
    v[0]=newVersionByte;
    string result = EncodeBase58Check(v);
    return result;
}

boost::filesystem::path getShareDropsPath(const char *fileName)
{
#ifdef MAC_OSX
    char path[FILENAME_MAX];
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    CFURLRef mainBundleURL = CFBundleCopyBundleURL(mainBundle);
    CFStringRef cfStringRef = CFURLCopyFileSystemPath(mainBundleURL, kCFURLPOSIXPathStyle);
    CFStringGetCString(cfStringRef, path, sizeof(path), kCFStringEncodingASCII);
    CFRelease(mainBundleURL);
    CFRelease(cfStringRef);
    return boost::filesystem::path(path) / "Contents" / fileName;
#else
    return boost::filesystem::path(fileName);
#endif
}

void addShareDrops(CBlock &block){
    //Add airdrops to genesis block
    std::string line;
    int dgCount=0;
    char intStr [10];
    int64 runningTotalCoins=0;
    //load from disk - distribute with exe
    ifstream myfile;

    myfile.open(getShareDropsPath("bitcoin.txt").string().c_str());
    if (myfile.is_open()){
        while ( myfile.good() ){
            std::getline (myfile,line);
                dgCount++;
                sprintf(intStr,"%d",dgCount);
                CTransaction txNew;
                txNew.vin.resize(1);
                txNew.vin[0].scriptSig = CScript() << 486604799 << CBigNum(4) << vector<unsigned char>((const unsigned char*)intStr, (const unsigned char*)intStr + strlen(intStr));
                txNew.vout.resize(1);
                txNew.vout[0].nValue = 20 * COIN;
                runningTotalCoins+=txNew.vout[0].nValue;
                CBitcoinAddress address(convertAddress(line.c_str(),0x30));
                txNew.vout[0].scriptPubKey.SetDestination( address.Get() );
                block.vtx.push_back(txNew);
        }
        myfile.close();
    }else{
        printf("bitcoin.txt - required for airdrop, not found\n");
    }
    printf("after bitcoin, total coins :%llu\n",runningTotalCoins);



    myfile.open(getShareDropsPath("dogecoin.txt").string().c_str());
    if (myfile.is_open()){
        while ( myfile.good() ){
            std::getline (myfile,line);
                dgCount++;
                sprintf(intStr,"%d",dgCount);
                CTransaction txNew;
                txNew.vin.resize(1);
                txNew.vin[0].scriptSig = CScript() << 486604799 << CBigNum(4) << vector<unsigned char>((const unsigned char*)intStr, (const unsigned char*)intStr + strlen(intStr));
                txNew.vout.resize(1);
                txNew.vout[0].nValue = 80 * COIN;
                runningTotalCoins+=txNew.vout[0].nValue;
                CBitcoinAddress address(convertAddress(line.c_str(),0x30));
                txNew.vout[0].scriptPubKey.SetDestination( address.Get() );
                block.vtx.push_back(txNew);
        }
        myfile.close();
    }else{
        printf("dogecoin.txt - required for airdrop, not found\n");
    }
    printf("after doge, total coins :%llu\n",runningTotalCoins);

    myfile.open(getShareDropsPath("protoshares.txt").string().c_str());
    if (myfile.is_open()){
                while ( myfile.good() ){
                    std::getline (myfile,line);
                    std::vector<std::string> strs;
                    boost::split(strs, line, boost::is_any_of(":"));
                    if(strs.size()==2){
                        int64 distributionAmount = atoi64(strs[1].c_str());
                        while(distributionAmount>0){
                            dgCount++;
                            sprintf(intStr,"%d",dgCount);
                            CTransaction txNew;
                            txNew.vin.resize(1);
                            txNew.vin[0].scriptSig = CScript() << 486604799 << CBigNum(4) << vector<unsigned char>((const unsigned char*)intStr, (const unsigned char*)intStr + strlen(intStr));
                            txNew.vout.resize(1);
                            if(distributionAmount>10000*COIN){
                                txNew.vout[0].nValue =10000*COIN;
                                distributionAmount=distributionAmount-txNew.vout[0].nValue;
                            }else{
                                txNew.vout[0].nValue =distributionAmount;
                                distributionAmount=0;
                            }
                            runningTotalCoins+=txNew.vout[0].nValue;
                            CBitcoinAddress address(convertAddress(strs[0].c_str(),0x30));
                            txNew.vout[0].scriptPubKey.SetDestination( address.Get() );
                            block.vtx.push_back(txNew);
                        }
                    }else{
                        printf("protoshares.txt - %s line parse failed\n",line.c_str());
                    }
                }
                myfile.close();
            }else{
                printf("protoshares.txt - required for distribution, not found\n");
            }
    printf("after pts, total coins :%llu\n",runningTotalCoins);

    myfile.open(getShareDropsPath("memorycoin.txt").string().c_str());
    if (myfile.is_open()){
                while ( myfile.good() ){
                    std::getline (myfile,line);
                    std::vector<std::string> strs;
                    boost::split(strs, line, boost::is_any_of(":"));
                    if(strs.size()==2){
                        int64 distributionAmount = atoi64(strs[1].c_str());
                        while(distributionAmount>0){
                            dgCount++;
                            sprintf(intStr,"%d",dgCount);;
                            CTransaction txNew;
                            txNew.vin.resize(1);
                            txNew.vin[0].scriptSig = CScript() << 486604799 << CBigNum(4) << vector<unsigned char>((const unsigned char*)intStr, (const unsigned char*)intStr + strlen(intStr));
                            txNew.vout.resize(1);
                            if(distributionAmount>10000*COIN){
                                txNew.vout[0].nValue =10000*COIN;
                                distributionAmount=distributionAmount-txNew.vout[0].nValue;
                            }else{
                                txNew.vout[0].nValue =distributionAmount;
                                distributionAmount=0;
                            }
                            runningTotalCoins+=txNew.vout[0].nValue;
                            CBitcoinAddress address(convertAddress(strs[0].c_str(),0x30));
                            txNew.vout[0].scriptPubKey.SetDestination( address.Get() );
                            block.vtx.push_back(txNew);
                        }
                    }else{
                        printf("memorycoin.txt - %s line parse failed\n",line.c_str());
                    }
                }
                myfile.close();
            }else{
                printf("memorycoin.txt - required for distribution, not found\n");
            }
    printf("after mmc, total coins :%llu\n",runningTotalCoins);


    myfile.open(getShareDropsPath("angelshares.txt").string().c_str());
    if (myfile.is_open()){
                while ( myfile.good() ){
                    std::getline (myfile,line);
                    std::vector<std::string> strs;
                    boost::split(strs, line, boost::is_any_of(":"));
                    if(strs.size()==2){
                        int64 distributionAmount = atoi64(strs[1].c_str());
                        while(distributionAmount>0){
                            dgCount++;
                            sprintf(intStr,"%d",dgCount);
                            CTransaction txNew;
                            txNew.vin.resize(1);
                            txNew.vin[0].scriptSig = CScript() << 486604799 << CBigNum(4) << vector<unsigned char>((const unsigned char*)intStr, (const unsigned char*)intStr + strlen(intStr));
                            txNew.vout.resize(1);
                            if(distributionAmount>10000*COIN){
                                txNew.vout[0].nValue =10000*COIN;
                                distributionAmount=distributionAmount-txNew.vout[0].nValue;
                            }else{
                                txNew.vout[0].nValue =distributionAmount;
                                distributionAmount=0;
                            }
                            runningTotalCoins+=txNew.vout[0].nValue;
                            CBitcoinAddress address(convertAddress(strs[0].c_str(),0x30));
                            txNew.vout[0].scriptPubKey.SetDestination( address.Get() );
                            block.vtx.push_back(txNew);
                        }
                    }else{
                        printf("angelshares.txt - %s line parse failed\n",line.c_str());
                    }
                }
                myfile.close();
            }else{
                printf("angelshares.txt - required for distribution, not found\n");
            }
    printf("after ags, total coins :%llu\n",runningTotalCoins);
}



